// Geodesy for flight planning by coordinates: parsing and formatting latitude/longitude, great-circle distance,
// courses and intermediate points on the sphere, and the forward projection of a coordinate onto the run's grid - the
// spherical azimuthal-equidistant projection of the engine (src/weather/observation_grid.cpp), with its cell rule.
// Pure functions.
import { EARTH_RADIUS_M, M_PER_NM, norm360 } from './station.js'

const RAD = Math.PI / 180

// ---- Parsing ---------------------------------------------------------------------------------------------------------
// Accepts the usual ways of writing a position (latitude first):
//   decimal degrees            "46.95, 7.45"   "-37.8 144.9"   "46.95N 7.45E"
//   degrees and minutes        "46°57.0'N 007°27.0'E"   "N46 57.0 E7 27.0"
//   degrees, minutes, seconds  "46°57'00\"N 7°27'00\"E"
//   ICAO flight-plan format    "4657N00727E" (degrees and minutes), "465700N0072700E" (with seconds)
// Returns { lat, lon } or throws an Error that says what is wrong.
export function parseCoordinate(text) {
  const raw = String(text ?? '').trim().toUpperCase()
  if (!raw) throw new Error('Enter a position, for example 46.95, 7.45 or 4657N00727E.')
  // ICAO compact form.
  let m = raw.replace(/\s+/g, '').match(/^(\d{2})(\d{2})(\d{2})?([NS])(\d{3})(\d{2})(\d{2})?([EW])$/)
  if (m) {
    const lat = (+m[1] + +m[2] / 60 + (m[3] ? +m[3] / 3600 : 0)) * (m[4] === 'S' ? -1 : 1)
    const lon = (+m[5] + +m[6] / 60 + (m[7] ? +m[7] / 3600 : 0)) * (m[8] === 'W' ? -1 : 1)
    return check(lat, lon)
  }
  // Split into a latitude part and a longitude part: at a comma, at a hemisphere letter, or between two numbers.
  const cleaned = raw.replace(/[°º'′"″]/g, ' ').replace(/\s+/g, ' ').trim()
  let parts = cleaned.split(/\s*[,;]\s*/)
  if (parts.length !== 2) {
    const h = cleaned.match(/^([NS]?\s*[\d.\s-]+?\s*[NS]?)\s+([EW]?\s*[\d.\s-]+?\s*[EW]?)$/)
    if (h && /[NS]/.test(h[1]) && /[EW]/.test(h[2])) parts = [h[1], h[2]]
    else { const n = cleaned.split(' '); if (n.length === 2) parts = n; else if (n.length === 4) parts = [n.slice(0, 2).join(' '), n.slice(2).join(' ')]; else if (n.length === 6) parts = [n.slice(0, 3).join(' '), n.slice(3).join(' ')] }
  }
  if (parts.length !== 2) throw new Error(`Cannot read "${text}" as latitude and longitude.`)
  return check(angle(parts[0], 'NS'), angle(parts[1], 'EW'))
}
function angle(part, letters) {
  let p = part.trim(), sign = 1
  const letter = p.match(new RegExp(`[${letters}]`))
  if (letter) { if (letter[0] === letters[1]) sign = -1; p = p.replace(letter[0], '').trim() }
  if (/[A-Z]/.test(p)) throw new Error(`Unexpected letters in "${part}".`)
  const nums = p.split(/\s+/).filter(Boolean).map(Number)
  if (!nums.length || nums.some(v => !Number.isFinite(v))) throw new Error(`Cannot read "${part}".`)
  if (nums.length > 1 && (nums[1] >= 60 || nums[1] < 0 || (nums[2] ?? 0) >= 60 || (nums[2] ?? 0) < 0)) throw new Error(`Minutes and seconds must be below 60 in "${part}".`)
  if (nums[0] < 0) { sign = -sign; nums[0] = -nums[0] }
  return sign * (nums[0] + (nums[1] ?? 0) / 60 + (nums[2] ?? 0) / 3600)
}
function check(lat, lon) {
  if (!(Math.abs(lat) <= 90)) throw new Error('Latitude must be between 90° S and 90° N.')
  if (!(Math.abs(lon) <= 180)) throw new Error('Longitude must be between 180° W and 180° E.')
  return { lat, lon }
}

// "46°57.0'N 007°27.0'E" (degrees and decimal minutes, as on charts) and "4657N00727E" (ICAO).
export function formatDM({ lat, lon }) {
  const f = (v, pos, neg, w) => { const a = Math.abs(v), d = Math.floor(a), m = (a - d) * 60; const mm = m.toFixed(1) === '60.0' ? [d + 1, 0] : [d, m]; return `${String(mm[0]).padStart(w, '0')}°${mm[1].toFixed(1).padStart(4, '0')}'${v < 0 ? neg : pos}` }
  return `${f(lat, 'N', 'S', 2)} ${f(lon, 'E', 'W', 3)}`
}
export function formatIcao({ lat, lon }) {
  const f = (v, pos, neg, w) => { const a = Math.abs(v); let d = Math.floor(a), m = Math.round((a - d) * 60); if (m === 60) { d++; m = 0 } return `${String(d).padStart(w, '0')}${String(m).padStart(2, '0')}${v < 0 ? neg : pos}` }
  return `${f(lat, 'N', 'S', 2)}${f(lon, 'E', 'W', 3)}`
}

// ---- The sphere ------------------------------------------------------------------------------------------------------
// Central angle (rad) between two positions (haversine, as the engine).
export function centralAngle(a, b) {
  const p1 = a.lat * RAD, p2 = b.lat * RAD, dp = p2 - p1, dl = (b.lon - a.lon) * RAD
  const h = Math.sin(dp / 2) ** 2 + Math.cos(p1) * Math.cos(p2) * Math.sin(dl / 2) ** 2
  return 2 * Math.asin(Math.sqrt(Math.min(1, Math.max(0, h))))
}
export const distanceNm = (a, b) => centralAngle(a, b) * EARTH_RADIUS_M / M_PER_NM
// Initial true course (degrees) from a toward b along the great circle; the final course is the reverse of b's toward a.
export function initialCourse(a, b) {
  const p1 = a.lat * RAD, p2 = b.lat * RAD, dl = (b.lon - a.lon) * RAD
  return norm360(Math.atan2(Math.sin(dl) * Math.cos(p2), Math.cos(p1) * Math.sin(p2) - Math.sin(p1) * Math.cos(p2) * Math.cos(dl)) / RAD)
}
export const finalCourse = (a, b) => norm360(initialCourse(b, a) + 180)
// The point a fraction f of the way from a to b along the great circle (spherical linear interpolation).
export function intermediatePoint(a, b, f) {
  const d = centralAngle(a, b)
  if (d < 1e-12) return { lat: a.lat, lon: a.lon }
  const A = Math.sin((1 - f) * d) / Math.sin(d), B = Math.sin(f * d) / Math.sin(d)
  const p1 = a.lat * RAD, l1 = a.lon * RAD, p2 = b.lat * RAD, l2 = b.lon * RAD
  const x = A * Math.cos(p1) * Math.cos(l1) + B * Math.cos(p2) * Math.cos(l2), y = A * Math.cos(p1) * Math.sin(l1) + B * Math.cos(p2) * Math.sin(l2), z = A * Math.sin(p1) + B * Math.sin(p2)
  return { lat: Math.atan2(z, Math.hypot(x, y)) / RAD, lon: Math.atan2(y, x) / RAD }
}

// ---- Onto the grid ---------------------------------------------------------------------------------------------------
// Forward projection (observation_grid.cpp): a position to the grid's local east/north (m) about its anchor.
export function project(grid, { lat, lon }) {
  const la = lat * RAD, lo = lon * RAD, la0 = grid.geographic_anchor_latitude_deg * RAD, lo0 = grid.geographic_anchor_longitude_deg * RAD
  let dl = lo - lo0; dl -= 2 * Math.PI * Math.round(dl / (2 * Math.PI))
  const h = Math.sin((la - la0) / 2) ** 2 + Math.cos(la0) * Math.cos(la) * Math.sin(dl / 2) ** 2
  const c = 2 * Math.asin(Math.sqrt(Math.min(1, Math.max(0, h))))
  if (c < 1e-12) return { east: 0, north: 0 }
  if (Math.PI - c < 1e-10) throw new Error('Cannot project a position at the antipode of the grid.')
  const k = c / Math.sin(c)
  return { east: EARTH_RADIUS_M * k * Math.cos(la) * Math.sin(dl), north: EARTH_RADIUS_M * k * (Math.cos(la0) * Math.sin(la) - Math.sin(la0) * Math.cos(la) * Math.cos(dl)) }
}
// The cell of a position, or null outside the grid (the engine's rule: origin <= x < origin + n * spacing).
export function cellOf(grid, position) {
  const { east, north } = project(grid, position)
  const cx = (east - grid.origin_east_m) / grid.spacing_east_m, cy = (north - grid.origin_north_m) / grid.spacing_north_m
  if (!(cx >= 0 && cx < grid.nx && cy >= 0 && cy < grid.ny)) return null
  return { i: Math.floor(cy) * grid.nx + Math.floor(cx), x: Math.floor(cx), y: Math.floor(cy), cx, cy, east, north }
}
// The geographic extent of the grid (for the "simulated area" hint): the min/max latitude and longitude of its edge.
export function gridBounds(grid, inverse) {
  const pts = [], n = 24
  for (let k = 0; k <= n; k++) {
    const f = k / n
    pts.push(inverse(grid.origin_east_m + f * grid.nx * grid.spacing_east_m, grid.origin_north_m), inverse(grid.origin_east_m + f * grid.nx * grid.spacing_east_m, grid.origin_north_m + grid.ny * grid.spacing_north_m))
    pts.push(inverse(grid.origin_east_m, grid.origin_north_m + f * grid.ny * grid.spacing_north_m), inverse(grid.origin_east_m + grid.nx * grid.spacing_east_m, grid.origin_north_m + f * grid.ny * grid.spacing_north_m))
  }
  return { south: Math.min(...pts.map(p => p.lat)), north: Math.max(...pts.map(p => p.lat)), west: Math.min(...pts.map(p => p.lon)), east: Math.max(...pts.map(p => p.lon)) }
}
