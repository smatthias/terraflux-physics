// Coupled Earth: the views of the coupled forecast (terraflux.earth.forecast/v1) and the coupled climate
// (terraflux.earth.climate/v1). Validation of both blocks, the products the views show (city tables, skill, warnings,
// hurricane tracks, climate drivers, impacts, the CO2 map and the coupling graph) and their colour scales. Every
// number comes from the engine output; nothing is simulated in the browser.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v))

export const EARTH_VIEWS = [
  { key: 'overview', label: 'Overview', needs: 'climate', tooltip: 'What is coupled to what, how strongly, and the key answers of both runs.' },
  { key: 'drivers', label: 'Climate drivers', needs: 'climate', tooltip: 'Which drivers warm or cool the climate most, today and in 2050 and 2100, and what the warming does.' },
  { key: 'co2', label: 'CO₂ impact', needs: 'climate', tooltip: 'Where CO₂ comes from, where it goes, and where it hits hardest.' },
  { key: 'weather', label: 'Weather forecast', needs: 'forecast', tooltip: 'The 3, 5, 7 and 10-day ensemble forecast: temperatures, rain, clouds, cities, skill.' },
  { key: 'wind', label: 'Wind forecast', needs: 'forecast', tooltip: 'Wind and gusts hour by hour for the next three days, with city meteograms.' },
  { key: 'severe', label: 'Severe weather', needs: 'forecast', tooltip: 'Warnings for storms, floods, thunderstorms, tornadoes and hurricanes, and the hurricane track plume.' },
]
export const HORIZONS = [3, 5, 7, 10]
export const HAZARDS = [
  { key: 'wind', label: 'Wind storm', icon: '≋', value: 'gust (m/s)' },
  { key: 'rain', label: 'Heavy rain', icon: '☂', value: 'rain (mm/24 h)' },
  { key: 'flood', label: 'Flooding', icon: '≈', value: 'runoff (mm/72 h)' },
  { key: 'thunderstorm', label: 'Thunderstorm / hail', icon: 'ϟ', value: 'probability of thunder' },
  { key: 'tornado', label: 'Tornado', icon: '▼', value: 'probability of a tornado' },
  { key: 'hurricane', label: 'Hurricane', icon: '@', value: 'probability of storm-force winds' },
  { key: 'surge', label: 'Storm surge', icon: '⇑', value: 'surge (m)' },
]
export const LEVELS = [
  { level: 0, label: 'none', color: null },
  { level: 1, label: 'yellow', color: '#f2d338' },
  { level: 2, label: 'orange', color: '#f08a24' },
  { level: 3, label: 'red', color: '#dc2836' },
]
// Weather symbols 0-7 of the engine (documented in the forecast schema).
export const SYMBOLS = [
  { code: 0, label: 'clear', glyph: '☀', color: '#ffd166' },
  { code: 1, label: 'partly cloudy', glyph: '◐', color: '#e7d9a1' },
  { code: 2, label: 'cloudy', glyph: '☁', color: '#b7c4cc' },
  { code: 3, label: 'rain', glyph: '☂', color: '#64a9ee' },
  { code: 4, label: 'snow', glyph: '❄', color: '#e8f4ff' },
  { code: 5, label: 'thunderstorms', glyph: 'ϟ', color: '#c79bff' },
  { code: 6, label: 'gale', glyph: '≋', color: '#7fe0c4' },
  { code: 7, label: 'hurricane', glyph: '@', color: '#ff6b6b' },
]
export const symbolInfo = code => SYMBOLS[code] ?? SYMBOLS[2]
export const hazardInfo = key => HAZARDS.find(h => h.key === key) ?? HAZARDS[0]

// ---- Validation ----------------------------------------------------------------------------------------------------

export function earthDocumentKind(doc) {
  if (doc?.earth_forecast) return 'forecast'
  if (doc?.earth_climate) return 'climate'
  return null
}

export function validateEarthForecast(doc) {
  const f = doc?.earth_forecast
  check(f?.schema_version === 'terraflux.earth.forecast/v1' && f.method === 'coupled_ensemble_forecast', 'Expected a terraflux.earth.forecast/v1 block.')
  check(f.source_kind === 'engine_output', 'The coupled forecast must be engine output.')
  const g = doc.frames?.[0]?.grid
  check(g && Number.isInteger(g.nx) && Number.isInteger(g.ny) && g.nx >= 2 && g.ny >= 2, 'The forecast timeline needs a grid.')
  const n = g.nx * g.ny
  check(Number.isInteger(f.members) && f.members >= 2 && Number.isInteger(f.forecast_days) && f.forecast_days >= 1, 'Invalid members or forecast days.')
  check(Array.isArray(f.frames) && f.frames.length === f.frame_hours.length && f.frames.length === doc.frames.length, 'Forecast frames do not match the timeline.')
  check(['land_mask', 'latitude_deg', 'longitude_deg', 'elevation_m'].every(k => Array.isArray(f.static?.[k]) && f.static[k].length === n), 'Static fields do not match the grid.')
  for (const fr of f.frames) check(['pressure_hpa', 'precip_mm', 'p_precip_1mm', 'cloud_pct', 'gust_m_s', 'p_gust_orange', 'p_thunder', 'p_tornado'].every(k => Array.isArray(fr[k]) && fr[k].length === n), 'Frame products do not match the grid.')
  check(Array.isArray(f.days) && f.days.length === f.forecast_days, 'One product per forecast day.')
  for (const d of f.days) {
    check(['tmax_c', 'tmin_c', 'precip_mm', 'gust_m_s', 'symbol', 'p_precip_1mm'].every(k => Array.isArray(d[k]) && d[k].length === n), 'Daily products do not match the grid.')
    check(HAZARDS.every(h => Array.isArray(d.warnings?.[h.key]) && d.warnings[h.key].length === n && d.warnings[h.key].every(l => Number.isInteger(l) && l >= 0 && l <= 3)), 'Warnings must be levels 0-3 per cell.')
  }
  check(Array.isArray(f.warnings?.regions) && Array.isArray(f.cities) && f.tracks && f.verification && f.couplings, 'Missing warnings, cities, tracks, verification or couplings.')
  return f
}

