// Weather map support: validation of the documented
// terraflux.weather.precipitation_outcomes/v1 block, precipitation styling,
// isobar contouring of the pressure field and per-frame metrics. The map is a
// composition of switchable layers over engine (or labelled mock) output; no
// physics runs in the browser.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const PRECIPITATION_TYPES = [
  { key: 'none', label: 'Dry', symbol: '', color: '#1b2a33' },
  { key: 'drizzle', label: 'Drizzle', symbol: '’', color: '#7fb3c8' },
  { key: 'rain', label: 'Rain (continuous)', symbol: '●', color: '#2f8fd6' },
  { key: 'sleet', label: 'Sleet', symbol: '▲', color: '#8a7fd6' },
  { key: 'snow', label: 'Snow', symbol: '✱', color: '#d9e6f2' },
  { key: 'rain_showers', label: 'Rain showers', symbol: '▽', color: '#1f6fb8' },
  { key: 'sleet_showers', label: 'Sleet showers', symbol: '▽', color: '#7f6fc8' },
  { key: 'snow_showers', label: 'Snow showers', symbol: '▽', color: '#c9d9ea' },
]
export const PHASES = ['none', 'liquid', 'mixed', 'solid']
export const CHARACTERS = ['none', 'drizzle', 'continuous', 'intermittent', 'showers']
export const INTENSITIES = ['none', 'light', 'moderate', 'heavy', 'violent']
export const HAZARDS = ['thunderstorm', 'hail', 'heavy_rain', 'violent_rain', 'prolonged_rain', 'snow', 'sleet', 'drizzle', 'virga']
export const HAZARD_LABELS = { thunderstorm: '⚡ thunderstorm', hail: '◆ hail', heavy_rain: 'heavy rain', violent_rain: 'violent rain', prolonged_rain: 'prolonged rain', snow: 'snow', sleet: 'sleet', drizzle: 'drizzle', virga: 'virga' }
const typeIndex = Object.fromEntries(PRECIPITATION_TYPES.map((t, i) => [t.key, i]))
export const precipitationTypeInfo = key => PRECIPITATION_TYPES[typeIndex[key] ?? 0]
export const intensityClass = rate => rate < 0.05 ? 'none' : rate < 2.5 ? 'light' : rate < 10 ? 'moderate' : rate < 50 ? 'heavy' : 'violent'
// Rate colour ramp (mm/h): light blue -> deep blue -> purple -> red.
export function rateColor(rate) {
  if (rate < 0.05) return null
  const stops = [[0.05, [150, 200, 235]], [2.5, [60, 140, 220]], [10, [30, 70, 180]], [30, [120, 40, 160]], [60, [200, 40, 60]]]
  let a = stops[0], b = stops[stops.length - 1]
  for (let i = 0; i + 1 < stops.length; i++) if (rate >= stops[i][0] && rate <= stops[i + 1][0]) { a = stops[i]; b = stops[i + 1]; break }
  const w = rate <= a[0] ? 0 : rate >= b[0] ? 1 : (Math.log(rate) - Math.log(a[0])) / (Math.log(b[0]) - Math.log(a[0]))
  const c = a[1].map((v, i) => Math.round(v + (b[1][i] - v) * w))
  return `rgb(${c[0]},${c[1]},${c[2]})`
}

const FIELD_KEYS = ['precipitation_type', 'phase', 'character', 'intensity', 'rate_mm_h', 'duration_fraction', 'accumulation_mm',
  'hail_max_diameter_cm', 'lightning', 'virga', 'updraft_m_s', 'freezing_level_m', 'surface_wet_bulb_k', 'precipitable_water_mm',
  'cloud_base_temperature_k', 'cloud_top_temperature_k', 'hazards']
const CONFIG_KEYS = ['stratiform_efficiency', 'convective_efficiency', 'convective_updraft_fraction', 'updraft_parcel_factor', 'interval_seconds',
  'snow_wet_bulb_k', 'sleet_wet_bulb_k', 'lightning_top_temperature_k', 'hail_min_diameter_cm', 'hail_melting_cm_per_km', 'hail_max_diameter_cm',
  'drizzle_max_rate_mm_h', 'virga_surface_rh', 'virga_max_rate_mm_h', 'virga_min_base_m', 'minimum_rate_mm_h']

