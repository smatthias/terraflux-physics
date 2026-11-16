import test from 'node:test'
import assert from 'node:assert/strict'
import { parseCoordinate, formatDM, formatIcao, centralAngle, distanceNm, initialCourse, finalCourse, intermediatePoint, project, cellOf, gridBounds } from './geo.js'
import { inverseProject, cellLocation, EARTH_RADIUS_M, M_PER_NM } from './station.js'
import { grid } from './stationTestFrames.js'
import { landMaskFromCountries } from './geography.js'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)

test('coordinates are read in the usual notations', () => {
  const cases = {
    '46.95, 7.45': [46.95, 7.45], '-37.8 144.9': [-37.8, 144.9], '46.95N 7.45E': [46.95, 7.45], 'S37.8 E144.9': [-37.8, 144.9],
    "46°57.0'N 007°27.0'E": [46.95, 7.45], 'N46 57.0 E7 27.0': [46.95, 7.45], '46°57\'00"N 7°27\'00"E': [46.95, 7.45], '37 40 S 144 50 E': [-37.6666667, 144.8333333],
    '4657N00727E': [46.95, 7.45], '465700N0072700E': [46.95, 7.45], '3800S14930E': [-38, 149.5], '0030s07815w': [-0.5, -78.25],
  }
  for (const [text, [lat, lon]] of Object.entries(cases)) { const p = parseCoordinate(text); near(p.lat, lat, 1e-6, text); near(p.lon, lon, 1e-6, text) }
})

test('unreadable or impossible coordinates say what is wrong', () => {
  assert.throws(() => parseCoordinate(''), /Enter a position/)
  assert.throws(() => parseCoordinate('abc'), /Cannot read/)
  assert.throws(() => parseCoordinate('95, 10'), /Latitude must be between/)
  assert.throws(() => parseCoordinate('45, 190'), /Longitude must be between/)
  assert.throws(() => parseCoordinate('46 61 N 7 E'), /below 60/)
})

test('positions are written as on charts and in ICAO flight plans', () => {
  assert.equal(formatDM({ lat: 46.95, lon: 7.45 }), "46°57.0'N 007°27.0'E")
  assert.equal(formatDM({ lat: -37.8, lon: -0.5 }), "37°48.0'S 000°30.0'W")
  assert.equal(formatIcao({ lat: 46.95, lon: 7.45 }), '4657N00727E')
  assert.equal(formatIcao({ lat: -37.99999, lon: 144.99999 }), '3800S14500E', 'minutes rounding to 60 carry into the degrees')
  for (const p of [{ lat: 12.3456, lon: -45.678 }, { lat: -0.25, lon: 179.9 }]) { const q = parseCoordinate(formatDM(p)); near(q.lat, p.lat, 1 / 1200); near(q.lon, p.lon, 1 / 1200) }
})

test('great circles: distance, initial and final course, intermediate points', () => {
  // Along a meridian: one degree of latitude is the sphere's arc; course north.
  const a = { lat: 10, lon: 20 }, b = { lat: 11, lon: 20 }
  near(distanceNm(a, b), EARTH_RADIUS_M * Math.PI / 180 / M_PER_NM, 1e-9); near(initialCourse(a, b), 0, 1e-9); near(initialCourse(b, a), 180, 1e-9)
  // Along the equator: east, constant course.
  const e = { lat: 0, lon: 0 }, f = { lat: 0, lon: 10 }
  near(initialCourse(e, f), 90, 1e-9); near(finalCourse(e, f), 90, 1e-9); near(intermediatePoint(e, f, 0.5).lon, 5, 1e-9)
  // New York JFK to London Heathrow on the sphere: about 2 991 NM, leaving at about 51 degrees, arriving at about 108.
  const jfk = { lat: 40.6413, lon: -73.7781 }, lhr = { lat: 51.47, lon: -0.4543 }
  near(distanceNm(jfk, lhr), 2991, 3); near(initialCourse(jfk, lhr), 51.4, 0.3); near(finalCourse(jfk, lhr), 107.9, 0.3)
  // The intermediate points lie on the great circle: distances add up.
  const m = intermediatePoint(jfk, lhr, 0.3)
  near(distanceNm(jfk, m) + distanceNm(m, lhr), distanceNm(jfk, lhr), 1e-6); near(distanceNm(jfk, m), 0.3 * distanceNm(jfk, lhr), 1e-6)
  near(centralAngle(a, a), 0, 0)
})

test('the engine projection: the anchor at the origin, an exact inverse, and the cell rule', () => {
  const g = grid()          // 3 x 3 cells of 100 km about 45 N 10 E, origin at -150 km
  assert.deepEqual(project(g, { lat: 45, lon: 10 }), { east: 0, north: 0 })
  for (const p of [{ lat: 45.9, lon: 11.3 }, { lat: 44.2, lon: 8.7 }]) { const q = project(g, p), r = inverseProject(g, q.east, q.north); near(r.lat, p.lat, 1e-9); near(r.lon, p.lon, 1e-9) }
  for (let i = 0; i < 9; i++) assert.equal(cellOf(g, cellLocation(g, i)).i, i, 'a cell centre falls in its cell')
  assert.equal(cellOf(g, { lat: 40, lon: 10 }), null, 'outside: no cell')
  // Edges a millimetre either side (an exact edge does not survive the round trip through floating point).
  assert.equal(cellOf(g, inverseProject(g, -150000 + 1e-3, -150000 + 1e-3))?.i, 0, 'just inside the lower edge')
  assert.equal(cellOf(g, inverseProject(g, -150000 - 1e-3, 0)), null, 'just outside the lower edge')
  assert.equal(cellOf(g, inverseProject(g, 150000 - 1e-3, 0))?.i, 5, 'just inside the upper edge')
  assert.equal(cellOf(g, inverseProject(g, 150000 + 1e-3, 0)), null, 'just outside the upper edge (origin <= x < origin + n * spacing)')
  const b = gridBounds(g, (e, n) => inverseProject(g, e, n))
  assert.ok(b.south < 45 && b.north > 45 && b.west < 10 && b.east > 10)
  near(b.north - b.south, 300000 / (EARTH_RADIUS_M * Math.PI / 180), 0.05)
})

test('land from the country outlines: aerodromes are sited on land where a run has no land/sea mask', () => {
  const square = { type: 'FeatureCollection', features: [{ type: 'Feature', properties: {}, geometry: { type: 'Polygon', coordinates: [[[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]]] } }] }
  const pts = [{ lat: 5, lon: 5 }, { lat: 5, lon: 15 }, { lat: -1, lon: 5 }]
  assert.deepEqual([...landMaskFromCountries(square, 3, i => pts[i])], [1, 0, 0])
  assert.deepEqual([...landMaskFromCountries({ type: 'FeatureCollection', features: [] }, 2, i => pts[i])], [0, 0])
  // The shipped Natural Earth outlines: Germany is land, the North Sea and the Baltic are not.
  const here = dirname(fileURLToPath(import.meta.url))
  const world = JSON.parse(readFileSync(join(here, '..', 'public', 'geo', 'countries-110m.geojson'), 'utf8'))
  const places = [{ lat: 51.0, lon: 10.1 }, { lat: 48.1, lon: 11.6 }, { lat: 54.8, lon: 4.5 }, { lat: 55.0, lon: 17.0 }]
  assert.deepEqual([...landMaskFromCountries(world, 4, i => places[i])], [1, 1, 0, 0])
})
