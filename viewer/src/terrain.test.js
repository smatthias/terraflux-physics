import test from 'node:test'
import assert from 'node:assert/strict'
import { RELIEF_TARGET, reliefRange, verticalScale, exaggerationFactor, snowlineFor, treelineFor, strataSpacing, roughness } from './terrain.js'

test('regional relief is raised to a readable height, small domains stay at least true to scale', () => {
  const alps = 32 / 960000, range = 2460
  const v = verticalScale(range, alps)
  assert.ok(Math.abs(range * v * 2 - RELIEF_TARGET) < 1e-9, 'the full relief reaches the target at height scale 2')
  assert.ok(Math.round(exaggerationFactor(v, alps, 2)) > 40, 'the Alps need a strong exaggeration')
  const hill = 32 / 12000
  assert.ok(exaggerationFactor(verticalScale(700, hill), hill, 2) / 2 < 1.2, 'the 700 m example hill over 12 km is nearly true to scale')
  assert.equal(verticalScale(5000, hill), hill, 'never flatter than true scale')
  assert.equal(exaggerationFactor(hill, hill, 1), 1)
  assert.ok(verticalScale(0, alps) > 0, 'flat terrain does not divide by zero')
  assert.deepEqual(reliefRange([3, null, -2, NaN, 7]), { min: -2, max: 7, range: 9 })
  assert.deepEqual(reliefRange([]), { min: 0, max: 0, range: 0 })
})

test('the snow line falls from the tropics to the poles; trees stop below it', () => {
  assert.equal(snowlineFor(0), 5000)
  assert.ok(Math.abs(snowlineFor(46.8) - 2760) < 60, 'the Alps: about 2,700–2,800 m')
  assert.equal(snowlineFor(-46.8), snowlineFor(46.8), 'both hemispheres')
  assert.ok(snowlineFor(60) < 1500 && snowlineFor(60) > 1000, 'southern Greenland / Norway')
  assert.equal(snowlineFor(85), 0)
  let last = Infinity
  for (let a = 0; a <= 90; a += 5) { const h = snowlineFor(a); assert.ok(h <= last, `monotone at ${a}`); last = h }
  assert.equal(snowlineFor(NaN), snowlineFor(45))
  assert.equal(treelineFor(2900), 2000); assert.equal(treelineFor(300), 0)
  assert.equal(strataSpacing(2460), 2460 / 22); assert.equal(strataSpacing(50), 12); assert.equal(strataSpacing(20000), 350)
})

test('rough where steep or high, smooth in water, on ice and in river channels', () => {
  // A ridge rising to the east with a flat plain in the west.
  const nx = 8, ny = 3, elevations = Array.from({ length: nx * ny }, (_, i) => { const x = i % nx; return x < 3 ? 0 : (x - 2) * 400 })
  const r = roughness({ elevations, nx, ny })
  assert.ok(r[1 * nx + 0] < 0.05, 'the plain is smooth')
  assert.ok(r[1 * nx + 6] > 0.6, 'the high steep ridge is rough')
  assert.ok(r.every(v => v >= 0 && v <= 1))
  const water = new Float32Array(nx * ny).fill(1), ice = new Float32Array(nx * ny).fill(1), channel = new Uint8Array(nx * ny).fill(1)
  assert.ok(roughness({ elevations, nx, ny, water }).every(v => v === 0), 'water is flat')
  assert.ok(roughness({ elevations, nx, ny, ice })[1 * nx + 6] < 0.05 * r[1 * nx + 6] + 1e-6, 'ice sheets are smooth')
  assert.ok(Math.abs(roughness({ elevations, nx, ny, channel })[1 * nx + 6] - 0.15 * r[1 * nx + 6]) < 1e-6, 'channels stay smooth so rivers stay visible')
  assert.ok(roughness({ elevations: new Array(nx * ny).fill(5), nx, ny }).every(v => v === 0), 'flat land has no crags')
})

test('the app shows terrain realistically, honestly exaggerated, and its overlays wobble', async () => {
  const { readFileSync } = await import('node:fs')
  const app = readFileSync(new URL('./App.vue', import.meta.url), 'utf8')
  for (const needle of ['verticalScale(relief.range,scale)', 'terrainCells(elevations,waterCell)', 'centreHeights(sheetGeometry', 'skirtMaterial({terrain:', 'renderer.shadowMap.enabled=true',
    'setTerrainLighting(', 'v-model="terrainStyle.realistic"', 'v-model.number="terrainStyle.dataMix"', 'class="relief-note"', "v-wobbly=\"'key'\""]) assert.ok(app.includes(needle), `App.vue: ${needle}`)
  // The rebuild watch must see the terrain style, and terrainStyle must be declared before that watch runs.
  assert.ok(app.indexOf('const terrainStyle=') < app.indexOf('watch([data,layer'), 'declared before the rebuild watch')
  const rebuild = app.match(/watch\(\[data,layer[^\]]*\],draw/)[0]
  for (const dep of ['terrainStyle', 'waveStyle']) assert.ok(new RegExp(`[,\\[]${dep}[,\\]]`).test(rebuild), `the rebuild watch sees ${dep}`)
  assert.ok(app.indexOf('waveStyle=ref(') < app.indexOf('watch([data,layer'), 'waveStyle declared before the watch')
  const overlay = readFileSync(new URL('./InspectOverlay.vue', import.meta.url), 'utf8')
  assert.ok(overlay.includes('createWobble()') && overlay.includes('popFrom(') && overlay.includes("window.addEventListener('pointermove', moveDrag)"))
})
