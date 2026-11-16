// Station diagnostics shared by the flight weather report (aviation.js) and the sailing report (sailing.js): what the
// engine frame says at one grid cell, in the units and words reports need. Pure functions: no Vue, no three.js.
//
// Everything here reads engine output. Where the engine has no such quantity (visibility), the estimate is marked
// `estimated: true` and its method is documented in docs/weather/flight-sailing-reports.md. Where the inputs are
// missing, the value is null: the reports then print solidi ("////") rather than a made-up number.
import { cycloneWind } from './waves.js'

export const KT_PER_MS = 1.9438445          // knots per m/s
export const FT_PER_M = 3.2808399
export const M_PER_SM = 1609.344            // statute mile
export const M_PER_NM = 1852                // nautical mile
export const EARTH_RADIUS_M = 6371008.8     // as in observation_grid.cpp (the grid's projection)
export const HPA = 100

const finite = v => typeof v === 'number' && Number.isFinite(v)
export const finiteOr = (v, fallback) => (finite(v) ? v : fallback)
const at = (array, i) => (array && finite(array[i]) ? array[i] : null)
export const norm360 = deg => ((deg % 360) + 360) % 360
// Signed angle a - b in (-180, 180].
export const angleDiff = (a, b) => { const d = norm360(a - b); return d > 180 ? d - 360 : d }

// ---- Where and when --------------------------------------------------------------------------------------------------
// Cell centre in the grid's local azimuthal-equidistant frame (m) and its latitude/longitude by the exact spherical
// inverse about the grid's geographic anchor (the projection of observation_grid.cpp).
export function cellLocation(grid, i) {
  const x = i % grid.nx, y = Math.floor(i / grid.nx)
  const east = grid.origin_east_m + (x + 0.5) * grid.spacing_east_m, north = grid.origin_north_m + (y + 0.5) * grid.spacing_north_m
  return { x, y, east, north, ...inverseProject(grid, east, north) }
}
// Latitude/longitude of a point east/north (m) of the grid's anchor: the inverse of the engine's projection.
export function inverseProject(grid, east, north) {
  const lat0 = grid.geographic_anchor_latitude_deg * Math.PI / 180, lon0 = grid.geographic_anchor_longitude_deg * Math.PI / 180
  const rho = Math.hypot(east, north), c = rho / EARTH_RADIUS_M
  let lat = lat0, lon = lon0
  if (rho > 1e-6) {
    lat = Math.asin(Math.cos(c) * Math.sin(lat0) + north * Math.sin(c) * Math.cos(lat0) / rho)
    lon = lon0 + Math.atan2(east * Math.sin(c), rho * Math.cos(lat0) * Math.cos(c) - north * Math.sin(lat0) * Math.sin(c))
  }
  return { lat: lat * 180 / Math.PI, lon: ((lon * 180 / Math.PI + 540) % 360) - 180 }
}

export function formatLatLon({ lat, lon }) {
  const f = (v, pos, neg) => `${Math.abs(v).toFixed(2)}° ${v < 0 ? neg : pos}`
  return `${f(lat, 'N', 'S')} ${f(lon, 'E', 'W')}`
}

// The valid time of a frame (ms since the epoch): start + elapsed.
export const validTime = frame => Date.parse(frame.forecast.start_time_utc) + frame.forecast.elapsed_seconds * 1000

// The centre cell of the grid: the default station before anything is picked.
export const centreCell = grid => Math.floor(grid.ny / 2) * grid.nx + Math.floor(grid.nx / 2)

// ---- Wind ------------------------------------------------------------------------------------------------------------
// The surface (10 m) wind at a cell: the engine's wind field, or inside a tropical cyclone the storm's wind where it is
// stronger than the background, blowing around the eye (the same rule the wave field uses, waves.js).
// fromDeg: the direction the wind blows FROM, degrees true (meteorological convention). cyclone: the storm whose wind
// it is (or null); how dangerous that is depends on the speed here, not on the storm's peak (see cycloneHazard).
export function surfaceWind(frame, i) {
  const f = frame.forecast.fields, g = frame.forecast.grid
  let u = f.wind_east.values[i], v = f.wind_north.values[i], source = 'engine wind field', cyclone = null
  const storms = frame.storms?.list, stormSpeed = frame.storms?.env?.storm_wind_speed_m_s
  if (storms?.length && stormSpeed && stormSpeed[i] > Math.hypot(u, v)) {
    const { east, north } = cellLocation(g, i)
    let best = storms[0], bestR = Infinity
    for (const s of storms) { const r = Math.hypot(east - s.east_m, north - s.north_m); if (r < bestR) { bestR = r; best = s } }
    ;[u, v] = cycloneWind(east, north, best, stormSpeed[i], g.geographic_anchor_latitude_deg >= 0)
    source = best.id !== undefined ? `tropical cyclone #${best.id}` : 'tropical cyclone'
    cyclone = best
  }
  const speed = Math.hypot(u, v)
  return { u, v, speed, kt: speed * KT_PER_MS, fromDeg: speed > 1e-9 ? norm360(Math.atan2(-u, -v) * 180 / Math.PI) : 0, source, cyclone }
}

