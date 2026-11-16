// Aerodromes of a run: a fixed set of synthetic airfields on the map, for the flight weather view and flight planning.
// The runs are synthetic (synthetic terrain and coasts), so real airports would sit in the wrong places; instead the
// airfields are sited deterministically from the run itself, the way an airfield would be sited:
//   - on land (where the run has a land/sea mask), at least one cell in from the edge;
//   - on low ground (the lowest 40 % of the land where the run has terrain) - valleys and forelands, not ridges;
//   - spread over the map: the first nearest the centre, each next one as far as possible from those chosen
//     (farthest-point sampling);
//   - the runway along the site's prevailing wind over the whole run (the vector-mean surface wind), to 10 degrees.
// Identifiers start with X, a letter no ICAO region uses, so they cannot be mistaken for real airports.
// The set depends only on the run, not on the frame shown: the airfields do not move. Pure functions.
import { cellLocation, elevationAt, surfaceWind, norm360, FT_PER_M } from './station.js'

const COMPASS = ['North', 'North-east', 'East', 'South-east', 'South', 'South-west', 'West', 'North-west']

// The runway designator pair of a heading: "09/27", "36/18".
export function runwayPair(deg) {
  const a = Math.round(norm360(deg) / 10) % 36 || 36, b = (a + 18 - 1) % 36 + 1
  const pad = v => String(v).padStart(2, '0')
  return `${pad(Math.min(a, b))}/${pad(Math.max(a, b))}`
}

// Prevailing wind at a cell over the run: the vector mean of the surface wind. { fromDeg, speed (m/s) }.
export function prevailingWind(frames, i) {
  let u = 0, v = 0
  for (const f of frames) { const w = surfaceWind(f, i); u += w.u; v += w.v }
  u /= frames.length; v /= frames.length
  const speed = Math.hypot(u, v)
  return { fromDeg: speed > 1e-9 ? norm360(Math.atan2(-u, -v) * 180 / Math.PI) : 0, speed }
}

// The airfields of a run. land: mask (1 = land) or null (unknown: every cell counts as land).
// Returns [{ id, name, i, x, y, lat, lon, elevationM, elevationFt, runwayDeg, runway }].
export function siteAirports(frames, { land = null, count = null } = {}) {
  const f0 = frames[0], g = f0.forecast.grid, n = g.nx * g.ny
  const margin = g.nx >= 6 && g.ny >= 6 ? 1 : 0
  let candidates = []
  for (let i = 0; i < n; i++) {
    const x = i % g.nx, y = Math.floor(i / g.nx)
    if (x < margin || y < margin || x >= g.nx - margin || y >= g.ny - margin) continue
    if (land && !land[i]) continue
    candidates.push(i)
  }
  if (!candidates.length) return []
  if (f0.terrain) {
    const heights = candidates.map(i => elevationAt(f0, i) ?? 0).sort((a, b) => a - b)
    const limit = heights[Math.floor(0.4 * (heights.length - 1))]
    const low = candidates.filter(i => (elevationAt(f0, i) ?? 0) <= limit)
    if (low.length) candidates = low
  }
  const k = count ?? Math.max(3, Math.min(6, Math.round(Math.sqrt(candidates.length) / 2)))
  const pos = i => [(i % g.nx + 0.5) * g.spacing_east_m, (Math.floor(i / g.nx) + 0.5) * g.spacing_north_m]
  const centre = [g.nx * g.spacing_east_m / 2, g.ny * g.spacing_north_m / 2]
  const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1])
  let first = candidates[0]
  for (const i of candidates) if (dist(pos(i), centre) < dist(pos(first), centre)) first = i
  const chosen = [first], nearest = new Map(candidates.map(i => [i, dist(pos(i), pos(first))]))
  while (chosen.length < Math.min(k, candidates.length)) {
    let best = null
    for (const i of candidates) if (!chosen.includes(i) && (best === null || nearest.get(i) > nearest.get(best))) best = i
    if (best === null || nearest.get(best) <= 0) break
    chosen.push(best)
    for (const i of candidates) nearest.set(i, Math.min(nearest.get(i), dist(pos(i), pos(best))))
  }
  const used = new Map()
  return chosen.map((i, k2) => {
    const loc = cellLocation(g, i), p = pos(i), off = [p[0] - centre[0], p[1] - centre[1]]
    const central = dist(p, centre) < 0.18 * Math.min(g.nx * g.spacing_east_m, g.ny * g.spacing_north_m)
    const side = central ? 'Central' : COMPASS[Math.round(norm360(Math.atan2(off[0], off[1]) * 180 / Math.PI) / 45) % 8]
    const seen = (used.get(side) ?? 0) + 1; used.set(side, seen)
    const elevationM = Math.max(0, elevationAt(f0, i) ?? 0), wind = prevailingWind(frames, i)
    const runwayDeg = wind.speed > 0.5 ? Math.round(wind.fromDeg / 10) * 10 || 360 : 360
    return { id: `XA${String(k2 + 1).padStart(2, '0')}`, name: `${side} field${seen > 1 ? ` ${seen}` : ''}`, i, x: i % g.nx, y: Math.floor(i / g.nx), lat: loc.lat, lon: loc.lon, elevationM, elevationFt: elevationM * FT_PER_M, runwayDeg, runway: runwayPair(runwayDeg) }
  })
}

// The airfield nearest to a cell (for picking on the map).
export function nearestAirport(airports, grid, i) {
  const x = i % grid.nx, y = Math.floor(i / grid.nx)
  let best = null, bestD = Infinity
  for (const a of airports) { const d = Math.hypot((a.x - x) * grid.spacing_east_m, (a.y - y) * grid.spacing_north_m); if (d < bestD) { bestD = d; best = a } }
  return best
}
