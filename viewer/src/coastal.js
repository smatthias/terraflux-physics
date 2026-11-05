// Coastal flooding support: validation of the documented terraflux.coastal.flooding/v1 block (a rising sea:
// land lost, storm floods with wind setup, defences, rivers, islands and regions under the sea-level scenarios),
// per-frame summaries, the "where it hits hardest" ranking, the event table, chart series and colours. No physics
// runs in the browser apart from the Gumbel frequency of today's design flood for a cell's relative rise.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const COAST_SCHEMA = 'terraflux.coastal.flooding/v1'
export const COAST_METHOD = 'bathtub_connectivity_attenuation_wind_setup_bruun_gumbel'
export const COAST_TYPES = ['rock', 'sandy', 'muddy']
export const COAST_EVENTS = ['defences_overtopped', 'island_half_lost', 'lost_10pct', 'design_flood_every_year']
export const COAST_EVENT_LABELS = {
  defences_overtopped: 'Defences overtopped by the design storm',
  island_half_lost: 'An island has lost half its land',
  lost_10pct: 'A tenth of the land lost',
  design_flood_every_year: "Today's 100-year flood every year",
}
const COMPONENTS = ['thermal_expansion_m', 'glaciers_m', 'antarctic_m', 'greenland_m', 'land_water_m']
export const COMPONENT_LABELS = { thermal_expansion_m: 'Ocean warming (expansion)', glaciers_m: 'Mountain glaciers', antarctic_m: 'Antarctica', greenland_m: 'Greenland', land_water_m: 'Land water' }
const REGION_SERIES = ['lost_km2', 'lost_pct', 'inundated_km2', 'eroded_km2', 'storm_flooded_km2', 'newly_storm_flooded_km2', 'protected_low_km2', 'people_on_lost_land',
  'people_storm_flooded', 'people_protected_low', 'mean_relative_rise_m', 'design_flood_return_years', 'max_storm_depth_m']
const TOTALS = ['lost_km2', 'inundated_km2', 'eroded_km2', 'storm_flooded_km2', 'newly_storm_flooded_km2', 'protected_low_km2', 'people_on_lost_land', 'people_storm_flooded',
  'people_protected_low', 'max_setup_m', 'mean_coastal_setup_m']
const RIVER_SERIES = ['tidal_reach_km', 'surge_reach_km', 'salt_intrusion_km', 'mouth_storm_level_m', 'flooded_bank_km2']
const close = (a, b, rel, abs) => Math.abs(a - b) <= Math.max(rel * Math.max(Math.abs(a), Math.abs(b)), abs)
const cellList = (v, count) => Array.isArray(v) && v.every(i => Number.isInteger(i) && i >= 0 && i < count)

// Gumbel annual maxima: the return period of today's T-year level after a relative rise.
export function returnPeriodAfterRise(location, scale, period, rise) {
  const level = location - scale * Math.log(-Math.log(1 - 1 / period))
  const p = 1 - Math.exp(-Math.exp(-(level - location - rise) / scale))
  return p > 0 ? 1 / p : Infinity
}

