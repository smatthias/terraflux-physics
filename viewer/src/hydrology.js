// Catchment hydrology support: validation of the documented
// terraflux.hydrology.catchment/v1 block (drainage network, water stores,
// discharge and the hazard layer), styling for the catchment and hazard views
// and per-frame metrics. No hydrology runs in the browser: the viewer only
// checks the engine output against its contract and composes layers from it.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const SCHEMA = 'terraflux.hydrology.catchment/v1'
export const METHOD = 'priority_flood_d8_kinematic_cascade_darcy_infinite_slope_stream_power'

export const FLOOD_CLASSES = [
  { key: 'none', label: 'Dry / below threshold', color: null },
  { key: 'low', label: 'Low (< 0.3 m, d·v < 0.3)', color: '#7fc8ff' },
  { key: 'medium', label: 'Medium (< 1 m, d·v < 0.6)', color: '#ffd166' },
  { key: 'high', label: 'High (< 2 m, d·v < 1.5)', color: '#ff7b4a' },
  { key: 'extreme', label: 'Extreme (> 2 m or d·v > 1.5)', color: '#d7263d' },
]
export const floodClassInfo = index => FLOOD_CLASSES[index] ?? FLOOD_CLASSES[0]

const BASIN_PALETTE = ['#3b82f6', '#22c55e', '#f59e0b', '#a855f7', '#ef4444', '#14b8a6', '#eab308', '#ec4899', '#84cc16', '#06b6d4', '#f97316', '#8b5cf6']
export const basinColor = id => id === null || id < 0 ? '#10324a' : BASIN_PALETTE[id % BASIN_PALETTE.length]

const NETWORK_ARRAYS = ['downstream', 'flow_direction_deg', 'terminal', 'ocean', 'upstream_area_km2', 'strahler_order', 'channel', 'lake_id', 'basin_id',
  'distance_to_outlet_km', 'slope', 'channel_width_m', 'bankfull_depth_m', 'regolith_depth_m', 'channel_target', 'filled_depth_m']
const FRAME_ARRAYS = ['rainfall_rate_mm_h', 'snow_mm', 'surface_water_mm', 'soil_saturation', 'water_table_depth_m', 'discharge_m3_s', 'channel_depth_m',
  'flood_class', 'erosion_mm', 'deposition_mm', 'sediment_flux_kg_s', 'factor_of_safety', 'landslide', 'unstable', 'rainfall_threshold_exceeded']
const CUMULATIVE_KEYS = ['precipitation', 'evapotranspiration', 'ocean_discharge', 'edge_outflow', 'infiltration', 'percolation', 'baseflow', 'snowmelt']
const STORAGE_KEYS = ['snow', 'surface', 'soil', 'groundwater', 'channels', 'lakes', 'total']