export function validateEarthClimate(doc) {
  const c = doc?.earth_climate
  check(c?.schema_version === 'terraflux.earth.climate/v1' && c.method === 'forcing_attribution_two_layer_energy_balance', 'Expected a terraflux.earth.climate/v1 block.')
  check(c.source_kind === 'engine_output', 'The coupled climate must be engine output.')
  check(Array.isArray(c.years) && c.years.length >= 2 && c.years.includes(c.today), 'Invalid years.')
  check(Array.isArray(c.scenarios) && c.scenarios.length >= 1 && c.scenarios.every(s => c.series[s.key] && c.drivers[s.key]), 'Every scenario needs its series and drivers.')
  for (const s of c.scenarios) check(['global_k', 'global_low_k', 'global_high_k', 'co2_ppm'].every(k => c.series[s.key][k]?.length === c.years.length), 'Series must cover every year.')
  const m = c.map
  check(m && Array.isArray(m.cell_degrees) && m.land_mask?.length > 0 && m.pattern?.length === m.land_mask.length, 'Invalid climate map.')
  const [dx, dy] = m.cell_degrees, nx = Math.round(360 / dx), ny = Math.round(180 / dy)
  check(nx * ny === m.land_mask.length, 'The climate map must be a global grid.')
  for (const key of ['emissions_t_km2_yr', 'sinks_t_km2_yr', 'net_t_km2_yr', 'surface_ppm_anomaly', 'impact_score']) check(c.co2[key]?.length === nx * ny, `CO2 map ${key} does not match the grid.`)
  check(Array.isArray(c.couplings) && c.couplings.length > 0, 'Missing couplings.')
  return c
}

// ---- Geometry ------------------------------------------------------------------------------------------------------

export function forecastGrid(doc) {
  const g = doc.frames[0].grid
  return { nx: g.nx, ny: g.ny, spacing: g.spacing_east_m, originEast: g.origin_east_m, originNorth: g.origin_north_m, lat0: g.geographic_anchor_latitude_deg, lon0: g.geographic_anchor_longitude_deg }
}
const RAD = Math.PI / 180, EARTH_RADIUS = 6.371e6
// Azimuthal equidistant projection about the anchor (as in the engine), in fractional cells from the south-west corner.
export function projectToGrid(lat, lon, grid) {
  const p = lat * RAD, p0 = grid.lat0 * RAD, dl = (lon - grid.lon0) * RAD
  const cosc = clamp(Math.sin(p0) * Math.sin(p) + Math.cos(p0) * Math.cos(p) * Math.cos(dl), -1, 1), c = Math.acos(cosc)
  const k = c < 1e-12 ? 1 : c / Math.sin(c)
  const east = EARTH_RADIUS * k * Math.cos(p) * Math.sin(dl), north = EARTH_RADIUS * k * (Math.cos(p0) * Math.sin(p) - Math.sin(p0) * Math.cos(p) * Math.cos(dl))
  return { x: (east - grid.originEast) / grid.spacing, y: (north - grid.originNorth) / grid.spacing }
}
// Equirectangular global map: fractional cells from 180 W, 90 S.
export function climateGrid(block) {
  const [dx, dy] = block.map.cell_degrees
  return { nx: Math.round(360 / dx), ny: Math.round(180 / dy), dx, dy }
}
export const geoToClimateCell = (lat, lon, g) => ({ x: (lon + 180) / g.dx, y: (lat + 90) / g.dy })

// ---- Colours -------------------------------------------------------------------------------------------------------

