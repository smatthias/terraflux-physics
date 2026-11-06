import test from 'node:test'
import assert from 'node:assert/strict'
import * as THREE from 'three'
import { surfaceSubdivisions, latticeAxis, centreHeights, noise2, ridged, cavityOcclusion, sampleCubic, sampleLinear, cellAtUv, planeSurface, globeSurface, surfaceSkirt, surfaceMaterial, skirtMaterial } from './surface.js'

const field = (nx, ny, f) => Float32Array.from({ length: nx * ny }, (_, i) => f(i % nx, Math.floor(i / nx)))
const colours = n => Float32Array.from({ length: n * 3 }, (_, k) => (k % 3) / 2)

test('the subdivision keeps small grids fine and large grids within budget', () => {
  assert.equal(surfaceSubdivisions(12, 10), 8)
  const s = surfaceSubdivisions(140, 84)
  assert.ok(s >= 1 && (140 * s + 1) * (84 * s + 1) <= 100000)
  assert.equal(surfaceSubdivisions(2000, 2000), 1)
  for (const [nx, ny] of [[24, 20], [30, 22], [52, 32], [90, 45], [140, 84]]) { const t = surfaceSubdivisions(nx, ny); assert.ok(t === 1 || t % 2 === 0, `${nx}x${ny}: even`) }
})

test('every lattice has a vertex on each cell centre and spans the whole footprint', () => {
  for (const s of [1, 2, 4, 8]) {
    const a = latticeAxis(5, s)
    assert.equal(a[0], -0.5); assert.equal(a.at(-1), 4.5)
    for (let i = 0; i < 5; i++) assert.ok(a.some(f => Math.abs(f - i) < 1e-9), `s=${s}: centre ${i}`)
    assert.ok(a.every((f, k) => k === 0 || f > a[k - 1]), 'increasing')
  }
})

test('the surface passes exactly through every cell value (overlays stay on it)', () => {
  const h = field(7, 5, (x, y) => Math.sin(x) * 2 + y * y * 0.3)
  for (let y = 0; y < 5; y++) for (let x = 0; x < 7; x++) assert.ok(Math.abs(sampleCubic(h, 7, 5, x, y) - h[y * 7 + x]) < 1e-5, `${x},${y}`)
})

test('interpolation is smooth but never overshoots a step', () => {
  const step = field(8, 3, x => (x < 4 ? 0 : 10))
  for (let fx = -0.5; fx <= 7.5; fx += 0.05) {
    const v = sampleCubic(step, 8, 3, fx, 1)
    assert.ok(v >= 0 && v <= 10, `no ringing at ${fx.toFixed(2)}: ${v}`)
  }
  assert.ok(sampleCubic(step, 8, 3, 3.5, 1) > 0 && sampleCubic(step, 8, 3, 3.5, 1) < 10, 'the step is blended')
  const ramp = field(8, 3, x => x)
  assert.ok(Math.abs(sampleCubic(ramp, 8, 3, 2.25, 1) - 2.25) < 1e-6, 'a linear ramp stays linear')
  assert.equal(sampleCubic(ramp, 8, 3, -0.5, 1), 0, 'the half cell beyond the edge continues the edge value')
})

test('longitude wraps on the globe', () => {
  const ring = field(6, 2, x => (x === 0 ? 6 : 0))
  assert.ok(sampleCubic(ring, 6, 2, 5.5, 0, true) > 0, 'between the last and the first column')
  assert.equal(sampleCubic(ring, 6, 2, 5.5, 0, false), 0)
  assert.equal(sampleLinear(Float32Array.from([0, 10, 20, 30]), 1, 0, 2, 2, 0.5, 0.5), 15)
})

test('a surface uv maps back to its cell', () => {
  assert.equal(cellAtUv(0, 0, 10, 8), 0)
  assert.equal(cellAtUv(0.999, 0.999, 10, 8), 79)
  assert.equal(cellAtUv(0.15, 0.3, 10, 8), 2 * 10 + 1)
  assert.equal(cellAtUv(1.2, -0.1, 10, 8), 9, 'clamped')
})

