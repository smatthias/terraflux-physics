// Coupled wind / moist-layer support: validation of engine output for display
// and a request editor that mirrors the C++ configuration bounds. No physics
// is solved in the browser; the request is downloaded and run with
// terraflux-run-coupled-wind, and the resulting timeline is opened here.
import { validateCloudFormation } from './cloudFormation.js'
import { validatePrecipitation } from './weatherMap.js'
import { validateSurface, validateStormSystems, validateSevereConvection } from './stormSystems.js'
import { validateOrography } from './orography.js'
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }
const clone = value => JSON.parse(JSON.stringify(value))

export const CORIOLIS_MODES = ['none', 'f_plane', 'beta_plane', 'prescribed']
export const DENSITY_MODES = ['diagnosed', 'constant']
export const SCALAR_BOUNDARIES = ['zero_gradient', 'periodic']
export const GRADIENT_BOUNDARIES = ['one_sided', 'periodic']
export const CONVECTIVE_POTENTIALS = ['none', 'weak', 'moderate', 'strong']
// Cloud formation configuration keys edited in the request form (all required in a request).
export const CLOUD_CONFIG_KEYS = [
  ['frontal_gradient_threshold_k_m', 'Frontal |∇T| threshold K/m', 2e-5], ['advection_threshold_k_s', 'Advection threshold K/s', 0.1 / 3600],
  ['boundary_layer_depth_m', 'Boundary-layer depth m', 1500], ['lifting_time_scale_s', 'Lifting time scale s', 21600],
  ['mixed_layer_growth_m_per_k', 'Mixed-layer growth m/K', 300], ['warm_front_slope', 'Warm front slope', 1 / 200],
  ['cold_front_slope', 'Cold front slope', 1 / 75], ['warm_front_reach_m', 'Warm front cloud reach m', 800000],
  ['cold_front_reach_m', 'Cold front cloud reach m', 200000], ['frontal_band_width_m', 'Frontal band width m', 150000],
  ['cin_trigger_j_kg', 'CIN trigger J/kg', 20], ['cape_threshold_j_kg', 'CAPE threshold J/kg', 100], ['max_cloud_top_m', 'Max cloud top m', 12000],
]
export const defaultCloudConfig = () => Object.fromEntries(CLOUD_CONFIG_KEYS.map(([k, , v]) => [k, v]))
// Precipitation outcome configuration keys edited in the request form.
export const PRECIP_CONFIG_KEYS = [
  ['stratiform_efficiency', 'Stratiform efficiency', 0.7], ['convective_efficiency', 'Convective efficiency', 0.4],
  ['convective_updraft_fraction', 'Convective updraft area fraction', 0.08], ['updraft_parcel_factor', 'Updraft parcel factor', 0.5],
  ['interval_seconds', 'Accumulation interval s', 28800], ['snow_wet_bulb_k', 'Snow wet-bulb limit K', 273.65],
  ['sleet_wet_bulb_k', 'Sleet wet-bulb limit K', 275.15], ['lightning_top_temperature_k', 'Lightning cloud-top limit K', 253.15],
  ['hail_min_diameter_cm', 'Minimum hail cm', 0.5], ['hail_melting_cm_per_km', 'Hail melting cm/km', 0.3], ['hail_max_diameter_cm', 'Maximum hail cm', 10],
  ['drizzle_max_rate_mm_h', 'Drizzle cap mm/h', 0.5], ['virga_surface_rh', 'Virga surface RH (fraction)', 0.4], ['virga_max_rate_mm_h', 'Virga rate limit mm/h', 1],
  ['virga_min_base_m', 'Virga minimum base m', 2000], ['minimum_rate_mm_h', 'Minimum reported rate mm/h', 0.05],
]
export const defaultPrecipConfig = () => Object.fromEntries(PRECIP_CONFIG_KEYS.map(([k, , v]) => [k, v]))
// Orography process parameters edited in the request form.
export const OROGRAPHY_KEYS = [
  ['blocking_time_scale_s', 'Blocking time scale s', 1800], ['obstacle_radius_m', 'Obstacle look-ahead radius m', 200000],
  ['influence_radius_m', 'Valley neighbourhood radius m', 100000], ['brunt_vaisala_s', 'Stability N 1/s', 0.01],
  ['boundary_layer_depth_m', 'Boundary-layer depth m', 1500], ['daytime_heating_k_per_s', 'Daytime heating K/s', 0],
  ['nocturnal_cooling_k_per_s', 'Nocturnal cooling K/s', 0], ['diurnal_phase_seconds', 'Sunrise offset s', 0],
  ['valley_pooling_per_km', 'Valley pooling per km depth', 1], ['initial_lapse_rate_k_m', 'Initial lapse rate K/m', 0.0065],
]
const EARTH_OMEGA = 7.2921159e-5, EARTH_RADIUS = 6371000

// Form model: pressure centers carry co-located temperature and humidity
// anomalies, matching examples/coupled-wind-request.json.
export function defaultCoupledForm() {
  return {
    start_time_utc: '2026-11-01T00:00:00Z', timestep_seconds: 300, steps: 576, snapshot_every_steps: 96,
    nx: 24, ny: 18, spacing_east_m: 40000, spacing_north_m: 40000,
    geographic_anchor_longitude_deg: 145, geographic_anchor_latitude_deg: -37.8,
    air_density_kg_m3: 1.225, surface_friction_coefficient_s: 2e-5, coriolis_mode: 'beta_plane', coriolis_parameter_s: 0,
    temperature_diffusivity_m2_s: 5000, humidity_diffusivity_m2_s: 5000, momentum_diffusivity_m2_s: 5000,
    pressure_gradient_boundary: 'one_sided', scalar_boundary: 'zero_gradient', density_mode: 'diagnosed',
    background_pressure_pa: 101500, background_temperature_k: 287, background_humidity_kg_kg: 0.007,
    column_enabled: true, column_layer_count: 24, column_layer_thickness_m: 500, column_lapse_rate_k_m: 0.0065,
    column_tropopause_height_m: 11000, column_humidity_scale_height_m: 2500, column_parcel_step_m: 50,
    cloud_enabled: false, cloud: defaultCloudConfig(), cloud_heating_amplitude_k: 6, cloud_heating_phase_seconds: 7200,
    precip_enabled: false, precip: defaultPrecipConfig(),
    inversion_enabled: false, inversion_base_m: 1500, inversion_depth_m: 500, inversion_strength_k: 1, lapse_rate_above_inversion_k_m: 0.0085,
    surface_enabled: false, coast_east_m: -200000, land_side: 'west', sea_surface_temperature_k: 302.5, sst_gradient_north_k_per_m: 0,
    storms_enabled: false, storm_seeds: [], severe_enabled: false,
    orography_enabled: false, terrain_base_elevation_m: 400, ridges: [], basins: [],
    orography: { blocking_time_scale_s: 1800, obstacle_radius_m: 200000, influence_radius_m: 100000, brunt_vaisala_s: 0.01, boundary_layer_depth_m: 1500, daytime_heating_k_per_s: 0, nocturnal_cooling_k_per_s: 0, diurnal_phase_seconds: 0, valley_pooling_per_km: 1, initial_lapse_rate_k_m: 0.0065 },
    airmasses: [],
    centers: [
      { kind: 'high', east_m: -300000, north_m: -40000, radius_m: 350000, pressure_anomaly_pa: 800, temperature_anomaly_k: -4, humidity_anomaly_kg_kg: -0.003 },
      { kind: 'low', east_m: 260000, north_m: 100000, radius_m: 300000, pressure_anomaly_pa: -900, temperature_anomaly_k: 6, humidity_anomaly_kg_kg: 0.006 },
    ],
  }
}

