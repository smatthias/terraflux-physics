// Storm systems, severe convection and surface support for the weather map:
// validation of the documented blocks, category styling and metrics. No
// physics runs in the browser.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const CATEGORIES = [
  { key: 'depression', label: 'Tropical depression', short: 'TD', color: '#5ba8e8' },
  { key: 'tropical_storm', label: 'Tropical storm', short: 'TS', color: '#4dd6c7' },
  { key: 'category_1', label: 'Category 1', short: 'C1', color: '#ffe066' },
  { key: 'category_2', label: 'Category 2', short: 'C2', color: '#ffb347' },
  { key: 'category_3', label: 'Category 3', short: 'C3', color: '#ff7f50' },
  { key: 'category_4', label: 'Category 4', short: 'C4', color: '#ff4d4d' },
  { key: 'category_5', label: 'Category 5', short: 'C5', color: '#c724b1' },
]
export const EVENT_KINDS = ['seed', 'genesis', 'landfall', 'merger', 'dissipation', 'exit']
export const TORNADO_RISK = ['none', 'low', 'moderate', 'significant']
export const RISK_COLORS = { none: null, low: '#ffe066', moderate: '#ff7f50', significant: '#c724b1' }
const categoryIndex = Object.fromEntries(CATEGORIES.map((c, i) => [c.key, i]))
export const categoryInfo = key => CATEGORIES[categoryIndex[key] ?? 0]
export const categoryFor = wind => CATEGORIES[wind < 17 ? 0 : wind < 33 ? 1 : wind < 43 ? 2 : wind < 50 ? 3 : wind < 58 ? 4 : wind < 70 ? 5 : 6]

export function validateSurface(surface, frames) {
  const count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  check(surface?.schema_version === 'terraflux.weather.surface/v1', 'Unsupported surface contract.')
  check(Array.isArray(surface.land) && surface.land.length === count && surface.land.every(v => v === 0 || v === 1), 'Invalid land mask.')
  check(Array.isArray(surface.sea_surface_temperature_k) && surface.sea_surface_temperature_k.length === count && surface.sea_surface_temperature_k.every(v => finite(v) && v > 0), 'Invalid sea-surface temperature.')
  frames.forEach(frame => { frame.surface = { land: surface.land, sst: surface.sea_surface_temperature_k, source: surface.source } })
  return surface
}

const STORM_FIELDS = ['potential_intensity_m_s', 'genesis_potential_index', 'vertical_shear_m_s', 'relative_vorticity_s', 'mid_level_relative_humidity',
  'outflow_temperature_k', 'enthalpy_disequilibrium_j_kg', 'storm_wind_speed_m_s', 'storm_pressure_anomaly_pa', 'storm_rain_rate_mm_h', 'storm_surge_m']

