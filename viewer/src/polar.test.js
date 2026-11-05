import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { validatePolar, summarizePolar, eventTable, scenarioSeries, scenarioColor, iceThicknessColor, smbColor, EVENT_KEYS, EVENT_LABELS } from './polar.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'polar', 'engine-timeline.json'), 'utf8'))
const find = (block, prefix) => block.scenarios.find(s => s.name.startsWith(prefix))

test('polar melt fixture validates: Greenland maps per output year and all scenarios', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].polar.block
  assert.equal(frames.length, b.output_years.length)
  assert.equal(frames[0].polar.year, b.output_years[0])
  assert.equal(frames[0].polar.synthetic, false)
  assert.ok(b.scenarios.some(s => s.control), 'a control run separates the model drift from the forced response')
  assert.ok(frames[0].polar.thickness.some(h => h > 2000), 'Greenland starts with a thick dome')
  // Greenland of the map scenario loses ice and raises the sea.
  const first = frames[0].polar, last = frames.at(-1).polar
  assert.ok(last.volume < first.volume && last.seaLevelRise > first.seaLevelRise + 0.1)
  assert.ok(b.calibration.threshold_sv > b.calibration.present_freshwater_sv)
})

test('metrics: the high scenario weakens and collapses the overturning, warms and de-oxygenates the tropics', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].polar.block
  const high = find(b, 'high'), low = find(b, 'low')
  const m0 = summarizePolar(frames[0], high.name), mEnd = summarizePolar(frames.at(-1), high.name)
  assert.equal(m0.scenario, high.name)
  assert.ok(Math.abs(m0.overturning_pct - 100) < 5, 'the run starts at the present-day strength')
  assert.ok(mEnd.overturning_pct < 20, 'collapsed by the end of the high scenario')
  assert.ok(mEnd.freshwater_sv > mEnd.threshold_sv, 'past the tipping point')
  assert.ok(mEnd.recovery_time_years === null, 'no stable strong state is left once the threshold is passed')
  assert.ok(mEnd.tropical_atlantic_change_k > 1, 'the tropics warm')
  assert.ok(mEnd.south_oxygen_change_pct < 0, 'warmer water holds less oxygen')
  assert.ok(mEnd.arctic_ice_september_m < 0.05 && mEnd.arctic_ice_free_days > 30, 'the Arctic is ice-free in late summer')
  assert.ok(mEnd.greenland_smb_gt_yr < 0)
  const lowEnd = summarizePolar(frames.at(-1), low.name)
  assert.ok(lowEnd.overturning_pct > 60, 'the low scenario stays on the strong branch')
  assert.ok(lowEnd.recovery_time_years !== null && lowEnd.recovery_time_years > 0)
  // An unknown scenario name falls back to the map scenario.
  assert.equal(summarizePolar(frames[5], 'nope').scenario, b.map_scenario)
})

test('event table answers "when do the currents collapse?" per scenario and across the ensemble', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].polar.block
  const rows = eventTable(b)
  assert.equal(rows.length, b.scenarios.filter(s => !s.control).length, 'the control has no events row')
  const high = rows.find(r => r.scenario.startsWith('high')), low = rows.find(r => r.scenario.startsWith('low'))
  for (const key of EVENT_KEYS) assert.ok(key in high.events && typeof EVENT_LABELS[key] === 'string')
  const e = high.events
  assert.ok(e.threshold_crossed && e.overturning_collapsed, 'the high scenario tips and collapses')
  assert.ok(e.overturning_weakened_25pct.year <= e.overturning_weakened_50pct.year && e.overturning_weakened_50pct.year <= e.overturning_collapsed.year)
  assert.ok(e.threshold_crossed.year < e.overturning_collapsed.year, 'the collapse lags the tipping point')
  assert.equal(high.lag_years, e.overturning_collapsed.year - e.threshold_crossed.year)
  assert.ok(e.overturning_collapsed.global_warming_k > e.threshold_crossed.global_warming_k)
  assert.ok(e.first_ice_free_september.year < e.threshold_crossed.year, 'the summer sea ice goes first')
  assert.equal(high.ensemble.collapsed, high.ensemble.members, 'every ensemble member collapses in the high scenario')
  assert.ok(high.ensemble.earliest <= e.overturning_collapsed.year && e.overturning_collapsed.year <= high.ensemble.latest)
  assert.equal(low.events.overturning_collapsed, null)
  assert.equal(low.ensemble.collapsed, 0)
  assert.ok(high.sea_level_2100_m > low.sea_level_2100_m && high.sea_level_end_m > 0.5)
})