export function formToRequest(form) {
  const anomaly = (c, amplitude) => ({ east_m: c.east_m, north_m: c.north_m, radius_m: c.radius_m, amplitude })
  const atmosphere = { air_density_kg_m3: form.air_density_kg_m3, surface_friction_coefficient_s: form.surface_friction_coefficient_s, coriolis_mode: form.coriolis_mode }
  if (form.coriolis_mode === 'prescribed') atmosphere.coriolis_parameter_s = form.coriolis_parameter_s
  const airmass = (a, amplitude) => ({ east_m: a.east_m, north_m: a.north_m, radius_m: a.radius_m, amplitude })
  const cloud = form.cloud_enabled ? { cloud_formation: { ...Object.fromEntries(CLOUD_CONFIG_KEYS.map(([k]) => [k, form.cloud[k]])), surface_heating: { amplitude_k: form.cloud_heating_amplitude_k, phase_seconds: form.cloud_heating_phase_seconds } } } : {}
  const precip = form.cloud_enabled && form.precip_enabled ? { precipitation: Object.fromEntries(PRECIP_CONFIG_KEYS.map(([k]) => [k, form.precip[k]])) } : {}
  const column = form.column_enabled ? { column: {
    layer_count: form.column_layer_count, layer_thickness_m: form.column_layer_thickness_m, environment_lapse_rate_k_m: form.column_lapse_rate_k_m,
    tropopause_height_m: form.column_tropopause_height_m, humidity_scale_height_m: form.column_humidity_scale_height_m, parcel_step_m: form.column_parcel_step_m,
    ...(form.inversion_enabled ? { inversion_base_m: form.inversion_base_m, inversion_depth_m: form.inversion_depth_m, inversion_strength_k: form.inversion_strength_k, lapse_rate_above_inversion_k_m: form.lapse_rate_above_inversion_k_m } : {}),
  } } : {}
  const surface = form.surface_enabled ? { surface: { land: { coast_east_m: form.coast_east_m, land_side: form.land_side }, sea_surface_temperature_k: { background: form.sea_surface_temperature_k, gradient_north_k_per_m: form.sst_gradient_north_k_per_m } } } : {}
  const storms = form.surface_enabled && form.column_enabled && form.storms_enabled ? { storms: { seeds: form.storm_seeds.map(x => ({ east_m: x.east_m, north_m: x.north_m, max_wind_m_s: x.max_wind_m_s, radius_max_wind_m: x.radius_max_wind_m })) } } : {}
  const severe = form.cloud_enabled && form.severe_enabled ? { severe: {} } : {}
  const orography = form.orography_enabled ? { orography: { terrain: { base_elevation_m: form.terrain_base_elevation_m, ridges: form.ridges.map(r => ({ orientation_deg: r.orientation_deg, center_east_m: r.center_east_m, center_north_m: r.center_north_m, height_m: r.height_m, half_width_m: r.half_width_m, length_m: r.length_m })), basins: form.basins.map(b => ({ center_east_m: b.center_east_m, center_north_m: b.center_north_m, depth_m: b.depth_m, radius_m: b.radius_m })) }, ...Object.fromEntries(OROGRAPHY_KEYS.map(([k]) => [k, form.orography[k]])) } } : {}
  return {
    schema_version: 'terraflux.weather.coupled-wind-run/v1',
    time: { start_time_utc: form.start_time_utc, timestep_seconds: form.timestep_seconds, steps: form.steps },
    grid: {
      nx: form.nx, ny: form.ny,
      origin_east_m: -0.5 * form.nx * form.spacing_east_m, origin_north_m: -0.5 * form.ny * form.spacing_north_m,
      spacing_east_m: form.spacing_east_m, spacing_north_m: form.spacing_north_m,
      geographic_anchor_longitude_deg: form.geographic_anchor_longitude_deg, geographic_anchor_latitude_deg: form.geographic_anchor_latitude_deg,
    },
    atmosphere,
    diffusion: { temperature_m2_s: form.temperature_diffusivity_m2_s, humidity_m2_s: form.humidity_diffusivity_m2_s, momentum_m2_s: form.momentum_diffusivity_m2_s },
    pressure_gradient_boundary: form.pressure_gradient_boundary, scalar_boundary: form.scalar_boundary, density_mode: form.density_mode,
    pressure: { pressure_systems: { background_pressure_pa: form.background_pressure_pa, centers: form.centers.map(c => ({ kind: c.kind, east_m: c.east_m, north_m: c.north_m, pressure_anomaly_pa: c.pressure_anomaly_pa, radius_m: c.radius_m })) } },
    initial_fields: {
      air_temperature_k: { background: form.background_temperature_k, anomalies: [...form.centers.map(c => anomaly(c, c.temperature_anomaly_k)), ...form.airmasses.map(a => airmass(a, a.temperature_anomaly_k))] },
      specific_humidity_kg_kg: { background: form.background_humidity_kg_kg, anomalies: [...form.centers.map(c => anomaly(c, c.humidity_anomaly_kg_kg)), ...form.airmasses.map(a => airmass(a, a.humidity_anomaly_kg_kg))] },
    },
    ...column,
    ...cloud,
    ...precip,
    ...orography,
    ...surface,
    ...storms,
    ...severe,
    snapshot_every_steps: form.snapshot_every_steps,
  }
}