export function validateStormSystems(block, frames) {
  const b = block, count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  check(b?.schema_version === 'terraflux.weather.storm_systems/v1' && b.method === 'mpi_gpi_rankine_fujiwhara_holland_kaplan_demaria', 'Unsupported storm systems contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Storm source must be engine_output or synthetic.')
  check(b.config && finite(b.config.holland_b) && finite(b.config.decay_alpha_per_s) && Array.isArray(b.config.seeds), 'Invalid storm configuration.')
  check(Array.isArray(b.events) && b.events.every(e => EVENT_KINDS.includes(e.kind) && finite(e.elapsed_seconds) && Number.isInteger(e.storm_id) && finite(e.east_m) && finite(e.north_m) && finite(e.max_wind_m_s) && finite(e.surge_m)), 'Invalid storm events.')
  check(Array.isArray(b.frames) && b.frames.length === frames.length, 'Storm frame count mismatch.')
  frames.forEach((frame, i) => {
    const f = b.frames[i]
    check(f && f.elapsed_seconds === frame.forecast.elapsed_seconds, 'Storm frame times must match the timeline.')
    check(STORM_FIELDS.every(k => Array.isArray(f[k]) && f[k].length === count && f[k].every(finite)), 'Storm environment arrays do not match the grid.')
    check(f.storm_wind_speed_m_s.every(v => v >= 0) && f.storm_pressure_anomaly_pa.every(v => v <= 0) && f.storm_rain_rate_mm_h.every(v => v >= 0) && f.storm_surge_m.every(v => v >= 0) && f.potential_intensity_m_s.every(v => v >= 0), 'Storm fields out of range.')
    check(Array.isArray(f.storms) && f.storm_count === f.storms.length, 'Storm count disagrees with the list.')
    for (const s of f.storms) {
      check(Number.isInteger(s.id) && finite(s.east_m) && finite(s.north_m) && finite(s.max_wind_m_s) && s.max_wind_m_s >= 0 && finite(s.radius_max_wind_m) && s.radius_max_wind_m > 0, 'Invalid storm position or intensity.')
      check(categoryIndex[s.category] !== undefined && s.category === categoryFor(s.max_wind_m_s).key, 'Storm category must follow the Saffir-Simpson wind thresholds.')
      check(finite(s.central_pressure_pa) && finite(s.environment_pressure_pa) && s.central_pressure_pa < s.environment_pressure_pa, 'Central pressure must be below the environment.')
      check(typeof s.over_land === 'boolean' && finite(s.surge_estimate_m) && s.surge_estimate_m >= 0 && finite(s.core_rain_rate_mm_h) && Array.isArray(s.track) && s.track.every(p => Array.isArray(p) && p.length === 2 && p.every(finite)), 'Invalid storm diagnostics or track.')
    }
    frame.storms = { list: f.storms, env: Object.fromEntries(STORM_FIELDS.map(k => [k, f[k]])), events: b.events, config: b.config, synthetic: b.source_kind === 'synthetic' }
  })
  return b
}

const SEVERE_FIELDS = ['low_level_shear_m_s', 'deep_shear_m_s', 'storm_motion_east_m_s', 'storm_motion_north_m_s', 'helicity_m2_s2', 'significant_tornado_parameter',
  'heated_cape_j_kg', 'heated_cin_j_kg', 'cap_strength_j_kg', 'lcl_height_m', 'capped', 'cap_broken', 'tornado_risk', 'tornado_possible']

