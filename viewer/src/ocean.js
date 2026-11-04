// Ocean circulation support: validation of the documented
// terraflux.ocean.circulation/v1 block (currents, thickness, salinity,
// temperature, deep-water formation, streams and river exchange points),
// styling for the ocean-streams view and per-frame metrics. No dynamics run in
// the browser: the viewer checks the engine output and composes layers from it.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const SCHEMA = 'terraflux.ocean.circulation/v1'
export const METHOD = 'reduced_gravity_c_grid_sverdrup_stommel_formation'

const FRAME_ARRAYS = ['u_m_s', 'v_m_s', 'thickness_m', 'salinity_psu', 'surface_salinity_psu', 'temperature_k', 'sinking_m_s', 'front', 'stream_id']
const FRAME_NUMBERS = ['overturning_sv', 'mean_upwelling_m_s', 'river_input_m3_s', 'net_precipitation_m3_s', 'sponge_volume_m3_s', 'sponge_salt_kg_s', 'volume_m3', 'salt_kg',
  'kinetic_energy_j', 'volume_balance_error_m3', 'salt_balance_error_kg', 'formation_area_km2', 'mean_salinity_psu', 'min_salinity_psu', 'max_speed_m_s']
const DIRECTIONS = ['northward', 'southward', 'eastward', 'westward']