export function validateCoast(block, frames, terrain) {
  const b = block, grid = frames[0].forecast.grid, count = grid.nx * grid.ny, km2 = grid.spacing_east_m * grid.spacing_north_m / 1e6
  check(b?.schema_version === COAST_SCHEMA && b.method === COAST_METHOD, 'Unsupported coastal contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Coast source must be engine_output or synthetic.')
  check(terrain && Array.isArray(terrain.values) && terrain.values.length === count, 'Coastal flooding requires the terrain grid.')
  check(cellList(b.sea_cells, count) && b.sea_cells.every((v, k) => k === 0 || v > b.sea_cells[k - 1]) && b.sea_cells.length > 0, 'Invalid sea cells.')
  const sea = new Array(count).fill(false)
  for (const i of b.sea_cells) sea[i] = true
  check(b.sea_cells.every(i => terrain.values[i] < 0), 'The sea lies below mean sea level.')
  check(Array.isArray(b.coast_type) && b.coast_type.length === count && b.coast_type.every(t => Number.isInteger(t) && t >= 0 && t <= 2), 'Invalid coast types.')
  check(Array.isArray(b.population) && b.population.length === count && b.population.every((p, i) => finite(p) && p >= 0 && (!sea[i] || p === 0)), 'Invalid population: people live on land.')
  check(Array.isArray(b.subsidence_mm_yr) && b.subsidence_mm_yr.length === count && b.subsidence_mm_yr.every(finite), 'Invalid subsidence.')
  check(Array.isArray(b.region_names) && Array.isArray(b.region) && b.region.length === count && b.region.every(r => Number.isInteger(r) && r >= -1 && r < b.region_names.length), 'Invalid regions.')
  check(Array.isArray(b.defences) && b.defences.every(d => Array.isArray(d) && d.length === 2 && Number.isInteger(d[0]) && d[0] >= 0 && d[0] < count && !sea[d[0]] && finite(d[1]) && d[1] > terrain.values[d[0]]), 'Defences stand on land above the ground.')
  check(Array.isArray(b.rivers) && b.rivers.every(r => typeof r.name === 'string' && cellList(r.cells, count) && r.cells.length > 0 && r.cells.every(i => !sea[i])), 'Invalid rivers.')
  const t = b.today
  check(t && cellList(t.intertidal_cells, count) && Array.isArray(t.storm_depth_m) && t.storm_depth_m.length === count && t.storm_depth_m.every(v => finite(v) && v >= 0) &&
    Array.isArray(t.setup_m) && t.setup_m.length === count && t.setup_m.every(finite), 'Invalid state of today.')
  const d = b.design_storm
  check(d && ['return_period_years', 'high_tide_m', 'surge_m', 'offshore_level_m', 'surge_location_m', 'surge_scale_m', 'wind_speed_m_s', 'wind_toward_deg', 'wind_stress_pa'].every(k => finite(d[k])) && d.surge_scale_m > 0, 'Invalid design storm.')
  const years = b.output_years
  check(Array.isArray(years) && years.length >= 2 && years[0] === b.reference_year && years.every((y, k) => Number.isInteger(y) && (k === 0 || y > years[k - 1])), 'Output years must increase from the reference year.')
  check(Array.isArray(b.map_years) && b.map_years.every(y => years.includes(y)) && b.map_years.length === frames.length, 'Map years must be output years, one per frame.')
  check(Array.isArray(b.map_frames) && b.map_frames.length === frames.length, 'Coastal map frames must match the timeline.')
  check(Array.isArray(b.scenarios) && b.scenarios.length >= 1 && b.scenarios.some(s => s.name === b.map_scenario), 'The map scenario must be one of the scenarios.')
  const n = years.length
  const series = (o, keys, what) => check(o && keys.every(k => Array.isArray(o[k]) && o[k].length === n && o[k].every(v => v === null || finite(v))), `${what}: series must match the output years.`)
  const names = new Set()
  for (const s of b.scenarios) {
    check(typeof s.name === 'string' && !names.has(s.name), 'Scenario names must be unique.')
    names.add(s.name)
    series(s.sea_level, [...COMPONENTS, 'global_m', 'regional_m', 'global_warming_k'], `Scenario ${s.name} sea level`)
    for (let k = 0; k < n; k++) {
      const sum = COMPONENTS.reduce((a, c) => a + s.sea_level[c][k], 0)
      check(close(sum, s.sea_level.global_m[k], 2e-3, 2e-4), `Scenario ${s.name}: the global rise must be the sum of its parts.`)
    }
    check(s.sea_level.global_m[0] === 0 && s.sea_level.regional_m[0] === 0, 'The rise is counted from the reference year.')
    series(s.totals, TOTALS, `Scenario ${s.name} totals`)
    check(Array.isArray(s.regions) && s.regions.length === b.region_names.length && s.regions.every((r, k) => r.name === b.region_names[k]), `Scenario ${s.name}: one entry per region.`)
    for (const r of s.regions) {
      series(r.series, REGION_SERIES, `Region ${r.name}`)
      check(r.events && COAST_EVENTS.every(e => r.events[e] === null || (Number.isInteger(r.events[e]) && r.events[e] >= b.reference_year)), `Region ${r.name}: invalid events.`)
      check(r.series.lost_pct.every(v => v >= 0 && v <= 100.0001), `Region ${r.name}: lost share out of range.`)
    }
    check(Array.isArray(s.rivers) && s.rivers.length === b.rivers.length, `Scenario ${s.name}: one entry per river.`)
    for (const r of s.rivers) series(r.series, RIVER_SERIES, `River ${r.name}`)
    check(Array.isArray(s.islands) && s.islands.every(i => Array.isArray(i.lost_fraction) && i.lost_fraction.length === n && i.lost_fraction.every(v => v >= 0 && v <= 1) && Number.isInteger(i.top_cell) && !sea[i.top_cell]), `Scenario ${s.name}: invalid islands.`)
    const pairs = v => Array.isArray(v) && v.every(p => Array.isArray(p) && p.length === 2 && Number.isInteger(p[0]) && p[0] >= 0 && p[0] < count && !sea[p[0]] && Number.isInteger(p[1]) && p[1] >= b.reference_year)
    check(pairs(s.year_lost) && pairs(s.year_storm_flooded), `Scenario ${s.name}: invalid first-year maps.`)
  }
  const map = b.scenarios.find(s => s.name === b.map_scenario)
  const lostYear = new Map(map.year_lost), stormYear = new Map(map.year_storm_flooded)
  const base = { block: b, sea, count, km2, lostYearMap: lostYear, stormYearMap: stormYear }
  frames.forEach((frame, k) => {
    const m = b.map_frames[k]
    check(m.year === b.map_years[k] && m.elapsed_seconds === frame.forecast.elapsed_seconds, 'Coastal map years must match the frames.')
    check(['lost_fraction', 'inundation_depth_m', 'storm_depth_m', 'erosion_retreat_m'].every(key => Array.isArray(m[key]) && m[key].length === count && m[key].every(v => finite(v) && v >= 0)), 'Coastal maps must match the grid.')
    check(m.lost_fraction.every((v, i) => v <= 1 && (!sea[i] || v === 0)) && m.storm_depth_m.every((v, i) => !sea[i] || v === 0), 'Only land is lost or flooded.')
    check(m.inundation_depth_m.every((v, i) => v === 0 || m.lost_fraction[i] === 1), 'Inundated land is wholly lost.')
    check(cellList(m.protected_low_cells, count) && cellList(m.river_salt_cells, count) && m.protected_low_cells.every(i => !sea[i] && m.lost_fraction[i] < 1), 'Invalid protected or salt cells.')
    const ky = years.indexOf(m.year)
    const lost = m.lost_fraction.reduce((a, v) => a + v, 0) * km2
    check(close(lost, map.totals.lost_km2[ky], 2e-3, 0.5), 'Lost land disagrees with its map.')
    const storm = m.storm_depth_m.reduce((a, v) => a + (v > 0 ? km2 : 0), 0)
    check(close(storm, map.totals.storm_flooded_km2[ky], 1e-6, 0.5), 'Storm-flooded land disagrees with its map.')
    check(m.lost_fraction.every((v, i) => v < 0.5 || (lostYear.has(i) && lostYear.get(i) <= m.year)), 'A cell lost on the map must carry its year.')
    const protectedSet = new Set(m.protected_low_cells), saltSet = new Set(m.river_salt_cells)
    frame.coast = {
      ...base, year: m.year, yearIndex: ky, index: k, rise: m.regional_rise_m, lost: m.lost_fraction, inundation: m.inundation_depth_m, storm: m.storm_depth_m,
      erosion: m.erosion_retreat_m, protectedLow: protectedSet, salt: saltSet, synthetic: b.source_kind === 'synthetic',
    }
  })
  return b
}