export function validateNetwork(network, count) {
  const n = network
  check(n && NETWORK_ARRAYS.every(k => Array.isArray(n[k]) && n[k].length === count), 'Hydrology network arrays do not match the grid.')
  check(Array.isArray(n.lakes) && Array.isArray(n.basins), 'Hydrology network needs lake and basin lists.')
  for (let i = 0; i < count; i++) {
    const down = n.downstream[i], ocean = n.ocean[i]
    check(typeof ocean === 'boolean' && Number.isInteger(down) && down >= -1 && down < count && down !== i, 'Invalid downstream receiver.')
    check(['none', 'ocean', 'edge'].includes(n.terminal[i]) && typeof n.channel[i] === 'boolean', 'Invalid terminal kind or channel flag.')
    check(n.flow_direction_deg[i] === null || (Number.isInteger(n.flow_direction_deg[i]) && n.flow_direction_deg[i] % 45 === 0 && n.flow_direction_deg[i] >= 0 && n.flow_direction_deg[i] < 360), 'Invalid D8 direction.')
    check(finite(n.upstream_area_km2[i]) && n.upstream_area_km2[i] >= 0 && Number.isInteger(n.strahler_order[i]) && n.strahler_order[i] >= 0, 'Invalid upstream area or stream order.')
    check(Number.isInteger(n.lake_id[i]) && n.lake_id[i] >= -1 && n.lake_id[i] < n.lakes.length && Number.isInteger(n.basin_id[i]) && n.basin_id[i] >= -1 && n.basin_id[i] < n.basins.length, 'Invalid lake or basin id.')
    check(finite(n.distance_to_outlet_km[i]) && n.distance_to_outlet_km[i] >= 0 && finite(n.slope[i]) && n.slope[i] >= 0, 'Invalid outlet distance or slope.')
    check(finite(n.channel_width_m[i]) && n.channel_width_m[i] >= 0 && finite(n.bankfull_depth_m[i]) && n.bankfull_depth_m[i] >= 0 && (n.channel[i] ? n.channel_width_m[i] > 0 : n.channel_width_m[i] === 0), 'Channel geometry must exist on channel cells only.')
    check(finite(n.regolith_depth_m[i]) && n.regolith_depth_m[i] >= 0 && Number.isInteger(n.channel_target[i]) && n.channel_target[i] >= -1 && n.channel_target[i] < count, 'Invalid regolith depth or channel target.')
    check(finite(n.filled_depth_m[i]) && n.filled_depth_m[i] >= -1e-9, 'Invalid filled depth.')
    if (ocean) check(down === -1 && n.basin_id[i] === -1 && !n.channel[i] && n.flow_direction_deg[i] === null, 'Ocean cells are not routed.')
    else {
      check(n.basin_id[i] >= 0 && n.strahler_order[i] >= 1 && n.upstream_area_km2[i] > 0, 'Land cells belong to a basin and carry area.')
      if (down < 0) check(n.terminal[i] === 'edge', 'A land cell without receiver must leave across the edge.')
      else check(n.terminal[i] === (n.ocean[down] ? 'ocean' : 'none'), 'Terminal kind must match the receiver.')
    }
  }
  n.lakes.forEach((l, k) => check(l && l.id === k && Number.isInteger(l.cell_count) && l.cell_count >= 1 && finite(l.area_km2) && finite(l.capacity_m3) && l.capacity_m3 >= 0 && finite(l.spill_elevation_m) && Number.isInteger(l.outlet_cell) && l.outlet_cell >= -1 && l.outlet_cell < count && ['land', 'ocean', 'edge', 'none'].includes(l.outlet_kind), 'Invalid lake description.'))
  n.basins.forEach((b, k) => check(b && b.id === k && Number.isInteger(b.outlet_cell) && b.outlet_cell >= 0 && b.outlet_cell < count && finite(b.area_km2) && b.area_km2 > 0 && ['ocean', 'edge'].includes(b.terminal) && (k === 0 || b.area_km2 <= n.basins[k - 1].area_km2 + 1e-9), 'Invalid basin description (basins are sorted by area).'))
  return n
}

