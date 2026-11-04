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

// ---- Rain on the sea and the coast, evaporation, air-sea heat and the layered column ----------------------
import { summarizeOceanHeat, columnProfile, columnSection, heatSeries, strongestCooling, heatFluxColor, evaporationColor, depthColor, rampColor, PROCESSES } from './ocean.js'

const heatFixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'ocean-heat', 'engine-timeline.json'), 'utf8'))

test('ocean heat and rain fixture validates: rain lenses, coastal runoff, evaporation, heat fluxes and the layered column', () => {
  const frames = validateSequence(heatFixture())
  assert.equal(frames.length, 33)
  const o0 = frames[0].ocean
  assert.deepEqual(o0.processes, ['rain', 'coastal_runoff', 'air_sea', 'thermal_layers'])
  assert.equal(o0.thermal.fractions.length, 10)
  assert.ok(o0.rain.receiver.some(r => r >= 0) && o0.rain.receiver.some((r, i) => r < 0 && !o0.mask[i]), 'a coastal band and an inland hinterland')
  const metrics = frames.map(summarizeOceanHeat)
  assert.ok(Object.values(metrics[0].cumulative).every(v => v === 0), 'totals start after the spin-up (its last fluxes are still shown)')
  for (const m of metrics) {
    assert.ok(m.mean_sst_c > 24 && m.mean_sst_c < 30 && m.min_sst_c <= m.mean_sst_c && m.mean_sst_c <= m.max_sst_c, 'late-summer subtropical SST')
    assert.ok(m.mean_mld_m > 5 && m.mean_mld_m < 80 && m.max_mld_m >= m.mean_mld_m, 'mixed layers of tens of metres')
    assert.ok(m.mean_evaporation_mm_day > 1 && m.mean_evaporation_mm_day < 10, 'a few mm/day of evaporation')
    assert.ok(Math.abs(m.fluxes_w_m2.shortwave + m.fluxes_w_m2.longwave + m.fluxes_w_m2.sensible + m.fluxes_w_m2.latent - m.net_heat_w_m2) < 3, 'mean net heat is the sum of its parts')
    assert.ok(m.fluxes_w_m2.latent < 0 && m.fluxes_w_m2.longwave < 0 && m.fluxes_w_m2.shortwave >= 0)
  }
  const meanNet = metrics.slice(1).reduce((a, m) => a + m.net_heat_w_m2, 0) / (metrics.length - 1)
  assert.ok(meanNet > 10 && meanNet < 120, 'the late-summer sea gains heat on average')
  assert.ok(metrics.some(m => m.max_evaporation_mm_day > 9), 'the cold-air outbreak evaporates strongly')
  assert.ok(metrics.some(m => m.coastal_runoff_m3_s > 5000) && metrics.some(m => m.rain_coast_m3_s > 1000 && m.rain_inland_m3_s > 1000), 'landfall rains on the coast and inland, the coast drains to the sea')
  assert.ok(frames.some(f => f.ocean.lenses.some(l => l.kind === 'rain')) && frames.some(f => f.ocean.lenses.some(l => l.kind === 'river')), 'rain and river lenses')
  // The landfall trail and the coastal runoff merge into one nearshore lens; the land keeps draining into it.
  const runoffShare = f => Math.max(0, ...f.ocean.lenses.map(l => l.source_shares.coastal_runoff))
  assert.ok(runoffShare(frames.at(-1)) > 0.25 && runoffShare(frames.at(-1)) > runoffShare(frames[16]), 'the coastal-runoff share of the nearshore lens grows after landfall')
  assert.ok(metrics.at(-1).cumulative.rain_sea_km3 > 0 && metrics.at(-1).cumulative.runoff_km3 > 0 && metrics.at(-1).cumulative.evaporation_km3 > 0)
  // The water column of a cell.
  const sea = o0.mask.indexOf(true) + 200
  const profile = columnProfile(frames[10], sea)
  assert.equal(profile.layers.length, 10)
  assert.ok(Math.abs(profile.layers.at(-1).bottom - frames[10].ocean.thickness[sea]) < 1e-6 && profile.layers[0].top === 0, 'the layers span the thermocline')
  assert.ok(profile.layers[0].temperature_k > profile.layers.at(-1).temperature_k + 5, 'warm surface over a cold thermocline')
  assert.ok(profile.layers.every((l, k) => k === 0 || l.density_kg_m3 >= profile.layers[k - 1].density_kg_m3 - 1e-3), 'a statically stable column')
  assert.equal(columnProfile(frames[10], o0.mask.indexOf(false)), null, 'no column on land')
  const section = columnSection(frames, sea)
  assert.equal(section.rects.length, 33 * 10)
  assert.ok(section.end > section.start && section.temperature_range_k[1] > section.temperature_range_k[0])
  const series = heatSeries(frames)
  assert.equal(series.length, 33)
  assert.ok(series.some(p => p.rain_sea_m3_s > 0) && series.some(p => p.evaporation_m3_s > 0))
  const cooled = strongestCooling(frames[0], frames.at(-1))
  assert.ok(o0.mask[cooled] && frames[0].ocean.thermal.sst[cooled] > frames.at(-1).ocean.thermal.sst[cooled], 'the strongest cooling is found')
})

