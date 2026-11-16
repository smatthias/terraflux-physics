// Data for the 3D maps of the Coupled Earth workspace (EarthScene.vue): what the coupled forecast and climate runs
// computed, arranged for drawing. Pure functions.
//   - the forecast domain (52 x 32 cells of 210 km) as terrain, with clouds, rain, isobars, winds, pressure systems,
//     cities, warnings and the hurricane;
//   - the climate grid (4 degree cells) as a globe with the warming or the CO2 fields, and the CO2 fluxes as arrows.
import { contourLines } from './coupledEarth.js'

// Cloud forms from the forecast's cloud cover, precipitation and thunder probability (the forecast reports no cloud
// genus or heights): thunder likely -> cumulonimbus to 11 km; overcast and raining -> nimbostratus; mostly covered ->
// stratocumulus; else fair-weather cumulus. Heights are typical for each form. Cells under 25 % cover stay clear.
export function cloudForms({ cover, precip, thunder }) {
  return cover.map((c, i) => {
    if (!(c >= 25)) return null
    const t = thunder?.[i] ?? 0, p = precip?.[i] ?? 0
    if (t >= 40) return { type: 'cumulonimbus', base: 900, top: 11000, fraction: Math.min(1, c / 100) }
    if (c >= 80 && p >= 1) return { type: 'nimbostratus', base: 700, top: 4500, fraction: Math.min(1, c / 100) }
    if (c >= 65) return { type: 'stratocumulus', base: 800, top: 1800, fraction: Math.min(1, c / 100) }
    return { type: 'cumulus', base: 1000, top: 2600, fraction: Math.min(1, c / 100) }
  })
}

// Isobars every `step` hPa as segments in grid coordinates (cell centres at integer + 0.5 after the shift below),
// with the level; major lines every 2 steps.
export function isobars(pressure, nx, ny, step = 4) {
  const lo = Math.ceil(Math.min(...pressure) / step) * step, hi = Math.max(...pressure), out = []
  for (let level = lo; level <= hi; level += step) {
    const segments = contourLines(pressure, nx, ny, level)
    if (segments.length) out.push({ level, major: level % (2 * step) === 0, segments })
  }
  return out
}

// A track point at any hour: linear between the reported hours (position, wind), the category of the earlier point.
export function trackAt(points, hour) {
  if (!points?.length || hour < points[0].hour || hour > points.at(-1).hour) return null
  const k = points.findIndex(p => p.hour >= hour)
  if (points[k].hour === hour) return { ...points[k] }
  const a = points[k - 1], b = points[k], f = (hour - a.hour) / ((b.hour - a.hour) || 1)
  return { hour, lat: a.lat + (b.lat - a.lat) * f, lon: a.lon + (b.lon - a.lon) * f, wind: a.wind + (b.wind - a.wind) * f, category: a.category }
}

// The gale radius (cells) of a storm at grid position (x, y): the farthest cell within `reach` cells where the wind
// reaches gale (17 m/s), walking out from the centre in the forecast's own wind field.
export function galeRadius(speed, nx, ny, x, y, { gale = 17, reach = 5 } = {}) {
  let r = 0
  for (let j = Math.floor(y - reach); j <= Math.ceil(y + reach); j++) for (let i = Math.floor(x - reach); i <= Math.ceil(x + reach); i++) {
    if (i < 0 || j < 0 || i >= nx || j >= ny) continue
    const d = Math.hypot(i + 0.5 - x, j + 0.5 - y)
    if (d <= reach && speed[j * nx + i] >= gale && d > r) r = d
  }
  return r
}

// Cells at or above a warning level (for the warning curtains).
export const levelMask = (levels, min) => Uint8Array.from(levels, l => (l >= min ? 1 : 0))

// CO2 fluxes as arrows: sources (positive net flux) rise, sinks sink; one arrow per `stride` cells where the flux is at
// least `min` (t/km2/yr), with a strength 0..1 on a log scale.
export function fluxArrows(net, nx, ny, { stride = 2, min = 20 } = {}) {
  const out = [], big = Math.max(min * 10, ...net.map(Math.abs))
  for (let j = 0; j < ny; j += stride) for (let i = 0; i < nx; i += stride) {
    const k = j * nx + i, v = net[k]
    if (!(Math.abs(v) >= min)) continue
    out.push({ i: k, up: v > 0, strength: Math.min(1, Math.log(Math.abs(v) / min + 1) / Math.log(big / min + 1)) })
  }
  return out
}

// Unit direction of latitude / longitude on the globe: north up (+y), 0 degrees longitude towards the viewer (+z).
export function globeDir(lat, lon) {
  const a = lat * Math.PI / 180, b = lon * Math.PI / 180
  return [Math.cos(a) * Math.sin(b), Math.sin(a), Math.cos(a) * Math.cos(b)]
}

// The next value of an animation index that steps through n frames (loops).
export const nextFrame = (k, n) => (n > 0 ? (k + 1) % n : 0)

// The storm of the ensemble at an hour: the mean position and wind of the members that still carry it, and the most
// common category among them (the forecast's storm; the truth run is only for verification).
export function ensembleStorm(tracks, hour) {
  const at = tracks.filter(t => !t.truth).map(t => trackAt(t.points, hour)).filter(Boolean)
  if (!at.length) return null
  const mean = key => at.reduce((a, p) => a + p[key], 0) / at.length
  const counts = {}
  for (const p of at) counts[p.category] = (counts[p.category] ?? 0) + 1
  const category = Object.entries(counts).sort((a, b) => b[1] - a[1])[0][0]
  return { hour, lat: mean('lat'), lon: mean('lon'), wind: mean('wind'), category, members: at.length }
}

// The frame whose hour is closest to `hour`.
export function nearestFrame(hours, hour) {
  let best = 0
  hours.forEach((h, k) => { if (Math.abs(h - hour) < Math.abs(hours[best] - hour)) best = k })
  return best
}

// Whether the cloud of a cell is drawn: a deterministic share of the cells that grows with the cover, from none at
// 30 % (scattered fair-weather clouds leave the map readable) to all cells at 90 % (overcast).
export function cloudDrawn(i, cover) {
  if (!(cover >= 35)) return false
  const h = Math.sin((i + 1) * 12.9898) * 43758.5453
  return h - Math.floor(h) < Math.min(1, (cover - 30) / 60)
}
