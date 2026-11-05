import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { validateCoast, summarizeCoast, coastRanking, coastEventTable, coastSeries, firstYearMaps, coastalCells, cellFrequency, returnPeriodAfterRise,
  floodDepthColor, yearLostColor, frequencyColor, coastLandColor, coastSeaColor, COAST_EVENTS, RANK_METRICS } from './coastal.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'coast', 'engine-timeline.json'), 'utf8'))
const scenario = (b, prefix) => b.scenarios.find(s => s.name.startsWith(prefix)).name

test('coastal fixture validates: maps per year of the high scenario, five regions, three scenarios', () => {
  const frames = validateSequence(fixture())
  const c = frames[0].coast, b = c.block
  assert.equal(frames.length, b.map_years.length)
  assert.deepEqual(b.region_names, ['Delta Lowlands', 'Estuary City', 'Barrier Coast', 'Cliff Coast', 'Low Islands'])
  assert.equal(b.scenarios.length, 3)
  assert.equal(c.year, 2020)
  assert.ok(c.lost.every(v => v === 0), 'nothing is lost today')
  assert.ok(frames.at(-1).coast.lost.some(v => v === 1), 'land is lost by the end')
  assert.ok(b.defences.length > 50 && b.rivers.length === 3)
})

test('summaries: the sea rises with the scenario, islands and the delta are hit, cliffs hold', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].coast.block
  const k2100 = b.map_years.indexOf(2100)
  const low = summarizeCoast(frames[k2100], scenario(b, 'low')), high = summarizeCoast(frames[k2100], scenario(b, 'high'))
  assert.ok(low.global_rise_m < high.global_rise_m && high.global_rise_m > 0.5 && high.global_rise_m < 0.9, 'AR6-like global rise by 2100')
  assert.ok(Object.values(high.components).every(v => v >= 0) && Math.abs(Object.values(high.components).reduce((a, v) => a + v, 0) - high.global_rise_m) < 1e-3)
  assert.ok(high.people_storm_flooded >= high.people_storm_today, 'more people under the design storm than today')
  const ranking = coastRanking(high, 'lost_pct')
  assert.equal(ranking[0].name, 'Low Islands', 'the low islands lose the largest share of their land')
  assert.equal(ranking.at(-1).name, 'Cliff Coast')
  assert.equal(ranking.at(-1).lost_pct, 0)
  const frequency = coastRanking(high, 'design_flood_return_years')
  assert.equal(frequency[0].name, 'Delta Lowlands', 'the sinking delta sees its 100-year flood most often')
  assert.ok(frequency[0].design_flood_return_years < 5)
  assert.ok(Object.keys(RANK_METRICS).every(m => coastRanking(high, m).length === 5))
  const end = summarizeCoast(frames.at(-1), scenario(b, 'high'))
  const delta = end.regions.find(r => r.name === 'Delta Lowlands')
  assert.ok(delta.lost_pct > 50 && delta.people_on_lost_land > 1e6, 'by 2300 the delta is lost once its dikes are overtopped at high water')
  assert.ok(end.rivers.every(r => r.surge_reach_km >= r.surge_today_km && r.salt_intrusion_km > r.salt_today_km), 'the sea pushes further up every river')
})

test('events, first-year maps, cell frequency and chart series', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].coast.block
  const table = coastEventTable(b)
  assert.equal(table.length, 5)
  const delta = table.find(r => r.region === 'Delta Lowlands')
  const overtopped = delta.scenarios.map(s => s.events.defences_overtopped)
  assert.ok(overtopped.every(y => y !== null) && overtopped[2] <= overtopped[1] && overtopped[1] <= overtopped[0], 'the more warming, the sooner the delta dikes are overtopped')
  const islands = table.find(r => r.region === 'Low Islands').scenarios.map(s => s.events.island_half_lost)
  assert.ok(islands[2] <= islands[0], 'islands go first under the high scenario')
  assert.ok(COAST_EVENTS.every(e => e in delta.scenarios[0].events))
  const maps = firstYearMaps(b, scenario(b, 'high'))
  const last = frames.at(-1).coast
  assert.ok(last.lost.every((v, i) => v < 0.5 || (maps.lost[i] !== null && maps.lost[i] <= last.year)))
  assert.ok(maps.storm.filter(y => y === 2020).length > 100, 'today\'s design storm already floods low land')
  const coastal = coastalCells(last, frames[0].forecast.grid)
  const cell = coastal.findIndex((v, i) => v && b.subsidence_mm_yr[i] > 4)
  const f = cellFrequency(frames[0].coast, scenario(b, 'high'), cell)
  assert.equal(f.relative_rise_m, 0)
  assert.ok(Math.abs(f.return_years - 100) < 1e-6, 'today the design flood is the 100-year flood')
  const later = cellFrequency(frames[b.map_years.indexOf(2100)].coast, scenario(b, 'high'), cell)
  assert.ok(later.relative_rise_m > frames[b.map_years.indexOf(2100)].coast.rise && later.return_years < 5, 'subsidence adds to the rise')
  assert.ok(Math.abs(returnPeriodAfterRise(0.6, 0.2, 1000, 0.2) - 1000 / Math.E) / (1000 / Math.E) < 0.01)
  const s = coastSeries(b, x => x.sea_level.regional_m)
  assert.equal(s.length, 3)
  assert.ok(s.every(x => x.points.length === b.output_years.length))
})

test('coastal contract rejects inconsistent blocks', () => {
  const base = fixture()
  const frames = validateSequence(base)
  const terrain = frames[0].terrain
  const land = base.coast.region.findIndex((r, i) => r === 0 && !base.coast.sea_cells.includes(i))
  const mutate = (f, message) => {
    const copy = JSON.parse(JSON.stringify(base.coast))
    f(copy)
    assert.throws(() => validateCoast(copy, frames, terrain), new RegExp(message))
  }
  mutate(c => { c.method = 'other' }, 'Unsupported coastal contract')
  mutate(c => { c.population[c.sea_cells[0]] = 10 }, 'people live on land')
  mutate(c => { c.scenarios[0].sea_level.global_m[3] += 0.5 }, 'sum of its parts')
  mutate(c => { c.map_frames[2].lost_fraction[land] = 1 }, 'Lost land disagrees|carry its year|protected')
  mutate(c => { c.map_frames[2].lost_fraction[c.sea_cells[0]] = 0.5 }, 'Only land is lost')
  mutate(c => { c.map_frames[3].storm_depth_m[land] += 5 }, 'Storm-flooded land disagrees')
  mutate(c => { c.scenarios[1].regions[0].events.lost_10pct = 1900 }, 'invalid events')
  mutate(c => { c.defences.push([c.sea_cells[0], 5]) }, 'Defences stand on land')
  mutate(c => { c.map_years = c.map_years.slice(1) }, 'Map years')
  mutate(c => { c.scenarios[0].regions[2].series.lost_pct.pop() }, 'series must match')
})

test('coastal colours', async () => {
  const { Color } = await import('three')
  for (const css of [floodDepthColor(1), yearLostColor(2050, 2020, 2300), frequencyColor(3), coastLandColor(2), coastSeaColor(-20)]) {
    const parsed = new Color(0x123456).setStyle(css)
    assert.notEqual(parsed.getHex(), 0x123456, `THREE ignored ${css}`)
  }
  assert.equal(yearLostColor(null, 2020, 2300), null)
  assert.notEqual(floodDepthColor(0.2), floodDepthColor(2.5))
  assert.equal(floodDepthColor(5), floodDepthColor(3))
  assert.notEqual(frequencyColor(1), frequencyColor(100))
})