function ramp(stops, t) {
  const x = clamp(t, 0, 1) * (stops.length - 1), i = Math.min(stops.length - 2, Math.floor(x)), w = x - i
  const a = stops[i], b = stops[i + 1]
  return `rgb(${Math.round(a[0] + (b[0] - a[0]) * w)},${Math.round(a[1] + (b[1] - a[1]) * w)},${Math.round(a[2] + (b[2] - a[2]) * w)})`
}
const THERMAL = [[49, 54, 149], [69, 117, 180], [116, 173, 209], [171, 217, 233], [224, 243, 248], [254, 224, 144], [253, 174, 97], [244, 109, 67], [215, 48, 39], [165, 0, 38]]
const DIVERGING = [[33, 102, 172], [103, 169, 207], [209, 229, 240], [247, 247, 247], [253, 219, 199], [239, 138, 98], [178, 24, 43]]
const WET = [[27, 42, 51], [60, 112, 150], [66, 160, 220], [92, 110, 230], [170, 80, 220], [230, 60, 120]]
const WINDY = [[24, 48, 62], [38, 120, 140], [80, 180, 120], [220, 210, 80], [240, 130, 50], [210, 40, 60], [150, 30, 140]]
const IMPACT = [[26, 40, 48], [60, 110, 100], [200, 190, 80], [235, 120, 50], [200, 30, 60]]
export const temperatureColor = c => ramp(THERMAL, (c + 10) / 45)
export const precipColor = mm => mm < 0.2 ? null : ramp(WET, Math.log10(mm / 0.2) / Math.log10(150 / 0.2))
export const probabilityColor = pct => pct < 5 ? null : ramp(WET, pct / 100)
export const cloudColor = pct => ramp([[20, 34, 44], [120, 136, 146], [226, 232, 236]], pct / 100)
export const windColor = ms => ramp(WINDY, ms / 40)
export const spreadColor = k => ramp([[20, 40, 50], [90, 160, 150], [240, 200, 80], [230, 70, 60]], k / 3)
export const warmingColor = (k, max = 6) => ramp(DIVERGING, 0.5 + k / (2 * max))
// Local warming (K): blue below zero, then pale yellow through orange and red to purple at 8 K.
const HEAT = [[250, 246, 232], [253, 220, 140], [246, 160, 75], [224, 82, 48], [170, 20, 45], [95, 10, 75]]
export const localWarmingColor = k => (k < 0 ? ramp([[209, 229, 240], [103, 169, 207], [33, 102, 172]], -k / 2) : ramp(HEAT, k / 8))
export const levelColor = level => LEVELS[level]?.color ?? null
export const impactColor = s => ramp(IMPACT, s)
// CO2 fluxes (t CO2 per km2 per year): a signed logarithmic scale from 1 to 1,000 (sinks blue, sources red).
export function fluxColor(v) {
  if (!finite(v) || Math.abs(v) < 1) return 'rgb(247,247,247)'
  const t = Math.log10(Math.abs(v)) / 3
  return ramp(DIVERGING, 0.5 + Math.sign(v) * clamp(t, 0, 1) * 0.5)
}

// ---- Forecast products ---------------------------------------------------------------------------------------------

export const FORECAST_FIELDS = [
  { key: 'tmax_c', label: 'Maximum temperature', unit: '°C', color: temperatureColor },
  { key: 'tmin_c', label: 'Minimum temperature', unit: '°C', color: temperatureColor },
  { key: 'precip_mm', label: 'Precipitation (day)', unit: 'mm', color: precipColor },
  { key: 'p_precip_1mm', label: 'Chance of rain (≥ 1 mm)', unit: '%', color: probabilityColor },
  { key: 'p_precip_10mm', label: 'Chance of heavy rain (≥ 10 mm)', unit: '%', color: probabilityColor },
  { key: 'cloud_pct', label: 'Cloud cover (daily mean)', unit: '%', color: cloudColor },
  { key: 'gust_m_s', label: 'Strongest gust', unit: 'm/s', color: windColor },
  { key: 'tmax_spread_k', label: 'Uncertainty of the maximum (ensemble spread)', unit: 'K', color: spreadColor },
  { key: 'symbol', label: 'Weather symbol', unit: '', color: v => symbolInfo(v).color },
]
export const fieldInfo = key => FORECAST_FIELDS.find(f => f.key === key) ?? FORECAST_FIELDS[0]

// The frame at noon (12 h after the start) of a forecast day, or the nearest frame.
export function noonFrame(block, day) {
  const target = 24 * (day - 1) + 12
  let best = 0
  block.frame_hours.forEach((h, k) => { if (Math.abs(h - target) < Math.abs(block.frame_hours[best] - target)) best = k })
  return best
}

export function validTime(block, hours) {
  return new Date(Date.parse(block.start_utc) + hours * 3600e3)
}
const WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']
export function dayLabel(block, day) {
  const t = validTime(block, 24 * (day - 1))
  return `${WEEKDAYS[t.getUTCDay()]} ${t.getUTCDate()}.${t.getUTCMonth() + 1}.`
}

// City table over a horizon: one row per city, one cell per day.
export function cityForecast(block, horizon) {
  return block.cities.filter(c => c.inside).map(c => ({
    name: c.name, land: c.land, elevation: c.elevation_m, latitude: c.latitude_deg, longitude: c.longitude_deg, cell: c.cell,
    days: c.days.slice(0, horizon).map(d => {
      const hazards = HAZARDS.filter(h => d.warnings[h.key] > 0).map(h => ({ key: h.key, level: d.warnings[h.key] }))
      return { day: d.day, symbol: d.symbol, tmax: d.tmax_c, tmin: d.tmin_c, tmaxRange: [d.tmax_p10_c, d.tmax_p90_c], precip: d.precip_mm, precipP90: d.precip_p90_mm,
        rainChance: Math.round(100 * d.p_precip_1mm), gust: d.gust_m_s, cloud: d.cloud, level: Math.max(0, ...hazards.map(h => h.level)), hazards,
        truth: { tmax: d.truth_tmax_c, precip: d.truth_precip_mm } }
    }),
  }))
}

// The forecast of one grid cell (the inspector): daily values and warnings.
export function cellForecast(block, cell, horizon) {
  return block.days.slice(0, horizon).map(d => ({
    day: d.day, symbol: d.symbol[cell], tmax: d.tmax_c[cell], tmin: d.tmin_c[cell], spread: d.tmax_spread_k?.[cell] ?? null, precip: d.precip_mm[cell],
    rainChance: d.p_precip_1mm[cell], gust: d.gust_m_s[cell], cloud: d.cloud_pct?.[cell] ?? null,
    warnings: HAZARDS.filter(h => d.warnings[h.key][cell] > 0).map(h => ({ key: h.key, level: d.warnings[h.key][cell] })),
  }))
}