// Validate the precipitation block and attach frame.precipitation.
export function validatePrecipitation(block, frames) {
  const p = block, count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  check(p?.schema_version === 'terraflux.weather.precipitation_outcomes/v1' && p.method === 'genus_condensation_rate_wet_bulb_phase_updraft_hail', 'Unsupported precipitation contract.')
  check(['engine_output', 'synthetic'].includes(p.source_kind), 'Precipitation source must be engine_output or synthetic.')
  check(p.config && CONFIG_KEYS.every(k => finite(p.config[k])) && p.config.interval_seconds > 0, 'Invalid precipitation configuration.')
  check(Array.isArray(p.frames) && p.frames.length === frames.length, 'Precipitation frame count mismatch.')
  frames.forEach((frame, i) => {
    const f = p.frames[i]
    check(f && f.elapsed_seconds === frame.forecast.elapsed_seconds, 'Precipitation frame times must match the timeline.')
    check(FIELD_KEYS.every(k => Array.isArray(f[k]) && f[k].length === count), 'Precipitation arrays do not match the grid.')
    let precipitating = 0, lightning = 0, hail = 0, snow = 0, total = 0
    for (let j = 0; j < count; j++) {
      const kind = f.precipitation_type[j], rate = f.rate_mm_h[j]
      check(typeIndex[kind] !== undefined && PHASES.includes(f.phase[j]) && CHARACTERS.includes(f.character[j]) && INTENSITIES.includes(f.intensity[j]), 'Unknown precipitation type, phase, character or intensity.')
      check(finite(rate) && rate >= 0 && f.intensity[j] === intensityClass(rate), 'Intensity class must follow the rate.')
      check(finite(f.accumulation_mm[j]) && f.accumulation_mm[j] >= 0 && finite(f.duration_fraction[j]) && f.duration_fraction[j] >= 0 && f.duration_fraction[j] <= 1, 'Invalid accumulation or duration.')
      if (kind === 'none') check(rate === 0 && f.phase[j] === 'none' && f.character[j] === 'none' && f.accumulation_mm[j] === 0 && !f.lightning[j] && f.hail_max_diameter_cm[j] === 0, 'Dry cells must have no precipitation properties.')
      else { precipitating++; check(rate > 0 && f.phase[j] !== 'none' && f.character[j] !== 'none' && Math.abs(f.accumulation_mm[j] - rate * f.duration_fraction[j] * p.config.interval_seconds / 3600) < 1e-6, 'Precipitating cells need rate, phase, character and a consistent accumulation.') }
      check(typeof f.lightning[j] === 'boolean' && typeof f.virga[j] === 'boolean' && finite(f.hail_max_diameter_cm[j]) && f.hail_max_diameter_cm[j] >= 0, 'Invalid lightning, virga or hail values.')
      check(!(f.hail_max_diameter_cm[j] > 0) || f.lightning[j], 'Hail requires lightning.')
      check(Array.isArray(f.hazards[j]) && f.hazards[j].every(h => HAZARDS.includes(h)), 'Unknown hazard.')
      check(f.lightning[j] === f.hazards[j].includes('thunderstorm') && (f.hail_max_diameter_cm[j] > 0) === f.hazards[j].includes('hail'), 'Hazards must match lightning and hail.')
      check(finite(f.updraft_m_s[j]) && f.updraft_m_s[j] >= 0 && (f.freezing_level_m[j] === null || (finite(f.freezing_level_m[j]) && f.freezing_level_m[j] >= 0)), 'Invalid updraft or freezing level.')
      check(finite(f.surface_wet_bulb_k[j]) && f.surface_wet_bulb_k[j] > 0 && finite(f.precipitable_water_mm[j]) && f.precipitable_water_mm[j] >= 0, 'Invalid wet-bulb or precipitable water.')
      check((f.cloud_base_temperature_k[j] === null) === (f.cloud_top_temperature_k[j] === null), 'Cloud temperatures come in pairs.')
      lightning += f.lightning[j] ? 1 : 0; hail += f.hail_max_diameter_cm[j] > 0 ? 1 : 0; snow += f.phase[j] === 'solid' ? 1 : 0; total += f.accumulation_mm[j]
    }
    check(f.precipitating_cells === precipitating && f.lightning_cells === lightning && f.hail_cells === hail && f.snow_cells === snow, 'Precipitation frame totals disagree with the arrays.')
    check(finite(f.max_rate_mm_h) && Math.abs(f.max_rate_mm_h - Math.max(0, ...f.rate_mm_h)) < 1e-9 && finite(f.total_accumulation_mm) && Math.abs(f.total_accumulation_mm - total) < 1e-6, 'Precipitation frame maxima and totals disagree.')
    frame.precipitation = { type: f.precipitation_type, phase: f.phase, character: f.character, intensity: f.intensity, rate: f.rate_mm_h, duration: f.duration_fraction, accumulation: f.accumulation_mm, hail: f.hail_max_diameter_cm, lightning: f.lightning, virga: f.virga, updraft: f.updraft_m_s, freezingLevel: f.freezing_level_m, wetBulb: f.surface_wet_bulb_k, precipitableWater: f.precipitable_water_mm, baseTemperature: f.cloud_base_temperature_k, topTemperature: f.cloud_top_temperature_k, hazards: f.hazards, precipitating, lightningCells: lightning, hailCells: hail, snowCells: snow, maxRate: f.max_rate_mm_h, total, config: p.config, synthetic: p.source_kind === 'synthetic' }
  })
  return p
}

