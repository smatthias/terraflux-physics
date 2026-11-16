// GAFOR (General Aviation Forecast, Germany): VFR categories from visibility and the main cloud base, the VFR airspace
// that stays clear of cloud with the ground in sight, and the GAFOR areas. Pure functions.
//
// Categories as specified for this project (confirmed table; see docs/weather/gafor-vfr.md):
//   Charlie (C, blue)   visibility >= 10 km              hs >= 5 000 ft   clear: best VFR weather
//   Oscar   (O, green)  visibility >= 8 km               hs >= 2 000 ft   open: good VFR conditions
//   Delta   (D, yellow) 5 000 m <= visibility < 8 000 m  1 000 <= hs < 2 000 ft   difficult: extra planning
//   Mike    (M, orange) 1 500 m <= visibility < 5 000 m  500 <= hs < 1 000 ft     marginal: high workload
//   X-ray   (X, red)    visibility < 1 500 m             hs < 500 ft      closed: VFR practically impossible
// The category of a place is the worse of its visibility category and its cloud-base category.
// hs, the main cloud base ("Hauptwolkenuntergrenze"): the base of the lowest layer covering more than 4/8 of the sky
// (BKN or OVC), above ground - or above the area's reference altitude for a GAFOR area. No such layer: unlimited.
import { FT_PER_M } from './station.js'
import { flightConditions } from './aviation.js'

export const GAFOR_CATEGORIES = [
  { key: 'C', name: 'Charlie', colour: '#2f80ff', minVisibilityM: 10000, minBaseFt: 5000, meaning: 'Clear – best VFR weather' },
  { key: 'O', name: 'Oscar', colour: '#2ecc71', minVisibilityM: 8000, minBaseFt: 2000, meaning: 'Open – good VFR conditions' },
  { key: 'D', name: 'Delta', colour: '#ffd60a', minVisibilityM: 5000, minBaseFt: 1000, meaning: 'Difficult – extra planning required' },
  { key: 'M', name: 'Mike', colour: '#ff9f1c', minVisibilityM: 1500, minBaseFt: 500, meaning: 'Marginal – sub-optimal, high workload' },
  { key: 'X', name: 'X-ray', colour: '#ff3b30', minVisibilityM: 0, minBaseFt: 0, meaning: 'Closed – VFR flight practically impossible' },
]
export const GAFOR = Object.fromEntries(GAFOR_CATEGORIES.map((c, k) => [c.key, { ...c, rank: k }]))
export const UNKNOWN_GAFOR = { key: '?', name: 'unknown', colour: '#6f8894', rank: -1, meaning: 'No visibility and no cloud data at this point' }

// The category of a visibility (m) and of a main cloud base (ft; Infinity = none), each on its own; null when unknown.
export const visibilityCategory = m => (m === null || m === undefined || !Number.isFinite(m) ? null : GAFOR_CATEGORIES.find(c => m >= c.minVisibilityM).key)
export const baseCategory = ft => (ft === null || ft === undefined || Number.isNaN(ft) ? null : GAFOR_CATEGORIES.find(c => ft >= c.minBaseFt).key)

// The GAFOR category: the worse of the two. { key, ...category, byVisibility, byBase, partial, basis }.
export function gaforCategory(visibilityM, baseFt) {
  const byVisibility = visibilityCategory(visibilityM), byBase = baseCategory(baseFt)
  const known = [byVisibility, byBase].filter(Boolean)
  if (!known.length) return { ...UNKNOWN_GAFOR, byVisibility, byBase, partial: true, basis: 'no visibility and no cloud data' }
  const worst = known.reduce((a, b) => (GAFOR[b].rank > GAFOR[a].rank ? b : a))
  const partial = known.length < 2
  return { ...GAFOR[worst], byVisibility, byBase, partial, basis: partial ? (byVisibility ? 'visibility only (cloud not modelled)' : 'cloud base only (visibility not available)') : 'visibility and main cloud base' }
}

