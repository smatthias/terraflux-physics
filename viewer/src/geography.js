// Orientation on the map: country borders (Natural Earth 1:110m, public domain; viewer/public/geo, written by
// tools/make_country_outlines.py) and the larger German cities, projected onto the run's grid with the engine's
// projection (geo.js). Coarse outlines for orientation only, not aviation boundaries. Pure functions.
import { project } from './geo.js'
import { inPolygons } from './gafor.js'

// Larger German cities (and a few neighbours' capitals and Baltic harbours), latitude and longitude in degrees.
export const CITIES = [
  ['Berlin', 52.520, 13.405], ['Hamburg', 53.551, 9.994], ['Munich', 48.137, 11.575], ['Cologne', 50.938, 6.960], ['Frankfurt', 50.110, 8.682],
  ['Stuttgart', 48.776, 9.183], ['Düsseldorf', 51.228, 6.773], ['Leipzig', 51.340, 12.375], ['Dortmund', 51.514, 7.466], ['Bremen', 53.079, 8.801],
  ['Dresden', 51.051, 13.738], ['Hanover', 52.376, 9.732], ['Nuremberg', 49.454, 11.077], ['Rostock', 54.092, 12.099], ['Kiel', 54.323, 10.123],
  ['Freiburg', 47.999, 7.842], ['Erfurt', 50.985, 11.030], ['Magdeburg', 52.121, 11.628], ['Saarbrücken', 49.240, 6.997], ['Kassel', 51.312, 9.480],
  ['Prague', 50.087, 14.421], ['Vienna', 48.208, 16.373], ['Amsterdam', 52.370, 4.895], ['Copenhagen', 55.676, 12.568], ['Zurich', 47.377, 8.541],
  // Baltic harbours (the sailing example).
  ['Lübeck', 53.866, 10.686], ['Stralsund', 54.315, 13.090], ['Sassnitz', 54.516, 13.643], ['Malmö', 55.605, 13.004], ['Ystad', 55.430, 13.820], ['Rønne', 55.100, 14.706], ['Gedser', 54.575, 11.928],
].map(([name, lat, lon]) => ({ name, lat, lon }))

// Grid coordinates (cells, continuous) of a position: x = (east - origin) / spacing.
export function gridXY(grid, lat, lon) {
  const q = project(grid, { lat, lon })
  return { x: (q.east - grid.origin_east_m) / grid.spacing_east_m, y: (q.north - grid.origin_north_m) / grid.spacing_north_m }
}
const inside = (grid, p, margin = 0) => p.x >= -margin && p.y >= -margin && p.x <= grid.nx + margin && p.y <= grid.ny + margin

// The border lines of a GeoJSON of (Multi)LineStrings or (Multi)Polygons on the grid: runs of points inside the grid
// (a line leaving the grid is cut), each run at least two points. Returns [[{ x, y }...], ...] in grid cells.
export function borderRuns(geojson, grid) {
  const runs = []
  const addLine = coords => {
    let run = []
    for (const [lon, lat] of coords) {
      let p; try { p = gridXY(grid, lat, lon) } catch { p = null }
      if (p && inside(grid, p)) run.push(p)
      else { if (run.length > 1) runs.push(run); run = [] }
    }
    if (run.length > 1) runs.push(run)
  }
  for (const f of geojson?.features ?? []) {
    const g = f.geometry
    if (!g) continue
    if (g.type === 'LineString') addLine(g.coordinates)
    else if (g.type === 'MultiLineString' || g.type === 'Polygon') g.coordinates.forEach(addLine)
    else if (g.type === 'MultiPolygon') g.coordinates.forEach(poly => poly.forEach(addLine))
  }
  return runs
}

// The cities inside the grid, with their grid coordinates.
export const citiesOnGrid = grid => CITIES.map(c => ({ ...c, ...gridXY(grid, c.lat, c.lon) })).filter(c => inside(grid, c, -0.3))

// A land mask from the country outlines (each closed ring is land; even-odd inside), for runs without one: 1 = land.
// cellLatLon(i) -> { lat, lon }. Coarse (1:110m): for siting aerodromes on land, not for coastlines.
export function landMaskFromCountries(geojson, count, cellLatLon) {
  const polygons = []
  for (const f of geojson?.features ?? []) {
    const g = f.geometry
    if (!g) continue
    const rings = g.type === 'MultiLineString' || g.type === 'Polygon' ? g.coordinates : g.type === 'MultiPolygon' ? g.coordinates.flat() : []
    for (const r of rings) if (r.length >= 4) polygons.push([r])
  }
  return Uint8Array.from({ length: count }, (_, i) => { const c = cellLatLon(i); return inPolygons(polygons, c.lon, c.lat) ? 1 : 0 })
}
