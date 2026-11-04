// Orography support for the weather map: validation of the documented
// terraflux.weather.orography/v1 block, styling of mountain effects and
// per-frame metrics. Terrain geometry itself uses the terrain-grid contract.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

const FIELDS = ['upslope_lifting_m_s', 'obstacle_height_m', 'froude_number', 'blocked_fraction', 'valley_depth_m', 'cross_barrier_wind_m_s',
  'along_barrier_wind_m_s', 'ridge_orientation_deg', 'surface_pressure_pa', 'cold_pool_k', 'cold_pool', 'foehn', 'foehn_warming_k',
  'orographic_precipitation_mm', 'orographic_precipitation_rate_mm_h', 'cumulative_orographic_precipitation_mm']
const CONFIG_KEYS = ['blocking_time_scale_s', 'obstacle_radius_m', 'influence_radius_m', 'brunt_vaisala_s', 'boundary_layer_depth_m',
  'daytime_heating_k_per_s', 'nocturnal_cooling_k_per_s', 'diurnal_phase_seconds', 'valley_pooling_per_km', 'initial_lapse_rate_k_m']

export function validateOrography(block, frames, terrain) {
  const b = block, count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  check(b?.schema_version === 'terraflux.weather.orography/v1' && b.method === 'terrain_following_adiabats_froude_blocking_valley_pooling', 'Unsupported orography contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Orography source must be engine_output or synthetic.')
  check(terrain && Array.isArray(terrain.values) && terrain.values.length === count, 'Orography requires the terrain grid.')
  check(b.config && CONFIG_KEYS.every(k => finite(b.config[k])) && b.config.blocking_time_scale_s > 0 && b.config.brunt_vaisala_s > 0 && b.config.terrain?.kind, 'Invalid orography configuration.')
  check(Array.isArray(b.frames) && b.frames.length === frames.length, 'Orography frame count mismatch.')
  frames.forEach((frame, i) => {
    const f = b.frames[i]
    check(f && f.elapsed_seconds === frame.forecast.elapsed_seconds, 'Orography frame times must match the timeline.')
    check(FIELDS.every(k => Array.isArray(f[k]) && f[k].length === count), 'Orography arrays do not match the grid.')
    let pools = 0, foehns = 0
    for (let j = 0; j < count; j++) {
      check(finite(f.upslope_lifting_m_s[j]) && finite(f.obstacle_height_m[j]) && f.obstacle_height_m[j] >= 0 && finite(f.blocked_fraction[j]) && f.blocked_fraction[j] >= 0 && f.blocked_fraction[j] <= 1, 'Invalid lifting, obstacle or blocking values.')
      check(f.froude_number[j] === null || (finite(f.froude_number[j]) && f.froude_number[j] >= 0), 'Invalid Froude number.')
      check(f.ridge_orientation_deg[j] === null || (finite(f.ridge_orientation_deg[j]) && f.ridge_orientation_deg[j] >= 0 && f.ridge_orientation_deg[j] < 180), 'Invalid ridge orientation.')
      check(finite(f.valley_depth_m[j]) && f.valley_depth_m[j] >= 0 && finite(f.cold_pool_k[j]) && f.cold_pool_k[j] >= 0 && finite(f.surface_pressure_pa[j]) && f.surface_pressure_pa[j] > 0, 'Invalid valley, cold-pool or pressure values.')
      check(typeof f.cold_pool[j] === 'boolean' && typeof f.foehn[j] === 'boolean' && finite(f.foehn_warming_k[j]) && f.foehn_warming_k[j] >= 0 && (f.foehn_warming_k[j] > 0) === f.foehn[j], 'Cold-pool and foehn flags must be consistent.')
      check(finite(f.orographic_precipitation_rate_mm_h[j]) && f.orographic_precipitation_rate_mm_h[j] >= -1e-9 && finite(f.cumulative_orographic_precipitation_mm[j]) && f.cumulative_orographic_precipitation_mm[j] >= -1e-9, 'Invalid orographic precipitation.')
      pools += f.cold_pool[j] ? 1 : 0; foehns += f.foehn[j] ? 1 : 0
    }
    check(f.cold_pool_cells === pools && f.foehn_cells === foehns && finite(f.surface_tendency_k_s), 'Orography frame totals disagree with the arrays.')
    frame.orography = { lifting: f.upslope_lifting_m_s, obstacle: f.obstacle_height_m, froude: f.froude_number, blocked: f.blocked_fraction, valley: f.valley_depth_m, cross: f.cross_barrier_wind_m_s, along: f.along_barrier_wind_m_s, orientation: f.ridge_orientation_deg, surfacePressure: f.surface_pressure_pa, coldPool: f.cold_pool_k, coldPoolFlag: f.cold_pool, foehn: f.foehn, foehnWarming: f.foehn_warming_k, precipRate: f.orographic_precipitation_rate_mm_h, precip: f.orographic_precipitation_mm, cumulative: f.cumulative_orographic_precipitation_mm, poolCells: pools, foehnCells: foehns, tendency: f.surface_tendency_k_s, config: b.config, synthetic: b.source_kind === 'synthetic' }
  })
  return b
}

export function summarizeOrography(frame) {
  const o = frame.orography, n = o.lifting.length
  const up = o.lifting.filter(w => w > 0.02).length, down = o.lifting.filter(w => w < -0.02).length
  return {
    foehn_cells: o.foehnCells,
    max_foehn_warming_k: Math.max(0, ...o.foehnWarming),
    cold_pool_cells: o.poolCells,
    max_cold_pool_k: Math.max(0, ...o.coldPool),
    rising_pct: up / n * 100,
    sinking_pct: down / n * 100,
    max_blocked_fraction: Math.max(0, ...o.blocked),
    max_precipitation_rate: Math.max(0, ...o.precipRate),
    total_cumulative_mm: o.cumulative.reduce((a, b) => a + b, 0),
    surface_tendency_k_h: o.tendency * 3600,
    orientation_dominant_deg: dominantOrientation(o.orientation),
  }
}

// Dominant contour direction (0 = north-south, 90 = east-west) as an axial mean.
export function dominantOrientation(angles) {
  let sx = 0, sy = 0, n = 0
  for (const a of angles) { if (a === null) continue; const r = 2 * a * Math.PI / 180; sx += Math.cos(r); sy += Math.sin(r); n++ }
  if (!n) return null
  let mean = Math.atan2(sy, sx) / 2 * 180 / Math.PI
  if (mean < 0) mean += 180
  return mean
}

// Hillshade-like brightness (0..1) from the terrain and a light from the north-west.
export function hillshade(terrain, grid) {
  const { nx, ny } = grid, out = new Array(nx * ny)
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    const w = terrain[y * nx + Math.max(0, x - 1)], e = terrain[y * nx + Math.min(nx - 1, x + 1)]
    const s = terrain[Math.max(0, y - 1) * nx + x], nn = terrain[Math.min(ny - 1, y + 1) * nx + x]
    const sx = (e - w) / (2 * grid.spacing_east_m), sy = (nn - s) / (2 * grid.spacing_north_m)
    const shade = 0.5 + 3 * (-sx * 0.7 + sy * 0.7)  // light from the north-west
    out[y * nx + x] = Math.min(1, Math.max(0.1, shade))
  }
  return out
}