export function validateHydrology(block, frames, terrain) {
  const b = block, grid = frames[0].forecast.grid, count = grid.nx * grid.ny
  check(b?.schema_version === SCHEMA && b.method === METHOD, 'Unsupported hydrology contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Hydrology source must be engine_output or synthetic.')
  check(terrain && Array.isArray(terrain.values) && terrain.values.length === count, 'Hydrology requires the terrain grid.')
  check(b.request && b.request.time && finite(b.request.time.timestep_seconds) && b.request.time.timestep_seconds > 0 && b.request.soil && b.request.groundwater && b.request.channels && b.request.hazards && b.request.domain, 'Invalid hydrology request echo.')
  check(finite(b.initial_storage_m3) && b.initial_storage_m3 >= 0, 'Invalid initial storage.')
  const network = validateNetwork(b.network, count)
  check(Array.isArray(b.frames) && b.frames.length === frames.length, 'Hydrology frame count mismatch.')
  check(Array.isArray(b.events) && b.events.every(e => e && e.kind === 'landslide' && finite(e.elapsed_seconds) && Number.isInteger(e.cell) && e.cell >= 0 && e.cell < count && Number.isInteger(e.runout_cell) && e.runout_cell >= -1 && e.runout_cell < count && finite(e.volume_m3) && e.volume_m3 > 0 && finite(e.factor_of_safety) && e.factor_of_safety < 1 && ['slope', 'channel', 'lake', 'ocean', 'edge'].includes(e.deposited_in)), 'Invalid landslide events.')
  const area = grid.spacing_east_m * grid.spacing_north_m
  let previous = null
  frames.forEach((frame, i) => {
    const f = b.frames[i]
    check(f && f.elapsed_seconds === frame.forecast.elapsed_seconds, 'Hydrology frame times must match the timeline.')
    check(FRAME_ARRAYS.every(k => Array.isArray(f[k]) && f[k].length === count), 'Hydrology arrays do not match the grid.')
    check(Array.isArray(f.lake_level_m) && f.lake_level_m.length === network.lakes.length && Array.isArray(f.lake_volume_m3) && f.lake_volume_m3.length === network.lakes.length, 'Lake arrays do not match the lake list.')
    check(f.storage_m3 && STORAGE_KEYS.every(k => finite(f.storage_m3[k]) && f.storage_m3[k] >= -1e-6) && f.cumulative_m3 && CUMULATIVE_KEYS.every(k => finite(f.cumulative_m3[k]) && f.cumulative_m3[k] >= -1e-6), 'Invalid storage or cumulative totals.')
    check(finite(f.balance_error_m3) && finite(f.sediment_yield_kg) && f.sediment_yield_kg >= 0 && Number.isInteger(f.landslide_count) && finite(f.landslide_volume_m3) && finite(f.flooded_area_km2) && finite(f.max_discharge_m3_s) && Number.isInteger(f.peak_discharge_cell), 'Invalid frame totals.')
    const counts = { none: 0, low: 0, medium: 0, high: 0, extreme: 0 }
    let slides = 0, thresholds = 0, unstable = 0, flooded = 0, maxQ = 0
    for (let j = 0; j < count; j++) {
      check(finite(f.rainfall_rate_mm_h[j]) && f.rainfall_rate_mm_h[j] >= 0 && finite(f.snow_mm[j]) && f.snow_mm[j] >= 0 && finite(f.surface_water_mm[j]) && f.surface_water_mm[j] >= -1e-9, 'Invalid rain, snow or surface water.')
      check(finite(f.soil_saturation[j]) && f.soil_saturation[j] >= 0 && f.soil_saturation[j] <= 1 + 1e-9 && finite(f.water_table_depth_m[j]) && f.water_table_depth_m[j] >= 0, 'Invalid soil saturation or water table.')
      check(finite(f.discharge_m3_s[j]) && f.discharge_m3_s[j] >= 0 && (network.channel[j] || f.discharge_m3_s[j] === 0) && finite(f.channel_depth_m[j]) && f.channel_depth_m[j] >= 0, 'Discharge exists on channel cells only.')
      check(Number.isInteger(f.flood_class[j]) && f.flood_class[j] >= 0 && f.flood_class[j] <= 4 && (!network.ocean[j] || f.flood_class[j] === 0), 'Invalid flood class.')
      check(finite(f.erosion_mm[j]) && f.erosion_mm[j] >= 0 && finite(f.deposition_mm[j]) && f.deposition_mm[j] >= 0 && finite(f.sediment_flux_kg_s[j]) && f.sediment_flux_kg_s[j] >= 0, 'Invalid erosion, deposition or sediment flux.')
      if (previous) check(f.erosion_mm[j] + 1e-9 >= previous.erosion_mm[j] && f.deposition_mm[j] + 1e-9 >= previous.deposition_mm[j], 'Cumulative erosion and deposition cannot decrease.')
      check(f.factor_of_safety[j] === null || (finite(f.factor_of_safety[j]) && f.factor_of_safety[j] > 0), 'Invalid factor of safety.')
      check(typeof f.landslide[j] === 'boolean' && typeof f.unstable[j] === 'boolean' && typeof f.rainfall_threshold_exceeded[j] === 'boolean', 'Hazard flags must be boolean.')
      if (f.landslide[j]) check(f.factor_of_safety[j] !== null, 'A landslide needs a defined factor of safety.')
      counts[FLOOD_CLASSES[f.flood_class[j]].key]++
      if (f.flood_class[j] > 0) flooded += area
      slides += f.landslide[j] ? 1 : 0; thresholds += f.rainfall_threshold_exceeded[j] ? 1 : 0; unstable += f.unstable[j] ? 1 : 0
      maxQ = Math.max(maxQ, f.discharge_m3_s[j])
    }
    check(['none', 'low', 'medium', 'high', 'extreme'].every(k => f.flood_class_cells[k] === counts[k]) && f.landslide_cells === slides && f.threshold_cells === thresholds && f.unstable_cells === unstable, 'Frame counts disagree with the arrays.')
    check(Math.abs(f.flooded_area_km2 - flooded / 1e6) <= 1e-6 && Math.abs(f.max_discharge_m3_s - maxQ) <= 1e-4 * Math.max(1, maxQ), 'Flooded area or peak discharge disagrees with the arrays.')
    f.lake_level_m.forEach((level, k) => check(finite(level) && finite(f.lake_volume_m3[k]) && f.lake_volume_m3[k] >= -1e-6 && f.lake_volume_m3[k] <= network.lakes[k].capacity_m3 * (1 + 1e-6) + 1e-6 && level <= network.lakes[k].spill_elevation_m + 1e-6, 'Lakes cannot exceed their capacity or spill level.'))
    const s = f.storage_m3, c = f.cumulative_m3
    check(Math.abs(s.snow + s.surface + s.soil + s.groundwater + s.channels + s.lakes - s.total) <= 1e-6 * Math.max(1, s.total), 'Storage components do not add up.')
    const scale = Math.max(1, b.initial_storage_m3, c.precipitation)
    check(Math.abs(s.total + c.evapotranspiration + c.ocean_discharge + c.edge_outflow - c.precipitation - b.initial_storage_m3) <= 1e-6 * scale, 'Water budget: stores + ET + outflow must equal initial storage + precipitation.')
    check(Math.abs(f.balance_error_m3) <= 1e-6 * scale, 'Per-step balance error too large.')
    if (previous) check(CUMULATIVE_KEYS.every(k => c[k] + 1e-9 >= previous.cumulative_m3[k]) && f.sediment_yield_kg + 1e-9 >= previous.sediment_yield_kg && f.landslide_count >= previous.landslide_count, 'Cumulative totals cannot decrease.')
    else check(c.precipitation === 0 && f.landslide_count === 0, 'The first frame is the initial state.')
    const lakeDepth = new Array(count).fill(0)
    network.lakes.forEach((l, k) => { for (let j = 0; j < count; j++) if (network.lake_id[j] === k) lakeDepth[j] = Math.max(0, f.lake_level_m[k] - terrain.values[j]) })
    frame.hydrology = {
      rain: f.rainfall_rate_mm_h, snow: f.snow_mm, surface: f.surface_water_mm, saturation: f.soil_saturation, waterTable: f.water_table_depth_m,
      discharge: f.discharge_m3_s, channelDepth: f.channel_depth_m, floodClass: f.flood_class, erosion: f.erosion_mm, deposition: f.deposition_mm,
      sediment: f.sediment_flux_kg_s, fs: f.factor_of_safety, landslide: f.landslide, unstable: f.unstable, threshold: f.rainfall_threshold_exceeded,
      lakeLevel: f.lake_level_m, lakeVolume: f.lake_volume_m3, lakeDepth, storage: s, cumulative: c, balance: f.balance_error_m3,
      sedimentYield: f.sediment_yield_kg, landslideCount: f.landslide_count, landslideVolume: f.landslide_volume_m3, landslideCells: slides, unstableCells: unstable,
      thresholdCells: thresholds, floodedArea: f.flooded_area_km2, classCells: f.flood_class_cells, maxDischarge: f.max_discharge_m3_s, peakCell: f.peak_discharge_cell,
      network, events: b.events, config: b.request, synthetic: b.source_kind === 'synthetic',
    }
    previous = f
  })
  return b
}

