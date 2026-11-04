// Cloud formation view support: validation of the documented
// terraflux.weather.cloud_formation/v1 block, a synthetic mock in the same
// format, categorical styling and per-frame metrics. No physics runs here.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const CLOUD_TYPES = [
  { key: 'none', label: 'Clear', abbreviation: '—', color: '#1b2a33', level: 'none' },
  { key: 'cumulus', label: 'Cumulus', abbreviation: 'Cu', color: '#d9e8f2', level: 'low' },
  { key: 'towering_cumulus', label: 'Towering cumulus', abbreviation: 'TCu', color: '#b7c9d6', level: 'low' },
  { key: 'cumulonimbus', label: 'Cumulonimbus (thunderstorm)', abbreviation: 'Cb', color: '#7a3f6d', level: 'deep' },
  { key: 'stratocumulus', label: 'Stratocumulus', abbreviation: 'Sc', color: '#8fa4b2', level: 'low' },
  { key: 'stratus', label: 'Stratus', abbreviation: 'St', color: '#6f8491', level: 'low' },
  { key: 'nimbostratus', label: 'Nimbostratus (rain)', abbreviation: 'Ns', color: '#48606f', level: 'low' },
  { key: 'altocumulus', label: 'Altocumulus', abbreviation: 'Ac', color: '#c5d5e0', level: 'mid' },
  { key: 'altostratus', label: 'Altostratus', abbreviation: 'As', color: '#9fb3c1', level: 'mid' },
  { key: 'cirrus', label: 'Cirrus', abbreviation: 'Ci', color: '#eef5fa', level: 'high' },
  { key: 'cirrostratus', label: 'Cirrostratus', abbreviation: 'Cs', color: '#d7e6ef', level: 'high' },
]
export const MECHANISMS = ['none', 'convective', 'frontal', 'frontal_convective']
export const TRIGGERS = ['none', 'free', 'thermals', 'frontal', 'convergence', 'overrunning']
export const FRONT_TYPES = ['none', 'cold', 'warm', 'stationary']
export const FRONT_COLORS = { cold: '#4da3ff', warm: '#ff6b6b', stationary: '#c792ea' }
const typeIndex = Object.fromEntries(CLOUD_TYPES.map((t, i) => [t.key, i]))
export const cloudTypeIndex = key => typeIndex[key] ?? 0
export const cloudTypeInfo = key => CLOUD_TYPES[cloudTypeIndex(key)]

const FIELD_KEYS = ['cloud_type', 'mechanism', 'trigger', 'base_m', 'top_m', 'cloud_fraction', 'thunderstorm', 'lifting_depth_m',
  'heated_cape_j_kg', 'heated_cin_j_kg', 'front_type', 'temperature_gradient_k_m', 'temperature_advection_k_s', 'divergence_s',
  'distance_to_front_m', 'side']

