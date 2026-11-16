// Wobbly overlays: dragging a window (or a legend) makes it wobble like jelly. Each of its four corners hangs on a
// damped spring at its place in the rectangle. The point you hold follows the pointer exactly; the farther a corner is
// from it, the more it lags behind when you move, and on release the corners swing back with a little overshoot. The
// corners are drawn with a true perspective warp (a homography as a CSS matrix3d), so the window really bends.

const CORNERS = [[0, 0], [1, 0], [1, 1], [0, 1]]

// The projective map of the w x h rectangle onto the quad q (four [x, y] corners: top-left, top-right, bottom-right,
// bottom-left) as a CSS matrix3d, for transform-origin 0 0. Heckbert's closed form for the unit square, then scaled.
export function homography(w, h, q) {
  const [[x0, y0], [x1, y1], [x2, y2], [x3, y3]] = q
  const dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3, dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3
  let a, b, d, e, g = 0, k = 0
  const det = dx1 * dy2 - dx2 * dy1
  if (Math.abs(dx3) > 1e-9 || Math.abs(dy3) > 1e-9) { g = det ? (dx3 * dy2 - dx2 * dy3) / det : 0; k = det ? (dx1 * dy3 - dx3 * dy1) / det : 0 }
  a = x1 - x0 + g * x1; b = x3 - x0 + k * x3; d = y1 - y0 + g * y1; e = y3 - y0 + k * y3
  const A = a / w, B = b / h, D = d / w, E = e / h, G = g / w, H = k / h
  return [A, D, 0, G, B, E, 0, H, 0, 0, 1, 0, x0, y0, 0, 1]
}
export const matrixCss = m => `matrix3d(${m.map(v => +v.toFixed(6)).join(',')})`

// Where the matrix takes the point (x, y): for tests and for placing things on a wobbling window.
export function project(m, x, y) {
  const X = m[0] * x + m[4] * y + m[12], Y = m[1] * x + m[5] * y + m[13], W = m[3] * x + m[7] * y + m[15]
  return [X / W, Y / W]
}

// The spring model. Offsets are the corners' displacements from their rest places, in pixels.
// Soft enough to see (a period of about half a second, a few swings), damped enough to settle within a second or two.
export function createWobble({ stiffness = 140, damping = 9, maxOffset = 90 } = {}) {
  const state = { w: 1, h: 1, offset: CORNERS.map(() => [0, 0]), velocity: CORNERS.map(() => [0, 0]), lag: CORNERS.map(() => 1) }
  // Each corner's reach is scaled by its lag, so a hard throw still bends the window instead of shifting it whole.
  const clampOffset = (o, i) => { const cap = maxOffset * Math.max(0.15, state.lag[i]), l = Math.hypot(o[0], o[1]); if (l > cap) { o[0] *= cap / l; o[1] *= cap / l } }
  return {
    state,
    resize(w, h) { state.w = Math.max(1, w); state.h = Math.max(1, h) },
    // Hold the window at (x, y) in its own coordinates: the corners' lag grows with their distance from that point.
    grab(x, y) {
      const diag = Math.hypot(state.w, state.h)
      state.lag = CORNERS.map(([u, v]) => Math.min(1, Math.hypot(u * state.w - x, v * state.h - y) / diag) ** 1.15)
    },
    release() { state.lag = CORNERS.map(() => 1) },
    // The window moved by (dx, dy) under the pointer: each corner is left behind by its lag.
    move(dx, dy) {
      state.offset.forEach((o, i) => { o[0] -= dx * state.lag[i]; o[1] -= dy * state.lag[i]; clampOffset(o, i) })
    },
    // Start from displaced corners (e.g. pulled towards a point when a window opens) and let them spring out.
    kick(offsets) { offsets.forEach((o, i) => { state.offset[i] = [o[0], o[1]]; state.velocity[i] = [0, 0] }) },
    step(dt) {
      let left = Math.min(0.1, Math.max(0, dt))
      while (left > 1e-6) {
        const h = Math.min(left, 1 / 120)
        for (let i = 0; i < 4; i++) for (let j = 0; j < 2; j++) {
          state.velocity[i][j] += (-stiffness * state.offset[i][j] - damping * state.velocity[i][j]) * h
          state.offset[i][j] += state.velocity[i][j] * h
        }
        left -= h
      }
    },
    resting(eps = 0.08) { return state.offset.every((o, i) => Math.abs(o[0]) < eps && Math.abs(o[1]) < eps && Math.hypot(...state.velocity[i]) < eps * 4) },
    settle() { state.offset.forEach(o => { o[0] = 0; o[1] = 0 }); state.velocity.forEach(v => { v[0] = 0; v[1] = 0 }) },
    corners() { return CORNERS.map(([u, v], i) => [u * state.w + state.offset[i][0], v * state.h + state.offset[i][1]]) },
    matrix() { return homography(state.w, state.h, this.corners()) },
  }
}