test('the slab fixture has no extensions and no heat metrics', () => {
  const frames = validateSequence(fixture())
  assert.deepEqual(frames[0].ocean.processes, [])
  assert.equal(summarizeOceanHeat(frames[3]), null)
  assert.equal(columnProfile(frames[3], frames[3].ocean.mask.indexOf(true)), null)
})

test('ocean extension contract rejects inconsistent blocks', () => {
  const base = heatFixture()
  const frames = validateSequence(base)
  const terrain = frames[0].terrain
  const sea = base.ocean.ocean_mask.indexOf(true), land = base.ocean.ocean_mask.indexOf(false)
  const mutate = (f, message) => {
    const copy = JSON.parse(JSON.stringify(base.ocean))
    f(copy)
    assert.throws(() => validateOcean(copy, frames, terrain), new RegExp(message))
  }
  mutate(o => { o.processes = ['rain', 'volcanoes'] }, 'Invalid ocean processes')
  mutate(o => { o.thermal_layers.fractions[0] *= 2 }, 'Invalid thermal layers')
  mutate(o => { o.coastal.receiver[land] = land }, 'Invalid coastal drainage')
  mutate(o => { o.frames[4].cumulative_m3.evaporation *= 3 }, 'Ocean volume budget')
  mutate(o => { o.frames[4].cumulative_m3.coastal_loss += 1e9 }, 'Coastal water budget')
  mutate(o => { o.frames[4].heat_budget.heat_balance_error_j = 1e20 }, 'heat budget does not close')
  mutate(o => { o.frames[4].air_sea.net_heat_w_m2[sea + 200] += 300 }, 'Net heat flux must be')
  mutate(o => { o.frames[4].air_sea.latent_w_m2[land] = -50 }, 'Land cells carry no air-sea fluxes')
  mutate(o => { o.frames[4].rain.rate_mm_h[sea + 200] += 500 }, 'Rain falls everywhere|Rain on the sea disagrees')
  mutate(o => { o.frames[4].rain.coastal_inflow_m3_s[land] = 5 }, 'only the sea receives it')
  mutate(o => { o.frames[4].thermal.layer_temperature_k[2][land] = 290 }, 'Land cells carry no column')
  mutate(o => { o.frames[4].thermal.sst_k[sea + 200] += 1 }, 'The surface must be the top layer')
  mutate(o => { o.frames[4].thermal.mixed_layer_depth_m[sea + 200] = 5000 }, 'Mixed-layer depths must lie within the layer')
  mutate(o => { o.frames[4].surface_lenses[0].kind = 'volcanic' }, 'Invalid surface lenses')
  mutate(o => { delete o.frames[4].heat_budget }, 'Invalid ocean heat budget')
})

test('every ocean colour helper produces a colour THREE can parse', async () => {
  const { Color } = await import('three')
  const samples = [speedColor(0.4), salinityColor(33), salinityColor(36), temperatureColor(290), heatFluxColor(-200), heatFluxColor(150), evaporationColor(6), depthColor(40), rampColor(299, 295, 303)]
  for (const css of samples) {
    const parsed = new Color(0x123456).setStyle(css)
    assert.notEqual(parsed.getHex(), 0x123456, `THREE ignored ${css}`)
  }
})

test('heat and rain styling helpers', () => {
  assert.equal(PROCESSES.length, 4)
  assert.notEqual(heatFluxColor(-300), heatFluxColor(300))
  assert.equal(heatFluxColor(800), heatFluxColor(400), 'saturates')
  assert.ok(heatFluxColor(0).startsWith('rgb('))
  assert.ok(evaporationColor(5).startsWith('hsl(') && evaporationColor(30) === evaporationColor(15))
  assert.notEqual(depthColor(10), depthColor(60))
  assert.equal(rampColor(300, 290, 300), rampColor(310, 290, 300))
})
