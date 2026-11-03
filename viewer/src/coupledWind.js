// Coupled wind / moist-layer support: validation of engine output for display
// and a request editor that mirrors the C++ configuration bounds. No physics
// is solved in the browser; the request is downloaded and run with
// terraflux-run-coupled-wind, and the resulting timeline is opened here.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }
const clone = value => JSON.parse(JSON.stringify(value))

export const CORIOLIS_MODES = ['none', 'f_plane', 'beta_plane', 'prescribed']
export const DENSITY_MODES = ['diagnosed', 'constant']
export const SCALAR_BOUNDARIES = ['zero_gradient', 'periodic']
export const GRADIENT_BOUNDARIES = ['one_sided', 'periodic']
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
    centers: [
      { kind: 'high', east_m: -300000, north_m: -40000, radius_m: 350000, pressure_anomaly_pa: 800, temperature_anomaly_k: -4, humidity_anomaly_kg_kg: -0.003 },
      { kind: 'low', east_m: 260000, north_m: 100000, radius_m: 300000, pressure_anomaly_pa: -900, temperature_anomaly_k: 5, humidity_anomaly_kg_kg: 0.004 },
    ],
  }
}

export function formToRequest(form) {
  const anomaly = (c, amplitude) => ({ east_m: c.east_m, north_m: c.north_m, radius_m: c.radius_m, amplitude })
  const atmosphere = { air_density_kg_m3: form.air_density_kg_m3, surface_friction_coefficient_s: form.surface_friction_coefficient_s, coriolis_mode: form.coriolis_mode }
  if (form.coriolis_mode === 'prescribed') atmosphere.coriolis_parameter_s = form.coriolis_parameter_s
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
      air_temperature_k: { background: form.background_temperature_k, anomalies: form.centers.map(c => anomaly(c, c.temperature_anomaly_k)) },
      specific_humidity_kg_kg: { background: form.background_humidity_kg_kg, anomalies: form.centers.map(c => anomaly(c, c.humidity_anomaly_kg_kg)) },
    },
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
  check(ta.length === centers.length && qa.length === centers.length, 'Temperature and humidity anomalies must match the pressure centers one-to-one.')
  const same = (a, c) => a.east_m === c.east_m && a.north_m === c.north_m && a.radius_m === c.radius_m
  check(centers.every((c, i) => same(ta[i], c) && same(qa[i], c)), 'Anomalies must be co-located with the pressure centers.')
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
    centers: centers.map((c, i) => ({ kind: c.kind, east_m: c.east_m, north_m: c.north_m, radius_m: c.radius_m, pressure_anomaly_pa: c.pressure_anomaly_pa, temperature_anomaly_k: ta[i].amplitude, humidity_anomaly_kg_kg: qa[i].amplitude })),
  }
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
  if (errors.length) return { errors, warnings, estimates }

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
  check(a?.schema_version === 'terraflux.weather.coupled_atmosphere/v1' && a.model === 'coupled_wind_moist_layer_v1' && a.source_kind === 'engine_output', 'Unsupported coupled atmosphere contract.')
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
    frame.coupled = { pressure: a.pressure_pa, humidity: rh, density: rho, specificHumidity: q, coriolis: a.coriolis_parameter_s, supersaturated: a.supersaturated_cells_by_frame[i], stability: a.scalar_stability_number_by_frame[i], request: r, sources: { pressure: a.pressure_source, temperature: a.temperature_source, humidity: a.humidity_source }, raw: a }
  })
  return a
}