// Which land cells are coastal (next to today's sea).
export function coastalCells(coast, grid) {
  const { sea, count } = coast, nx = grid.nx
  const out = new Array(count).fill(false)
  for (let i = 0; i < count; i++) {
    if (sea[i]) continue
    const x = i % nx
    out[i] = (x + 1 < nx && sea[i + 1]) || (x > 0 && sea[i - 1]) || (i + nx < count && sea[i + nx]) || (i >= nx && sea[i - nx])
  }
  return out
}

// Per-cell relative rise (regional + subsidence x years) and how often today's design flood comes there.
export function cellFrequency(coast, scenarioName, i) {
  const b = coast.block, s = scenarioOf(b, scenarioName), d = b.design_storm
  const rise = s.sea_level.regional_m[coast.yearIndex] + b.subsidence_mm_yr[i] / 1000 * (coast.year - b.reference_year)
  return { relative_rise_m: rise, return_years: returnPeriodAfterRise(d.surge_location_m, d.surge_scale_m, d.return_period_years, rise) }
}

export const scenarioOf = (block, name) => block.scenarios.find(s => s.name === name) ?? block.scenarios.find(s => s.name === block.map_scenario)

// First-year maps of a scenario as dense arrays (null = not within the run).
export function firstYearMaps(block, scenarioName) {
  const count = block.population.length, s = scenarioOf(block, scenarioName)
  const lost = new Array(count).fill(null), storm = new Array(count).fill(null)
  for (const [i, y] of s.year_lost) lost[i] = y
  for (const [i, y] of s.year_storm_flooded) storm[i] = y
  return { lost, storm }
}