// Skill against the lead time (the hidden truth of the model world): pressure and temperature scores per frame.
export function skillSeries(block) {
  return block.verification.frames.map(f => ({
    hour: f.hour,
    pressureAcc: f.pressure_hpa.anomaly_correlation, pressureRmse: f.pressure_hpa.rmse, pressureSpread: f.pressure_hpa.spread,
    pressureClimatology: f.pressure_hpa.climatology_rmse, pressurePersistence: f.pressure_hpa.persistence_rmse,
    temperatureRmse: f.temperature_k.rmse, temperatureSpread: f.temperature_k.spread, temperatureClimatology: f.temperature_k.climatology_rmse,
    rainSkill: f.rain_1mm ? f.rain_1mm.skill : null,
  }))
}

// How far the forecast beats the climatology (first lead where the pressure anomaly correlation drops below 0.6).
export function usefulLead(block) {
  const s = skillSeries(block)
  const lost = s.find(p => p.hour > 0 && p.pressureAcc < 0.6)
  return { hours: block.verification.useful_hours ?? (lost ? lost.hour : s.at(-1).hour), acc06: lost ? lost.hour : null }
}

// ---- Wind ----------------------------------------------------------------------------------------------------------

// Frames within the first three days (the wind forecast).
export function windFrames(block, maxHours = 72) {
  return block.frame_hours.map((h, k) => ({ hour: h, index: k })).filter(f => f.hour <= maxHours)
}
export function windField(doc, index) {
  const f = doc.frames[index].fields
  const u = f.wind_east.values, v = f.wind_north.values
  return { u, v, speed: u.map((x, i) => Math.hypot(x, v[i])) }
}
// Direction the wind blows from (degrees, meteorological).
export const directionFrom = (u, v) => (Math.atan2(-u, -v) / RAD + 360) % 360
const COMPASS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW']
export const compass = deg => COMPASS[Math.round(((deg % 360) + 360) % 360 / 22.5) % 16]
const BEAUFORT = [0.3, 1.6, 3.4, 5.5, 8, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7]
export const beaufort = ms => { const k = BEAUFORT.findIndex(x => ms < x); return k === -1 ? 12 : k }

// Per city: the strongest gust of each of the first three days, when it comes and from where.
export function cityWindTable(block) {
  return block.cities.filter(c => c.inside && c.hourly).map(c => {
    const h = c.hourly
    const days = [1, 2, 3].map(day => {
      let best = -1
      h.hour.forEach((hr, k) => { if (hr > 24 * (day - 1) && hr <= 24 * day && (best < 0 || h.gust_m_s[k] > h.gust_m_s[best])) best = k })
      if (best < 0) return null
      return { day, gust: h.gust_m_s[best], gustP90: h.gust_p90_m_s[best], wind: h.wind_m_s[best], hour: h.hour[best], direction: h.direction_deg[best] }
    })
    const meanWind = h.wind_m_s.reduce((a, b) => a + b, 0) / h.wind_m_s.length
    return { name: c.name, days, meanWind, peak: days.filter(Boolean).reduce((a, b) => (b.gust > (a?.gust ?? -1) ? b : a), null) }
  })
}

// A meteogram of one city: hourly mean wind with its 10-90 % range, gusts, the truth and the direction.
export function meteogram(city) {
  const h = city.hourly
  const max = Math.max(10, ...h.gust_p90_m_s, ...h.wind_p90_m_s, ...h.truth_wind_m_s) * 1.08
  const X = hr => (hr - h.hour[0]) / ((h.hour.at(-1) - h.hour[0]) || 1) * 100, Y = v => 48 - v / max * 44
  const line = values => values.map((v, k) => `${X(h.hour[k]).toFixed(2)},${Y(v).toFixed(2)}`).join(' ')
  const band = h.wind_p10_m_s.map((v, k) => `${X(h.hour[k]).toFixed(2)},${Y(v).toFixed(2)}`).concat(h.wind_p90_m_s.map((v, k) => `${X(h.hour[k]).toFixed(2)},${Y(v).toFixed(2)}`).reverse()).join(' ')
  const arrows = h.hour.filter((_, k) => k % 6 === 2).map(hr => { const k = h.hour.indexOf(hr); return { x: X(hr), direction: h.direction_deg[k], speed: h.wind_m_s[k] } })
  const ticks = [0, 12, 24, 36, 48, 60, 72].filter(t => t >= h.hour[0] - 1 && t <= h.hour.at(-1)).map(t => ({ hour: t, x: X(t) }))
  const levels = [10, 20, 30, 40, 50].filter(v => v < max).map(v => ({ v, y: Y(v) }))
  return { max, wind: line(h.wind_m_s), gust: line(h.gust_m_s), gustP90: line(h.gust_p90_m_s), truth: line(h.truth_wind_m_s), band, arrows, ticks, levels, x: X, y: Y }
}

// ---- Severe weather ------------------------------------------------------------------------------------------------

