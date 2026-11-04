// Polar melt support: validation of the documented terraflux.polar.melt/v1 block (Greenland maps per frame,
// warming scenarios with the overturning, Greenland, the Arctic sea ice and ecosystem-relevant indicators, the
// events, the uncertainty ensemble and the equilibrium diagram of the overturning), metrics, the "when" table,
// chart series and colours. No physics runs in the browser: the viewer checks the engine output.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const POLAR_SCHEMA = 'terraflux.polar.melt/v1'
export const POLAR_METHOD = 'ice_sheet_sia_pdd_frontal_sea_ice_column_stommel_box'
export const EVENT_KEYS = ['first_ice_free_september', 'greenland_negative_smb', 'threshold_crossed', 'overturning_weakened_25pct', 'overturning_weakened_50pct', 'overturning_collapsed']
export const EVENT_LABELS = {
  first_ice_free_september: 'First ice-free September (Arctic)',
  greenland_negative_smb: 'Greenland surface mass balance turns negative',
  threshold_crossed: 'Overturning tipping point crossed (no stable strong state left)',
  overturning_weakened_25pct: 'Overturning weakened by 25 %',
  overturning_weakened_50pct: 'Overturning weakened by 50 %',
  overturning_collapsed: 'Overturning collapsed (below 20 % of today)',
}
const SERIES = ['global_warming_k', 'overturning_sv', 'heat_transport_pw', 'threshold_sv', 'freshwater_sv', 'freshwater_greenland_sv', 'freshwater_arctic_sv', 'freshwater_hydrological_sv',
  'recovery_time_years', 'north_temperature_k', 'south_temperature_k', 'greenland_volume_m3', 'greenland_sea_level_equivalent_m', 'sea_level_rise_m', 'greenland_smb_gt_yr', 'greenland_melt_gt_yr',
  'arctic_ice_march_m', 'arctic_ice_september_m', 'arctic_min_concentration', 'arctic_ice_free_days', 'north_atlantic_temperature_change_k', 'tropical_atlantic_temperature_change_k',
  'north_oxygen_change_pct', 'south_oxygen_change_pct']
const OCEAN_AREA = 3.618e14, ICE_TO_WATER = 0.917

const validEvent = e => e === null || (e && Number.isInteger(e.year) && (e.global_warming_k === null || finite(e.global_warming_k)))
const nullableSeries = (v, n) => Array.isArray(v) && v.length === n && v.every(x => x === null || finite(x))

