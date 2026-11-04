import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { validateHydrology, summarizeHydrology, waterPath, riverSegments, hydrographSeries, floodClassInfo, waterColor, riverWidth, sedimentColor, basinColor, FLOOD_CLASSES } from './hydrology.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'flood', 'engine-timeline.json'), 'utf8'))

test('alpine flood fixture validates: rivers reach the sea, lakes fill, the cloudburst floods the plain and triggers landslides', () => {
  const frames = validateSequence(fixture())
  assert.equal(frames.length, 25)
  const h0 = frames[0].hydrology, n = h0.network
  assert.equal(h0.rain.length, 1200)
  assert.ok(n.lakes.length >= 5 && n.basins.length > 10 && n.channel.filter(Boolean).length > 300)
  const summaries = frames.map(summarizeHydrology)
  assert.equal(summaries[0].landslides_total, 0)
  const peak = summaries.reduce((best, s) => s.max_discharge_m3_s > best.max_discharge_m3_s ? s : best)
  assert.ok(peak.max_discharge_m3_s > 100 && peak.flooded_area_km2 > 10, 'the burst produces a flood')
  assert.ok(summaries.some(s => s.class_cells.high > 0), 'some cells reach the high flood class')
  assert.ok(summaries.at(-1).landslides_total > 10 && summaries.at(-1).sediment_yield_t > 1000, 'landslides and sediment yield after the storm')
  assert.ok(summaries.at(-1).runoff_ratio > 0.05 && summaries.at(-1).runoff_ratio < 1, 'part of the rain leaves as runoff')
  assert.ok(summaries.some(s => s.threshold_cells > 0), 'the intensity-duration threshold is exceeded during the storm')
  // Every land cell drains to the ocean or across the edge; the path length matches the network distance.
  for (const cell of [0, 500, 700, 1199]) {
    const path = waterPath(n, cell)
    assert.ok(['ocean', 'edge'].includes(path.ends))
    assert.ok(path.cells.length >= 1 && path.cells.every(c => c >= 0 && c < 1200))
  }
  assert.equal(riverSegments(n, frames[0].forecast.grid).length, n.channel.filter(Boolean).length)
  const series = hydrographSeries(frames, peak.peak_cell)
  assert.equal(series.length, 25)
  assert.ok(Math.max(...series.map(p => p.discharge_m3_s)) === peak.max_discharge_m3_s)
  // Lakes never exceed their spill level and the big lake fills during the storm.
  const big = n.lakes.reduce((a, l) => l.capacity_m3 > a.capacity_m3 ? l : a)
  assert.ok(frames.every(f => f.hydrology.lakeLevel[big.id] <= big.spill_elevation_m + 1e-6))
  assert.ok(frames.at(-1).hydrology.lakeVolume[big.id] > frames[0].hydrology.lakeVolume[big.id], 'the lake gains water')
  assert.equal(h0.synthetic, false)
  assert.ok(h0.events.length > 0 && h0.events.every(e => ['slope', 'channel', 'lake', 'ocean', 'edge'].includes(e.deposited_in)))
})

test('hydrology contract rejects inconsistent blocks', () => {
  const base = fixture()
  const frames = validateSequence(base)
  const terrain = frames[0].terrain
  const mutate = (f, message) => {
    const copy = JSON.parse(JSON.stringify(base.hydrology))
    f(copy)
    assert.throws(() => validateHydrology(copy, frames, terrain), new RegExp(message))
  }
  mutate(h => { h.method = 'other' }, 'Unsupported hydrology contract')
  mutate(h => { const i = h.network.channel.findIndex((v, j) => !v && !h.network.ocean[j]); h.frames[3].discharge_m3_s[i] = 1 }, 'Discharge exists on channel cells only')
  mutate(h => { h.frames[2].storage_m3.total += 1000 }, 'Storage components do not add up')
  mutate(h => { h.frames[2].flood_class_cells.low += 1 }, 'Frame counts disagree')
  mutate(h => { h.frames[2].erosion_mm[5] = -1 }, 'Invalid erosion')
  mutate(h => { h.frames[8].cumulative_m3.precipitation = 0 }, 'Water budget|Cumulative totals cannot decrease')
  mutate(h => { h.frames[4].lake_volume_m3[0] = h.network.lakes[0].capacity_m3 * 2 }, 'Lakes cannot exceed')
  mutate(h => { h.network.downstream[h.network.ocean.indexOf(true)] = 3 }, 'Ocean cells are not routed')
  mutate(h => { h.events.push({ kind: 'landslide', elapsed_seconds: 1, cell: 5, runout_cell: -1, volume_m3: 1, factor_of_safety: 1.2, deposited_in: 'slope' }) }, 'Invalid landslide events')
  mutate(h => { h.frames[1].landslide[0] = true }, 'A landslide needs a defined factor of safety')
})

test('styling helpers', () => {
  assert.equal(FLOOD_CLASSES.length, 5)
  assert.equal(floodClassInfo(3).key, 'high')
  assert.equal(floodClassInfo(99).key, 'none')
  assert.equal(waterColor(0.5), null)
  assert.ok(waterColor(10).startsWith('rgb('))
  assert.ok(riverWidth(1) < riverWidth(100) && riverWidth(1e6) === 1)
  assert.equal(sedimentColor(0, 0), null)
  assert.equal(sedimentColor(2, 0.1).color, '#ff5c5c')
  assert.equal(sedimentColor(0.1, 2).color, '#d9b36b')
  assert.equal(basinColor(-1), '#10324a')
  assert.equal(basinColor(0), basinColor(12))
})