test('the flat surface covers the old cell footprint, faces up and carries uv, colours and values', () => {
  const nx = 6, ny = 4, s = 4, dx = 2, dz = 1.5
  const values = Array.from({ length: nx * ny }, (_, i) => (i === 5 ? null : i / 23))
  const g = planeSurface({ nx, ny, dx, dz, heights: field(nx, ny, () => 1), colors: colours(nx * ny), values, s })
  const cols = nx * s + 1, rows = ny * s + 1
  assert.equal(g.attributes.position.count, cols * rows)
  assert.equal(g.index.count, (cols - 1) * (rows - 1) * 6)
  g.computeBoundingBox()
  assert.ok(Math.abs(g.boundingBox.min.x + nx * dx / 2) < 1e-6 && Math.abs(g.boundingBox.max.x - nx * dx / 2) < 1e-6)
  assert.ok(Math.abs(g.boundingBox.min.z + ny * dz / 2) < 1e-6 && Math.abs(g.boundingBox.max.z - ny * dz / 2) < 1e-6)
  for (let k = 0; k < g.attributes.normal.count; k++) assert.ok(g.attributes.normal.getY(k) > 0.99, 'normals point up')
  // A vertex sits on the centre of cell (3, 2), at the cell's height, and its uv maps back to that cell.
  const pos = g.attributes.position, cx = (3 + 0.5 - nx / 2) * dx, cz = -(2 + 0.5 - ny / 2) * dz
  const centre = Array.from({ length: pos.count }, (_, k) => k).find(k => Math.abs(pos.getX(k) - cx) < 1e-6 && Math.abs(pos.getZ(k) - cz) < 1e-6)
  assert.ok(centre !== undefined, 'a vertex on the cell centre')
  assert.equal(pos.getY(centre), 1)
  assert.equal(cellAtUv(g.attributes.uv.getX(centre), g.attributes.uv.getY(centre), nx, ny), 2 * nx + 3)
  const vals = g.attributes.aValue.array
  assert.ok(vals.every(v => v === -1 || (v >= 0 && v <= 1)), 'values are normalised or marked missing')
})

test('the globe closes its seam, faces outward and lifts by height', () => {
  const nx = 12, ny = 6, R = 6, centre = new THREE.Vector3(0, 7, 0)
  const dir = (lat, lon) => { const a = lat * Math.PI / 180, b = lon * Math.PI / 180; return [Math.cos(a) * Math.cos(b), Math.sin(a), -Math.cos(a) * Math.sin(b)] }
  const g = globeSurface({ nx, ny, heights: field(nx, ny, () => 0.5), colors: colours(nx * ny), values: null, R, centre, dir, s: 2 })
  const pos = g.attributes.position, normal = g.attributes.normal, p = new THREE.Vector3(), n = new THREE.Vector3()
  for (let k = 0; k < pos.count; k++) {
    p.fromBufferAttribute(pos, k).sub(centre)
    assert.ok(Math.abs(p.length() - 6.5) < 1e-4, 'radius R + h')
    assert.ok(n.fromBufferAttribute(normal, k).dot(p.normalize()) > 0.9, 'outward normals, also at the seam and the poles')
  }
  const { cols } = g.userData.lattice
  assert.ok(p.fromBufferAttribute(pos, cols * 3).distanceTo(n.fromBufferAttribute(pos, cols * 3 + cols - 1)) < 1e-4, 'the seam columns coincide')
})

test('the skirt hangs from the rim to the floor, and the material exposes its uniforms', () => {
  const g = planeSurface({ nx: 3, ny: 2, dx: 1, dz: 1, heights: field(3, 2, () => 2), colors: colours(6), values: null, s: 2 })
  const skirt = surfaceSkirt(g, 0)
  const ys = Array.from({ length: skirt.attributes.position.count }, (_, k) => skirt.attributes.position.getY(k))
  assert.ok(ys.includes(0) && ys.includes(2))
  const m = surfaceMaterial({ nx: 3, ny: 2, wrap: true })
  for (const u of ['uHover', 'uHoverOn', 'uSel', 'uSelOn', 'uIso', 'uIsoOn', 'uContours', 'uTime']) assert.ok(m.userData.uniforms[u], u)
  assert.equal(m.userData.uniforms.uWrap.value, 1)
  // The shader hooks must find their anchors in this three.js version.
  const shader = { uniforms: {}, vertexShader: THREE.ShaderLib.standard.vertexShader, fragmentShader: THREE.ShaderLib.standard.fragmentShader }
  m.onBeforeCompile(shader)
  assert.ok(shader.vertexShader.includes('vCell = uv * uGrid - 0.5;'))
  assert.ok(shader.fragmentShader.includes('footprint(dh)'))
})