// Metrics of one scenario at the frame's year.
export function summarizeCoast(frame, scenarioName) {
  const c = frame.coast, b = c.block, s = scenarioOf(b, scenarioName), k = c.yearIndex
  const at = series => series[k]
  const regions = s.regions.map((r, idx) => {
    const land = r.series.lost_pct[k] > 0 ? r.series.lost_km2[k] / (r.series.lost_pct[k] / 100) : null
    return {
      index: idx, name: r.name, lost_km2: at(r.series.lost_km2), lost_pct: at(r.series.lost_pct), people_on_lost_land: at(r.series.people_on_lost_land),
      people_storm_flooded: at(r.series.people_storm_flooded), people_storm_today: r.series.people_storm_flooded[0], protected_low_km2: at(r.series.protected_low_km2),
      people_protected_low: at(r.series.people_protected_low), design_flood_return_years: at(r.series.design_flood_return_years), relative_rise_m: at(r.series.mean_relative_rise_m),
      max_storm_depth_m: at(r.series.max_storm_depth_m), events: r.events, land_km2: land,
    }
  })
  const comps = {}
  for (const key of COMPONENTS) comps[key] = at(s.sea_level[key])
  const t = s.totals
  return {
    scenario: s.name, year: c.year, map_scenario: b.map_scenario,
    global_rise_m: at(s.sea_level.global_m), regional_rise_m: at(s.sea_level.regional_m), warming_k: at(s.sea_level.global_warming_k), components: comps,
    lost_km2: at(t.lost_km2), inundated_km2: at(t.inundated_km2), eroded_km2: at(t.eroded_km2), people_on_lost_land: at(t.people_on_lost_land),
    storm_flooded_km2: at(t.storm_flooded_km2), people_storm_flooded: at(t.people_storm_flooded), people_storm_today: t.people_storm_flooded[0],
    protected_low_km2: at(t.protected_low_km2), people_protected_low: at(t.people_protected_low), max_setup_m: at(t.max_setup_m), mean_setup_m: at(t.mean_coastal_setup_m),
    design_storm: b.design_storm, regions,
    rivers: s.rivers.map(r => ({ name: r.name, tidal_reach_km: at(r.series.tidal_reach_km), surge_reach_km: at(r.series.surge_reach_km), salt_intrusion_km: at(r.series.salt_intrusion_km),
      tidal_today_km: r.series.tidal_reach_km[0], surge_today_km: r.series.surge_reach_km[0], salt_today_km: r.series.salt_intrusion_km[0], flooded_bank_km2: at(r.series.flooded_bank_km2) })),
    islands: s.islands.map(i => ({ id: i.id, region: i.region, land_km2: i.land_km2, population: i.population, highest_m: i.highest_m, lost_fraction: i.lost_fraction[k], top_cell: i.top_cell })),
  }
}