export function summarizeHydrology(frame) {
  const h = frame.hydrology, n = h.network, count = h.rain.length
  let saturation = 0, land = 0, rainCells = 0, wet = 0
  for (let i = 0; i < count; i++) { if (n.ocean[i]) continue; land++; saturation += h.saturation[i]; if (h.rain[i] > 0.05) rainCells++; if (h.waterTable[i] < 2) wet++ }
  const outflow = h.cumulative.ocean_discharge + h.cumulative.edge_outflow
  return {
    max_discharge_m3_s: h.maxDischarge,
    peak_cell: h.peakCell,
    flooded_area_km2: h.floodedArea,
    class_cells: h.classCells,
    landslides_total: h.landslideCount,
    landslides_now: h.landslideCells,
    unstable_cells: h.unstableCells,
    threshold_cells: h.thresholdCells,
    sediment_yield_t: h.sedimentYield / 1000,
    landslide_volume_m3: h.landslideVolume,
    mean_saturation: land ? saturation / land : 0,
    raining_pct: land ? rainCells / land * 100 : 0,
    saturated_area_pct: land ? wet / land * 100 : 0,
    runoff_ratio: h.cumulative.precipitation > 0 ? outflow / h.cumulative.precipitation : null,
    storage: h.storage,
    cumulative: h.cumulative,
    lakes: n.lakes.length,
    basins: n.basins.length,
    channel_km: n.channel.reduce((a, v, i) => a + (v ? (n.downstream[i] >= 0 ? 1 : 0) : 0), 0),
    max_erosion_mm: Math.max(0, ...h.erosion),
    max_deposition_mm: Math.max(0, ...h.deposition),
    min_factor_of_safety: h.fs.reduce((m, v) => v === null ? m : Math.min(m, v), Infinity),
  }
}