test('the fast separable lattice gives exactly the reference interpolation', () => {
  const nx = 9, ny = 7, s = 4, rnd = (k => () => (k = (k * 16807) % 2147483647) / 2147483647)(42)
  const heights = Float32Array.from({ length: nx * ny }, () => rnd() * 5), colors = Float32Array.from({ length: nx * ny * 3 }, rnd)
  const values = Array.from({ length: nx * ny }, (_, i) => (i % 11 === 0 ? null : rnd()))
  const g = planeSurface({ nx, ny, dx: 1, dz: 1, heights, colors, values, s })
  const ax = latticeAxis(nx, s), ay = latticeAxis(ny, s), pos = g.attributes.position, col = g.attributes.color
  let worst = 0
  ay.forEach((fy, r) => ax.forEach((fx, c) => {
    const k = r * ax.length + c
    worst = Math.max(worst, Math.abs(pos.getY(k) - sampleCubic(heights, nx, ny, fx, fy)))
    for (let j = 0; j < 3; j++) worst = Math.max(worst, Math.abs(col.getComponent(k, j) - sampleLinear(colors, 3, j, nx, ny, fx, fy)))
  }))
  assert.ok(worst < 1e-4, `largest difference ${worst}`)
  // Analytic normals of a tilted plane h = 0.5 x are all (-0.5, 1, 0) normalised.
  const tilt = planeSurface({ nx: 5, ny: 4, dx: 1, dz: 1, heights: Float32Array.from({ length: 20 }, (_, i) => 0.5 * (i % 5)), colors: new Float32Array(60), values: null, s: 2 })
  const n = tilt.attributes.normal, expect = [-0.5, 1, 0].map(v => v / Math.hypot(0.5, 1))
  for (let k = 0; k < n.count; k++) {
    const x = tilt.attributes.position.getX(k)
    // Interior only: at the edges the clamped spline flattens on purpose (the edge value continues).
    if (x > -0.6 && x < 0.6) assert.ok(Math.abs(n.getX(k) - expect[0]) < 1e-5 && Math.abs(n.getY(k) - expect[1]) < 1e-5 && Math.abs(n.getZ(k)) < 1e-5, `normal ${k}`)
  }
})

