// The weather at a glance: a symbol for what it is like at a point (sun, cloud, rain, showers, thunderstorm, snow,
// fog...) and the layers of the air above it (freezing level, cloud layer, icing band, thunderstorm column, turbulent
// layers), in feet above mean sea level. The flight weather view stacks these layers over the map; the symbols stand
// on the map and on the aerodrome cards. Pure functions over engine output (aviation.js, windsAloft.js).
import { FT_PER_M, elevationAt, cloudLayer, presentWeather, cellLocation, validTime } from './station.js'
import { flightConditions, icingBand } from './aviation.js'
import { CAT_SHEAR_KT_PER_1000FT, LAPSE_K_PER_M } from './windsAloft.js'

export const SYMBOLS = {
  clear: { label: 'Clear' }, few: { label: 'A few clouds' }, partly: { label: 'Partly cloudy' }, cloudy: { label: 'Cloudy' }, overcast: { label: 'Overcast' },
  drizzle: { label: 'Drizzle' }, rain: { label: 'Rain' }, showers: { label: 'Showers' }, thunder: { label: 'Thunderstorm' }, hail: { label: 'Thunderstorm with hail' },
  snow: { label: 'Snow' }, sleet: { label: 'Sleet' }, fog: { label: 'Fog' }, mist: { label: 'Mist' },
}

// The symbol of a point, in order of what matters most: thunder (with hail), what falls, fog or mist, then the sky.
// null when the run says nothing about the sky or the weather there (no cloud and no precipitation model, no humidity).
export function weatherSymbol(frame, i) {
  const c = flightConditions(frame, i), wx = c.wx, layer = c.cloud.layer
  if (wx.thunder) return wx.hail >= 0.5 ? 'hail' : 'thunder'
  if (wx.kind === 'snow') return 'snow'
  if (wx.kind === 'sleet') return 'sleet'
  if (wx.kind === 'drizzle') return 'drizzle'
  if (wx.kind) return wx.showers ? 'showers' : 'rain'
  if (c.vis?.obscuration === 'fog') return 'fog'
  if (c.vis?.obscuration === 'mist') return 'mist'
  if (!c.cloud.modelled) return wx.modelled ? 'clear' : null
  if (!layer) return 'clear'
  return { FEW: 'few', SCT: 'partly', BKN: 'cloudy', OVC: 'overcast' }[layer.cover] ?? 'cloudy'
}

// The column of air over a point, in feet above mean sea level:
//   freezingFt: the 0 degC level (the precipitation kernel's freezing level, else the surface temperature at 6.5 K/km);
//   cloud: { baseFt, topFt, genus, cover, convective } or null; icing: the cloud's 0 to -20 degC band or null;
//   thunder: { topFt } (cumulonimbus or lightning: a column from the ground to the cloud top) or null;
//   turbulence: [{ fromFt, toFt, shear }] between winds-aloft levels where the shear reaches 6 kt per 1 000 ft.
export function weatherColumn(frame, i, aloft = null) {
  const elevFt = Math.max(0, elevationAt(frame, i) ?? 0) * FT_PER_M, surfaceC = frame.forecast.fields.air_temperature.values[i] - 273.15
  const wx = presentWeather(frame, i), layer = cloudLayer(frame, i).layer
  const freezingFt = Number.isFinite(wx.freezingLevel) ? elevFt + wx.freezingLevel * FT_PER_M : elevFt + Math.max(0, surfaceC / LAPSE_K_PER_M) * FT_PER_M
  let cloud = null, icing = null, thunder = null
  if (layer) {
    const baseFt = elevFt + layer.base * FT_PER_M, topFt = layer.top === null ? baseFt + 1000 : elevFt + layer.top * FT_PER_M
    cloud = { baseFt, topFt, genus: layer.genus, cover: layer.cover, convective: layer.convective, abbreviation: layer.abbreviation }
    const band = icingBand(layer, surfaceC, wx)
    if (band) icing = { fromFt: elevFt + band.fromFt, toFt: elevFt + band.toFt, severity: band.severity }
  }
  if (wx.thunder || layer?.convective === 'CB') thunder = { topFt: cloud ? cloud.topFt : elevFt + 35000 }
  const turbulence = []
  if (aloft?.available) {
    const rows = aloft.profile(i).rows
    rows.forEach((r, k) => { const up = rows[k + 1]; if (up && r.shearKtPer1000ft !== null && r.shearKtPer1000ft >= CAT_SHEAR_KT_PER_1000FT) turbulence.push({ fromFt: Math.max(elevFt, r.level.ft), toFt: up.level.ft, shear: r.shearKtPer1000ft }) })
  }
  return { elevFt, freezingFt, cloud, icing, thunder, turbulence }
}

// Cloud heights in three families for colour: low (base below 6 500 ft AGL), middle (to 20 000 ft), high.
export const cloudFamily = (cloud, elevFt = 0) => (cloud.convective === 'CB' ? 'cb' : cloud.baseFt - elevFt < 6500 ? 'low' : cloud.baseFt - elevFt < 20000 ? 'middle' : 'high')
export const LAYER_COLOURS = { low: '#c9d6df', middle: '#9fb8c9', high: '#eef5fa', cb: '#5e6b78', icing: '#4fa8ff', turbulence: '#ffb15c', thunder: '#ff4d4d', freezing: '#7fe0ff' }

// ---- Day and night: the symbols show a moon instead of the sun when the sun is below the horizon --------------------
// Solar elevation (degrees) at a position and time: the NOAA approximation (declination and equation of time from the
// fractional year), good to a fraction of a degree - enough to tell day from night.
export function solarElevation(lat, lon, ms) {
  const d = new Date(ms), start = Date.UTC(d.getUTCFullYear(), 0, 1), day = (ms - start) / 86400000
  const g = 2 * Math.PI / 365 * (day - 0.5)
  const eqt = 229.18 * (0.000075 + 0.001868 * Math.cos(g) - 0.032077 * Math.sin(g) - 0.014615 * Math.cos(2 * g) - 0.040849 * Math.sin(2 * g))
  const decl = 0.006918 - 0.399912 * Math.cos(g) + 0.070257 * Math.sin(g) - 0.006758 * Math.cos(2 * g) + 0.000907 * Math.sin(2 * g) - 0.002697 * Math.cos(3 * g) + 0.00148 * Math.sin(3 * g)
  const minutes = d.getUTCHours() * 60 + d.getUTCMinutes() + d.getUTCSeconds() / 60
  const hourAngle = ((minutes + eqt + 4 * lon) / 4 - 180) * Math.PI / 180, phi = lat * Math.PI / 180
  return Math.asin(Math.sin(phi) * Math.sin(decl) + Math.cos(phi) * Math.cos(decl) * Math.cos(hourAngle)) * 180 / Math.PI
}
// Night at a cell of a frame: the sun more than 0.833 degrees below the horizon (sunset / sunrise).
export function isNight(frame, i) {
  const { lat, lon } = cellLocation(frame.forecast.grid, i)
  return solarElevation(lat, lon, validTime(frame)) < -0.833
}
