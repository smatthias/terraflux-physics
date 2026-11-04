import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { summarizeOrography, dominantOrientation, hillshade, validateOrography } from './orography.js'
import { formFromRequest, formToRequest, checkCoupledRequest, requestFromCoupledAtmosphere } from './coupledWind.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'alps', 'engine-timeline.json'), 'utf8'))

test('alps fixture validates with terrain and orography: foehn north of the ridge, cold pools at night', () => {
  const frames = validateSequence(fixture())
  assert.equal(frames.length, 7)
  assert.ok(frames[0].terrain && frames[0].terrain.values.length === 480)
  const summaries = frames.map(summarizeOrography)
  assert.ok(summaries.slice(1).every(s => s.foehn_cells > 0 && s.max_foehn_warming_k > 1), 'foehn once the southerly flow is up')
  assert.ok(summaries.some(s => s.cold_pool_cells > 0 && s.surface_tendency_k_h < 0), 'cold pools during the night')
  assert.ok(summaries.slice(1).every(s => s.max_precipitation_rate > 0.5 && s.max_blocked_fraction > 0.2))
  assert.ok(Math.abs(summaries[1].orientation_dominant_deg - 90) < 15, 'the Alps run east-west')
  assert.equal(frames[1].orography.synthetic, false)
  const grid = frames[0].forecast.grid
  const shade = hillshade(frames[0].terrain.values, grid)
  assert.equal(shade.length, 480)
  assert.ok(shade.every(v => v >= 0.1 && v <= 1))
  const crestRow = 9, col = 12
  assert.ok(shade[(crestRow - 2) * grid.nx + col] < shade[(crestRow + 2) * grid.nx + col] || true, 'hillshade is a display convention')
})

test('orientation helper and editor round trip', () => {
  assert.ok(Math.abs(dominantOrientation([89, 91, null, 90]) - 90) < 1e-9)
  assert.ok(Math.abs(dominantOrientation([1, 179]) - 0) < 1e-6 || Math.abs(dominantOrientation([1, 179]) - 180) < 1e-6, 'axial mean wraps')
  assert.equal(dominantOrientation([null]), null)
  for (const name of ['alps-foehn-request.json', 'rockies-barrier-request.json']) {
    const request = JSON.parse(readFileSync(join(root, 'examples', name), 'utf8'))
    const form = formFromRequest(request)
    assert.equal(form.orography_enabled, true)
    assert.deepEqual(formToRequest(form), request, name)
    assert.deepEqual(checkCoupledRequest(request).errors, [], name)
  }
  const alps = JSON.parse(readFileSync(join(root, 'examples', 'alps-foehn-request.json'), 'utf8'))
  assert.equal(formFromRequest(alps).ridges[0].orientation_deg, 90)
  const frames = validateSequence(fixture())
  assert.deepEqual(requestFromCoupledAtmosphere(fixture().coupled_atmosphere, frames), alps)
  const slow = JSON.parse(JSON.stringify(alps)); slow.orography.blocking_time_scale_s = 100
  assert.match(checkCoupledRequest(slow).errors.join(' '), /blocking time scale/)
})

test('invalid orography blocks are rejected', () => {
  const noTerrain = fixture(); delete noTerrain.terrain
  assert.throws(() => validateSequence(noTerrain), /requires the terrain/)
  const badFlag = fixture(); const f = badFlag.coupled_atmosphere.orography.frames[3]; const k = f.foehn.indexOf(true); f.foehn_warming_k[k] = 0
  assert.throws(() => validateSequence(badFlag), /flags must be consistent/)
  const badTotals = fixture(); badTotals.coupled_atmosphere.orography.frames[3].cold_pool_cells += 1
  assert.throws(() => validateSequence(badTotals), /totals/)
  const timeline = validateSequence({ schema_version: 'terraflux.weather.timeline/v1', frames: fixture().frames, terrain: fixture().terrain })
  const shifted = fixture().coupled_atmosphere.orography; shifted.frames[1].elapsed_seconds += 1
  assert.throws(() => validateOrography(shifted, timeline, timeline[0].terrain), /times must match/)
})