test('the app draws the field as a surface, lights points on hover and explains them on click', async () => {
  const { readFileSync } = await import('node:fs')
  const app = readFileSync(new URL('./App.vue', import.meta.url), 'utf8')
  assert.ok(!app.includes('GridHelper'), 'no grid floor')
  assert.ok(!/InstancedMesh\([^)]*BoxGeometry\(dx\*\.94/.test(app), 'no box per cell')
  for (const needle of ['planeSurface(', 'globeSurface(', 'surfaceSkirt(', 'softFloor()', "addEventListener('pointermove',onPointerMove)", "addEventListener('pointerup',onPointerUp)",
    'Math.hypot(e.clientX-d.x,e.clientY-d.y)>5', '<InspectOverlay', 'class="hover-tip"', 'openInspector(i,$event)', 'closeInspector()']) assert.ok(app.includes(needle), `App.vue: ${needle}`)
  // Hovering must not rebuild the scene: the rebuild watch lists neither the hover nor the selection itself.
  const rebuild = app.match(/watch\(\[data,layer[^\]]*\],draw/)[0]
  assert.ok(!rebuild.includes('hoverCell') && !/[,[]selected[,\]]/.test(rebuild), rebuild)
})

test('fine relief: deterministic noise, crags only where asked, overlays read the final height', () => {
  assert.equal(noise2(1.3, 2.7, 5), noise2(1.3, 2.7, 5), 'deterministic')
  assert.notEqual(noise2(1.3, 2.7, 5), noise2(1.3, 2.7, 6), 'seeded')
  for (let k = 0; k < 200; k++) { const r = ridged(k * 0.37, k * 0.11); assert.ok(r >= 0 && r <= 1) }
  const nx = 6, ny = 5, heights = new Float32Array(nx * ny).fill(1), colors = new Float32Array(nx * ny * 3)
  const amplitude = Float32Array.from({ length: nx * ny }, (_, i) => (i % nx >= 3 ? 0.5 : 0))
  for (const s of [1, 2, 4]) {
    const g = planeSurface({ nx, ny, dx: 1, dz: 1, heights, colors, values: null, s, detail: { amplitude, seed: 3 } })
    const centre = centreHeights(g, nx, ny)
    assert.ok(Math.abs(centre[0] - 1) < 1e-6, `s=${s}: no relief where the amplitude is zero`)
    const pos = g.attributes.position
    for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
      const cx = x + 0.5 - nx / 2, cz = -(y + 0.5 - ny / 2)
      const k = Array.from({ length: pos.count }, (_, j) => j).find(j => Math.abs(pos.getX(j) - cx) < 1e-6 && Math.abs(pos.getZ(j) - cz) < 1e-6)
      assert.ok(Math.abs(pos.getY(k) - centre[y * nx + x]) < 1e-6, `s=${s}: centre height of ${x},${y} is the vertex there`)
    }
    if (s === 4) assert.ok(Array.from({ length: pos.count }, (_, j) => pos.getY(j)).some(h => Math.abs(h - 1) > 0.05), 'crags where the amplitude is set')
  }
  const ao = cavityOcclusion(Float32Array.from([1, 1, 1, 1, 0, 1, 1, 1, 1]), 3, 3, 1, 1)
  assert.ok(ao[4] < ao[0] && ao[4] >= 0.35 - 1e-6, 'the pit is occluded, within bounds (float32)')
})

test('terrain attributes reach the geometry and the terrain shader compiles its hooks', () => {
  const nx = 3, ny = 2, ones = new Float32Array(6).fill(1)
  const g = planeSurface({ nx, ny, dx: 1, dz: 1, heights: ones, colors: new Float32Array(18), values: null, s: 2, extras: { aElev: ones, aIce: ones, aSnow: ones, aWater: ones }, occlusion: 0.1 })
  for (const a of ['aElev', 'aIce', 'aSnow', 'aWater', 'aAO']) assert.ok(g.attributes[a], a)
  const skirt = surfaceSkirt(g, 0, { iceDepth: 0.5 })
  assert.ok(skirt.attributes.aTop && skirt.attributes.aIceDepth.array.every(v => v === 0.5) && skirt.attributes.aWater)
  const m = surfaceMaterial({ nx, ny, terrain: { dataMix: 0.3, snowline: 2500, treeline: 1600, strata: 80 } })
  assert.equal(m.userData.uniforms.uDataMix.value, 0.3); assert.equal(m.defines.TF_TERRAIN, ''); assert.equal(m.defines.STANDARD, '', 'the standard define is kept')
  const shader = { uniforms: {}, vertexShader: THREE.ShaderLib.standard.vertexShader, fragmentShader: THREE.ShaderLib.standard.fragmentShader }
  m.onBeforeCompile(shader)
  for (const hook of ['vElev = aElev', 'vTerrainNormal = normalize']) assert.ok(shader.vertexShader.includes(hook), hook)
  for (const hook of ['float rockAmount', 'roughnessFactor = mix(roughnessFactor, 0.18', 'footprint(dh)']) assert.ok(shader.fragmentShader.includes(hook), hook)
  const side = skirtMaterial({ terrain: true }), sideShader = { uniforms: {}, vertexShader: THREE.ShaderLib.standard.vertexShader, fragmentShader: THREE.ShaderLib.standard.fragmentShader }
  side.onBeforeCompile(sideShader)
  assert.ok(sideShader.fragmentShader.includes('float band = fract(vSidePos.y * uSkirtStrata'))
  assert.equal(skirtMaterial().defines.TF_TERRAIN, undefined, 'without terrain the sides stay plain')
  assert.equal(skirtMaterial({ terrain: true }).defines.STANDARD, '')
})