// Inverse of formToRequest for synthetic requests whose temperature and
// humidity anomalies are co-located with the pressure centers.
export function formFromRequest(request) {
  check(request?.schema_version === 'terraflux.weather.coupled-wind-run/v1', 'Expected a coupled wind run request.')
  const systems = request.pressure?.pressure_systems, t = request.initial_fields?.air_temperature_k, q = request.initial_fields?.specific_humidity_kg_kg
  check(systems && t?.background !== undefined && q?.background !== undefined, 'The editor only represents Gaussian pressure systems and background + anomaly fields.')
  const centers = systems.centers ?? [], ta = t.anomalies ?? [], qa = q.anomalies ?? []
  check(ta.length === qa.length && ta.length >= centers.length, 'Temperature and humidity anomalies must pair up, one per pressure center first, then free air masses.')
  const same = (a, c) => a.east_m === c.east_m && a.north_m === c.north_m && a.radius_m === c.radius_m
  check(centers.every((c, i) => same(ta[i], c) && same(qa[i], c)), 'The first anomalies must be co-located with the pressure centers.')
  check(ta.slice(centers.length).every((a, i) => same(a, qa[centers.length + i])), 'Air-mass temperature and humidity anomalies must be co-located.')
  const cf = request.cloud_formation
  if (cf !== undefined) {
    check(CLOUD_CONFIG_KEYS.every(([k]) => finite(cf[k])) && cf.surface_heating && finite(cf.surface_heating.amplitude_k) && finite(cf.surface_heating.phase_seconds), 'Cloud formation needs every configuration value and the surface heating.')
    check(Object.keys(cf).length === CLOUD_CONFIG_KEYS.length + 1, 'The editor represents the full cloud formation configuration only.')
  }
  const pr = request.precipitation
  if (pr !== undefined) {
    check(cf !== undefined && PRECIP_CONFIG_KEYS.every(([k]) => finite(pr[k])) && Object.keys(pr).length === PRECIP_CONFIG_KEYS.length, 'The editor represents the full precipitation configuration only, with cloud formation enabled.')
  }
  const col = request.column
  if (col && col.inversion_strength_k !== undefined) check(['inversion_base_m', 'inversion_depth_m', 'inversion_strength_k', 'lapse_rate_above_inversion_k_m'].every(k => finite(col[k])), 'The editor needs all four inversion values.')
  const sf = request.surface
  if (sf !== undefined) check(sf.land?.coast_east_m !== undefined && sf.sea_surface_temperature_k?.background !== undefined, 'The editor represents straight coasts and background sea-surface temperature only.')
  const stm = request.storms
  if (stm !== undefined) check(Object.keys(stm).length === 1 && Array.isArray(stm.seeds), 'The editor represents storm seeds with default storm physics only.')
  if (request.severe !== undefined) check(Object.keys(request.severe).length === 0, 'The editor represents default severe-convection settings only.')
  const og = request.orography
  if (og !== undefined) check(og.terrain?.ridges !== undefined && og.terrain.base_elevation_m !== undefined && OROGRAPHY_KEYS.every(([k]) => finite(og[k])) && Object.keys(og).length === OROGRAPHY_KEYS.length + 1, 'The editor represents synthetic ridges/basins terrain with every orography parameter only.')
  const g = request.grid
  check(g.origin_east_m === -0.5 * g.nx * g.spacing_east_m && g.origin_north_m === -0.5 * g.ny * g.spacing_north_m, 'The editor centers the grid on the geographic anchor.')
  return {
    start_time_utc: request.time.start_time_utc, timestep_seconds: request.time.timestep_seconds, steps: request.time.steps,
    snapshot_every_steps: request.snapshot_every_steps ?? request.time.steps,
    nx: g.nx, ny: g.ny, spacing_east_m: g.spacing_east_m, spacing_north_m: g.spacing_north_m,
    geographic_anchor_longitude_deg: g.geographic_anchor_longitude_deg, geographic_anchor_latitude_deg: g.geographic_anchor_latitude_deg,
    air_density_kg_m3: request.atmosphere.air_density_kg_m3, surface_friction_coefficient_s: request.atmosphere.surface_friction_coefficient_s,
    coriolis_mode: request.atmosphere.coriolis_mode, coriolis_parameter_s: request.atmosphere.coriolis_parameter_s ?? 0,
    temperature_diffusivity_m2_s: request.diffusion.temperature_m2_s, humidity_diffusivity_m2_s: request.diffusion.humidity_m2_s, momentum_diffusivity_m2_s: request.diffusion.momentum_m2_s,
    pressure_gradient_boundary: request.pressure_gradient_boundary ?? 'one_sided', scalar_boundary: request.scalar_boundary ?? 'zero_gradient', density_mode: request.density_mode ?? 'diagnosed',
    background_pressure_pa: systems.background_pressure_pa, background_temperature_k: t.background, background_humidity_kg_kg: q.background,
    column_enabled: request.column !== undefined,
    column_layer_count: request.column?.layer_count ?? 24, column_layer_thickness_m: request.column?.layer_thickness_m ?? 500,
    column_lapse_rate_k_m: request.column?.environment_lapse_rate_k_m ?? 0.0065, column_tropopause_height_m: request.column?.tropopause_height_m ?? 11000,
    column_humidity_scale_height_m: request.column?.humidity_scale_height_m ?? 2500, column_parcel_step_m: request.column?.parcel_step_m ?? 50,
    cloud_enabled: cf !== undefined, cloud: cf ? Object.fromEntries(CLOUD_CONFIG_KEYS.map(([k]) => [k, cf[k]])) : defaultCloudConfig(),
    cloud_heating_amplitude_k: cf?.surface_heating.amplitude_k ?? 6, cloud_heating_phase_seconds: cf?.surface_heating.phase_seconds ?? 7200,
    precip_enabled: pr !== undefined, precip: pr ? Object.fromEntries(PRECIP_CONFIG_KEYS.map(([k]) => [k, pr[k]])) : defaultPrecipConfig(),
    inversion_enabled: col?.inversion_strength_k !== undefined, inversion_base_m: col?.inversion_base_m ?? 1500, inversion_depth_m: col?.inversion_depth_m ?? 500,
    inversion_strength_k: col?.inversion_strength_k ?? 1, lapse_rate_above_inversion_k_m: col?.lapse_rate_above_inversion_k_m ?? 0.0085,
    surface_enabled: sf !== undefined, coast_east_m: sf?.land.coast_east_m ?? -200000, land_side: sf?.land.land_side ?? 'west',
    sea_surface_temperature_k: sf?.sea_surface_temperature_k.background ?? 302.5, sst_gradient_north_k_per_m: sf?.sea_surface_temperature_k.gradient_north_k_per_m ?? 0,
    storms_enabled: stm !== undefined, storm_seeds: (stm?.seeds ?? []).map(x => ({ east_m: x.east_m, north_m: x.north_m, max_wind_m_s: x.max_wind_m_s, radius_max_wind_m: x.radius_max_wind_m ?? 40000 })),
    severe_enabled: request.severe !== undefined,
    orography_enabled: og !== undefined, terrain_base_elevation_m: og?.terrain.base_elevation_m ?? 400,
    ridges: (og?.terrain.ridges ?? []).map(r => ({ orientation_deg: r.orientation_deg, center_east_m: r.center_east_m, center_north_m: r.center_north_m, height_m: r.height_m, half_width_m: r.half_width_m, length_m: r.length_m })),
    basins: (og?.terrain.basins ?? []).map(b => ({ center_east_m: b.center_east_m, center_north_m: b.center_north_m, depth_m: b.depth_m, radius_m: b.radius_m })),
    orography: og ? Object.fromEntries(OROGRAPHY_KEYS.map(([k]) => [k, og[k]])) : Object.fromEntries(OROGRAPHY_KEYS.map(([k, , v]) => [k, v])),
    airmasses: ta.slice(centers.length).map((a, i) => ({ east_m: a.east_m, north_m: a.north_m, radius_m: a.radius_m, temperature_anomaly_k: a.amplitude, humidity_anomaly_kg_kg: qa[centers.length + i].amplitude })),
    centers: centers.map((c, i) => ({ kind: c.kind, east_m: c.east_m, north_m: c.north_m, radius_m: c.radius_m, pressure_anomaly_pa: c.pressure_anomaly_pa, temperature_anomaly_k: ta[i].amplitude, humidity_anomaly_kg_kg: qa[i].amplitude })),
  }
}