// "Where it hits hardest": the regions ranked by a metric at the frame's year.
export const RANK_METRICS = {
  lost_pct: { label: 'Land lost (%)', better: 'higher' },
  people_on_lost_land: { label: 'People on lost land', better: 'higher' },
  people_storm_flooded: { label: 'People under the design storm', better: 'higher' },
  people_protected_low: { label: 'People behind defences below high water', better: 'higher' },
  design_flood_return_years: { label: "Today's 100-year flood now every … years", better: 'lower' },
}
export function coastRanking(summary, metric = 'lost_pct') {
  const spec = RANK_METRICS[metric] ?? RANK_METRICS.lost_pct
  const key = RANK_METRICS[metric] ? metric : 'lost_pct'
  return [...summary.regions].sort((a, b) => spec.better === 'lower' ? a[key] - b[key] : b[key] - a[key]).map((r, rank) => ({ ...r, rank: rank + 1 }))
}

// The events of every region under every scenario.
export function coastEventTable(block) {
  return block.region_names.map((name, r) => ({
    region: name,
    scenarios: block.scenarios.map(s => ({ name: s.name, events: s.regions[r].events })),
  }))
}

// Chart series: one key of a region / the totals / the sea level, per scenario, as [year, value].
export function coastSeries(block, pick) {
  return block.scenarios.map(s => ({ name: s.name, points: block.output_years.map((y, k) => [y, pick(s)[k]]).filter(p => p[1] !== null && Number.isFinite(p[1])) }))
}

// ---- Colours -----------------------------------------------------------------------------------------
export function scenarioTint(index) { return ['#7fd1ff', '#ffd166', '#ff6b6b', '#b388ff', '#9fe870'][index % 5] }

// Storm flood depth: pale to deep blue, saturating at 3 m.
export function floodDepthColor(depth) {
  const t = Math.max(0, Math.min(1, depth / 3))
  return `rgb(${Math.round(150 - 130 * t)},${Math.round(210 - 120 * t)},${Math.round(255 - 60 * t)})`
}

// The year a cell is lost: soon (red) -> late (yellow); null = not within the run.
export function yearLostColor(year, start, end) {
  if (year === null) return null
  const t = Math.max(0, Math.min(1, (year - start) / ((end - start) || 1)))
  return `hsl(${Math.round(350 + 60 * t) % 360}, 85%, ${Math.round(45 + 15 * t)}%)`
}

// How often today's design flood comes: every year (red) -> still rare (green), logarithmic.
export function frequencyColor(years) {
  const t = Math.max(0, Math.min(1, Math.log10(Math.max(1, years)) / 2))
  return `hsl(${Math.round(120 * t)}, 70%, 45%)`
}

// Land elevation (m) for the coastal base map: low green -> higher brown.
export function coastLandColor(elevation) {
  if (elevation < 0) return 'rgb(120,150,110)'
  const t = Math.max(0, Math.min(1, Math.log10(1 + elevation) / Math.log10(250)))
  return `rgb(${Math.round(90 + 100 * t)},${Math.round(140 - 10 * t)},${Math.round(80 + 10 * t)})`
}

// Sea depth: shallow turquoise -> deep navy over 60 m.
export function coastSeaColor(elevation) {
  const t = Math.max(0, Math.min(1, -elevation / 60))
  return `rgb(${Math.round(40 - 25 * t)},${Math.round(140 - 95 * t)},${Math.round(170 - 60 * t)})`
}