// Validate the clouds block and attach frame.clouds for display.
export function validateCloudFormation(clouds, frames) {
  const c = clouds, count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  check(c?.schema_version === 'terraflux.weather.cloud_formation/v1' && c.method === 'frontal_convergence_overrunning_heated_parcel_convection', 'Unsupported cloud formation contract.')
  check(['engine_output', 'synthetic'].includes(c.source_kind), 'Cloud formation source must be engine_output or synthetic.')
  const cfg = c.config
  check(cfg && ['frontal_gradient_threshold_k_m', 'advection_threshold_k_s', 'boundary_layer_depth_m', 'lifting_time_scale_s', 'mixed_layer_growth_m_per_k', 'warm_front_slope', 'cold_front_slope', 'warm_front_reach_m', 'cold_front_reach_m', 'frontal_band_width_m', 'cin_trigger_j_kg', 'cape_threshold_j_kg', 'max_cloud_top_m'].every(k => finite(cfg[k]) && cfg[k] >= 0), 'Invalid cloud formation configuration.')
  check(cfg.warm_front_slope <= cfg.cold_front_slope && cfg.max_cloud_top_m > 0, 'Invalid frontal slopes or cloud top.')
  check(cfg.surface_heating && finite(cfg.surface_heating.amplitude_k) && cfg.surface_heating.amplitude_k >= 0 && finite(cfg.surface_heating.phase_seconds), 'Invalid surface heating configuration.')
  check(Array.isArray(c.frames) && c.frames.length === frames.length, 'Cloud frame count mismatch.')
  frames.forEach((frame, i) => {
    const f = c.frames[i]
    check(f && f.elapsed_seconds === frame.forecast.elapsed_seconds, 'Cloud frame times must match the timeline.')
    check(finite(f.surface_heating_k) && f.surface_heating_k >= 0, 'Invalid surface heating.')
    check(FIELD_KEYS.every(k => Array.isArray(f[k]) && f[k].length === count), 'Cloud arrays do not match the grid.')
    let cloudy = 0, storms = 0, fronts = 0
    for (let j = 0; j < count; j++) {
      const kind = f.cloud_type[j], mech = f.mechanism[j], trig = f.trigger[j], base = f.base_m[j], top = f.top_m[j]
      check(typeIndex[kind] !== undefined && MECHANISMS.includes(mech) && TRIGGERS.includes(trig), 'Unknown cloud type, mechanism or trigger.')
      if (kind === 'none') check(mech === 'none' && trig === 'none' && base === null && top === null && f.cloud_fraction[j] === 0, 'Clear cells must have no cloud properties.')
      else { cloudy++; check(mech !== 'none' && trig !== 'none' && finite(base) && finite(top) && base >= 0 && top > base && finite(f.cloud_fraction[j]) && f.cloud_fraction[j] > 0 && f.cloud_fraction[j] <= 1, 'Cloudy cells need base < top and a fraction.') }
      check(typeof f.thunderstorm[j] === 'boolean' && f.thunderstorm[j] === (kind === 'cumulonimbus'), 'Thunderstorm flag must follow cumulonimbus.')
      storms += f.thunderstorm[j] ? 1 : 0
      check(finite(f.lifting_depth_m[j]) && f.lifting_depth_m[j] >= 0 && finite(f.heated_cape_j_kg[j]) && f.heated_cape_j_kg[j] >= 0 && finite(f.heated_cin_j_kg[j]) && f.heated_cin_j_kg[j] >= 0, 'Invalid lifting or convective energies.')
      check(FRONT_TYPES.includes(f.front_type[j]) && [-1, 0, 1].includes(f.side[j]) && finite(f.temperature_gradient_k_m[j]) && finite(f.temperature_advection_k_s[j]) && finite(f.divergence_s[j]), 'Invalid front analysis values.')
      if (f.front_type[j] !== 'none') { fronts++; check(f.side[j] === 0, 'Frontal cells have no side.') }
      check(f.distance_to_front_m[j] === null ? fronts === 0 || f.front_cells === 0 : finite(f.distance_to_front_m[j]) && f.distance_to_front_m[j] >= 0, 'Invalid distance to front.')
    }
    check(f.cloudy_cells === cloudy && f.thunderstorm_cells === storms && f.front_cells === fronts, 'Cloud frame totals disagree with the arrays.')
    frame.clouds = { type: f.cloud_type, mechanism: f.mechanism, trigger: f.trigger, base: f.base_m, top: f.top_m, fraction: f.cloud_fraction, thunderstorm: f.thunderstorm, lifting: f.lifting_depth_m, cape: f.heated_cape_j_kg, cin: f.heated_cin_j_kg, frontType: f.front_type, gradient: f.temperature_gradient_k_m, advection: f.temperature_advection_k_s, divergence: f.divergence_s, distance: f.distance_to_front_m, side: f.side, heating: f.surface_heating_k, cloudy, storms, fronts, config: cfg, synthetic: c.source_kind === 'synthetic' }
  })
  return c
}

