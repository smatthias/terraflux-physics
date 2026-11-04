import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { validateOcean, summarizeOcean, streamLabel, speedColor, salinityColor, temperatureColor, circulationSeries } from './ocean.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'ocean', 'engine-timeline.json'), 'utf8'))

test('Atlantic streams fixture validates: boundary currents, river plumes, and a meltwater pulse that weakens the overturning', () => {
  const frames = validateSequence(fixture())
  assert.equal(frames.length, 22)
  const o0 = frames[0].ocean
  assert.equal(o0.u.length, 2000)
  assert.ok(o0.mask.filter(Boolean).length > 1500)
  const summaries = frames.map(summarizeOcean)
  assert.ok(summaries[0].stream_count >= 3 && summaries[0].main_stream.transport_sv > 10, 'streams are detected with transports in Sverdrups')
  assert.ok(frames[0].ocean.streams.some(s => s.direction === 'northward' && (s.core_cell % 50) < 12), 'a northward western boundary current exists')
  assert.ok(summaries[0].overturning_sv > 0.5, 'deep-water formation before the pulse')
  const before = summaries[10].overturning_sv, after = summaries.at(-1).overturning_sv
  assert.ok(after < 0.3 * before, 'the meltwater pulse collapses the overturning')
  assert.ok(summaries.at(-1).min_surface_salinity_psu < 25, 'the meltwater plume is very fresh at the surface')
  assert.ok(summaries.at(-1).plume_area_km2 > summaries[0].plume_area_km2 * 5, 'plumes grow with the pulse')
  assert.ok(frames.every(f => f.ocean.exchangePoints.length === 4), 'four exchange points')
  const series = circulationSeries(frames)
  assert.equal(series.length, 22)
  assert.ok(series.at(-1).river_sv > series[0].river_sv)
  assert.equal(o0.synthetic, false)
  assert.ok(frames[0].ocean.formationCells > 0 && frames[0].ocean.freshwater.every(v => v >= 0 && v <= 1))
})

test('ocean contract rejects inconsistent blocks', () => {
  const base = fixture()
  const frames = validateSequence(base)
  const terrain = frames[0].terrain
  const mutate = (f, message) => {
    const copy = JSON.parse(JSON.stringify(base.ocean))
    f(copy)
    assert.throws(() => validateOcean(copy, frames, terrain), new RegExp(message))
  }
  mutate(o => { o.method = 'other' }, 'Unsupported ocean contract')
  mutate(o => { const i = o.ocean_mask.indexOf(false); o.frames[2].u_m_s[i] = 0.5 }, 'Land cells carry no ocean state')
  mutate(o => { const i = o.ocean_mask.indexOf(true); o.frames[2].surface_salinity_psu[i] = o.frames[2].salinity_psu[i] + 1 }, 'Surface salinity cannot exceed')
  mutate(o => { o.frames[3].formation_cells += 1 }, 'Formation cell count disagrees')
  mutate(o => { o.frames[3].overturning_sv *= 2 }, 'Overturning disagrees')
  mutate(o => { o.frames[4].streams[0].core_cell = o.ocean_mask.indexOf(true) }, 'Stream cores must lie in their stream|Invalid stream')
  mutate(o => { o.frames[4].cumulative_m3.river = -5 }, 'volume budget|cannot decrease')
  mutate(o => { o.frames[5].volume_m3 += 1e12 }, 'Ocean volume budget')
  mutate(o => { o.frames[1].exchange_points[0].deflection_deg = 400 }, 'Invalid exchange points')
  mutate(o => { o.sponge[o.ocean_mask.indexOf(false)] = 0.5 }, 'Invalid sponge')
})

test('styling helpers', () => {
  assert.ok(speedColor(0).startsWith('rgb(') && speedColor(2) === speedColor(1))
  assert.notEqual(salinityColor(32), salinityColor(35))
  assert.equal(salinityColor(35), salinityColor(35, 35))
  assert.ok(temperatureColor(280).startsWith('hsl('))
  const grid = { nx: 50, ny: 40 }
  assert.match(streamLabel({ core_cell: 5 + 10 * 50, direction: 'northward', transport_sv: 12.3 }, grid), /western boundary current \(northward, 12\.3 Sv\)/)
  assert.match(streamLabel({ core_cell: 25 + 38 * 50, direction: 'westward', transport_sv: 70 }, grid), /northern jet/)
})