export function validateOcean(block, frames, terrain) {
  const b = block, grid = frames[0].forecast.grid, count = grid.nx * grid.ny
  check(b?.schema_version === SCHEMA && b.method === METHOD, 'Unsupported ocean contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Ocean source must be engine_output or synthetic.')
  check(terrain && Array.isArray(terrain.values) && terrain.values.length === count, 'Ocean circulation requires the terrain grid.')
  check(b.request && b.request.physics && finite(b.request.physics.reference_salinity_psu) && finite(b.request.physics.mixed_layer_depth_m) && b.request.ocean && finite(b.request.ocean.initial_thickness_m), 'Invalid ocean request echo.')
  check(Array.isArray(b.ocean_mask) && b.ocean_mask.length === count && b.ocean_mask.every(v => typeof v === 'boolean') && b.ocean_mask.some(Boolean), 'Invalid ocean mask.')
  check(Array.isArray(b.sponge) && b.sponge.length === count && b.sponge.every((v, i) => finite(v) && v >= 0 && v <= 1 && (b.ocean_mask[i] || v === 0)), 'Invalid sponge strengths.')
  check(finite(b.initial_volume_m3) && b.initial_volume_m3 > 0 && finite(b.initial_salt_kg) && b.initial_salt_kg >= 0, 'Invalid initial ocean volume or salt.')
  check(Array.isArray(b.frames) && b.frames.length === frames.length, 'Ocean frame count mismatch.')
  const mask = b.ocean_mask, physics = b.request.physics
  let previous = null
  frames.forEach((frame, i) => {
    const f = b.frames[i]
    check(f && f.elapsed_seconds === frame.forecast.elapsed_seconds, 'Ocean frame times must match the timeline.')
    check(FRAME_ARRAYS.every(k => Array.isArray(f[k]) && f[k].length === count), 'Ocean arrays do not match the grid.')
    check(FRAME_NUMBERS.every(k => finite(f[k])), 'Invalid ocean frame numbers.')
    check(f.cumulative_m3 && ['river', 'net_precipitation', 'sinking', 'sponge'].every(k => finite(f.cumulative_m3[k])), 'Invalid cumulative ocean totals.')
    check(Number.isInteger(f.formation_cells) && f.formation_cells >= 0 && Number.isInteger(f.stream_count) && f.stream_count >= 0, 'Invalid ocean counters.')
    let formation = 0, maxSpeed = 0, streamCells = 0
    const speed = new Array(count).fill(0)
    for (let j = 0; j < count; j++) {
      const water = mask[j]
      check(finite(f.u_m_s[j]) && finite(f.v_m_s[j]) && Math.abs(f.u_m_s[j]) <= 20 && Math.abs(f.v_m_s[j]) <= 20, 'Invalid current components.')
      check(finite(f.thickness_m[j]) && f.thickness_m[j] >= 0 && finite(f.salinity_psu[j]) && f.salinity_psu[j] >= 0 && f.salinity_psu[j] <= 45, 'Invalid thickness or salinity.')
      check(finite(f.surface_salinity_psu[j]) && f.surface_salinity_psu[j] >= 0 && f.surface_salinity_psu[j] <= f.salinity_psu[j] + 1e-6, 'Surface salinity cannot exceed the bulk salinity.')
      check(finite(f.temperature_k[j]) && (!water || (f.temperature_k[j] > 250 && f.temperature_k[j] < 330)), 'Invalid ocean temperature.')
      check(finite(f.sinking_m_s[j]) && f.sinking_m_s[j] >= 0 && typeof f.front[j] === 'boolean' && Number.isInteger(f.stream_id[j]) && f.stream_id[j] >= -1, 'Invalid sinking, front or stream id.')
      if (!water) check(f.u_m_s[j] === 0 && f.v_m_s[j] === 0 && f.thickness_m[j] === 0 && f.sinking_m_s[j] === 0 && f.stream_id[j] === -1 && !f.front[j], 'Land cells carry no ocean state.')
      speed[j] = Math.hypot(f.u_m_s[j], f.v_m_s[j])
      if (f.stream_id[j] >= 0) check(water && speed[j] >= physics.stream_speed_threshold_m_s * 0.999, 'Stream cells must be water flowing above the threshold.')
      if (f.sinking_m_s[j] > 0) formation++
      if (f.stream_id[j] >= 0) streamCells++
      maxSpeed = Math.max(maxSpeed, speed[j])
    }
    check(f.formation_cells === formation, 'Formation cell count disagrees with the sinking array.')
    check(Math.abs(f.max_speed_m_s - maxSpeed) <= 1e-3 * Math.max(1, maxSpeed), 'Peak speed disagrees with the arrays.')
    check(Math.abs(f.overturning_sv * 1e6 - f.sinking_m_s.reduce((a, v) => a + v, 0) * grid.spacing_east_m * grid.spacing_north_m) <= 1e-3 * Math.max(1e6, f.overturning_sv * 1e6), 'Overturning disagrees with the sinking array.')
    check(Array.isArray(f.streams) && f.streams.length === f.stream_count && f.streams.every(s => s && Number.isInteger(s.id) && Number.isInteger(s.core_cell) && s.core_cell >= 0 && s.core_cell < count && mask[s.core_cell] && Number.isInteger(s.cell_count) && s.cell_count >= 2 && finite(s.max_speed_m_s) && finite(s.mean_speed_m_s) && s.mean_speed_m_s <= s.max_speed_m_s + 1e-6 && finite(s.transport_sv) && finite(s.mean_salinity_psu) && finite(s.mean_temperature_k) && DIRECTIONS.includes(s.direction) && Array.isArray(s.path) && s.path.length >= 1 && s.path.every(p => Array.isArray(p) && p.length === 2 && p.every(v => finite(v) && v >= 0 && v <= 1))), 'Invalid stream descriptions.')
    check(f.streams.every((s, k) => k === 0 || s.transport_sv <= f.streams[k - 1].transport_sv + 1e-9), 'Streams are sorted by transport.')
    check(f.streams.every(s => f.stream_id[s.core_cell] === s.id), 'Stream cores must lie in their stream.')
    check(Array.isArray(f.exchange_points) && f.exchange_points.every(p => p && typeof p.name === 'string' && Number.isInteger(p.mouth_cell) && p.mouth_cell >= 0 && p.mouth_cell < count && mask[p.mouth_cell] && finite(p.discharge_m3_s) && p.discharge_m3_s >= 0 && finite(p.mouth_salinity_psu) && finite(p.minimum_salinity_psu) && p.minimum_salinity_psu <= p.mouth_salinity_psu + 1e-6 && finite(p.plume_area_km2) && p.plume_area_km2 >= 0 && finite(p.plume_freshwater_m3) && p.plume_freshwater_m3 >= 0 && finite(p.plume_extent_km) && p.plume_extent_km >= 0 && finite(p.deflection_deg) && p.deflection_deg >= 0 && p.deflection_deg < 360), 'Invalid exchange points.')
    // Budgets: volume and salt change by the explicit sources only.
    const c = f.cumulative_m3
    check(Math.abs(f.volume_m3 - b.initial_volume_m3 - c.river - c.net_precipitation - c.sponge) <= 1e-6 * Math.max(b.initial_volume_m3, 1), 'Ocean volume budget: rivers + net precipitation + sponge must explain the change.')
    check(Math.abs(f.volume_balance_error_m3) <= 1e-6 * Math.max(b.initial_volume_m3, 1) && Math.abs(f.salt_balance_error_kg) <= 1e-6 * Math.max(b.initial_salt_kg, 1), 'Per-step ocean budget errors too large.')
    if (previous) check(c.river + 1e-6 >= previous.cumulative_m3.river && c.sinking + 1e-6 >= previous.cumulative_m3.sinking, 'Cumulative river input and sinking cannot decrease.')
    else check(c.river === 0 && c.sinking === 0 && c.net_precipitation === 0 && c.sponge === 0, 'The first frame is the initial state.')
    const reference = physics.reference_salinity_psu
    frame.ocean = {
      u: f.u_m_s, v: f.v_m_s, speed, thickness: f.thickness_m, salinity: f.salinity_psu, surfaceSalinity: f.surface_salinity_psu, temperature: f.temperature_k,
      sinking: f.sinking_m_s, front: f.front, streamId: f.stream_id, freshwater: f.surface_salinity_psu.map((s, j) => mask[j] ? Math.max(0, (reference - s) / reference) : 0),
      streams: f.streams, exchangePoints: f.exchange_points, overturning: f.overturning_sv, upwelling: f.mean_upwelling_m_s, riverInput: f.river_input_m3_s,
      netPrecipitation: f.net_precipitation_m3_s, sponge: f.sponge_volume_m3_s, volume: f.volume_m3, salt: f.salt_kg, kineticEnergy: f.kinetic_energy_j,
      cumulative: c, formationCells: f.formation_cells, formationArea: f.formation_area_km2, meanSalinity: f.mean_salinity_psu, minSalinity: f.min_salinity_psu,
      maxSpeed: f.max_speed_m_s, streamCells, mask, spongeStrength: b.sponge, config: b.request, forcing: b.forcing_source, synthetic: b.source_kind === 'synthetic',
    }
    previous = f
  })
  return b
}