// Maximum level per day and hazard, and the number of warned cells.
export function warningMatrix(block) {
  return block.days.map(d => ({
    day: d.day,
    hazards: HAZARDS.map(h => {
      const lv = d.warnings[h.key]
      let max = 0, cells = 0
      for (const l of lv) { if (l > max) max = l; if (l > 0) cells++ }
      return { key: h.key, level: max, cells }
    }),
  }))
}
// Warning levels of a day for one hazard, or the highest of all hazards.
export function warningLevels(block, day, hazard = 'all') {
  const d = block.days[day - 1]
  if (hazard !== 'all') return d.warnings[hazard]
  const n = d.warnings.wind.length, out = new Array(n).fill(0)
  for (const h of HAZARDS) d.warnings[h.key].forEach((l, i) => { if (l > out[i]) out[i] = l })
  return out
}
// The warning areas of a day (the engine's connected regions), highest level and largest first.
export function warningList(block, day, hazard = 'all') {
  return block.warnings.regions.filter(r => r.day === day && (hazard === 'all' || r.hazard === hazard))
    .sort((a, b) => b.level - a.level || b.area_km2 - a.area_km2 || b.peak_probability - a.peak_probability)
}
export function warningCounts(block, day) {
  const out = { 1: 0, 2: 0, 3: 0 }
  for (const r of block.warnings.regions) if (r.day === day) out[r.level]++
  return out
}
export function peakValueLabel(region) {
  const v = region.peak_value
  switch (region.hazard) {
    case 'wind': return `gusts ${v.toFixed(0)} m/s`
    case 'rain': return `${v.toFixed(0)} mm in 24 h`
    case 'flood': return `${v.toFixed(0)} mm runoff in 72 h`
    case 'surge': return `surge ${v.toFixed(1)} m`
    default: return `${Math.round(v * 100)} % of the members`   // thunderstorms, tornadoes, hurricanes: a probability
  }
}
export const SEVERE_LAYERS = [
  { key: 'warnings', label: 'Warning level' },
  { key: 'p_thunder', label: 'Chance of thunderstorms', unit: '%', color: probabilityColor },
  { key: 'p_hail', label: 'Chance of large hail', unit: '%', color: probabilityColor },
  { key: 'p_tornado', label: 'Chance of a tornado', unit: '%', color: probabilityColor },
  { key: 'p_tropical_storm', label: 'Chance of tropical-storm winds', unit: '%', color: probabilityColor },
  { key: 'p_hurricane', label: 'Chance of hurricane winds', unit: '%', color: probabilityColor },
  { key: 'runoff_72h_mm', label: 'Runoff over 72 h (flood index)', unit: 'mm', color: precipColor },
  { key: 'surge_m', label: 'Storm surge', unit: 'm', color: v => (v < 0.1 ? null : ramp(WET, v / 4)) },
]

const CATEGORY_COLORS = { depression: '#7fb3c8', tropical_storm: '#64d9a0', category_1: '#f2d338', category_2: '#f0a424', category_3: '#f06424', category_4: '#dc2836', category_5: '#b01a8c' }
export const categoryColor = c => CATEGORY_COLORS[c] ?? '#c8d6de'
export const CATEGORY_LABELS = { depression: 'depression', tropical_storm: 'tropical storm', category_1: 'category 1', category_2: 'category 2', category_3: 'category 3', category_4: 'category 4', category_5: 'category 5' }
// Tracks of every member and of the truth: [{name, truth, points:[{hour, lat, lon, wind, category}]}].
export function trackLines(block) {
  const toPoints = t => t.points.map(([hour, lat, lon, wind, category]) => ({ hour, lat, lon, wind, category }))
  const out = []
  block.tracks.members.forEach((member, m) => member.forEach(t => out.push({ name: t.name, member: m + 1, truth: false, points: toPoints(t) })))
  for (const t of block.tracks.truth) out.push({ name: t.name, member: null, truth: true, points: toPoints(t) })
  return out
}
// Where the members put the storm at a lead time: the positions and their spread (km).
export function trackSpread(block, hour) {
  const pos = trackLines(block).filter(t => !t.truth).map(t => t.points.find(p => p.hour === hour)).filter(Boolean)
  if (!pos.length) return null
  const lat = pos.reduce((a, p) => a + p.lat, 0) / pos.length, lon = pos.reduce((a, p) => a + p.lon, 0) / pos.length
  const km = pos.map(p => Math.hypot((p.lat - lat) * 111.2, (p.lon - lon) * 111.2 * Math.cos(lat * RAD)))
  return { hour, members: pos.length, lat, lon, spreadKm: Math.sqrt(km.reduce((a, d) => a + d * d, 0) / pos.length), maxWind: Math.max(...pos.map(p => p.wind)) }
}

// ---- Climate -------------------------------------------------------------------------------------------------------

export const PERIODS = [{ key: 'today', label: 'Today' }, { key: 'y2050', label: '2050' }, { key: 'y2100', label: '2100' }]
const GROUP_COLORS = { 'greenhouse gases': '#ff7b6b', 'air pollution': '#8fb8de', land: '#9fcf7a', transport: '#d9a6ff', natural: '#ffd166', space: '#7fe0e0' }
export const groupColor = g => GROUP_COLORS[g] ?? '#b7c4cc'
export const SCENARIO_COLORS = ['#64b5f6', '#ffb74d', '#ef5350', '#ba68c8', '#81c784', '#90a4ae']
export const scenarioColor = (block, key) => SCENARIO_COLORS[Math.max(0, block.scenarios.findIndex(s => s.key === key))]