// Beaufort force by wind speed (m/s upper limits, WMO; the same classes as the inspector's explain.js).
const BEAUFORT = [[0.5, 'calm'], [1.6, 'light air'], [3.4, 'light breeze'], [5.5, 'gentle breeze'], [8, 'moderate breeze'], [10.8, 'fresh breeze'], [13.9, 'strong breeze'], [17.2, 'near gale'], [20.8, 'gale'], [24.5, 'strong gale'], [28.5, 'storm'], [32.7, 'violent storm'], [Infinity, 'hurricane force']]
export function beaufort(speedMs) {
  const force = BEAUFORT.findIndex(([limit]) => speedMs < limit)
  return { force, label: BEAUFORT[force][1] }
}

// The tropical-cyclone hazard at a point by the storm's wind there: tropical-storm force from 34 kt (severe), its outer
// circulation below (caution). null outside any storm.
export function cycloneHazard(wind) {
  const s = wind.cyclone
  if (!s) return null
  const name = s.id !== undefined ? `tropical cyclone #${s.id}` : 'a tropical cyclone', peak = Number.isFinite(s.max_wind_m_s) ? `, ${Math.round(s.max_wind_m_s * KT_PER_MS)} kt at its core` : ''
  const category = s.category ? ` (${String(s.category).replace(/_/g, ' ')}${peak})` : peak ? ` (${peak.slice(2)})` : ''
  return wind.kt >= 34
    ? { level: 'severe', text: `Inside ${name}${category}: ${Math.round(wind.kt)} kt here` }
    : { level: 'caution', text: `Outer circulation of ${name}${category}: ${Math.round(wind.kt)} kt here; stay clear of its track` }
}

const POINTS16 = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW']
export const compass16 = deg => POINTS16[Math.round(norm360(deg) / 22.5) % 16]

// ---- Temperature, humidity, pressure -----------------------------------------------------------------------------
// Saturation vapour pressure (Pa) and its inverse for the dew point, with the engine's Magnus coefficients
// (Alduchov & Eskridge 1996; include/terraflux/weather/moist_air.hpp).
export const saturationVapourPressure = tK => 610.94 * Math.exp(17.625 * (tK - 273.15) / (tK - 273.15 + 243.04))
export function dewPoint(tK, rhPct) {
  if (!finite(tK) || !finite(rhPct) || rhPct <= 0) return null
  const t = tK - 273.15, g = Math.log(Math.min(rhPct, 100) / 100) + 17.625 * t / (243.04 + t)
  return 243.04 * g / (17.625 - g) + 273.15
}

// Relative humidity (%) at the cell: the coupled engine state, else the synthetic atmosphere display, else null.
export const relativeHumidity = (frame, i) => at(frame.coupled?.humidity, i) ?? at(frame.atmosphere?.humidity, i)

// Terrain elevation (m) at the cell when the run carries terrain.
export const elevationAt = (frame, i) => at(frame.terrain?.values, i)

// QNH (Pa): the coupled pressure field is the prescribed sea-level pressure; the synthetic display carries hPa.
// Station pressure: the orography kernel's terrain surface pressure, else QNH reduced to the terrain height by the ISA
// barometric law, else QNH itself (flat sea-level field).
// Inside a tropical cyclone the storm kernel's pressure anomaly is added, so the barometer falls as a storm comes.
export function pressures(frame, i) {
  const base = at(frame.coupled?.pressure, i) ?? (at(frame.atmosphere?.pressure, i) !== null ? frame.atmosphere.pressure[i] * HPA : null)
  if (base === null) return { qnh: null, station: null, stationSource: null, stormAnomaly: 0 }
  const stormAnomaly = at(frame.storms?.env?.storm_pressure_anomaly_pa, i) ?? 0, qnh = base + stormAnomaly
  const sp = at(frame.orography?.surfacePressure, i)
  if (sp !== null) return { qnh, station: sp + stormAnomaly, stationSource: 'terrain surface pressure (orography kernel)', stormAnomaly }
  const h = elevationAt(frame, i)
  if (h !== null && h > 0) return { qnh, station: qnh * (1 - 2.25577e-5 * h) ** 5.25588, stationSource: 'QNH reduced to the terrain height (ISA)', stormAnomaly }
  return { qnh, station: qnh, stationSource: 'sea-level field', stormAnomaly }
}