// ---- VFR airspace: clear of cloud, with the ground in sight (SERA.5001, SERA.5005(f)) ---------------------------------
//   - the lowest usable height: 500 ft above the ground (SERA.5005(f); 1 000 ft over congested areas is not modelled);
//   - the low-level band up to 3 000 ft AMSL or 1 000 ft above the ground, whichever is higher: clear of cloud with the
//     surface in sight, 5 km visibility (1 500 m allowed for slow aircraft);
//   - above the band: 1 000 ft vertically below cloud (and 1 500 m horizontally), 5 km visibility;
//   - above a broken or overcast deck the ground is not in sight: no VFR there; the space is capped at FL100 (above it
//     8 km visibility would be needed - not drawn).
export const VFR = { minHeightFt: 500, lowBandAmslFt: 3000, lowBandAglFt: 1000, verticalFromCloudFt: 1000, visibilityM: 5000, slowVisibilityM: 1500, capFt: 10000 }
// elevationFt: ground (ft AMSL); ceilingAmslFt: base of the BKN/OVC deck (ft AMSL) or Infinity; visibilityM: estimate or null.
// Returns { bottomFt, topFt, room (ft), ok, limitedBy, lowBandTopFt }.
export function vfrSpace(elevationFt, ceilingAmslFt, visibilityM) {
  const bottomFt = elevationFt + VFR.minHeightFt, lowBandTopFt = Math.max(VFR.lowBandAmslFt, elevationFt + VFR.lowBandAglFt)
  let topFt = VFR.capFt, limitedBy = 'FL100 cap'
  if (Number.isFinite(ceilingAmslFt)) {
    // Clear of cloud inside the low band; 1 000 ft below cloud above it.
    topFt = ceilingAmslFt <= lowBandTopFt ? ceilingAmslFt : Math.max(lowBandTopFt, ceilingAmslFt - VFR.verticalFromCloudFt)
    topFt = Math.min(topFt, ceilingAmslFt, VFR.capFt); limitedBy = 'cloud'
  }
  if (visibilityM !== null && visibilityM !== undefined && visibilityM < VFR.visibilityM) {
    if (visibilityM < VFR.slowVisibilityM) return { bottomFt, topFt: bottomFt, room: 0, ok: false, limitedBy: 'visibility below 1 500 m', lowBandTopFt }
    topFt = Math.min(topFt, lowBandTopFt); limitedBy = 'visibility below 5 km: low band only (slow aircraft)'
  }
  const room = Math.max(0, topFt - bottomFt)
  return { bottomFt, topFt: Math.max(bottomFt, topFt), room, ok: room > 0, limitedBy: room > 0 ? limitedBy : 'cloud base below the minimum height', lowBandTopFt }
}

// Everything the VFR view needs at a cell: visibility, main cloud base above ground, category and VFR space.
export function vfrAt(frame, i) {
  const c = flightConditions(frame, i), elevationFt = Math.max(0, frame.terrain?.values?.[i] ?? 0) * FT_PER_M
  const visibilityM = c.vis ? c.vis.metres : null
  const hsFt = c.ceilingFt                                    // BKN/OVC base above ground (ft), Infinity, or null
  const category = gaforCategory(visibilityM, hsFt)
  const ceilingAmslFt = hsFt === null ? Infinity : elevationFt + hsFt
  return { i, elevationFt, visibilityM, hsFt, category, space: vfrSpace(elevationFt, ceilingAmslFt, visibilityM), cloudModelled: c.cloud.modelled }
}