// Offsets that start a window shrunk towards a point (px, py in its own coordinates) so that it springs open.
export function popFrom(w, h, px, py, amount = 0.14) {
  return CORNERS.map(([u, v]) => [(px - u * w) * amount, (py - v * h) * amount])
}

// ---- v-wobbly: a draggable, wobbly overlay -----------------------------------------------------------------------------
// Drag the element to move it (its position is kept per key while the page is open); double-click to put it back.
// Interactive children (buttons, inputs, links) keep their clicks. Honors prefers-reduced-motion (no wobble, still drags).
const remembered = new Map()
const reduceMotion = () => typeof window !== 'undefined' && !!window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
const INTERACTIVE = 'button,input,select,textarea,a,summary,[data-no-drag]'

export const vWobbly = {
  mounted(el, binding) {
    const key = binding.value ?? null, sim = createWobble()
    const at = key && remembered.get(key) ? [...remembered.get(key)] : [0, 0]
    let drag = null, raf = 0, last = 0
    const place = () => { el.style.translate = at[0] || at[1] ? `${at[0]}px ${at[1]}px` : '' }
    const frame = now => {
      sim.step(last ? (now - last) / 1000 : 0); last = now
      if (!drag && sim.resting()) { sim.settle(); el.style.transform = ''; raf = 0; last = 0; return }
      el.style.transform = matrixCss(sim.matrix()); raf = requestAnimationFrame(frame)
    }
    const run = () => { if (!raf && !reduceMotion()) raf = requestAnimationFrame(frame) }
    const down = e => {
      if (e.button !== 0 || e.target.closest(INTERACTIVE)) return
      const r = el.getBoundingClientRect()
      sim.resize(el.offsetWidth, el.offsetHeight); sim.grab(e.clientX - r.left, e.clientY - r.top)
      drag = { x: e.clientX, y: e.clientY }
      el.setPointerCapture(e.pointerId); el.classList.add('wobbly-dragging'); e.preventDefault(); e.stopPropagation()
      run()
    }
    const move = e => {
      if (!drag) return
      const dx = e.clientX - drag.x, dy = e.clientY - drag.y
      drag = { x: e.clientX, y: e.clientY }; at[0] += dx; at[1] += dy; place(); sim.move(dx, dy)
      if (key) remembered.set(key, [...at])
    }
    const up = () => { if (!drag) return; drag = null; sim.release(); el.classList.remove('wobbly-dragging'); run() }
    const reset = e => {
      if (e.target.closest(INTERACTIVE)) return
      sim.resize(el.offsetWidth, el.offsetHeight); sim.grab(el.offsetWidth / 2, el.offsetHeight / 2); sim.move(-at[0] * 0.25, -at[1] * 0.25); sim.release()
      at[0] = 0; at[1] = 0; place(); if (key) remembered.delete(key); run()
    }
    el.style.transformOrigin = '0 0'; el.classList.add('wobbly'); place()
    el.addEventListener('pointerdown', down); el.addEventListener('pointermove', move); el.addEventListener('pointerup', up); el.addEventListener('pointercancel', up); el.addEventListener('dblclick', reset)
    el._wobbly = () => { cancelAnimationFrame(raf); el.removeEventListener('pointerdown', down); el.removeEventListener('pointermove', move); el.removeEventListener('pointerup', up); el.removeEventListener('pointercancel', up); el.removeEventListener('dblclick', reset) }
  },
  unmounted(el) { el._wobbly?.() },
}