// Column configuration as a request: the echoed config lists the inversion
// fields explicitly (zero strength / negative lapse mean "none").
function columnRequest(config) {
  const { inversion_base_m, inversion_depth_m, inversion_strength_k, lapse_rate_above_inversion_k_m, ...rest } = config
  return inversion_strength_k > 0 ? { ...rest, inversion_base_m, inversion_depth_m, inversion_strength_k, lapse_rate_above_inversion_k_m } : rest
}

// Rebuild the run request recorded in an engine output.
export function requestFromCoupledAtmosphere(atmosphere, frames) {
  const r = atmosphere.request, { coordinate_system, array_order, ...grid } = r.grid
  void coordinate_system; void array_order
  const field = source => source.kind === 'synthetic_gaussian_anomalies' ? { background: source.background, anomalies: clone(source.anomalies) } : null
  const atm = { air_density_kg_m3: r.atmosphere.air_density_kg_m3, surface_friction_coefficient_s: r.atmosphere.surface_friction_coefficient_s, coriolis_mode: r.atmosphere.coriolis_mode }
  if (atm.coriolis_mode === 'prescribed') atm.coriolis_parameter_s = r.atmosphere.coriolis_parameter_s
  const pressure = atmosphere.pressure_source.kind === 'synthetic_gaussian_systems'
    ? { pressure_systems: { background_pressure_pa: atmosphere.pressure_source.background_pressure_pa, centers: clone(atmosphere.pressure_source.centers) } }
    : { pressure_pa: clone(atmosphere.pressure_pa) }
  const t = field(atmosphere.temperature_source), q = field(atmosphere.humidity_source)
  return {
    schema_version: 'terraflux.weather.coupled-wind-run/v1', time: clone(r.time), grid, atmosphere: atm, diffusion: clone(r.diffusion),
    pressure_gradient_boundary: r.pressure_gradient_boundary, scalar_boundary: r.scalar_boundary, density_mode: r.density_mode,
    pressure,
    initial_fields: {
      air_temperature_k: t ?? { values: clone(frames[0].forecast.fields.air_temperature.values) },
      specific_humidity_kg_kg: q ?? { values: clone(atmosphere.specific_humidity_kg_kg_by_frame[0]) },
    },
    ...(atmosphere.columns ? { column: columnRequest(atmosphere.columns.config) } : {}),
    ...(atmosphere.clouds ? { cloud_formation: clone(atmosphere.clouds.config) } : {}),
    ...(atmosphere.precipitation ? { precipitation: clone(atmosphere.precipitation.config) } : {}),
    ...(atmosphere.surface && atmosphere.surface.source?.land?.kind === 'straight_coast' ? { surface: { land: { coast_east_m: atmosphere.surface.source.land.coast_east_m, land_side: atmosphere.surface.source.land.land_side }, sea_surface_temperature_k: { background: atmosphere.surface.source.sea_surface_temperature.background, gradient_north_k_per_m: atmosphere.surface.source.sea_surface_temperature.gradient_north_k_per_m } } } : {}),
    ...(atmosphere.orography && atmosphere.orography.config.terrain.kind === 'synthetic_ridges_and_basins' ? { orography: { terrain: { base_elevation_m: atmosphere.orography.config.terrain.base_elevation_m, ridges: clone(atmosphere.orography.config.terrain.ridges), basins: clone(atmosphere.orography.config.terrain.basins) }, ...Object.fromEntries(OROGRAPHY_KEYS.map(([k]) => [k, atmosphere.orography.config[k]])) } } : {}),
    ...(atmosphere.storms ? { storms: { seeds: clone(atmosphere.storms.config.seeds) } } : {}),
    ...(atmosphere.severe ? { severe: {} } : {}),
    snapshot_every_steps: r.snapshot_every_steps ?? r.time.steps,
  }
}