// ---- Cloud -----------------------------------------------------------------------------------------------------------
// The engine's single cloud layer at the cell. { modelled: false } when the run has no cloud model (nothing may be said
// about the sky); { modelled: true, layer: null } when the sky is clear; otherwise the layer with METAR coverage.
const GENUS = { cumulonimbus: 'Cb', towering_cumulus: 'TCu', cumulus: 'Cu', stratocumulus: 'Sc', stratus: 'St', nimbostratus: 'Ns', altocumulus: 'Ac', altostratus: 'As', cirrus: 'Ci', cirrostratus: 'Cs' }
export const coverOf = oktas => (oktas <= 0 ? null : oktas <= 2 ? 'FEW' : oktas <= 4 ? 'SCT' : oktas <= 7 ? 'BKN' : 'OVC')
export function cloudLayer(frame, i) {
  const c = frame.clouds
  if (!c) return { modelled: false, layer: null }
  const type = c.type[i]
  if (!type || type === 'none' || !finite(c.base[i]) || !(c.fraction[i] > 0)) return { modelled: true, layer: null }
  const oktas = Math.max(1, Math.min(8, Math.round(c.fraction[i] * 8)))
  return {
    modelled: true,
    layer: {
      genus: type, abbreviation: GENUS[type] ?? type, base: c.base[i], top: finite(c.top[i]) ? c.top[i] : null, fraction: c.fraction[i], oktas, cover: coverOf(oktas),
      convective: type === 'cumulonimbus' ? 'CB' : type === 'towering_cumulus' ? 'TCU' : null, thunderstorm: !!c.thunderstorm?.[i],
    },
  }
}

// ---- Present weather -------------------------------------------------------------------------------------------------
// What falls at the cell, as a neutral description (aviation.js turns it into METAR codes, sailing.js into words).
// { modelled: false } without a precipitation model. Sleet is the engine's mixed phase (rain and snow together).
export function presentWeather(frame, i) {
  const p = frame.precipitation, cloud = cloudLayer(frame, i).layer
  const thunder = !!(p?.lightning?.[i] || cloud?.thunderstorm)
  if (!p) return { modelled: false, kind: null, intensity: 'none', rate: 0, showers: false, thunder, hail: 0, freezing: false, virga: false }
  const type = p.type[i] ?? 'none', rate = at(p.rate, i) ?? 0
  const kind = type === 'none' ? null : type.replace('_showers', '')
  const tC = frame.forecast.fields.air_temperature.values[i] - 273.15
  return {
    modelled: true, kind, intensity: p.intensity[i] ?? 'none', rate, showers: type.endsWith('_showers'), thunder,
    hail: at(p.hail, i) ?? 0, freezing: !!kind && (kind === 'rain' || kind === 'drizzle') && tC < 0, virga: !!p.virga?.[i],
    freezingLevel: at(p.freezingLevel, i), baseTemperature: at(p.baseTemperature, i), topTemperature: at(p.topTemperature, i), updraft: at(p.updraft, i),
  }
}

// Present weather in plain words ("heavy rain showers with thunder and hail", "mist", "dry"); null when the run has no
// precipitation model and no obscuration is estimated (nothing may be said).
const KIND_WORDS = { rain: 'rain', drizzle: 'drizzle', snow: 'snow', sleet: 'sleet (rain and snow)' }
export function weatherWords(wx, visibility = null) {
  const parts = []
  if (wx?.kind) {
    const intensity = wx.intensity === 'violent' ? 'violent' : wx.intensity === 'heavy' ? 'heavy' : wx.intensity === 'light' ? 'light' : 'moderate'
    let text = `${intensity} ${wx.freezing ? 'freezing ' : ''}${KIND_WORDS[wx.kind] ?? wx.kind}${wx.showers ? ' showers' : ''}`
    const extra = [wx.thunder ? 'thunder' : null, wx.hail > 0 ? 'hail' : null].filter(Boolean)
    if (extra.length) text += ` with ${extra.join(' and ')}`
    parts.push(text)
  } else if (wx?.thunder) parts.push('thunderstorm without precipitation at the surface')
  if (visibility?.obscuration) parts.push(visibility.obscuration)
  if (parts.length) return parts.join(', ')
  return wx?.modelled ? 'dry' : null
}