// ---- GAFOR areas ------------------------------------------------------------------------------------------------------
// The area file: GeoJSON FeatureCollection of Polygon / MultiPolygon features (longitude, latitude) with properties
//   id (the area code, e.g. "31"), name, and reference_altitude_ft (the "Bezugshoehe" the cloud base is measured above).
// Returns the areas, or throws with the reason. No areas are built in: they must come from the official source.
export function parseGaforAreas(doc) {
  if (!doc || doc.type !== 'FeatureCollection' || !Array.isArray(doc.features)) throw new Error('Expected a GeoJSON FeatureCollection of GAFOR areas.')
  const areas = doc.features.map((f, k) => {
    const p = f?.properties ?? {}, g = f?.geometry
    const id = String(p.id ?? p.code ?? p.gafor ?? p.area ?? '').trim()
    const ref = Number(p.reference_altitude_ft ?? p.reference_ft ?? p.bezugshoehe_ft ?? p.ref_ft)
    if (!id) throw new Error(`Feature ${k + 1}: no area code (property "id").`)
    if (!Number.isFinite(ref) || ref < -1000 || ref > 15000) throw new Error(`Area ${id}: missing or invalid reference_altitude_ft.`)
    if (!g || !['Polygon', 'MultiPolygon'].includes(g.type)) throw new Error(`Area ${id}: geometry must be a Polygon or MultiPolygon.`)
    const polygons = g.type === 'Polygon' ? [g.coordinates] : g.coordinates
    for (const poly of polygons) for (const ring of poly) if (!Array.isArray(ring) || ring.length < 4 || ring.some(pt => !Array.isArray(pt) || !(Math.abs(pt[0]) <= 180) || !(Math.abs(pt[1]) <= 90))) throw new Error(`Area ${id}: invalid ring (need closed [longitude, latitude] rings).`)
    return { id, name: String(p.name ?? ''), referenceFt: ref, polygons }
  })
  const ids = new Set()
  for (const a of areas) { if (ids.has(a.id)) throw new Error(`Area ${a.id} appears twice.`); ids.add(a.id) }
  return areas
}

// Point-in-polygon (even-odd) on longitude/latitude; holes are the polygon's further rings.
export function inPolygons(polygons, lon, lat) {
  const inRing = ring => { let inside = false; for (let a = 0, b = ring.length - 1; a < ring.length; b = a++) { const [xa, ya] = ring[a], [xb, yb] = ring[b]; if ((ya > lat) !== (yb > lat) && lon < (xb - xa) * (lat - ya) / (yb - ya) + xa) inside = !inside } return inside }
  return polygons.some(poly => inRing(poly[0]) && !poly.slice(1).some(inRing))
}

// The cells of each area (cell centres inside the polygon). cellLatLon(i) -> { lat, lon }.
export function areaCells(areas, count, cellLatLon) {
  const centres = Array.from({ length: count }, (_, i) => cellLatLon(i))
  return new Map(areas.map(a => [a.id, centres.flatMap((c, i) => (inPolygons(a.polygons, c.lon, c.lat) ? [i] : []))]))
}

// An area's category in a frame: the cloud base is taken above the area's reference altitude (hs = base AMSL - reference)
// and each cell classified; the area reports the prevailing category (covering most of it) and the worst one.
export function areaCategory(frame, area, cells) {
  if (!cells.length) return { covered: false }
  const counts = new Map(), rows = []
  for (const i of cells) {
    const c = flightConditions(frame, i), elevationFt = Math.max(0, frame.terrain?.values?.[i] ?? 0) * FT_PER_M
    const hs = c.ceilingFt === null ? null : c.ceilingFt === Infinity ? Infinity : elevationFt + c.ceilingFt - area.referenceFt
    const cat = gaforCategory(c.vis ? c.vis.metres : null, hs)
    counts.set(cat.key, (counts.get(cat.key) ?? 0) + 1); rows.push({ i, cat, hs, visibilityM: c.vis?.metres ?? null })
  }
  const order = [...counts.entries()].sort((a, b) => b[1] - a[1] || (GAFOR[b[0]]?.rank ?? -2) - (GAFOR[a[0]]?.rank ?? -2))
  const prevailing = order[0][0], worst = rows.reduce((w, r) => (r.cat.rank > w.rank ? r.cat : w), rows[0].cat).key
  const hsValues = rows.map(r => r.hs).filter(v => v !== null && Number.isFinite(v)), vis = rows.map(r => r.visibilityM).filter(v => v !== null)
  return {
    covered: true, cells: cells.length, prevailing, worst, share: Object.fromEntries([...counts.entries()].map(([k, v]) => [k, v / cells.length * 100])),
    minHsFt: hsValues.length ? Math.min(...hsValues) : null, minVisibilityM: vis.length ? Math.min(...vis) : null,
  }
}