// Mirror of validate_coupled_wind_config plus a priori estimates for the
// wind-dependent scalar bound, which the solver checks at every step.
export function checkCoupledRequest(request) {
  const errors = [], warnings = [], estimates = {}
  const fail = (ok, message) => { if (!ok) errors.push(message) }
  const t = request.time, g = request.grid, a = request.atmosphere, d = request.diffusion
  fail(finite(t.timestep_seconds) && t.timestep_seconds > 0, 'Timestep must be positive.')
  fail(Number.isInteger(t.steps) && t.steps >= 1, 'Step count must be a positive integer.')
  fail(request.snapshot_every_steps === undefined || (Number.isInteger(request.snapshot_every_steps) && request.snapshot_every_steps >= 1 && request.snapshot_every_steps <= t.steps), 'Snapshot interval must be between 1 and the step count.')
  fail(typeof t.start_time_utc === 'string' && t.start_time_utc.endsWith('Z') && Number.isFinite(Date.parse(t.start_time_utc)), 'Start time must be a UTC timestamp ending in Z.')
  fail(Number.isInteger(g.nx) && Number.isInteger(g.ny) && g.nx >= 2 && g.ny >= 2, 'Grid needs at least 2 × 2 cells.')
  fail(g.nx * g.ny <= 65536, 'The viewer displays at most 65,536 cells.')
  fail(finite(g.spacing_east_m) && finite(g.spacing_north_m) && g.spacing_east_m > 0 && g.spacing_north_m > 0, 'Grid spacing must be positive.')
  fail(finite(g.geographic_anchor_latitude_deg) && Math.abs(g.geographic_anchor_latitude_deg) <= 90 && finite(g.geographic_anchor_longitude_deg) && Math.abs(g.geographic_anchor_longitude_deg) <= 180, 'Anchor latitude/longitude out of range.')
  fail(finite(a.air_density_kg_m3) && a.air_density_kg_m3 > 0, 'Air density must be positive.')
  fail(finite(a.surface_friction_coefficient_s) && a.surface_friction_coefficient_s >= 0, 'Friction coefficient must not be negative.')
  fail(CORIOLIS_MODES.includes(a.coriolis_mode), 'Unknown Coriolis mode.')
  fail(a.coriolis_mode !== 'prescribed' || finite(a.coriolis_parameter_s), 'Prescribed mode needs a finite Coriolis parameter.')
  for (const key of ['temperature_m2_s', 'humidity_m2_s', 'momentum_m2_s']) fail(finite(d[key]) && d[key] >= 0, `Diffusivity ${key} must not be negative.`)
  fail(DENSITY_MODES.includes(request.density_mode), 'Unknown density mode.')
  fail(SCALAR_BOUNDARIES.includes(request.scalar_boundary), 'Unknown scalar boundary.')
  fail(GRADIENT_BOUNDARIES.includes(request.pressure_gradient_boundary), 'Unknown pressure-gradient boundary.')
  const systems = request.pressure?.pressure_systems
  if (systems) {
    fail(finite(systems.background_pressure_pa) && systems.background_pressure_pa > 0, 'Background pressure must be positive.')
    for (const c of systems.centers) {
      fail(['high', 'low'].includes(c.kind), 'Pressure center kind must be high or low.')
      fail(finite(c.radius_m) && c.radius_m > 0, 'Pressure center radius must be positive.')
      fail(c.kind !== 'high' || c.pressure_anomaly_pa > 0, 'A high needs a positive pressure anomaly.')
      fail(c.kind !== 'low' || c.pressure_anomaly_pa < 0, 'A low needs a negative pressure anomaly.')
    }
  }
  const tf = request.initial_fields?.air_temperature_k, qf = request.initial_fields?.specific_humidity_kg_kg
  if (tf?.background !== undefined) {
    const extreme = tf.background + (tf.anomalies ?? []).reduce((s, x) => s + Math.min(0, x.amplitude), 0)
    fail(finite(tf.background) && extreme > 0, 'Temperature must stay above 0 K everywhere.')
  }
  if (qf?.background !== undefined) {
    const low = qf.background + (qf.anomalies ?? []).reduce((s, x) => s + Math.min(0, x.amplitude), 0)
    const high = qf.background + (qf.anomalies ?? []).reduce((s, x) => s + Math.max(0, x.amplitude), 0)
    fail(finite(qf.background) && low >= 0 && high < 1, 'Specific humidity must stay within [0, 1) kg/kg.')
  }
  const col = request.column
  if (col !== undefined) {
    fail(Number.isInteger(col.layer_count) && col.layer_count >= 2, 'Column needs at least 2 layers.')
    fail(finite(col.layer_thickness_m) && col.layer_thickness_m > 0, 'Column layer thickness must be positive.')
    fail(finite(col.environment_lapse_rate_k_m) && Math.abs(col.environment_lapse_rate_k_m) <= 0.05, 'Environment lapse rate must be within ±0.05 K/m.')
    fail(finite(col.tropopause_height_m) && col.tropopause_height_m >= 0, 'Tropopause height must not be negative.')
    fail(finite(col.humidity_scale_height_m) && col.humidity_scale_height_m > 0, 'Humidity scale height must be positive.')
    fail(finite(col.parcel_step_m) && col.parcel_step_m > 0 && col.parcel_step_m <= col.layer_thickness_m, 'Parcel step must be positive and no larger than the layer thickness.')
    if (tf?.background !== undefined && finite(col.layer_count) && finite(col.layer_thickness_m)) {
      const coldest = tf.background + (tf.anomalies ?? []).reduce((s, x) => s + Math.min(0, x.amplitude), 0)
      const top = Math.min(col.layer_count * col.layer_thickness_m, col.tropopause_height_m)
      fail(coldest - col.environment_lapse_rate_k_m * top > 0, 'The environment lapse rate drives the column below 0 K.')
    }
  }
  const cf = request.cloud_formation
  if (cf !== undefined) {
    fail(col !== undefined, 'Cloud formation requires the vertical column diagnostics.')
    for (const [key, label] of CLOUD_CONFIG_KEYS) fail(finite(cf[key]) && cf[key] > 0, `${label} must be positive.`)
    fail(finite(cf.warm_front_slope) && finite(cf.cold_front_slope) && cf.warm_front_slope <= cf.cold_front_slope, 'A warm front must be gentler than a cold front.')
    fail(cf.surface_heating && finite(cf.surface_heating.amplitude_k) && cf.surface_heating.amplitude_k >= 0 && finite(cf.surface_heating.phase_seconds), 'Surface heating needs a non-negative amplitude and a finite phase.')
  }
  const pr = request.precipitation
  if (pr !== undefined) {
    fail(cf !== undefined, 'Precipitation outcomes require cloud formation.')
    for (const [key, label] of PRECIP_CONFIG_KEYS) fail(finite(pr[key]) && pr[key] > 0, `${label} must be positive.`)
    fail(!(finite(pr.snow_wet_bulb_k) && finite(pr.sleet_wet_bulb_k)) || pr.snow_wet_bulb_k <= pr.sleet_wet_bulb_k, 'Snow wet-bulb limit must not exceed the sleet limit.')
    fail(!finite(pr.lightning_top_temperature_k) || pr.lightning_top_temperature_k < 273.15, 'Lightning needs a cloud-top limit below 0 °C (273.15 K).')
    for (const key of ['stratiform_efficiency', 'convective_efficiency', 'convective_updraft_fraction', 'updraft_parcel_factor', 'virga_surface_rh']) fail(!finite(pr[key]) || pr[key] <= 1, `${key} must not exceed 1.`)
  }
  const sf = request.surface, stm = request.storms
  if (stm !== undefined) fail(sf !== undefined && col !== undefined, 'Storm systems require the surface and column sections.')
  if (sf !== undefined) {
    fail(finite(sf.sea_surface_temperature_k?.background) && sf.sea_surface_temperature_k.background > 0, 'Sea-surface temperature must be positive.')
    fail(['west', 'east'].includes(sf.land?.land_side) && finite(sf.land?.coast_east_m), 'Coastline needs a finite position and a land side.')
  }
  if (stm !== undefined) for (const seed of stm.seeds ?? []) fail(finite(seed.east_m) && finite(seed.north_m) && finite(seed.max_wind_m_s) && seed.max_wind_m_s > 0 && finite(seed.radius_max_wind_m) && seed.radius_max_wind_m > 0, 'Storm seeds need a finite position, positive wind and radius.')
  if (request.severe !== undefined) fail(cf !== undefined, 'Severe convection requires cloud formation.')
  const og = request.orography
  if (og !== undefined) {
    for (const [key, label] of OROGRAPHY_KEYS) fail(finite(og[key]), `${label} must be a finite number.`)
    fail(!finite(og.blocking_time_scale_s) || og.blocking_time_scale_s >= t.timestep_seconds, 'The blocking time scale must be at least one timestep.')
    fail(!finite(og.brunt_vaisala_s) || og.brunt_vaisala_s > 0, 'The stability frequency must be positive.')
    for (const r of og.terrain?.ridges ?? []) fail(finite(r.orientation_deg) && finite(r.height_m) && r.half_width_m > 0 && r.length_m > 0, 'Ridges need an orientation, height, positive half width and length.')
    for (const b of og.terrain?.basins ?? []) fail(finite(b.depth_m) && b.radius_m > 0, 'Basins need a depth and a positive radius.')
  }
  if (col?.inversion_strength_k !== undefined) fail(col.inversion_strength_k >= 0 && col.inversion_depth_m > 0 && col.inversion_base_m >= 0, 'An inversion needs a positive depth and non-negative base and strength.')
  if (errors.length) return { errors, warnings, estimates }
  if (stm !== undefined && sf !== undefined) {
    const phi = g.geographic_anchor_latitude_deg * Math.PI / 180
    if (Math.abs(2 * EARTH_OMEGA * Math.sin(phi)) < 2.5e-5) warnings.push('Within about 10° of the equator the Coriolis parameter is too weak for tropical cyclones to spin up.')
    if (sf.sea_surface_temperature_k.background < 299.65) warnings.push('Sea-surface temperature below 26.5 °C: no potential intensity, storms will not intensify.')
  }
  if (col !== undefined) {
    estimates.column_top_m = col.layer_count * col.layer_thickness_m
    // Dry-adiabatic reference: the troposphere is stable when the lapse rate is below g / c_p.
    if (col.environment_lapse_rate_k_m > 9.80665 / 1004.6) warnings.push('Environment lapse rate exceeds the dry adiabatic 9.76 K/km; the column will be mixed by convective adjustment before the parcel diagnostics.')
  }

  // Coriolis parameter over the grid (beta plane varies with north distance).
  const phi = g.geographic_anchor_latitude_deg * Math.PI / 180
  const f0 = a.coriolis_mode === 'none' ? 0 : a.coriolis_mode === 'prescribed' ? a.coriolis_parameter_s : 2 * EARTH_OMEGA * Math.sin(phi)
  const beta = a.coriolis_mode === 'beta_plane' ? 2 * EARTH_OMEGA * Math.cos(phi) / EARTH_RADIUS : 0
  const northEdges = [g.origin_north_m + 0.5 * g.spacing_north_m, g.origin_north_m + (g.ny - 0.5) * g.spacing_north_m]
  const maxF = Math.max(...northEdges.map(y => Math.abs(f0 + beta * y)))
  const minF = Math.min(...northEdges.map(y => Math.abs(f0 + beta * y)))
  const k = a.surface_friction_coefficient_s, dt = t.timestep_seconds
  estimates.coriolis_parameter_s = f0
  estimates.inertial_period_h = f0 !== 0 ? 2 * Math.PI / Math.abs(f0) / 3600 : maxF > 0 ? 2 * Math.PI / maxF / 3600 : null
  estimates.friction_time_h = k > 0 ? 1 / k / 3600 : null
  estimates.coriolis_friction_number = dt * (maxF + k)
  fail(estimates.coriolis_friction_number <= 1, `Timestep × (|f| + k) = ${estimates.coriolis_friction_number.toFixed(3)} exceeds 1; reduce the timestep.`)
  const inv2 = 1 / g.spacing_east_m ** 2 + 1 / g.spacing_north_m ** 2
  estimates.momentum_diffusion_number = 2 * d.momentum_m2_s * dt * inv2
  fail(estimates.momentum_diffusion_number <= 1, `Momentum diffusion number ${estimates.momentum_diffusion_number.toFixed(3)} exceeds 1; reduce the diffusivity or timestep.`)
  estimates.cross_isobar_angle_deg = minF > 0 ? Math.atan(k / minF) * 180 / Math.PI : k > 0 ? 90 : null

  // A priori scalar bound from the configured pressure field: the balanced
  // speed is |∇p| / (ρ √(f² + k²)); spin-up from rest overshoots it by the
  // damped inertial factor 1 + e^(−kπ/|f|) (2 without friction).
  const gradient = maxPressureGradient(request)
  if (gradient !== null) {
    const rho = request.density_mode === 'constant' ? a.air_density_kg_m3 : a.air_density_kg_m3 * 0.97
    const balanced = minF > 0 || k > 0 ? gradient / (rho * Math.hypot(minF, k)) : null
    const overshoot = minF > 0 ? 1 + Math.exp(-k * Math.PI / minF) : 1
    estimates.max_pressure_gradient_pa_m = gradient
    estimates.balanced_speed_estimate_m_s = balanced
    estimates.peak_speed_estimate_m_s = balanced === null ? null : overshoot * balanced
    const kappa = Math.max(d.temperature_m2_s, d.humidity_m2_s)
    estimates.scalar_stability_estimate = balanced === null ? null : dt * (estimates.peak_speed_estimate_m_s * (1 / g.spacing_east_m + 1 / g.spacing_north_m) + 2 * kappa * inv2)
    if (balanced === null) warnings.push('Without rotation or friction the wind grows without bound; the solver will reject the run once |u| Δt / Δx exceeds 1.')
    else if (estimates.scalar_stability_estimate > 1) warnings.push(`Estimated scalar stability number ${estimates.scalar_stability_estimate.toFixed(2)} at the spin-up peak exceeds 1; the solver may reject the run mid-way. Reduce the timestep or the pressure anomalies.`)
    if (balanced !== null) {
      // First-order upwind adds implicit diffusion of about |u| Δx / 2.
      estimates.numerical_diffusivity_estimate_m2_s = 0.5 * balanced * Math.max(g.spacing_east_m, g.spacing_north_m)
      if (kappa > 0 && kappa < 0.1 * estimates.numerical_diffusivity_estimate_m2_s) warnings.push(`Configured diffusivity is far below the upwind numerical diffusion (~${Math.round(estimates.numerical_diffusivity_estimate_m2_s).toLocaleString('en-US')} m²/s at ${balanced.toFixed(0)} m/s); it mainly matters in calm regions.`)
    }
  }
  return { errors, warnings, estimates }
}