export function summarizeOcean(frame) {
  const o = frame.ocean, n = o.u.length
  let water = 0, fronts = 0, fresh = 0, salty = 0
  for (let i = 0; i < n; i++) { if (!o.mask[i]) continue; water++; if (o.front[i]) fronts++; if (o.freshwater[i] > 0.01) fresh++; if (o.surfaceSalinity[i] > o.config.physics.reference_salinity_psu + 0.3) salty++ }
  const main = o.streams[0] ?? null
  return {
    overturning_sv: o.overturning,
    formation_cells: o.formationCells,
    formation_area_km2: o.formationArea,
    max_speed_m_s: o.maxSpeed,
    stream_count: o.streams.length,
    main_stream: main ? { direction: main.direction, transport_sv: main.transport_sv, max_speed_m_s: main.max_speed_m_s, cells: main.cell_count, label: streamLabel(main, frame.forecast.grid) } : null,
    river_input_m3_s: o.riverInput,
    river_input_sv: o.riverInput / 1e6,
    net_precipitation_m3_s: o.netPrecipitation,
    mean_salinity_psu: o.meanSalinity,
    min_surface_salinity_psu: o.minSalinity,
    fresh_pct: water ? fresh / water * 100 : 0,
    salty_pct: water ? salty / water * 100 : 0,
    front_cells: fronts,
    plume_area_km2: o.exchangePoints.reduce((a, p) => a + p.plume_area_km2, 0),
    plume_freshwater_km3: o.exchangePoints.reduce((a, p) => a + p.plume_freshwater_m3, 0) / 1e9,
    kinetic_energy_pj: o.kineticEnergy / 1e15,
    cumulative_river_km3: o.cumulative.river / 1e9,
    cumulative_sinking_km3: o.cumulative.sinking / 1e9,
  }
}

// A readable name for a stream from where it runs and which way it flows.
export function streamLabel(stream, grid) {
  const x = (stream.core_cell % grid.nx + 0.5) / grid.nx, y = (Math.floor(stream.core_cell / grid.nx) + 0.5) / grid.ny
  const side = x < 0.25 ? 'western boundary' : x > 0.75 ? 'eastern boundary' : y < 0.2 ? 'southern' : y > 0.8 ? 'northern' : 'interior'
  const kind = side.includes('boundary') ? 'current' : 'jet'
  return `${side} ${kind} (${stream.direction}, ${stream.transport_sv.toFixed(1)} Sv)`
}

// Speed colour: dark blue (slow) -> cyan -> yellow -> red (fast), saturating at 1 m/s.
export function speedColor(speed) {
  const w = Math.min(1, speed)
  const stops = [[0, [20, 40, 90]], [0.15, [30, 120, 200]], [0.35, [80, 220, 220]], [0.6, [250, 220, 80]], [1.0, [230, 50, 40]]]
  let a = stops[0], b = stops[stops.length - 1]
  for (let i = 0; i + 1 < stops.length; i++) if (w >= stops[i][0] && w <= stops[i + 1][0]) { a = stops[i]; b = stops[i + 1]; break }
  const t = (w - a[0]) / ((b[0] - a[0]) || 1)
  const c = a[1].map((v, i) => Math.round(v + (b[1][i] - v) * t))
  return `rgb(${c[0]},${c[1]},${c[2]})`
}

// Salinity colour: fresh (green-white) -> reference (deep blue) -> salty (purple), over +-3 psu.
export function salinityColor(salinity, reference = 35) {
  const d = Math.max(-3, Math.min(3, salinity - reference))
  if (d < 0) { const t = -d / 3; return `rgb(${Math.round(20 + 180 * t)},${Math.round(70 + 170 * t)},${Math.round(140 + 60 * t)})` }
  const t = d / 3
  return `rgb(${Math.round(20 + 110 * t)},${Math.round(70 - 40 * t)},${Math.round(140 + 40 * t)})`
}

export function temperatureColor(kelvin) {
  const t = Math.max(0, Math.min(1, (kelvin - 275) / 22))
  return `hsl(${Math.round(230 - 230 * t)} 70% ${Math.round(30 + 20 * t)}%)`
}

// Time series of the overturning, main-stream transport and river input over the frames.
export function circulationSeries(frames) {
  return frames.map(f => ({ elapsed_seconds: f.forecast.elapsed_seconds, overturning_sv: f.ocean ? f.ocean.overturning : 0, stream_sv: f.ocean && f.ocean.streams[0] ? f.ocean.streams[0].transport_sv : 0, river_sv: f.ocean ? f.ocean.riverInput / 1e6 : 0, min_salinity_psu: f.ocean ? f.ocean.minSalinity : 0 }))
}