// Channel segments from each channel cell centre to its receiver, in grid fractions (0-1).
export function riverSegments(network, grid) {
  const out = []
  for (let i = 0; i < network.channel.length; i++) {
    if (!network.channel[i]) continue
    const down = network.downstream[i]
    const x0 = (i % grid.nx + 0.5) / grid.nx, y0 = (Math.floor(i / grid.nx) + 0.5) / grid.ny
    let x1 = x0, y1 = y0
    if (down >= 0) { x1 = (down % grid.nx + 0.5) / grid.nx; y1 = (Math.floor(down / grid.nx) + 0.5) / grid.ny }
    else { const d = network.flow_direction_deg[i]; if (d !== null) { x1 = x0 + Math.cos(d * Math.PI / 180) / grid.nx; y1 = y0 + Math.sin(d * Math.PI / 180) / grid.ny } }
    out.push({ cell: i, to: down, from: [x0, y0], end: [x1, y1], order: network.strahler_order[i], area_km2: network.upstream_area_km2[i] })
  }
  return out
}

// Follow the water from a cell to where it leaves the domain.
export function waterPath(network, cell) {
  const cells = [cell], lakes = new Set()
  let current = cell, guard = 0
  if (network.ocean[cell]) return { cells, lakes: [], ends: 'ocean', distance_km: 0, basin: null }
  while (guard++ < network.downstream.length) {
    if (network.lake_id[current] >= 0) lakes.add(network.lake_id[current])
    const next = network.downstream[current]
    if (next < 0) return { cells, lakes: [...lakes], ends: network.terminal[current] === 'edge' ? 'edge' : 'sink', distance_km: network.distance_to_outlet_km[cell], basin: network.basin_id[cell] }
    cells.push(next)
    if (network.ocean[next]) return { cells, lakes: [...lakes], ends: 'ocean', distance_km: network.distance_to_outlet_km[cell], basin: network.basin_id[cell] }
    current = next
  }
  throw new Error('Water path does not terminate.')
}

export function hydrographSeries(frames, cell) {
  return frames.map(f => ({ elapsed_seconds: f.forecast.elapsed_seconds, discharge_m3_s: f.hydrology ? f.hydrology.discharge[cell] : 0, rain_mm_h: f.hydrology ? f.hydrology.rain[cell] : 0 }))
}

// Colour for a water depth in millimetres (overland / flood water).
export function waterColor(mm) {
  if (mm < 1) return null
  const w = Math.min(1, Math.log10(mm) / 3)
  return `rgb(${Math.round(150 - 120 * w)},${Math.round(205 - 120 * w)},${Math.round(255 - 60 * w)})`
}

// Line width factor for a river with the given upstream area (km²).
export const riverWidth = areaKm2 => Math.min(1, 0.15 + 0.3 * Math.log10(Math.max(1, areaKm2)))

// Hazard colour: erosion (red) or deposition (tan) intensity in millimetres.
export function sedimentColor(erosionMm, depositionMm) {
  if (erosionMm < 0.05 && depositionMm < 0.05) return null
  if (erosionMm >= depositionMm) return { color: '#ff5c5c', weight: Math.min(0.85, 0.25 + Math.log10(1 + erosionMm) / 2) }
  return { color: '#d9b36b', weight: Math.min(0.85, 0.25 + Math.log10(1 + depositionMm) / 2) }
}