// The drivers of one scenario and period, ranked, with the range of each bar.
export function driverBars(block, scenario, period) {
  const rows = block.drivers[scenario][period]
  const max = Math.max(0.05, ...rows.map(r => Math.max(Math.abs(r.warming_low_k), Math.abs(r.warming_high_k), Math.abs(r.warming_k), Math.abs(r.hypothesis_k ?? 0))))
  const total = rows.reduce((a, r) => a + r.warming_k, 0)
  return { max, total, rows: rows.map(r => ({ ...r, lo: Math.min(r.warming_low_k, r.warming_high_k), hi: Math.max(r.warming_low_k, r.warming_high_k), color: groupColor(r.group) })) }
}

// A chart of one series for every scenario over the years (polylines in a 100 x 50 box), with the sensitivity band of
// the chosen scenario.
export function scenarioChart(block, key, { focus = null, from = null, to = null, band = null } = {}) {
  const years = block.years, i0 = from === null ? 0 : Math.max(0, years.indexOf(from)), i1 = to === null ? years.length - 1 : years.indexOf(to)
  const pick = arr => arr.slice(i0, i1 + 1)
  const span = pick(years)
  const series = block.scenarios.map(s => ({ key: s.key, name: s.name, values: pick(block.series[s.key][key]) }))
  const bandValues = band && focus ? [pick(block.series[focus][band[0]]), pick(block.series[focus][band[1]])] : null
  const all = series.flatMap(s => s.values).concat(bandValues ? bandValues.flat() : []).filter(finite)
  let lo = Math.min(...all), hi = Math.max(...all)
  if (lo > 0 && key.endsWith('_k')) lo = Math.min(0, lo)
  const pad = (hi - lo) * 0.06 || 1; lo -= pad; hi += pad
  const X = y => (y - span[0]) / ((span.at(-1) - span[0]) || 1) * 100, Y = v => 48 - (v - lo) / ((hi - lo) || 1) * 44
  const line = values => values.map((v, k) => (finite(v) ? `${X(span[k]).toFixed(2)},${Y(v).toFixed(2)}` : null)).filter(Boolean).join(' ')
  const lines = series.map(s => ({ key: s.key, name: s.name, color: scenarioColor(block, s.key), width: s.key === focus ? 0.8 : 0.45, points: line(s.values) }))
  const bandPath = bandValues ? bandValues[0].map((v, k) => `${X(span[k]).toFixed(2)},${Y(Math.min(v, bandValues[1][k])).toFixed(2)}`).concat(bandValues[1].map((v, k) => `${X(span[k]).toFixed(2)},${Y(Math.max(v, bandValues[0][k])).toFixed(2)}`).reverse()).join(' ') : null
  const step = span.at(-1) - span[0] > 150 ? 50 : 25
  const ticks = []
  for (let y = Math.ceil(span[0] / step) * step; y <= span.at(-1); y += step) ticks.push({ year: y, x: X(y) })
  return { lo, hi, lines, band: bandPath, today: X(block.today), zero: Y(0), ticks, x: X, y: Y }
}

// Forcing by group of agents for one scenario (W/m2 per year).
export function forcingByGroup(block, scenario) {
  const groups = {}
  for (const a of block.agents) {
    const f = block.series[scenario].agents[a.key]?.forcing_w_m2
    if (!f) continue
    groups[a.group] ??= new Array(block.years.length).fill(0)
    f.forEach((v, k) => { groups[a.group][k] += v ?? 0 })
  }
  return Object.entries(groups).map(([group, values]) => ({ group, color: groupColor(group), values }))
}

export function impactRows(block) {
  return block.impacts.map(i => ({ ...i, fmt: v => formatImpact(i, v) }))
}
export function formatImpact(impact, v) {
  if (v === null || v === undefined || !finite(v)) return '—'
  if (impact.key === 'ph') return v.toFixed(3)
  if (impact.unit === 'times as often') return `×${v.toFixed(v >= 10 ? 0 : 1)}`
  if (impact.unit === 'ZJ') return `${Math.round(v)} ZJ`
  if (impact.unit === 'm') return `${(v * 100).toFixed(0)} cm`
  if (impact.unit === '%') return `${v >= 0 ? '+' : ''}${v.toFixed(1)} %`
  return `${v.toFixed(impact.unit === 'K' ? 2 : 1)} ${impact.unit}`
}

export function feedbackRows(block) {
  const labels = { no_feedback_k: 'No feedback (Planck)', water_vapour_lapse_rate_k: 'Water vapour & lapse rate', surface_albedo_k: 'Snow & ice (albedo)', clouds_k: 'Clouds', other_k: 'Other' }
  const c = block.climate
  return Object.keys(labels).filter(k => k !== 'other_k' || c.feedback_shares_today[k] !== 0).map(k => ({ key: k, label: labels[k], today: c.feedback_shares_today[k], y2100: c.feedback_shares_2100[k] }))
}

// Map layers of the climate frames: local warming of a scenario, and the two regional terms of the central scenario.
export const CLIMATE_LAYERS = [
  { key: 'warming', label: 'Local warming since 1850–1900' },
  { key: 'aerosol', label: 'Aerosol cooling (masks part of the warming)' },
  { key: 'overturning', label: 'Cooling from a weaker Atlantic overturning' },
  { key: 'pattern', label: 'Warming pattern (× global mean)' },
]

// ---- CO2 -----------------------------------------------------------------------------------------------------------

