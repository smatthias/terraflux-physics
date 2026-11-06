import test from 'node:test'
import assert from 'node:assert/strict'
import { homography, matrixCss, project, createWobble, popFrom } from './wobbly.js'

const close = (a, b, eps = 1e-6) => Math.abs(a - b) < eps

test('the homography takes the rectangle exactly onto any quad', () => {
  const w = 360, h = 240
  for (const q of [[[0, 0], [w, 0], [w, h], [0, h]], [[10, -5], [370, 8], [350, 260], [-12, 230]], [[30, 20], [300, -10], [390, 300], [0, 250]]]) {
    const m = homography(w, h, q)
    ;[[0, 0], [w, 0], [w, h], [0, h]].forEach(([x, y], i) => {
      const [X, Y] = project(m, x, y)
      assert.ok(close(X, q[i][0], 1e-6) && close(Y, q[i][1], 1e-6), `corner ${i} → ${X},${Y} expected ${q[i]}`)
    })
  }
  assert.deepEqual(homography(100, 50, [[0, 0], [100, 0], [100, 50], [0, 50]]), [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 'the rest shape is the identity')
  assert.equal(matrixCss([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]), 'matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)')
})

test('the held corner follows the pointer, the far corner lags, and everything springs back with overshoot', () => {
  const sim = createWobble(); sim.resize(300, 200); sim.grab(0, 0)
  sim.move(40, 0)
  const [held, , far] = sim.state.offset
  assert.ok(Math.abs(held[0]) < 1e-9, 'the held corner does not lag')
  assert.ok(far[0] < -30, 'the opposite corner is left behind')
  sim.release()
  let overshoot = false
  for (let t = 0; t < 200; t++) { sim.step(1 / 60); if (sim.state.offset[2][0] > 0.5) overshoot = true }
  assert.ok(overshoot, 'it swings past its place: a wobble, not a slide')
  assert.ok(sim.resting(), 'and comes to rest')
})

test('the simulation is stable at slow frame rates and clamps extreme pulls', () => {
  const sim = createWobble({ maxOffset: 50 }); sim.resize(300, 200); sim.grab(0, 0)
  sim.move(5000, 0)
  assert.ok(Math.hypot(...sim.state.offset[2]) <= 50 + 1e-9, 'clamped')
  assert.ok(Math.hypot(...sim.state.offset[1]) < Math.hypot(...sim.state.offset[2]) - 5, 'a hard throw still bends: nearer corners reach less far')
  sim.release()
  for (let t = 0; t < 60; t++) sim.step(0.25)   // 4 frames per second
  assert.ok(sim.resting(), 'sub-stepped, no blow-up')
  sim.step(NaN); assert.ok(sim.resting())
})

test('windows spring open from the point they belong to', () => {
  const offsets = popFrom(200, 100, 0, 50, 0.2)
  assert.deepEqual(offsets[0], [0, 10]); assert.deepEqual(offsets[1], [-40, 10])
  const sim = createWobble(); sim.resize(200, 100); sim.kick(offsets)
  assert.ok(!sim.resting())
  for (let t = 0; t < 240; t++) sim.step(1 / 60)
  assert.ok(sim.resting())
  sim.kick(offsets); sim.settle(); assert.ok(sim.resting())
})
