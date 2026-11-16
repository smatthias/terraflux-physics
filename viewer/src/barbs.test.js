import test from 'node:test'
import assert from 'node:assert/strict'
import * as THREE from 'three'
import { barbParts, barbGlyph, barbSvg, barbColour, BARB } from './barbs.js'
import { barbLayer, levelSheet } from './windBarbs.js'

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)

test('barb parts: speed rounded to 5 kt, pennants 50, full barbs 10, half barbs 5', () => {
  const p = kt => { const b = barbParts(kt); return b.calm ? 'calm' : `${b.pennants}P ${b.full}F ${b.half}H` }
  assert.deepEqual([0, 2.4, 2.5, 7.4, 10, 12, 14, 23, 48, 50, 65, 105, 147.4].map(p),
    ['calm', 'calm', '0P 0F 1H', '0P 0F 1H', '0P 1F 0H', '0P 1F 0H', '0P 1F 1H', '0P 2F 1H', '1P 0F 0H', '1P 0F 0H', '1P 1F 1H', '2P 0F 1H', '2P 4F 1H'])
  assert.equal(barbParts(23).rounded, 25)
})

test('the staff points to where the wind comes from; the barbs stand at its outer end', () => {
  for (const from of [0, 90, 200, 315]) {
    const g = barbGlyph(20, from), [e0, n0, e1, n1] = g.lines[0], a = from * Math.PI / 180
    assert.deepEqual([e0, n0], [0, 0], 'the staff starts at the station')
    near(e1, Math.sin(a), 1e-12, `staff toward ${from}`); near(n1, Math.cos(a), 1e-12)
    assert.equal(g.lines.length, 3, 'staff and two full barbs')
    // Each barb starts on the staff near its tip and leans outward (further from the station than its root).
    for (const [be0, bn0, be1, bn1] of g.lines.slice(1)) assert.ok(Math.hypot(be1, bn1) > Math.hypot(be0, bn0) && Math.hypot(be0, bn0) > 0.6)
  }
})

test('barbs stand on the clockwise side (toward low pressure) in the north, anticlockwise in the south', () => {
  // A west wind: staff to the west; north of it is the clockwise side (lows lie poleward of westerlies).
  const nh = barbGlyph(30, 270), sh = barbGlyph(30, 270, { northernHemisphere: false })
  assert.ok(nh.lines.slice(1).every(([, n0, , n1]) => n1 > n0 + 0.1), 'NH: barbs point north of a westerly staff')
  assert.ok(sh.lines.slice(1).every(([, n0, , n1]) => n1 < n0 - 0.1), 'SH: barbs point south')
  // An east wind in the north: staff to the east, barbs toward the south.
  assert.ok(barbGlyph(30, 90).lines.slice(1).every(([, n0, , n1]) => n1 < n0))
})

test('pennants come first from the tip, then full barbs, then a half barb; a lone half barb sits in from the end', () => {
  const g = barbGlyph(65, 0)  // one pennant, one full, one half; staff due north
  assert.equal(g.triangles.length, 1); assert.equal(g.lines.length, 3)
  const pennantBase = Math.max(g.triangles[0][1], g.triangles[0][5]), fullRoot = g.lines[1][1], halfRoot = g.lines[2][1]
  assert.ok(pennantBase >= fullRoot && fullRoot > halfRoot, 'from the tip inward')
  const half = (g.lines[2][3] - g.lines[2][1]) / (g.lines[1][3] - g.lines[1][1]); near(half, 0.5, 1e-12, 'a half barb is half as long')
  const lone = barbGlyph(5, 0)
  near(lone.lines[1][1], 1 - BARB.spacing, 1e-12, 'a lone half barb is set in from the tip')
  assert.equal(barbGlyph(1, 0).calm, true); assert.deepEqual(barbGlyph(1, 0).lines, [])
})

test('SVG barbs: screen coordinates with north up, a circle for calm', () => {
  const s = barbSvg(10, 90, { size: 40 })
  assert.equal(s.lines[0].x1, 20); assert.equal(s.lines[0].y1, 20); near(s.lines[0].x2, 20 + 18, 1e-9, 'east is right'); near(s.lines[0].y2, 20, 1e-9)
  const n = barbSvg(10, 0, { size: 40 }); near(n.lines[0].y2, 2, 1e-9, 'north is up')
  assert.equal(barbSvg(0, 0).calm, true); assert.ok(barbSvg(0, 0).circle.r > 0)
  assert.equal(barbSvg(120, 45).polygons.length, 2)
  assert.deepEqual([10, 30, 60, 100, 150].map(barbColour), ['#cfe9f5', '#7fd1ff', '#ffd166', '#ff8c42', '#ff4d8d'])
})

test('a barb layer is one mesh: two triangles per ribbon, one per pennant, all at the layer\'s height', () => {
  const items = [{ x: 0, z: 0, kt: 65, fromDeg: 270 }, { x: 5, z: 5, kt: 15, fromDeg: 90 }, { x: -5, z: 2, kt: 0, fromDeg: 0 }]
  const mesh = barbLayer(items, { y: 3, length: 1, width: 0.05 })
  // 65 kt: staff + 1 full + 1 half = 3 ribbons, 1 pennant; 15 kt: staff + full + half = 3 ribbons; calm: a 12-segment ring.
  const triangles = mesh.geometry.attributes.position.count / 3
  assert.equal(triangles, (3 + 3 + 12) * 2 + 1); assert.equal(mesh.userData.count, 3)
  const y = mesh.geometry.attributes.position.array.filter((_, k) => k % 3 === 1)
  assert.ok(y.every(v => v === 3), 'flat at its altitude')
  const staffEnd = new THREE.Vector3(...mesh.geometry.attributes.position.array.slice(3, 6))
  assert.ok(staffEnd.x < -0.9, 'a west wind\'s staff points west (-x)')
  const lifted = barbLayer([{ x: 0, z: 0, kt: 10, fromDeg: 0, y: 7 }], { y: 1 })
  assert.ok(lifted.geometry.attributes.position.array.filter((_, k) => k % 3 === 1).every(v => v === 7), 'an item\'s own height wins')
  assert.equal(levelSheet(10, 8, 2).children.length, 2)
})