export const CO2_LAYERS = [
  { key: 'emissions_t_km2_yr', label: 'Emissions (fossil + land use)', unit: 't CO₂/km²/yr', color: v => fluxColor(v) },
  { key: 'sinks_t_km2_yr', label: 'Sinks (land and ocean uptake)', unit: 't CO₂/km²/yr', color: v => fluxColor(v) },
  { key: 'net_t_km2_yr', label: 'Net flux (sources − sinks)', unit: 't CO₂/km²/yr', color: v => fluxColor(v) },
  { key: 'surface_ppm_anomaly', label: 'CO₂ near the ground vs the global mean', unit: 'ppm', color: v => warmingColor(v, 6) },
  { key: 'warming_per_100ppm_k', label: 'Warming per +100 ppm (transient)', unit: 'K', color: v => warmingColor(v, 2) },
  { key: 'ph_drop_per_100ppm', label: 'Ocean pH drop per +100 ppm', unit: 'pH', color: v => (v === null ? null : impactColor((v - 0.07) / 0.03)) },
  { key: 'aragonite_2100', label: 'Aragonite saturation 2100 (< 1 corrosive)', unit: 'Ω', color: v => (v === null ? null : ramp([[200, 30, 60], [235, 120, 50], [200, 190, 80], [60, 130, 170], [40, 80, 140]], (v - 0.5) / 3.5)) },
  { key: 'impact_score', label: 'Where CO₂ hits hardest (impact score)', unit: '0–1', color: v => impactColor(v) },
]
export const co2LayerInfo = key => CO2_LAYERS.find(l => l.key === key) ?? CO2_LAYERS[0]

export function co2Budget(block) {
  const b = block.co2.budget
  const rows = [
    { key: 'fossil', label: 'Fossil fuels and cement', value: b.fossil_gt_co2_yr, color: '#ef5350' },
    { key: 'land_use', label: 'Land use (deforestation)', value: b.land_use_gt_co2_yr, color: '#ffa270' },
    { key: 'land_sink', label: 'Land sink (plants, soils)', value: b.land_sink_gt_co2_yr, color: '#81c784' },
    { key: 'ocean_sink', label: 'Ocean sink', value: b.ocean_sink_gt_co2_yr, color: '#64b5f6' },
    { key: 'atmosphere', label: 'Stays in the air', value: b.atmosphere_gt_co2_yr, color: '#e0e0e0' },
  ]
  return { rows, max: Math.max(...rows.map(r => Math.abs(r.value))), airborne: b.airborne_fraction, growth: b.growth_ppm_yr, northSouth: b.north_minus_south_ppm, tcre: b.tcre_k_per_1000_gt, cumulative: b.cumulative_total_gt_co2, warmingFromCumulative: b.warming_from_cumulative_k, per100: b.transient_warming_per_100ppm_k }
}

// ---- Couplings -----------------------------------------------------------------------------------------------------

// The components of the coupled Earth and where they sit in the diagram (a 100 x 62 box).
export const NODES = [
  { key: 'sun', label: 'Sun', x: 10, y: 8, color: '#ffd166' },
  { key: 'space', label: 'Space (wind, rays, dust)', x: 38, y: 6, color: '#7fe0e0' },
  { key: 'upper_atmosphere', label: 'Upper atmosphere', x: 70, y: 6, color: '#9fb8ff' },
  { key: 'carbon', label: 'Carbon (CO₂)', x: 10, y: 27, color: '#ef5350' },
  { key: 'atmosphere', label: 'Atmosphere', x: 42, y: 25, color: '#e8f4ff' },
  { key: 'water_vapour_clouds', label: 'Water vapour & clouds', x: 78, y: 22, color: '#b0c4de' },
  { key: 'weather', label: 'Weather (forecast)', x: 22, y: 46, color: '#c79bff' },
  { key: 'land', label: 'Land & soils', x: 8, y: 59, color: '#9fcf7a' },
  { key: 'ocean', label: 'Ocean', x: 42, y: 54, color: '#64b5f6' },
  { key: 'ice', label: 'Ice', x: 63, y: 40, color: '#e0f7ff' },
  { key: 'sea_level', label: 'Sea level', x: 67, y: 59, color: '#4dd0e1' },
  { key: 'extremes', label: 'Extremes', x: 89, y: 42, color: '#ff8a65' },
]
const STRENGTH = { dominant: 1, strong: 0.75, moderate: 0.5, regional: 0.4, weak: 0.25, negligible: 0.1, hypothetical: 0.1 }
export const strengthWeight = s => STRENGTH[String(s).split(' ')[0]] ?? 0.3
export function couplingGraph(block) {
  const node = key => NODES.find(n => n.key === key)
  const edges = new Map()
  for (const c of block.couplings) {
    const a = node(c.from), b = node(c.to)
    if (!a || !b) continue
    const id = `${c.from}>${c.to}`
    const e = edges.get(id) ?? { from: a, to: b, weight: 0, links: [] }
    e.weight = Math.max(e.weight, strengthWeight(c.strength))
    e.links.push(c)
    edges.set(id, e)
  }
  const used = new Set([...edges.values()].flatMap(e => [e.from.key, e.to.key]))
  return { nodes: NODES.filter(n => used.has(n.key)), edges: [...edges.values()].sort((a, b) => a.weight - b.weight) }
}
// A gently curved arrow between two nodes, shortened at both ends.
export function edgePath(e) {
  const dx = e.to.x - e.from.x, dy = e.to.y - e.from.y, len = Math.hypot(dx, dy) || 1
  const ux = dx / len, uy = dy / len, r = 3.6
  const x1 = e.from.x + ux * r, y1 = e.from.y + uy * r, x2 = e.to.x - ux * r, y2 = e.to.y - uy * r
  const mx = (x1 + x2) / 2 - uy * len * 0.12, my = (y1 + y2) / 2 + ux * len * 0.12
  return { d: `M${x1.toFixed(2)},${y1.toFixed(2)} Q${mx.toFixed(2)},${my.toFixed(2)} ${x2.toFixed(2)},${y2.toFixed(2)}`, labelX: mx, labelY: my }
}