// ---- Visibility (estimated: the engine has no visibility) ---------------------------------------------------------
// Koschmieder's law V = 3.912 / sigma (2 % contrast threshold), with the extinction sigma (1/km) summed from
//   - clean air:        sigma0 = 0.1 (about 39 km);
//   - humid haze/mist:  sigma0 ((1 - 0.85) / (1 - RH))^1.5 above 85 % RH (hygroscopic growth; RH capped at 99.5 %);
//   - fog:              a cloud base at or below 30 m (the cloud touches the ground): 3.912 / 0.2 km;
//   - precipitation:    a power law a R^b in the rate R (mm/h): rain 0.25 R^0.63, drizzle 0.55 R^0.63, snow 1.6 R^0.70,
//                       sleet (rain and snow) 0.9 R^0.66.
// Illustrative empirical coefficients, not calibrated to observations. null when neither humidity nor precipitation is
// known (nothing to base an estimate on).
export const VIS_CLEAR_KM = 3.912 / 0.1
const PRECIP_EXTINCTION = { rain: [0.25, 0.63], drizzle: [0.55, 0.63], snow: [1.6, 0.7], sleet: [0.9, 0.66] }
export function estimateVisibility({ rh = null, weather = null, cloudBase = null } = {}) {
  const hasPrecip = !!weather?.modelled
  if (rh === null && !hasPrecip && cloudBase === null) return null
  const parts = { air: 0.1, humidity: 0, fog: 0, precipitation: 0 }
  if (rh !== null && rh > 85) parts.humidity = 0.1 * ((0.15 / (1 - Math.min(rh, 99.5) / 100)) ** 1.5) - 0.1
  if (cloudBase !== null && cloudBase <= 30) parts.fog = 3.912 / 0.2
  if (weather?.kind && weather.rate > 0) { const [a, b] = PRECIP_EXTINCTION[weather.kind] ?? PRECIP_EXTINCTION.rain; parts.precipitation = a * weather.rate ** b }
  const sigma = parts.air + parts.humidity + parts.fog + parts.precipitation
  const metres = 3912 / sigma
  // The obscuration, if any: fog or mist when the humidity (or the fog) is the main cause.
  const moist = parts.humidity + parts.fog, cause = moist >= parts.precipitation ? 'humidity' : 'precipitation'
  const obscuration = metres < 1000 && cause === 'humidity' ? 'fog' : metres < 5000 && cause === 'humidity' && (rh ?? 0) >= 80 ? 'mist' : null
  return { metres, estimated: true, parts, cause: metres >= 10000 ? null : cause, obscuration, basis: [rh !== null ? 'humidity' : null, hasPrecip ? 'precipitation' : null, cloudBase !== null ? 'cloud base' : null].filter(Boolean) }
}

// ---- Pressure tendency over about 3 h ------------------------------------------------------------------------------
// From the timeline: the frame nearest to 3 h before the current one (or the previous frame if the run is coarser),
// scaled to hPa per 3 h. null at the first frame or without a pressure field.
export function pressureTendency(frames, index, i) {
  const now = frames[index], p1 = now ? pressures(now, i).qnh : null
  if (p1 === null || index < 1) return null
  const t1 = now.forecast.elapsed_seconds
  // The frame whose age is closest to 3 h, among those at most 4.5 h old; the previous frame if none is that recent.
  let best = index - 1, bestMiss = Infinity
  for (let k = index - 1; k >= 0; k--) {
    const age = t1 - frames[k].forecast.elapsed_seconds
    if (age > 3 * 3600 * 1.5) break
    if (Math.abs(age - 3 * 3600) < bestMiss) { bestMiss = Math.abs(age - 3 * 3600); best = k }
  }
  const before = frames[best], p0 = pressures(before, i).qnh, hours = (t1 - before.forecast.elapsed_seconds) / 3600
  if (p0 === null || !(hours > 0)) return null
  // The coupled model prescribes a pressure field that does not change in time: a "steady" barometer would only repeat
  // that assumption, so it is reported as not modelled (storm anomalies do change it).
  const fixed = !!now.coupled && before.coupled?.pressure === now.coupled.pressure && p1 === p0
  return { hPaPer3h: (p1 - p0) / HPA * 3 / hours, basisHours: hours, static: fixed }
}

// Per-frame values of a function of (frame, i) over the whole run: the trend strips.
export const stationSeries = (frames, i, fn) => frames.map(f => fn(f, i))
