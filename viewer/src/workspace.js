// The workspace of the observatory: every clicked data point opens its own inspector window; windows can be focused,
// minimised to the taskbar at the bottom and restored from it, and each is tied to its data cell by a coloured arrow.
// The slim top bar carries a clock. Pure helpers (window list bookkeeping, arrow geometry, clock text); App.vue holds
// the state, InspectOverlay.vue draws a window and its arrow, Taskbar.vue the taskbar.

// One colour per window, shared by its arrow, the marker on its cell, its title bar dot and its taskbar button.
export const WINDOW_COLOURS = ['#ffd166', '#7fe0ff', '#ff8fab', '#9be38c', '#c79bff', '#ffa36b']
export const MAX_WINDOWS = 6

const byZ = (a, b) => b.z - a.z
// The window to focus after `id` closes or minimises: the top-most other window that is open.
export function nextFocus(windows, id) {
  return windows.filter(w => w.id !== id && !w.minimised).sort(byZ)[0]?.id ?? null
}

// Open a window on `cell`, or bring back the one already there. Returns the new list, the id of the window and the ids
// that had to close (the least recently used beyond the maximum).
export function openWindow(windows, cell, { id, z, max = MAX_WINDOWS } = {}) {
  const existing = windows.find(w => w.cell === cell)
  if (existing) return { windows: windows.map(w => (w.id === existing.id ? { ...w, minimised: false, z } : w)), id: existing.id, closed: [] }
  // Make room first, so the new window can take the colour of the one that closed.
  let list = windows
  const closed = []
  while (list.length >= max) {
    const oldest = [...list].sort((a, b) => a.z - b.z)[0]
    closed.push(oldest.id); list = list.filter(w => w.id !== oldest.id)
  }
  const used = new Set(list.map(w => w.colour))
  const colour = WINDOW_COLOURS.find(c => !used.has(c)) ?? WINDOW_COLOURS[id % WINDOW_COLOURS.length]
  return { windows: [...list, { id, cell, minimised: false, colour, z }], id, closed }
}
export const raiseWindow = (windows, id, z) => windows.map(w => (w.id === id ? { ...w, z, minimised: false } : w))
export const minimiseWindow = (windows, id) => windows.map(w => (w.id === id ? { ...w, minimised: true } : w))
export const closeWindow = (windows, id) => windows.filter(w => w.id !== id)

// What a click on a taskbar button does: bring a minimised window back, minimise the focused one, focus another.
export function taskbarAction(w, focusedId) {
  if (w.minimised) return 'restore'
  return w.id === focusedId ? 'minimise' : 'focus'
}

// The arrow from a window (or a taskbar button) to its data cell: a quadratic curve that bows upwards, ending in an
// arrowhead that stops just outside the cell's marker ring. from, to: { x, y } in pixels.
export function linkArrow(from, to, { ring = 7, head = 9, bend = 0.22 } = {}) {
  const dx = to.x - from.x, dy = to.y - from.y, length = Math.hypot(dx, dy)
  if (!(length > ring + head)) return null
  const c = { x: (from.x + to.x) / 2, y: Math.min(from.y, to.y) - length * bend }
  // Direction of arrival (the curve's end tangent), then the tip backed off to the ring.
  let tx = to.x - c.x, ty = to.y - c.y
  const tl = Math.hypot(tx, ty) || 1; tx /= tl; ty /= tl
  const tip = { x: to.x - tx * ring, y: to.y - ty * ring }
  const base = { x: tip.x - tx * head, y: tip.y - ty * head }, nx = -ty * head * 0.45, ny = tx * head * 0.45
  const f = v => v.toFixed(1)
  return {
    path: `M${f(from.x)},${f(from.y)} Q${f(c.x)},${f(c.y)} ${f(base.x)},${f(base.y)}`,
    head: `${f(tip.x)},${f(tip.y)} ${f(base.x + nx)},${f(base.y + ny)} ${f(base.x - nx)},${f(base.y - ny)}`,
    tip,
  }
}

// Where an arrow leaves a window: the point of the window's edge nearest to the cell.
export function edgePoint(rect, to) {
  const x = Math.max(rect.x, Math.min(rect.x + rect.w, to.x)), y = Math.max(rect.y, Math.min(rect.y + rect.h, to.y))
  return { x, y, inside: x === to.x && y === to.y }
}

// The clock of the top bar: local time with seconds, the date, and UTC for the tooltip.
const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'], MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']
const two = n => String(n).padStart(2, '0')
export function clockParts(date) {
  return {
    time: `${two(date.getHours())}:${two(date.getMinutes())}`, seconds: two(date.getSeconds()),
    date: `${DAYS[date.getDay()]} ${date.getDate()} ${MONTHS[date.getMonth()]}`,
    utc: `${two(date.getUTCHours())}:${two(date.getUTCMinutes())} UTC · ${date.getUTCFullYear()}-${two(date.getUTCMonth() + 1)}-${two(date.getUTCDate())}`,
  }
}

// Where a new window goes: beside its point (right, else left), moved along until it covers as little as possible of
// the other open windows and of its own point. avoid: [{ x, y, w, h }]; all in the viewport's pixels.
export function placeWindow({ anchor, bounds, w, h, avoid = [] }) {
  const clampTo = (v, lo, hi) => Math.max(lo, Math.min(Math.max(lo, hi), v))
  const fit = (x, y) => ({ x: clampTo(x, bounds.left + 8, bounds.right - w - 8), y: clampTo(y, bounds.top + 8, bounds.bottom - h - 8) })
  const overlap = (r, o) => Math.max(0, Math.min(r.x + r.w, o.x + o.w) - Math.max(r.x, o.x)) * Math.max(0, Math.min(r.y + r.h, o.y + o.h) - Math.max(r.y, o.y))
  const y0 = anchor.y - Math.min(140, h / 2), candidates = []
  for (const side of [1, -1]) for (let k = 0; k < 6; k++) {
    const x = side > 0 ? anchor.x + 40 + k * 36 : anchor.x - 40 - w - k * 36
    // The right side is preferred a little (the reading direction), so equal choices do not flip with rounding.
    for (const dy of [0, 34 * k, -34 * k]) candidates.push({ ...fit(x, y0 + dy), bias: side > 0 ? 0 : 12 })
  }
  let best = null
  for (const c of candidates) {
    const r = { ...c, w, h }
    const covered = avoid.reduce((a, o) => a + overlap(r, o), 0)
    const onPoint = anchor.x > r.x - 6 && anchor.x < r.x + w + 6 && anchor.y > r.y - 6 && anchor.y < r.y + h + 6 ? w * h * 0.5 : 0
    const score = covered + onPoint + Math.hypot(r.x + w / 2 - anchor.x, r.y + h / 2 - anchor.y) * 2 + c.bias
    if (!best || score < best.score) best = { x: c.x, y: c.y, score }
  }
  return { x: best.x, y: best.y }
}