export function validateSevereConvection(block, frames) {
  const b = block, count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  check(b?.schema_version === 'terraflux.weather.severe_convection/v1' && b.method === 'capping_inversion_ekman_shear_bunkers_stp', 'Unsupported severe convection contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Severe source must be engine_output or synthetic.')
  check(b.config && finite(b.config.stp_low) && finite(b.config.stp_moderate) && finite(b.config.stp_significant) && b.config.stp_low <= b.config.stp_moderate && b.config.stp_moderate <= b.config.stp_significant, 'Invalid severe configuration.')
  check(Array.isArray(b.frames) && b.frames.length === frames.length, 'Severe frame count mismatch.')
  frames.forEach((frame, i) => {
    const f = b.frames[i]
    check(f && f.elapsed_seconds === frame.forecast.elapsed_seconds, 'Severe frame times must match the timeline.')
    check(SEVERE_FIELDS.every(k => Array.isArray(f[k]) && f[k].length === count), 'Severe arrays do not match the grid.')
    let capped = 0, broken = 0, possible = 0
    for (let j = 0; j < count; j++) {
      check(finite(f.significant_tornado_parameter[j]) && f.significant_tornado_parameter[j] >= 0 && TORNADO_RISK.includes(f.tornado_risk[j]), 'Invalid STP or risk class.')
      const stp = f.significant_tornado_parameter[j], c = b.config
      const expected = stp < c.stp_low ? 'none' : stp < c.stp_moderate ? 'low' : stp < c.stp_significant ? 'moderate' : 'significant'
      check(f.tornado_risk[j] === expected, 'Risk class must follow the STP thresholds.')
      check(typeof f.capped[j] === 'boolean' && typeof f.cap_broken[j] === 'boolean' && typeof f.tornado_possible[j] === 'boolean' && (!f.cap_broken[j] || f.capped[j]), 'Cap flags must be consistent.')
      check(!f.tornado_possible[j] || ['moderate', 'significant'].includes(f.tornado_risk[j]), 'Tornado potential needs at least a moderate class.')
      check(finite(f.heated_cape_j_kg[j]) && f.heated_cape_j_kg[j] >= 0 && finite(f.heated_cin_j_kg[j]) && f.heated_cin_j_kg[j] >= 0 && finite(f.cap_strength_j_kg[j]) && f.cap_strength_j_kg[j] >= 0, 'Invalid convective energies.')
      check(finite(f.low_level_shear_m_s[j]) && f.low_level_shear_m_s[j] >= 0 && finite(f.deep_shear_m_s[j]) && f.deep_shear_m_s[j] >= 0 && finite(f.helicity_m2_s2[j]), 'Invalid shear or helicity.')
      check(f.lcl_height_m[j] === null || (finite(f.lcl_height_m[j]) && f.lcl_height_m[j] >= 0), 'Invalid LCL.')
      capped += f.capped[j] ? 1 : 0; broken += f.cap_broken[j] ? 1 : 0; possible += f.tornado_possible[j] ? 1 : 0
    }
    check(f.capped_cells === capped && f.cap_broken_cells === broken && f.tornado_possible_cells === possible && Math.abs(f.max_stp - Math.max(0, ...f.significant_tornado_parameter)) < 1e-9, 'Severe frame totals disagree with the arrays.')
    frame.severe = { shear1: f.low_level_shear_m_s, shear6: f.deep_shear_m_s, motionEast: f.storm_motion_east_m_s, motionNorth: f.storm_motion_north_m_s, helicity: f.helicity_m2_s2, stp: f.significant_tornado_parameter, cape: f.heated_cape_j_kg, cin: f.heated_cin_j_kg, cap: f.cap_strength_j_kg, lcl: f.lcl_height_m, capped: f.capped, capBroken: f.cap_broken, risk: f.tornado_risk, possible: f.tornado_possible, cappedCells: capped, capBrokenCells: broken, possibleCells: possible, maxStp: f.max_stp, config: b.config, synthetic: b.source_kind === 'synthetic' }
  })
  return b
}

export function summarizeStorms(frame) {
  const s = frame.storms, list = s.list
  const strongest = list.reduce((a, b) => (b.max_wind_m_s > (a?.max_wind_m_s ?? -1) ? b : a), null)
  const until = frame.forecast.elapsed_seconds
  const events = s.events.filter(e => e.elapsed_seconds <= until)
  return {
    storm_count: list.length,
    strongest_wind_m_s: strongest ? strongest.max_wind_m_s : 0,
    strongest_category: strongest ? strongest.category : 'none',
    lowest_pressure_hpa: strongest ? strongest.central_pressure_pa / 100 : null,
    storms_over_land: list.filter(x => x.over_land).length,
    max_surge_m: Math.max(0, ...s.env.storm_surge_m),
    max_storm_wind_m_s: Math.max(0, ...s.env.storm_wind_speed_m_s),
    max_potential_intensity_m_s: Math.max(0, ...s.env.potential_intensity_m_s),
    max_genesis_index: Math.max(0, ...s.env.genesis_potential_index),
    landfalls_so_far: events.filter(e => e.kind === 'landfall').length,
    mergers_so_far: events.filter(e => e.kind === 'merger').length,
    geneses_so_far: events.filter(e => e.kind === 'genesis').length,
  }
}