export function summarizePrecipitation(frame) {
  const p = frame.precipitation, n = p.type.length, counts = {}
  for (const t of p.type) counts[t] = (counts[t] ?? 0) + 1
  const hazardCounts = {}
  for (const list of p.hazards) for (const h of list) hazardCounts[h] = (hazardCounts[h] ?? 0) + 1
  const wet = p.rate.map((r, i) => r > 0 ? i : null).filter(i => i !== null)
  return {
    precipitating_pct: p.precipitating / n * 100,
    mean_rate_mm_h: wet.length ? wet.reduce((a, i) => a + p.rate[i], 0) / wet.length : 0,
    max_rate_mm_h: p.maxRate,
    mean_accumulation_mm: p.total / n,
    max_accumulation_mm: Math.max(0, ...p.accumulation),
    lightning_cells: p.lightningCells,
    hail_cells: p.hailCells,
    max_hail_cm: Math.max(0, ...p.hail),
    snow_cells: p.snowCells,
    type_counts: counts,
    hazard_counts: hazardCounts,
    mean_precipitable_water_mm: p.precipitableWater.reduce((a, b) => a + b, 0) / n,
  }
}

// Isobars by marching squares on the cell-centre grid. Returns polylines in
// normalized map coordinates (u east 0..1, v north 0..1) per pressure level.
export function isobars(pressurePa, grid, intervalPa = 200) {
  const { nx, ny } = grid
  check(Array.isArray(pressurePa) && pressurePa.length === nx * ny && pressurePa.every(finite) && intervalPa > 0, 'Isobars need a finite pressure field and a positive interval.')
  let lo = Infinity, hi = -Infinity
  for (const p of pressurePa) { lo = Math.min(lo, p); hi = Math.max(hi, p) }
  const result = []
  const at = (x, y) => pressurePa[y * nx + x]
  const point = (x, y) => [(x + 0.5) / nx, (y + 0.5) / ny]
  const lerp = (a, b, va, vb, level) => { const t = vb === va ? 0.5 : (level - va) / (vb - va); return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t] }
  for (let level = Math.ceil(lo / intervalPa) * intervalPa; level <= hi; level += intervalPa) {
    const segments = []
    for (let y = 0; y + 1 < ny; y++) for (let x = 0; x + 1 < nx; x++) {
      const corners = [[x, y], [x + 1, y], [x + 1, y + 1], [x, y + 1]]
      const values = corners.map(([cx, cy]) => at(cx, cy))
      const inside = values.map(v => v >= level)
      const code = inside.reduce((c, b, i) => c | (b ? 1 << i : 0), 0)
      if (code === 0 || code === 15) continue
      const edge = i => lerp(point(...corners[i]), point(...corners[(i + 1) % 4]), values[i], values[(i + 1) % 4], level)
      // Edges: 0 bottom (c0-c1), 1 right (c1-c2), 2 top (c2-c3), 3 left (c3-c0).
      const table = { 1: [[3, 0]], 2: [[0, 1]], 3: [[3, 1]], 4: [[1, 2]], 5: [[3, 2], [0, 1]], 6: [[0, 2]], 7: [[3, 2]], 8: [[2, 3]], 9: [[0, 2]], 10: [[0, 3], [1, 2]], 11: [[1, 2]], 12: [[1, 3]], 13: [[0, 1]], 14: [[3, 0]] }
      for (const [a, b] of table[code]) segments.push([edge(a), edge(b)])
    }
    // Chain segments into polylines.
    const key = p => `${p[0].toFixed(6)},${p[1].toFixed(6)}`
    const unused = new Set(segments.map((_, i) => i)), byPoint = new Map()
    segments.forEach((s, i) => { for (const p of s) { const k = key(p); if (!byPoint.has(k)) byPoint.set(k, []); byPoint.get(k).push(i) } })
    const lines = []
    while (unused.size) {
      const start = unused.values().next().value; unused.delete(start)
      let line = [segments[start][0], segments[start][1]]
      for (const direction of [1, -1]) {
        let end = direction === 1 ? line[line.length - 1] : line[0]
        for (;;) {
          const next = (byPoint.get(key(end)) ?? []).find(i => unused.has(i))
          if (next === undefined) break
          unused.delete(next)
          const [a, b] = segments[next]
          const other = key(a) === key(end) ? b : a
          if (direction === 1) line.push(other); else line.unshift(other)
          end = other
        }
      }
      lines.push(line)
    }
    if (lines.length) result.push({ level_pa: level, level_hpa: level / 100, lines })
  }
  return result
}