// ---- Key answers ---------------------------------------------------------------------------------------------------

export function keyAnswers(climate, forecast) {
  const out = []
  if (climate) {
    const central = climate.central_scenario, today = driverBars(climate, central, 'today')
    const warming = today.rows.filter(r => r.warming_k > 0).slice(0, 3), cooling = today.rows.filter(r => r.warming_k < 0)[0]
    out.push({ key: 'warming', label: 'Warming today', value: `${climate.climate.warming_today_k.toFixed(2)} K`, detail: `since ${climate.reference[0]}–${climate.reference[1]}, ${climate.today}` })
    out.push({ key: 'driver', label: 'Biggest driver', value: `${warming[0].label} +${warming[0].warming_k.toFixed(2)} K`, detail: `then ${warming.slice(1).map(r => `${r.label.split(' (')[0]} +${r.warming_k.toFixed(2)}`).join(', ')}${cooling ? `; ${cooling.label.split(' (')[0]} ${cooling.warming_k.toFixed(2)} K` : ''}` })
    const end = climate.scenarios.map(s => `${climate.series[s.key].global_k.at(-1).toFixed(1)}`).join(' / ')
    out.push({ key: 'future', label: `Warming ${climate.years.at(-1)}`, value: `${end} K`, detail: climate.scenarios.map(s => s.name.replace(/ \(about (.*)\)/, ' ($1)')).join(' / ') })
    const b = climate.co2.budget, top = climate.co2.regions[0], hot = climate.co2.hotspots[0]
    out.push({ key: 'co2', label: 'CO₂ today', value: `${(b.fossil_gt_co2_yr + b.land_use_gt_co2_yr).toFixed(1)} Gt/yr`, detail: `${Math.round(b.airborne_fraction * 100)} % stays in the air (+${b.growth_ppm_yr.toFixed(1)} ppm/yr); largest emitter ${top.name}; hit hardest: ${hot.name}` })
  }
  if (forecast) {
    const lead = usefulLead(forecast), m = warningMatrix(forecast)
    const red = m.filter(d => d.hazards.some(h => h.level === 3)).map(d => d.day)
    out.push({ key: 'lead', label: 'Forecast skill', value: `${Math.round(lead.hours / 24 * 10) / 10} days`, detail: `useful lead (model world; ${forecast.members} members)` })
    const worst = [...forecast.warnings.regions].sort((a, b) => b.level - a.level || b.area_km2 - a.area_km2)[0]
    out.push({ key: 'warnings', label: 'Strongest warning', value: worst ? `${LEVELS[worst.level].label} · ${hazardInfo(worst.hazard).label}` : 'none', detail: worst ? `day ${worst.day}${worst.cities.length ? `: ${worst.cities.join(', ')}` : ` near ${worst.nearest_city}`}; red on day${red.length === 1 ? '' : 's'} ${red.join(', ') || '—'}` : '' })
  }
  return out
}

// Marching squares on a grid field (cell centres), in fractional cells from the south-west corner.
export function contourLines(values, nx, ny, level) {
  const segs = []
  const at = (x, y) => values[y * nx + x]
  for (let y = 0; y + 1 < ny; y++) for (let x = 0; x + 1 < nx; x++) {
    const c = [[x, y], [x + 1, y], [x + 1, y + 1], [x, y + 1]], v = c.map(([a, b]) => at(a, b))
    if (!v.every(finite)) continue
    const code = v.reduce((s, val, i) => s | (val >= level ? 1 << i : 0), 0)
    if (code === 0 || code === 15) continue
    const edge = i => { const j = (i + 1) % 4, t = v[j] === v[i] ? 0.5 : (level - v[i]) / (v[j] - v[i]); return [c[i][0] + 0.5 + (c[j][0] - c[i][0]) * t, c[i][1] + 0.5 + (c[j][1] - c[i][1]) * t] }
    const table = { 1: [[3, 0]], 2: [[0, 1]], 3: [[3, 1]], 4: [[1, 2]], 5: [[3, 2], [0, 1]], 6: [[0, 2]], 7: [[3, 2]], 8: [[2, 3]], 9: [[0, 2]], 10: [[0, 3], [1, 2]], 11: [[1, 2]], 12: [[1, 3]], 13: [[0, 1]], 14: [[3, 0]] }
    for (const [a, b] of table[code]) segs.push([edge(a), edge(b)])
  }
  return segs
}

export const fmt = (v, d = 1) => (v === null || v === undefined || !finite(v) ? '—' : v.toFixed(d))
export const signed = (v, d = 2) => (v === null || v === undefined || !finite(v) ? '—' : `${v >= 0 ? '+' : ''}${v.toFixed(d)}`)