export function summarizeSevere(frame) {
  const v = frame.severe
  return { max_stp: v.maxStp, tornado_possible_cells: v.possibleCells, capped_cells: v.cappedCells, cap_broken_cells: v.capBrokenCells,
    max_helicity: Math.max(0, ...v.helicity.map(Math.abs)), max_low_level_shear: Math.max(0, ...v.shear1), max_cape: Math.max(0, ...v.cape) }
}

// Synthetic storm/surface/severe blocks for the mock timeline (same format).
export function mockStormBlocks(frames, grid) {
  const count = grid.nx * grid.ny
  const land = [], sst = []
  for (let j = 0; j < count; j++) { const x = j % grid.nx; land.push(x < 4 ? 1 : 0); sst.push(302) }
  const events = [{ kind: 'seed', elapsed_seconds: 0, storm_id: 1, other_id: 0, east_m: 450000, north_m: -50000, max_wind_m_s: 25, category: 'tropical_storm', surge_m: 0 }]
  const stormFrames = [], severeFrames = []
  let landfallRecorded = false
  frames.forEach((frame, i) => {
    const t = frame.elapsed_seconds, east = 450000 - 12 * t, north = -50000 + 1.2 * t
    const cellX = Math.floor((east - grid.origin_east_m) / grid.spacing_east_m)
    const overLand = cellX < 4
    let wind = Math.min(62, 25 + 2.5 * t / 3600)
    if (overLand) { if (!landfallRecorded) { events.push({ kind: 'landfall', elapsed_seconds: t, storm_id: 1, other_id: 0, east_m: east, north_m: north, max_wind_m_s: wind * 0.9, category: categoryFor(wind * 0.9).key, surge_m: 4 }); landfallRecorded = true } wind = 13.75 + (wind * 0.9 - 13.75) * Math.exp(-0.095 * 4) }
    const drop = 1.15 * Math.E * wind * wind / 1.3
    const storm = { id: 1, east_m: east, north_m: north, max_wind_m_s: wind, radius_max_wind_m: 40000, central_pressure_pa: 101000 - drop, environment_pressure_pa: 101000, category: categoryFor(wind).key, over_land: overLand, age_seconds: t, time_over_land_seconds: overLand ? 14400 : 0, potential_intensity_m_s: overLand ? 0 : 70, surge_estimate_m: drop / (1025 * 9.80665) + 1.5e-3 * wind * wind, core_rain_rate_mm_h: 0.4 * wind, track: Array.from({ length: i + 1 }, (_, k) => [450000 - 12 * frames[k].elapsed_seconds, -50000 + 1.2 * frames[k].elapsed_seconds]) }
    const env = {}
    for (const key of STORM_FIELDS) env[key] = []
    for (let j = 0; j < count; j++) {
      const cx = grid.origin_east_m + (j % grid.nx + 0.5) * grid.spacing_east_m, cy = grid.origin_north_m + (Math.floor(j / grid.nx) + 0.5) * grid.spacing_north_m
      const r = Math.hypot(cx - east, cy - north), ocean = land[j] === 0
      env.potential_intensity_m_s.push(ocean ? 70 : 0); env.genesis_potential_index.push(ocean ? 6 : 0); env.vertical_shear_m_s.push(4); env.relative_vorticity_s.push(1e-5)
      env.mid_level_relative_humidity.push(0.6); env.outflow_temperature_k.push(205); env.enthalpy_disequilibrium_j_kg.push(ocean ? 15000 : 0)
      env.storm_wind_speed_m_s.push(r <= 40000 ? wind * r / 40000 : wind * 40000 / r); env.storm_pressure_anomaly_pa.push(-drop * (1 - Math.exp(-Math.pow(40000 / Math.max(r, 1), 1.3))))
      env.storm_rain_rate_mm_h.push(0.4 * wind * Math.exp(-r / 120000)); env.storm_surge_m.push(land[j] === 1 && (j % grid.nx) === 3 && r < 240000 ? storm.surge_estimate_m * Math.exp(-r / 80000) : 0)
    }
    stormFrames.push({ elapsed_seconds: t, storms: [storm], ...env, storm_count: 1 })
    const sv = { low_level_shear_m_s: [], deep_shear_m_s: [], storm_motion_east_m_s: [], storm_motion_north_m_s: [], helicity_m2_s2: [], significant_tornado_parameter: [], heated_cape_j_kg: [], heated_cin_j_kg: [], cap_strength_j_kg: [], lcl_height_m: [], capped: [], cap_broken: [], tornado_risk: [], tornado_possible: [] }
    let capped = 0, broken = 0, possible = 0, maxStp = 0
    for (let j = 0; j < count; j++) {
      const warm = frame.fields.air_temperature.values[j] > 288, heating = 6 * Math.max(0, Math.sin(2 * Math.PI * t / 86400))
      const stp = warm ? Math.max(0, 0.4 + 0.35 * heating - 0.02 * (j % 7)) : 0
      const risk = stp < 0.5 ? 'none' : stp < 1 ? 'low' : stp < 3 ? 'moderate' : 'significant'
      const cb = frame.fields.air_temperature.values[j] > 288 && heating > 2 && j % 5 === 0
      sv.low_level_shear_m_s.push(warm ? 14 : 6); sv.deep_shear_m_s.push(warm ? 32 : 15); sv.storm_motion_east_m_s.push(12); sv.storm_motion_north_m_s.push(-4); sv.helicity_m2_s2.push(warm ? 160 : 40)
      sv.significant_tornado_parameter.push(stp); sv.heated_cape_j_kg.push(warm ? 2000 + 200 * heating : 200); sv.heated_cin_j_kg.push(Math.max(0, 120 - 25 * heating)); sv.cap_strength_j_kg.push(warm ? 120 : 20); sv.lcl_height_m.push(900 + 120 * heating)
      const isCapped = warm, isBroken = warm && heating >= 4, isPossible = cb && (risk === 'moderate' || risk === 'significant')
      sv.capped.push(isCapped); sv.cap_broken.push(isBroken); sv.tornado_risk.push(risk); sv.tornado_possible.push(isPossible)
      capped += isCapped ? 1 : 0; broken += isBroken ? 1 : 0; possible += isPossible ? 1 : 0; maxStp = Math.max(maxStp, stp)
    }
    severeFrames.push({ elapsed_seconds: t, ...sv, capped_cells: capped, cap_broken_cells: broken, tornado_possible_cells: possible, max_stp: maxStp })
  })
  return {
    surface: { schema_version: 'terraflux.weather.surface/v1', source: { land: { kind: 'straight_coast', coast_east_m: grid.origin_east_m + 4 * grid.spacing_east_m, land_side: 'west' }, sea_surface_temperature: { kind: 'background_with_gradient', background: 302, gradient_north_k_per_m: 0 } }, land, sea_surface_temperature_k: sst },
    storms: { schema_version: 'terraflux.weather.storm_systems/v1', method: 'mpi_gpi_rankine_fujiwhara_holland_kaplan_demaria', source_kind: 'synthetic', config: { holland_b: 1.3, decay_alpha_per_s: 0.095 / 3600, decay_background_wind_m_s: 13.75, landfall_reduction: 0.9, surge_wind_coefficient_s2_m: 1.5e-3, rain_core_factor_mm_h_per_m_s: 0.4, seeds: [{ east_m: 450000, north_m: -50000, max_wind_m_s: 25, radius_max_wind_m: 40000 }] }, events, frames: stormFrames },
    severe: { schema_version: 'terraflux.weather.severe_convection/v1', method: 'capping_inversion_ekman_shear_bunkers_stp', source_kind: 'synthetic', config: { deep_shear_depth_m: 6000, bunkers_deviation_m_s: 7.5, geostrophic_air_density_kg_m3: 1.2, stp_low: 0.5, stp_moderate: 1, stp_significant: 3 }, frames: severeFrames },
  }
}