test('chart series and colours', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].polar.block
  const series = scenarioSeries(b, 'overturning_sv')
  assert.equal(series.length, b.scenarios.length)
  assert.ok(series.every(s => s.points.length === b.output_years.length && s.points.every(([y, v]) => Number.isInteger(y) && Number.isFinite(v))))
  assert.equal(scenarioColor(0, true), '#8a9aa5')
  assert.notEqual(scenarioColor(0, false), scenarioColor(1, false))
  assert.equal(iceThicknessColor(0), null)
  assert.notEqual(iceThicknessColor(100), iceThicknessColor(3000))
  assert.equal(iceThicknessColor(3000), iceThicknessColor(5000))
  assert.notEqual(smbColor(-2), smbColor(2))
  assert.equal(smbColor(-3), smbColor(-10))
})

test('polar contract rejects inconsistent blocks', () => {
  const base = fixture()
  const frames = validateSequence(base)
  const terrain = frames[0].terrain
  const mutate = (f, message) => {
    const copy = JSON.parse(JSON.stringify(base.polar))
    f(copy)
    assert.throws(() => validatePolar(copy, frames, terrain), new RegExp(message))
  }
  mutate(b => { b.method = 'other' }, 'Unsupported polar contract')
  mutate(b => { b.output_years[3] = b.output_years[2] }, 'output years')
  mutate(b => { b.calibration.threshold_sv = b.calibration.present_freshwater_sv * 0.5 }, 'calibration')
  mutate(b => { b.scenarios[1].series.overturning_sv.pop() }, 'series must match')
  mutate(b => { const ev = find(b, 'high').events; ev.overturning_weakened_50pct.year = ev.overturning_collapsed.year + 10 }, 'Weakening must precede')
  mutate(b => { find(b, 'high').ensemble.collapsed_members -= 1 }, 'collapse count')
  mutate(b => { const i = b.land_mask.indexOf(false); b.greenland_frames[4].ice_thickness_m[i] = 50 }, 'Ice rests on land')
  mutate(b => { b.greenland_frames[6].sea_level_rise_m += 0.5 }, 'Sea-level rise must equal the ice lost')
  mutate(b => { b.equilibrium_diagram.start.reversed[0][1] = 3 }, 'equilibrium diagram')
  assert.throws(() => validatePolar(base.polar, frames, { values: [1, 2] }), /terrain/)
})

test('optional early-warning statistics of a noisy run are validated and listed', () => {
  const base = fixture()
  const frames = validateSequence(base)
  const terrain = frames[0].terrain
  const copy = JSON.parse(JSON.stringify(base.polar))
  const high = find(copy, 'high')
  high.early_warning = { window_years: 50, until_year: 2137, autocorrelation: [0.1, 0.2, 0.3], variance: [0.01, 0.02, 0.03], kendall_tau_autocorrelation: 0.3, kendall_tau_variance: 0.55, caveat: 'windows' }
  validatePolar(copy, frames, terrain)
  const row = eventTable(copy).find(r => r.scenario === high.name)
  assert.deepEqual(row.early_warning, { window: 50, until: 2137, autocorrelation_tau: 0.3, variance_tau: 0.55 })
  assert.equal(eventTable(copy).find(r => r.scenario.startsWith('low')).early_warning, null)
  high.early_warning.kendall_tau_variance = 1.5
  assert.throws(() => validatePolar(copy, frames, terrain), /early-warning/)
})