// Per-frame metrics for the cloud view.
export function summarizeClouds(frame) {
  const c = frame.clouds, n = c.type.length, counts = {}
  for (const t of c.type) counts[t] = (counts[t] ?? 0) + 1
  const cloudy = c.type.map((t, i) => t === 'none' ? null : i).filter(i => i !== null)
  const mean = values => values.length ? values.reduce((a, b) => a + b, 0) / values.length : null
  const dominant = Object.entries(counts).filter(([k]) => k !== 'none').sort((a, b) => b[1] - a[1])[0]
  return {
    cloud_cover_pct: c.fraction.reduce((a, b) => a + b, 0) / n * 100,
    cloudy_pct: cloudy.length / n * 100,
    thunderstorm_cells: c.storms,
    cold_front_cells: c.frontType.filter(t => t === 'cold').length,
    warm_front_cells: c.frontType.filter(t => t === 'warm').length,
    stationary_front_cells: c.frontType.filter(t => t === 'stationary').length,
    dominant_type: dominant ? dominant[0] : 'none',
    mean_base_m: mean(cloudy.map(i => c.base[i])),
    mean_top_m: mean(cloudy.map(i => c.top[i])),
    frontal_cells: c.mechanism.filter(m => m === 'frontal' || m === 'frontal_convective').length,
    convective_cells: c.mechanism.filter(m => m === 'convective' || m === 'frontal_convective').length,
    surface_heating_k: c.heating,
  }
}

