import test from 'node:test'
import assert from 'node:assert/strict'
import { validateGlobalRelief, resampleGlobalRelief, globeReliefHeight } from './globalRelief.js'
const grid = () => ({ schema: 'terraflux.global-relief/v1', nx: 2, ny: 2, units: 'm', datum: 'mean_sea_level', row_order: 'south_to_north', source: 'Synthetic test', elevation_m: [-4000, null, 1000, 0] })
test('global relief retains hemispheres, nodata and seafloor when resampling', () => {
  assert.deepEqual(resampleGlobalRelief(grid(), 4, 2), [-4000, -4000, null, null, 1000, 1000, 0, 0])
  assert.deepEqual(resampleGlobalRelief(grid(), 2, 2), grid().elevation_m)
})
test('rejects ambiguous coordinates, missing provenance and invalid elevations', () => {
  for (const patch of [{ row_order: 'north_to_south' }, { datum: 'unknown' }, { source: '' }, { nx: 0 }, { elevation_m: [0, 0, NaN, 0] }, { elevation_m: [0, 0, '1', 0] }]) assert.throws(() => validateGlobalRelief({ ...grid(), ...patch }))
})
test('relief displaces seafloor inward, land outward and nodata nowhere', () => {
  assert.ok(globeReliefHeight(-4000) < 0)
  assert.ok(globeReliefHeight(1000) > 0)
  assert.equal(globeReliefHeight(null), 0)
  assert.equal(globeReliefHeight(0), 0)
  assert.ok(Math.abs(globeReliefHeight(-12000)) <= 9 * 0.04)
})

test('committed GEBCO sample satisfies the viewer contract and retains land and seabed', async () => {
  const { readFile } = await import('node:fs/promises')
  const doc = validateGlobalRelief(JSON.parse(await readFile(new URL('../public/relief/gebco-2026-10deg.json', import.meta.url), 'utf8')))
  assert.equal(doc.nx, 36)
  assert.equal(doc.ny, 18)
  assert.match(doc.source, /GEBCO_2026/)
  const values = resampleGlobalRelief(doc, 90, 45)
  assert.equal(values.length, 4050)
  assert.ok(values.some(v => v < 0))
  assert.ok(values.some(v => v > 0))
})