// Largest cell-centred pressure-gradient magnitude of the configured field,
// using the same central/one-sided stencil as the solver.
export function maxPressureGradient(request) {
  const g = request.grid, systems = request.pressure?.pressure_systems
  let pressure = request.pressure?.pressure_pa
  if (systems) {
    pressure = []
    for (let y = 0; y < g.ny; y++) for (let x = 0; x < g.nx; x++) {
      const east = g.origin_east_m + (x + 0.5) * g.spacing_east_m, north = g.origin_north_m + (y + 0.5) * g.spacing_north_m
      pressure.push(systems.centers.reduce((p, c) => p + c.pressure_anomaly_pa * Math.exp(-(((east - c.east_m) / c.radius_m) ** 2 + ((north - c.north_m) / c.radius_m) ** 2)), systems.background_pressure_pa))
    }
  }
  if (!Array.isArray(pressure) || pressure.length !== g.nx * g.ny || !pressure.every(finite)) return null
  const periodic = request.pressure_gradient_boundary === 'periodic'
  let max = 0
  for (let y = 0; y < g.ny; y++) for (let x = 0; x < g.nx; x++) {
    const west = x === 0 ? (periodic ? g.nx - 1 : x) : x - 1, east = x + 1 === g.nx ? (periodic ? 0 : x) : x + 1
    const south = y === 0 ? (periodic ? g.ny - 1 : y) : y - 1, north = y + 1 === g.ny ? (periodic ? 0 : y) : y + 1
    const dx = (pressure[y * g.nx + east] - pressure[y * g.nx + west]) / ((periodic ? 2 : east - west) * g.spacing_east_m)
    const dy = (pressure[north * g.nx + x] - pressure[south * g.nx + x]) / ((periodic ? 2 : north - south) * g.spacing_north_m)
    max = Math.max(max, Math.hypot(dx, dy))
  }
  return max
}