// Synthetic mock timeline in the documented format: a warm front ahead of a
// low (layered cloud rising with distance), a trailing cold front (narrow
// band with thunderstorms by day) and afternoon convection in the warm
// sector. Illustration only; labelled synthetic and not solver output.
export function cloudFormationDemo() {
  const nx = 24, ny = 16, spacing = 50000, count = nx * ny, frameCount = 6
  const grid = { nx, ny, origin_east_m: -600000, origin_north_m: -400000, spacing_east_m: spacing, spacing_north_m: spacing, geographic_anchor_longitude_deg: 10, geographic_anchor_latitude_deg: 50, coordinate_system: 'local_azimuthal_equidistant_m', array_order: 'row_major_y_then_x' }
  const config = { frontal_gradient_threshold_k_m: 2e-5, advection_threshold_k_s: 0.1 / 3600, boundary_layer_depth_m: 1500, lifting_time_scale_s: 21600, mixed_layer_growth_m_per_k: 300, warm_front_slope: 1 / 200, cold_front_slope: 1 / 75, warm_front_reach_m: 800000, cold_front_reach_m: 200000, frontal_band_width_m: 150000, cin_trigger_j_kg: 20, cape_threshold_j_kg: 100, max_cloud_top_m: 12000, surface_heating: { amplitude_k: 6, phase_seconds: 0 } }
  const frames = [], cloudFrames = [], humidity = [], rh = [], density = [], supersaturated = [], stability = []
  const pressure = [], coriolis = []
  for (let j = 0; j < count; j++) { pressure.push(101000); coriolis.push(1.1e-4) }
  for (let f = 0; f < frameCount; f++) {
    const t = f * 4 * 3600, heating = 6 * Math.max(0, Math.sin(2 * Math.PI * t / 86400))
    const shift = f * 1.2  // fronts move east over the sequence
    const temp = [], u = [], v = [], q = [], cl = { cloud_type: [], mechanism: [], trigger: [], base_m: [], top_m: [], cloud_fraction: [], thunderstorm: [], lifting_depth_m: [], heated_cape_j_kg: [], heated_cin_j_kg: [], front_type: [], temperature_gradient_k_m: [], temperature_advection_k_s: [], divergence_s: [], distance_to_front_m: [], side: [] }
    let cloudy = 0, storms = 0, fronts = 0
    for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
      // Warm front: diagonal band from (8,0) to (16,16); cold front: from (4,16) to (10,0), both shifted.
      const warmLine = 8 + shift + y * 0.5, coldLine = 10 + shift - y * 0.375
      const dWarm = (x - warmLine) * spacing * 0.9, dCold = (x - coldLine) * spacing * 0.9
      const warmSector = x > coldLine && x < warmLine
      const coldPocket = x <= 4 && y >= 13
      const T = coldPocket ? 275 : 283 + (warmSector ? 8 : x >= warmLine ? 2 : 0) + 0.04 * y
      temp.push(T); q.push(warmSector ? 0.012 : 0.006); u.push(8 + (warmSector ? 4 : 0)); v.push(warmSector ? 3 : -2)
      let front = 'none', gradient = 5e-6, advection = 1e-5
      if (Math.abs(dWarm) < spacing * 0.7) { front = 'warm'; gradient = 4e-5; advection = 1.2e-4 }
      else if (Math.abs(dCold) < spacing * 0.7) { front = 'cold'; gradient = 6e-5; advection = -2e-4 }
      const distance = Math.min(Math.abs(dWarm), Math.abs(dCold))
      const nearWarm = Math.abs(dWarm) <= Math.abs(dCold)
      const side = front !== 'none' ? 0 : (nearWarm ? (x < warmLine ? 1 : -1) : (x > coldLine ? 1 : -1))
      const divergence = front === 'cold' ? -1.6e-4 : front === 'warm' ? -8e-5 : warmSector ? -2e-5 : 1e-5
      const lifting = Math.max(0, -1500 * divergence) * 21600
      const cape = warmSector ? 900 + 250 * heating : 0, cin = warmSector ? Math.max(0, 60 - 12 * heating) : 0
      let kind = 'none', mech = 'none', trig = 'none', base = null, top = null
      if (front === 'cold' && cape > 0) { kind = heating > 2 ? 'cumulonimbus' : 'towering_cumulus'; mech = 'frontal_convective'; trig = 'frontal'; base = 900; top = heating > 2 ? 10500 : 5200 }
      else if (front === 'warm') { kind = 'nimbostratus'; mech = 'frontal'; trig = 'frontal'; base = 800; top = 6400 }
      else if (side === -1 && nearWarm && Math.abs(dWarm) < 800000) {
        const wedge = Math.abs(dWarm) / 200; base = Math.max(wedge, 700); top = wedge + 4000
        kind = base < 2000 ? 'nimbostratus' : base < 6000 ? 'altostratus' : 'cirrostratus'; mech = 'frontal'; trig = 'overrunning'
      } else if (warmSector && cape >= 100 && cin <= 20) { kind = heating >= 5 ? 'cumulonimbus' : 'towering_cumulus'; mech = Math.abs(dCold) < 150000 ? 'frontal_convective' : 'convective'; trig = 'free'; base = 1000 + 120 * heating; top = heating >= 5 ? 9800 : 4500 }
      else if (warmSector && heating > 0) { kind = 'cumulus'; mech = 'convective'; trig = 'thermals'; base = 1000 + 120 * heating; top = 1500 + 300 * heating }
      else if (side === -1 && !nearWarm && Math.abs(dCold) < 200000) { kind = 'stratocumulus'; mech = 'frontal'; trig = 'overrunning'; base = Math.max(Math.abs(dCold) / 75, 900); top = base + 400 }
      const fraction = cloudTypeInfo(kind).key === 'none' ? 0 : { cumulus: .3, towering_cumulus: .5, cumulonimbus: .9, stratocumulus: .6, stratus: .9, nimbostratus: 1, altocumulus: .5, altostratus: .9, cirrus: .3, cirrostratus: .7 }[kind]
      if (kind !== 'none') cloudy++
      if (kind === 'cumulonimbus') storms++
      if (front !== 'none') fronts++
      cl.cloud_type.push(kind); cl.mechanism.push(mech); cl.trigger.push(trig); cl.base_m.push(base); cl.top_m.push(top); cl.cloud_fraction.push(fraction); cl.thunderstorm.push(kind === 'cumulonimbus'); cl.lifting_depth_m.push(lifting); cl.heated_cape_j_kg.push(cape); cl.heated_cin_j_kg.push(cin); cl.front_type.push(front); cl.temperature_gradient_k_m.push(gradient); cl.temperature_advection_k_s.push(advection); cl.divergence_s.push(divergence); cl.distance_to_front_m.push(distance); cl.side.push(side)
    }
    frames.push({ schema_version: 'terraflux.weather.forecast/v1', start_time_utc: '2026-07-14T06:00:00Z', elapsed_seconds: t, grid: { ...grid }, observation_updates: [], fields: { air_temperature: { units: 'K', values: temp }, wind_east: { units: 'm/s', values: u }, wind_north: { units: 'm/s', values: v } } })
    cloudFrames.push({ elapsed_seconds: t, surface_heating_k: heating, ...cl, cloudy_cells: cloudy, thunderstorm_cells: storms, front_cells: fronts })
    humidity.push(q); rh.push(q.map((qq, j) => Math.min(99, 100 * qq / (0.0038 * Math.exp(0.0643 * (temp[j] - 273.15))) / 1.6))); density.push(temp.map((T, j) => 101000 / (287.05 * T * (1 + 0.608 * q[j])))); supersaturated.push(0); stability.push(0.3)
  }
  const request = { time: { start_time_utc: '2026-07-14T06:00:00Z', timestep_seconds: 300, steps: 240 }, grid: { ...grid }, atmosphere: { air_density_kg_m3: 1.225, surface_friction_coefficient_s: 2e-5, coriolis_mode: 'f_plane', coriolis_parameter_s: 0, earth_angular_speed_rad_s: 7.2921159e-5, earth_radius_m: 6371000 }, diffusion: { temperature_m2_s: 5000, humidity_m2_s: 5000, momentum_m2_s: 5000 }, pressure_gradient_boundary: 'one_sided', scalar_boundary: 'zero_gradient', density_mode: 'diagnosed', snapshot_every_steps: 48 }
  // Precipitation outcomes derived from the mock clouds with the documented rules.
  const pconfig = { stratiform_efficiency: .7, convective_efficiency: .4, convective_updraft_fraction: .08, updraft_parcel_factor: .5, interval_seconds: 14400, snow_wet_bulb_k: 273.65, sleet_wet_bulb_k: 275.15, lightning_top_temperature_k: 253.15, hail_min_diameter_cm: .5, hail_melting_cm_per_km: .3, hail_max_diameter_cm: 10, drizzle_max_rate_mm_h: .5, virga_surface_rh: .4, virga_max_rate_mm_h: 1, virga_min_base_m: 2000, minimum_rate_mm_h: .05 }
  const intensityOf = r => r < .05 ? 'none' : r < 2.5 ? 'light' : r < 10 ? 'moderate' : r < 50 ? 'heavy' : 'violent'
  const precipitationFrames = cloudFrames.map((cf, f) => {
    const temp = frames[f].fields.air_temperature.values
    const out = { precipitation_type: [], phase: [], character: [], intensity: [], rate_mm_h: [], duration_fraction: [], accumulation_mm: [], hail_max_diameter_cm: [], lightning: [], virga: [], updraft_m_s: [], freezing_level_m: [], surface_wet_bulb_k: [], precipitable_water_mm: [], cloud_base_temperature_k: [], cloud_top_temperature_k: [], hazards: [] }
    let precipitating = 0, lightning = 0, hail = 0, snow = 0, total = 0, maxRate = 0
    for (let j = 0; j < count; j++) {
      const kind = cf.cloud_type[j], T = temp[j], cold = T < 277 && j % 3 === 0
      const rule = { nimbostratus: ['continuous', 4.5, 1], stratus: ['drizzle', .4, 1], stratocumulus: ['intermittent', .2, .5], altostratus: ['intermittent', .8, .5], towering_cumulus: ['showers', 3.5, .2], cumulonimbus: ['showers', 22, .2] }[kind]
      const base = cf.base_m[j], top = cf.top_m[j]
      const fz = (T - 273.15) / .0065, tw = cold ? 273.2 : T - 3
      let type = 'none', phase = 'none', character = 'none', rate = 0, duration = 0, hailCm = 0, bolt = false, hazards = []
      if (rule) {
        [character, rate, duration] = rule
        phase = cold ? 'solid' : 'liquid'
        type = character === 'showers' ? (cold ? 'snow_showers' : 'rain_showers') : character === 'drizzle' || kind === 'stratocumulus' ? 'drizzle' : cold ? 'snow' : 'rain'
        if (kind === 'cumulonimbus') { bolt = true; hailCm = cf.surface_heating_k > 4 ? 2.5 : 0.8; hazards.push('thunderstorm', 'hail', 'heavy_rain') }
        if (character === 'continuous' && rate * duration * 4 >= 10) hazards.push('prolonged_rain')
        if (phase === 'solid') hazards.push('snow')
        if (type === 'drizzle') hazards.push('drizzle')
        precipitating++; if (bolt) lightning++; if (hailCm > 0) hail++; if (phase === 'solid') snow++
      }
      const accumulation = rate * duration * pconfig.interval_seconds / 3600
      total += accumulation; maxRate = Math.max(maxRate, rate)
      out.precipitation_type.push(type); out.phase.push(phase); out.character.push(character); out.intensity.push(intensityOf(rate)); out.rate_mm_h.push(rate); out.duration_fraction.push(duration); out.accumulation_mm.push(accumulation); out.hail_max_diameter_cm.push(hailCm); out.lightning.push(bolt); out.virga.push(false); out.updraft_m_s.push(kind === 'cumulonimbus' ? 25 : kind === 'towering_cumulus' ? 8 : rule ? (top - base) / 21600 : 0); out.freezing_level_m.push(fz > 0 ? fz : 0); out.surface_wet_bulb_k.push(tw); out.precipitable_water_mm.push(18); out.cloud_base_temperature_k.push(kind === 'none' ? null : T - .0065 * base); out.cloud_top_temperature_k.push(kind === 'none' ? null : T - .0065 * base - .006 * (top - base)); out.hazards.push(hazards)
    }
    return { elapsed_seconds: cf.elapsed_seconds, ...out, precipitating_cells: precipitating, lightning_cells: lightning, hail_cells: hail, snow_cells: snow, max_rate_mm_h: maxRate, total_accumulation_mm: total }
  })
  return {
    schema_version: 'terraflux.weather.timeline/v1', frames,
    coupled_atmosphere: {
      schema_version: 'terraflux.weather.coupled_atmosphere/v1', model: 'coupled_wind_moist_layer_v1', source_kind: 'synthetic', request,
      pressure_source: { kind: 'prescribed_cell_values' }, temperature_source: { kind: 'prescribed_cell_values' }, humidity_source: { kind: 'prescribed_cell_values' },
      pressure_pa: pressure, coriolis_parameter_s: coriolis, specific_humidity_kg_kg_by_frame: humidity, relative_humidity_pct_by_frame: rh, air_density_kg_m3_by_frame: density, supersaturated_cells_by_frame: supersaturated, scalar_stability_number_by_frame: stability,
      clouds: { schema_version: 'terraflux.weather.cloud_formation/v1', method: 'frontal_convergence_overrunning_heated_parcel_convection', source_kind: 'synthetic', config, frames: cloudFrames },
      precipitation: { schema_version: 'terraflux.weather.precipitation_outcomes/v1', method: 'genus_condensation_rate_wet_bulb_phase_updraft_hail', source_kind: 'synthetic', config: pconfig, frames: precipitationFrames },
    },
  }
}