export function validatePolar(block, frames, terrain) {
  const b = block, grid = frames[0].forecast.grid, count = grid.nx * grid.ny
  check(b?.schema_version === POLAR_SCHEMA && b.method === POLAR_METHOD, 'Unsupported polar contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Polar source must be engine_output or synthetic.')
  check(terrain && Array.isArray(terrain.values) && terrain.values.length === count, 'Polar melt requires the terrain (bed) grid.')
  check(Array.isArray(b.output_years) && b.output_years.length === frames.length && b.output_years.every((y, k) => Number.isInteger(y) && (k === 0 || y > b.output_years[k - 1])), 'Polar output years must match the frames.')
  check(Array.isArray(b.land_mask) && b.land_mask.length === count && b.land_mask.every(v => typeof v === 'boolean') && b.land_mask.some(Boolean), 'Invalid land mask.')
  check(Array.isArray(b.greenland_frames) && b.greenland_frames.length === frames.length, 'Greenland frames must match the timeline.')
  const c = b.calibration
  check(c && ['flow_constant_m3_s', 'present_freshwater_sv', 'threshold_sv', 'recovery_time_years'].every(k => finite(c[k])) && c.threshold_sv > c.present_freshwater_sv, 'Invalid overturning calibration: the present state must lie below the threshold.')
  const n = b.output_years.length
  check(Array.isArray(b.scenarios) && b.scenarios.length >= 1, 'Polar melt needs scenarios.')
  const names = new Set()
  for (const s of b.scenarios) {
    check(s && typeof s.name === 'string' && !names.has(s.name) && typeof s.control === 'boolean' && finite(s.initial_overturning_sv), 'Invalid scenario.')
    names.add(s.name)
    check(s.series && SERIES.every(k => nullableSeries(s.series[k], n)), `Scenario ${s.name}: series must match the output years.`)
    check(s.events && EVENT_KEYS.every(k => k in s.events && validEvent(s.events[k])), `Scenario ${s.name}: invalid events.`)
    const ev = s.events
    if (ev.overturning_collapsed && ev.overturning_weakened_50pct) check(ev.overturning_weakened_50pct.year <= ev.overturning_collapsed.year, 'Weakening must precede the collapse.')
    if (ev.overturning_weakened_50pct && ev.overturning_weakened_25pct) check(ev.overturning_weakened_25pct.year <= ev.overturning_weakened_50pct.year, 'Weakening order.')
    check(Array.isArray(s.trajectory) && s.trajectory.every(p => Array.isArray(p) && p.length === 2 && p.every(finite)), 'Invalid trajectory.')
    if (s.ensemble) {
      const e = s.ensemble
      check(Array.isArray(e.members) && e.members.length === e.member_count && e.members.every(m => finite(m.salinity_difference_psu) && finite(m.greenland_warming_amplification) && m.events && EVENT_KEYS.every(k => validEvent(m.events[k]))), `Scenario ${s.name}: invalid ensemble members.`)
      const collapses = e.members.filter(m => m.events.overturning_collapsed).map(m => m.events.overturning_collapsed.year)
      check(e.collapsed_members === collapses.length, 'Ensemble collapse count disagrees with its members.')
      if (collapses.length) check(e.earliest_collapse === Math.min(...collapses) && e.latest_collapse === Math.max(...collapses), 'Ensemble collapse range disagrees with its members.')
    }
  }
  const d = b.equilibrium_diagram
  const branch = x => x && finite(x.threshold_sv) && ['stable_strong', 'unstable', 'reversed'].every(k => Array.isArray(x[k]) && x[k].every(p => Array.isArray(p) && p.length === 2 && p.every(finite)))
  check(d && branch(d.start) && branch(d.warmest) && d.start.reversed.every(p => p[1] < 0) && d.start.unstable.every(p => p[1] > 0), 'Invalid equilibrium diagram.')
  const first = b.greenland_frames[0]
  frames.forEach((frame, k) => {
    const g = b.greenland_frames[k]
    check(g && g.year === b.output_years[k] && g.elapsed_seconds === frame.forecast.elapsed_seconds, 'Greenland frame times must match the timeline.')
    check(['ice_thickness_m', 'surface_elevation_m', 'smb_m_ice_yr', 'melt_m_ice_yr'].every(key => Array.isArray(g[key]) && g[key].length === count && g[key].every(finite)), 'Greenland maps must match the grid.')
    check(g.ice_thickness_m.every((h, i) => h >= 0 && (b.land_mask[i] || h === 0)), 'Ice rests on land and is never negative.')
    check(finite(g.volume_m3) && g.volume_m3 >= 0 && finite(g.sea_level_rise_m), 'Invalid Greenland totals.')
    // The sea rises by the ice lost, as water over the global ocean.
    check(Math.abs((g.sea_level_rise_m - first.sea_level_rise_m) - (first.volume_m3 - g.volume_m3) * ICE_TO_WATER / OCEAN_AREA) <= 1e-3 + 1e-3 * Math.abs(g.sea_level_rise_m), 'Sea-level rise must equal the ice lost.')
    frame.polar = {
      year: g.year, thickness: g.ice_thickness_m, surface: g.surface_elevation_m, smb: g.smb_m_ice_yr, melt: g.melt_m_ice_yr, volume: g.volume_m3, seaLevelRise: g.sea_level_rise_m,
      land: b.land_mask, index: k, block: b, synthetic: b.source_kind === 'synthetic',
    }
  })
  return b
}

const at = (series, k) => (series && series[k] !== undefined ? series[k] : null)