// Validate the coupled_atmosphere timeline extension and attach per-frame
// display fields. Returns the parsed extension.
export function validateCoupledAtmosphere(atmosphere, frames) {
  const a = atmosphere, grid = frames[0].forecast.grid, count = grid.nx * grid.ny
  check(a?.schema_version === 'terraflux.weather.coupled_atmosphere/v1' && a.model === 'coupled_wind_moist_layer_v1' && ['engine_output', 'synthetic'].includes(a.source_kind), 'Unsupported coupled atmosphere contract.')
  check(a.request && Object.keys(grid).every(key => a.request.grid?.[key] === grid[key]), 'Coupled atmosphere request grid must match the frames.')
  check(a.request.time?.start_time_utc === frames[0].forecast.start_time_utc, 'Coupled atmosphere start time must match the frames.')
  const r = a.request
  check(finite(r.time.timestep_seconds) && r.time.timestep_seconds > 0 && Number.isInteger(r.time.steps) && r.time.steps >= 1, 'Invalid coupled time configuration.')
  check(['temperature_m2_s', 'humidity_m2_s', 'momentum_m2_s'].every(key => finite(r.diffusion?.[key]) && r.diffusion[key] >= 0), 'Invalid coupled diffusion configuration.')
  check(DENSITY_MODES.includes(r.density_mode) && SCALAR_BOUNDARIES.includes(r.scalar_boundary) && GRADIENT_BOUNDARIES.includes(r.pressure_gradient_boundary) && CORIOLIS_MODES.includes(r.atmosphere?.coriolis_mode), 'Invalid coupled model options.')
  check(finite(r.atmosphere.air_density_kg_m3) && r.atmosphere.air_density_kg_m3 > 0 && finite(r.atmosphere.surface_friction_coefficient_s) && r.atmosphere.surface_friction_coefficient_s >= 0, 'Invalid coupled atmosphere parameters.')
  check(Array.isArray(a.pressure_pa) && a.pressure_pa.length === count && a.pressure_pa.every(v => finite(v) && v > 0), 'Invalid coupled pressure field.')
  check(Array.isArray(a.coriolis_parameter_s) && a.coriolis_parameter_s.length === count && a.coriolis_parameter_s.every(finite), 'Invalid Coriolis parameter field.')
  for (const key of ['specific_humidity_kg_kg_by_frame', 'relative_humidity_pct_by_frame', 'air_density_kg_m3_by_frame', 'supersaturated_cells_by_frame', 'scalar_stability_number_by_frame']) check(Array.isArray(a[key]) && a[key].length === frames.length, `Coupled ${key} frame count mismatch.`)
  frames.forEach((frame, i) => {
    const q = a.specific_humidity_kg_kg_by_frame[i], rh = a.relative_humidity_pct_by_frame[i], rho = a.air_density_kg_m3_by_frame[i]
    check(Array.isArray(q) && q.length === count && q.every(v => finite(v) && v >= 0 && v < 1), 'Invalid specific humidity field.')
    check(Array.isArray(rh) && rh.length === count && rh.every(v => finite(v) && v >= 0), 'Invalid relative humidity field.')
    check(Array.isArray(rho) && rho.length === count && rho.every(v => finite(v) && v > 0), 'Invalid air density field.')
    check(Number.isInteger(a.supersaturated_cells_by_frame[i]) && a.supersaturated_cells_by_frame[i] >= 0 && finite(a.scalar_stability_number_by_frame[i]), 'Invalid coupled frame diagnostics.')
    frame.coupled = { pressure: a.pressure_pa, humidity: rh, density: rho, specificHumidity: q, coriolis: a.coriolis_parameter_s, supersaturated: a.supersaturated_cells_by_frame[i], stability: a.scalar_stability_number_by_frame[i], request: r, sources: { pressure: a.pressure_source, temperature: a.temperature_source, humidity: a.humidity_source }, raw: a, synthetic: a.source_kind === 'synthetic' }
  })
  if (a.columns !== undefined) validateColumnDiagnostics(a.columns, frames)
  if (a.clouds !== undefined) validateCloudFormation(a.clouds, frames)
  if (a.precipitation !== undefined) { check(a.clouds !== undefined, 'Precipitation outcomes require the clouds block.'); validatePrecipitation(a.precipitation, frames) }
  if (a.surface !== undefined) validateSurface(a.surface, frames)
  if (a.storms !== undefined) { check(a.surface !== undefined, 'Storm systems require the surface block.'); validateStormSystems(a.storms, frames) }
  if (a.severe !== undefined) { check(a.clouds !== undefined, 'Severe convection requires the clouds block.'); validateSevereConvection(a.severe, frames) }
  if (a.orography !== undefined) validateOrography(a.orography, frames, frames[0].terrain)
  return a
}

