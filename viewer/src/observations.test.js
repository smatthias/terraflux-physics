import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { parseObservations, observationSummary, observationAge, observationPlot } from './observations.js'
const records = async () => JSON.parse(await readFile(new URL('../public/observations/mrms-synthetic.json', import.meta.url), 'utf8'))

test('decoder-format observations retain UTC frames, zero reflectivity and nodata', async () => {
  const input = await records(), before = JSON.stringify(input), doc = parseObservations(input.reverse())
  assert.equal(doc.frames.length, 2)
  assert.equal(doc.frames[0].time, '2026-01-01T00:00:00Z')
  const summary = observationSummary(doc.frames[0])
  assert.deepEqual(summary, { decoded: 5, no_data: 1, ambiguous: 0, total: 6, min: 0, max: 55 })
  assert.equal(doc.source.source_kind, 'synthetic_fixture')
  assert.equal(JSON.stringify(input.reverse()), before)
})
test('rejects conflicting products, duplicate locations, invalid dates and quality/value mismatches', async () => {
  for (const mutate of [
    r => { r[1].source.source_id = 'other' },
    r => { r[1].location = r[0].location },
    r => { r[0].valid_time_utc = '2026-02-30T00:00:00Z' },
    r => { r[0].valid_time_utc = '2026-01-01T00:00:00+03:00' },
    r => { r[0].value = null },
    r => { r[3].value = 0 },
    r => { r[0].location.coordinates[1] = 91 },
    r => { r[0].units = 'mm/h' },
  ]) { const r = await records(); mutate(r); assert.throws(() => parseObservations(r)) }
})
test('preview sampling leaves complete counts intact and follows geographic directions', async () => {
  const f = parseObservations(await records()).frames[0]
  const plot = observationPlot(f, 3)
  assert.equal(plot.points.length, 3)
  assert.equal(plot.sampled, true)
  assert.equal(observationSummary(f).total, 6)
  const full = observationPlot(f)
  assert.ok(full.points[1].x > full.points[0].x)
  assert.ok(full.points[3].y < full.points[0].y)
})
test('age distinguishes old, recent and future valid times', () => {
  assert.equal(observationAge(0, 16 * 60000).label, 'Older than 15 minutes')
  assert.equal(observationAge(0, 2 * 60000).label, 'Within 15 minutes')
  assert.equal(observationAge(5 * 60000, 0).label, 'Future valid time')
})