// Metrics of one scenario at the frame's year.
export function summarizePolar(frame, scenarioName) {
  const p = frame.polar, b = p.block, k = p.index
  const s = b.scenarios.find(x => x.name === scenarioName) ?? b.scenarios.find(x => x.name === b.map_scenario) ?? b.scenarios[0]
  const v = key => at(s.series[key], k)
  const q = v('overturning_sv')
  let iceArea = 0
  p.thickness.forEach(h => { if (h > 1) iceArea++ })
  return {
    scenario: s.name, year: p.year,
    global_warming_k: v('global_warming_k'),
    overturning_sv: q, overturning_pct: q === null ? null : q / s.initial_overturning_sv * 100,
    threshold_sv: v('threshold_sv'), freshwater_sv: v('freshwater_sv'), recovery_time_years: v('recovery_time_years'), heat_transport_pw: v('heat_transport_pw'),
    freshwater_components_sv: { greenland: v('freshwater_greenland_sv'), arctic: v('freshwater_arctic_sv'), hydrological: v('freshwater_hydrological_sv') },
    greenland_sle_m: v('greenland_sea_level_equivalent_m'), sea_level_rise_m: v('sea_level_rise_m'), sea_level_rise_vs_control_m: at(s.series.sea_level_rise_vs_control_m, k),
    greenland_smb_gt_yr: v('greenland_smb_gt_yr'), greenland_melt_gt_yr: v('greenland_melt_gt_yr'),
    arctic_ice_march_m: v('arctic_ice_march_m'), arctic_ice_september_m: v('arctic_ice_september_m'), arctic_ice_free_days: v('arctic_ice_free_days'),
    north_atlantic_change_k: v('north_atlantic_temperature_change_k'), tropical_atlantic_change_k: v('tropical_atlantic_temperature_change_k'),
    north_oxygen_change_pct: v('north_oxygen_change_pct'), south_oxygen_change_pct: v('south_oxygen_change_pct'),
    map_scenario: b.map_scenario, map_ice_volume_m3: p.volume, map_ice_cells: iceArea,
  }
}

// The "when" table: every scenario's events with their year and warming, and the ensemble range.
export function eventTable(block) {
  return block.scenarios.filter(s => !s.control).map(s => {
    const row = { scenario: s.name, events: {} }
    for (const key of EVENT_KEYS) row.events[key] = s.events[key]
    const e = s.ensemble
    row.ensemble = e ? {
      members: e.member_count, collapsed: e.collapsed_members, crossed: e.threshold_crossed_members,
      earliest: e.earliest_collapse, median: e.median_collapse_of_collapsed, latest: e.latest_collapse, earliest_crossing: e.earliest_threshold_crossing,
    } : null
    const years = block.output_years
    const idx = y => years.findIndex(v => v >= y)
    const i2100 = idx(2100), iEnd = years.length - 1
    row.sea_level_2100_m = i2100 >= 0 ? (s.series.sea_level_rise_vs_control_m ?? s.series.sea_level_rise_m)[i2100] : null
    row.sea_level_end_m = (s.series.sea_level_rise_vs_control_m ?? s.series.sea_level_rise_m)[iEnd]
    row.overturning_2100_sv = i2100 >= 0 ? s.series.overturning_sv[i2100] : null
    row.lag_years = s.events.threshold_crossed && s.events.overturning_collapsed ? s.events.overturning_collapsed.year - s.events.threshold_crossed.year : null
    return row
  })
}

// One key of every scenario as chart series [{name, control, points: [[year, value], ...]}].
export function scenarioSeries(block, key) {
  return block.scenarios.map(s => ({ name: s.name, control: s.control, points: block.output_years.map((y, k) => [y, s.series[key]?.[k] ?? null]).filter(p => p[1] !== null) }))
}

export function scenarioColor(index, control) {
  if (control) return '#8a9aa5'
  return ['#7fd1ff', '#ffd166', '#ff6b6b', '#b388ff', '#7fffd4', '#ffb366', '#9fe870'][index % 7]
}

// Ice thickness: bare land (null), thin ice pale blue to thick ice white.
export function iceThicknessColor(h) {
  if (!(h > 1)) return null
  const t = Math.min(1, h / 3000)
  return `rgb(${Math.round(150 + 105 * t)},${Math.round(190 + 65 * t)},${Math.round(225 + 30 * t)})`
}

// Surface mass balance (m of ice per year): melt red <- 0 -> accumulation blue, saturating at +-3 m/yr.
export function smbColor(v) {
  const t = Math.max(-1, Math.min(1, v / 3))
  if (t < 0) return `rgb(${Math.round(240)},${Math.round(235 + 190 * t)},${Math.round(225 + 190 * t)})`
  return `rgb(${Math.round(235 - 170 * t)},${Math.round(240 - 90 * t)},${Math.round(245)})`
}
