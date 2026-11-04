import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { cloudFormationDemo } from './cloudFormation.js'
import { summarizeStorms, summarizeSevere, categoryFor, categoryInfo, validateStormSystems, validateSevereConvection, CATEGORIES } from './stormSystems.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'hurricane', 'engine-timeline.json'), 'utf8'))

test('hurricane fixture validates: seeds merge, a storm intensifies, makes landfall and decays', () => {
  const frames = validateSequence(fixture())
  assert.equal(frames.length, 5)
  const first = summarizeStorms(frames[0]), peak = summarizeStorms(frames[1]), later = summarizeStorms(frames[3])
  assert.equal(first.storm_count, 2)
  assert.ok(peak.strongest_wind_m_s > 45 && ['category_2', 'category_3'].includes(peak.strongest_category) && peak.lowest_pressure_hpa < 970)
  assert.ok(later.landfalls_so_far >= 1 && later.mergers_so_far === 1 && later.geneses_so_far >= 1)
  assert.ok(later.storms_over_land >= 1 && frames.some(f => summarizeStorms(f).max_surge_m > 0))
  assert.ok(frames[1].surface.land.some(v => v === 1) && frames[1].surface.land.some(v => v === 0))
  assert.equal(frames[0].storms.synthetic, false)
  const tornado = summarizeSevere(frames[1])
  assert.ok(tornado.max_stp < 1 && tornado.tornado_possible_cells === 0, 'no tornado potential in the sheared-free tropics')
})

test('mock carries storms, surface and severe blocks in the documented format', () => {
  const frames = validateSequence(cloudFormationDemo())
  assert.ok(frames.every(f => f.storms && f.surface && f.severe))
  const summaries = frames.map(summarizeStorms)
  assert.ok(summaries.some(s => s.storms_over_land === 1) && summaries.some(s => s.storms_over_land === 0), 'the mock storm makes landfall')
  assert.ok(summaries.at(-1).landfalls_so_far === 1)
  assert.ok(frames.map(summarizeSevere).some(s => s.tornado_possible_cells > 0))
  assert.equal(frames[0].storms.synthetic, true)
})

test('category helpers', () => {
  assert.equal(CATEGORIES.length, 7)
  assert.equal(categoryFor(16.9).key, 'depression')
  assert.equal(categoryFor(33).key, 'category_1')
  assert.equal(categoryFor(70).key, 'category_5')
  assert.equal(categoryInfo('category_3').short, 'C3')
  assert.equal(categoryInfo('unknown').key, 'depression')
})

test('invalid storm and severe blocks are rejected', () => {
  const wrongCategory = fixture(); wrongCategory.coupled_atmosphere.storms.frames[1].storms[0].category = 'category_5'
  assert.throws(() => validateSequence(wrongCategory), /Saffir-Simpson/)
  const badPressure = fixture(); const s = badPressure.coupled_atmosphere.storms.frames[1].storms[0]; s.central_pressure_pa = s.environment_pressure_pa + 1
  assert.throws(() => validateSequence(badPressure), /below the environment/)
  const badCount = fixture(); badCount.coupled_atmosphere.storms.frames[1].storm_count += 1
  assert.throws(() => validateSequence(badCount), /Storm count/)
  const badEvent = fixture(); badEvent.coupled_atmosphere.storms.events[0].kind = 'eclipse'
  assert.throws(() => validateSequence(badEvent), /events/)
  const noSurface = fixture(); delete noSurface.coupled_atmosphere.surface
  assert.throws(() => validateSequence(noSurface), /require the surface/)
  const badRisk = fixture(); badRisk.coupled_atmosphere.severe.frames[0].tornado_risk[0] = 'significant'
  assert.throws(() => validateSequence(badRisk), /Risk class/)
  const badTotals = fixture(); badTotals.coupled_atmosphere.severe.frames[0].capped_cells += 1
  assert.throws(() => validateSequence(badTotals), /totals/)
  const timeline = validateSequence({ schema_version: 'terraflux.weather.timeline/v1', frames: fixture().frames })
  const shifted = fixture().coupled_atmosphere.storms; shifted.frames[1].elapsed_seconds += 1
  assert.throws(() => validateStormSystems(shifted, timeline), /times must match/)
  const severeShift = fixture().coupled_atmosphere.severe; severeShift.frames.pop()
  assert.throws(() => validateSevereConvection(severeShift, timeline), /frame count/)
})