// Optional vertical-column diagnostics (LCL, LFC, EL, CAPE, CIN) per cell.
export function validateColumnDiagnostics(columns, frames) {
  const c = columns, count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  check(c?.schema_version === 'terraflux.weather.column_diagnostics/v1' && c.method === 'hydrostatic_layers_convective_adjustment_surface_parcel', 'Unsupported column diagnostics contract.')
  const cfg = c.config
  check(cfg && Number.isInteger(cfg.layer_count) && cfg.layer_count >= 2 && finite(cfg.layer_thickness_m) && cfg.layer_thickness_m > 0 && finite(cfg.environment_lapse_rate_k_m) && finite(cfg.tropopause_height_m) && finite(cfg.humidity_scale_height_m) && cfg.humidity_scale_height_m > 0 && finite(cfg.parcel_step_m) && cfg.parcel_step_m > 0, 'Invalid column configuration.')
  check(['inversion_base_m', 'inversion_depth_m', 'inversion_strength_k', 'lapse_rate_above_inversion_k_m'].every(k => cfg[k] === undefined || finite(cfg[k])), 'Invalid inversion configuration.')
  check(Array.isArray(c.column_mass_kg_m2) && c.column_mass_kg_m2.length === count && c.column_mass_kg_m2.every(v => finite(v) && v > 0), 'Invalid column mass field.')
  check(Array.isArray(c.top_pressure_pa) && c.top_pressure_pa.length === count && c.top_pressure_pa.every(v => finite(v) && v > 0), 'Invalid column top pressure field.')
  for (const key of ['lcl_height_m_by_frame', 'lfc_height_m_by_frame', 'equilibrium_level_m_by_frame', 'cape_j_kg_by_frame', 'cin_j_kg_by_frame', 'convective_potential_by_frame', 'condensed_water_kg_m2_by_frame']) check(Array.isArray(c[key]) && c[key].length === frames.length, `Column ${key} frame count mismatch.`)
  const level = v => v === null || (finite(v) && v >= 0)
  frames.forEach((frame, i) => {
    const lcl = c.lcl_height_m_by_frame[i], lfc = c.lfc_height_m_by_frame[i], el = c.equilibrium_level_m_by_frame[i]
    const cape = c.cape_j_kg_by_frame[i], cin = c.cin_j_kg_by_frame[i], potential = c.convective_potential_by_frame[i], condensed = c.condensed_water_kg_m2_by_frame[i]
    check([lcl, lfc, el, cape, cin, potential, condensed].every(v => Array.isArray(v) && v.length === count), 'Column arrays do not match the grid.')
    check(lcl.every(level) && lfc.every(level) && el.every(level), 'Invalid column levels.')
    check(cape.every(v => finite(v) && v >= 0) && cin.every(v => finite(v) && v >= 0) && condensed.every(v => finite(v) && v >= 0), 'Invalid CAPE, CIN or condensate.')
    check(potential.every(v => CONVECTIVE_POTENTIALS.includes(v)), 'Invalid convective potential label.')
    check(lfc.every((v, j) => v === null || (lcl[j] !== null && el[j] !== null && v >= lcl[j] && el[j] > v && cape[j] > 0)), 'Column levels must be ordered LCL <= LFC < EL with positive CAPE.')
    frame.coupled.columns = { lcl, lfc, el, cape, cin, potential, condensed, config: cfg }
  })
  return c
}
