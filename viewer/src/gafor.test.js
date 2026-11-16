import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { GAFOR_CATEGORIES, GAFOR, UNKNOWN_GAFOR, visibilityCategory, baseCategory, gaforCategory, vfrSpace, vfrAt, parseGaforAreas, inPolygons, areaCells, areaCategory, VFR } from './gafor.js'
import { borderRuns, citiesOnGrid, gridXY, CITIES } from './geography.js'
import { solarElevation, isNight } from './weatherLayers.js'
import { validateSequence } from './data.js'
import { frameOf, grid } from './stationTestFrames.js'
import { cellLocation, FT_PER_M } from './station.js'

const here = dirname(fileURLToPath(import.meta.url)), root = join(here, '..', '..')
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)

test('the GAFOR table as specified: letters, names, colours, thresholds', () => {
  assert.deepEqual(GAFOR_CATEGORIES.map(c => [c.key, c.name, c.minVisibilityM, c.minBaseFt]), [['C', 'Charlie', 10000, 5000], ['O', 'Oscar', 8000, 2000], ['D', 'Delta', 5000, 1000], ['M', 'Mike', 1500, 500], ['X', 'X-ray', 0, 0]])
  assert.deepEqual(GAFOR_CATEGORIES.map(c => c.colour), ['#2f80ff', '#2ecc71', '#ffd60a', '#ff9f1c', '#ff3b30'], 'blue, green, yellow, orange, red')
  assert.deepEqual(GAFOR_CATEGORIES.map(c => GAFOR[c.key].rank), [0, 1, 2, 3, 4])
})

test('visibility classes at their edges (half-open ranges)', () => {
  const v = m => visibilityCategory(m)
  assert.deepEqual([10000, 9999, 8000, 7999, 5000, 4999, 1500, 1499, 0].map(v), ['C', 'O', 'O', 'D', 'D', 'M', 'M', 'X', 'X'])
  assert.equal(v(null), null); assert.equal(v(NaN), null)
})

test('cloud-base classes at their edges; no BKN/OVC layer is unlimited', () => {
  const b = ft => baseCategory(ft)
  assert.deepEqual([Infinity, 5000, 4999, 2000, 1999, 1000, 999, 500, 499, 0].map(b), ['C', 'C', 'O', 'O', 'D', 'D', 'M', 'M', 'X', 'X'])
  assert.equal(b(null), null)
})

test('the category is the worse of visibility and cloud base; unknowns are said', () => {
  assert.equal(gaforCategory(12000, 6000).key, 'C')
  assert.equal(gaforCategory(12000, 1500).key, 'D', 'a low base spoils good visibility')
  assert.equal(gaforCategory(3000, Infinity).key, 'M', 'poor visibility under a clear sky')
  assert.equal(gaforCategory(9000, 3000).key, 'O')
  assert.equal(gaforCategory(1000, 8000).key, 'X'); assert.equal(gaforCategory(20000, 300).key, 'X')
  const p = gaforCategory(20000, null)
  assert.equal(p.key, 'C'); assert.equal(p.partial, true); assert.match(p.basis, /visibility only/)
  assert.equal(gaforCategory(null, null).key, UNKNOWN_GAFOR.key)
  const both = gaforCategory(6000, 1200); assert.equal(both.byVisibility, 'D'); assert.equal(both.byBase, 'D'); assert.equal(both.partial, false)
})

test('VFR airspace: from 500 ft above ground to the clear-of-cloud limit, ground in sight', () => {
  // No ceiling: up to the FL100 cap.
  const open = vfrSpace(0, Infinity, 20000)
  assert.equal(open.bottomFt, 500); assert.equal(open.topFt, VFR.capFt); assert.equal(open.ok, true)
  // A deck inside the low band (up to 3 000 ft AMSL): clear of cloud, up to its base.
  const low = vfrSpace(0, 2200, 20000)
  assert.equal(low.topFt, 2200); near(low.room, 1700, 1e-9)
  // A deck above the band: 1 000 ft below it, but never below the band's top.
  assert.equal(vfrSpace(0, 6000, 20000).topFt, 5000); assert.equal(vfrSpace(0, 3500, 20000).topFt, 3000)
  // Over high ground the band reaches 1 000 ft above the ground.
  const hill = vfrSpace(2500, 4000, 20000); assert.equal(hill.lowBandTopFt, 3500); assert.equal(hill.topFt, 3500)
  // A base below the minimum height: no VFR.
  const closed = vfrSpace(0, 400, 20000); assert.equal(closed.ok, false); assert.equal(closed.room, 0)
  // Visibility: below 1 500 m none, below 5 km only the low band.
  assert.equal(vfrSpace(0, Infinity, 1200).ok, false)
  const hazy = vfrSpace(0, Infinity, 3000); assert.equal(hazy.topFt, 3000); assert.match(hazy.limitedBy, /low band only/)
})

test('a cell of the engine fixture over Germany reads consistently', () => {
  const frames = validateSequence(JSON.parse(readFileSync(join(root, 'viewer', 'public', 'gafor-germany', 'engine-timeline.json'), 'utf8')))
  assert.equal(frames.length, 37, 'Monday to Thursday, every 2 h'); near(frames[1].forecast.elapsed_seconds - frames[0].forecast.elapsed_seconds, 7200, 0, 'two-hour periods')
  const g = frames[0].forecast.grid, counts = {}
  for (let i = 0; i < g.nx * g.ny; i++) { const v = vfrAt(frames[0], i); counts[v.category.key] = (counts[v.category.key] ?? 0) + 1
    assert.equal(v.category.key, gaforCategory(v.visibilityM, v.hsFt).key)
    assert.ok(v.space.topFt >= v.space.bottomFt) }
  assert.ok(Object.keys(counts).length >= 4, `the first night has several categories (${JSON.stringify(counts)})`)
  // The grid covers Germany: Berlin, Munich, Hamburg and Cologne lie on it.
  for (const name of ['Berlin', 'Munich', 'Hamburg', 'Cologne']) { const c = CITIES.find(x => x.name === name), p = gridXY(g, c.lat, c.lon); assert.ok(p.x > 0 && p.x < g.nx && p.y > 0 && p.y < g.ny, name) }
})

// ---- Areas -------------------------------------------------------------------------------------------------------------
const square = (lon0, lat0, lon1, lat1) => [[lon0, lat0], [lon1, lat0], [lon1, lat1], [lon0, lat1], [lon0, lat0]]
const fc = features => ({ type: 'FeatureCollection', features })
const feature = (props, coords, type = 'Polygon') => ({ type: 'Feature', properties: props, geometry: { type, coordinates: coords } })

test('area files: code, name, reference altitude and polygons are required and checked', () => {
  const ok = parseGaforAreas(fc([feature({ id: 'T1', name: 'Test one', reference_altitude_ft: 1500 }, [square(9, 44, 11, 46)]), feature({ id: 'T2', reference_altitude_ft: 0 }, [[square(8, 44, 9, 45)]], 'MultiPolygon')]))
  assert.deepEqual(ok.map(a => [a.id, a.name, a.referenceFt, a.polygons.length]), [['T1', 'Test one', 1500, 1], ['T2', '', 0, 1]])
  assert.throws(() => parseGaforAreas({ type: 'Feature' }), /FeatureCollection/)
  assert.throws(() => parseGaforAreas(fc([feature({ name: 'x', reference_altitude_ft: 0 }, [square(0, 0, 1, 1)])])), /no area code/)
  assert.throws(() => parseGaforAreas(fc([feature({ id: '7' }, [square(0, 0, 1, 1)])])), /reference_altitude_ft/)
  assert.throws(() => parseGaforAreas(fc([feature({ id: '7', reference_altitude_ft: 0 }, [[0, 0], [1, 1]], 'LineString')])), /Polygon or MultiPolygon/)
  assert.throws(() => parseGaforAreas(fc([feature({ id: '7', reference_altitude_ft: 0 }, [square(0, 0, 1, 1)]), feature({ id: '7', reference_altitude_ft: 0 }, [square(1, 1, 2, 2)])])), /appears twice/)
})

test('point in polygon with holes; an area\'s cells', () => {
  const donut = [[square(0, 0, 10, 10), square(4, 4, 6, 6)]]
  assert.equal(inPolygons(donut, 2, 2), true); assert.equal(inPolygons(donut, 5, 5), false, 'in the hole'); assert.equal(inPolygons(donut, 11, 5), false)
  const g = grid()                                   // 3 x 3 cells of 100 km about 45 N 10 E
  const west = cellLocation(g, 3), [area] = parseGaforAreas(fc([feature({ id: 'W', reference_altitude_ft: 0 }, [square(west.lon - 0.3, west.lat - 0.3, west.lon + 0.3, west.lat + 0.3)])]))
  assert.deepEqual(areaCells([area], 9, i => cellLocation(g, i)).get('W'), [3])
})

test('an area\'s category measures the cloud base above its reference altitude', () => {
  const g = grid(), fill = v => Array(9).fill(v)
  // A 1 800 ft (549 m) overcast base over flat ground, 20 km visibility.
  const frame = frameOf({ g, coupled: { pressure: fill(101325), humidity: fill(60) }, clouds: { type: fill('stratus'), base: fill(1800 / FT_PER_M), top: fill(3000 / FT_PER_M), fraction: fill(1), thunderstorm: fill(false) } })
  const low = { id: 'L', referenceFt: 0 }, high = { id: 'H', referenceFt: 1000 }, cells = [0, 1, 2]
  const a = areaCategory(frame, low, cells), b = areaCategory(frame, high, cells)
  assert.equal(a.prevailing, 'D', '1 800 ft above a sea-level reference: Delta'); near(a.minHsFt, 1800, 1e-6)
  assert.equal(b.prevailing, 'M', '800 ft above a 1 000 ft reference: Mike'); near(b.minHsFt, 800, 1e-6)
  assert.equal(areaCategory(frame, low, []).covered, false)
})

// ---- Orientation and day/night ----------------------------------------------------------------------------------------
test('borders are clipped to the grid; cities on the grid are found', () => {
  const g = grid(), c = cellLocation(g, 4)
  const doc = { type: 'FeatureCollection', features: [{ type: 'Feature', geometry: { type: 'LineString', coordinates: [[c.lon - 5, c.lat], [c.lon - 0.5, c.lat], [c.lon, c.lat], [c.lon + 0.5, c.lat], [c.lon + 5, c.lat]] } }] }
  const runs = borderRuns(doc, g)
  assert.equal(runs.length, 1); assert.equal(runs[0].length, 3, 'the points outside the grid are cut')
  assert.ok(runs[0].every(p => p.x >= 0 && p.x <= 3 && p.y >= 0 && p.y <= 3))
  const ger = grid({ nx: 20, ny: 24, origin_east_m: -400000, origin_north_m: -480000, spacing_east_m: 40000, spacing_north_m: 40000, geographic_anchor_longitude_deg: 10.4, geographic_anchor_latitude_deg: 51.2 })
  const names = citiesOnGrid(ger).map(x => x.name)
  for (const n of ['Berlin', 'Hamburg', 'Munich', 'Frankfurt', 'Dresden']) assert.ok(names.includes(n), n)
  assert.ok(!citiesOnGrid(g).some(x => x.name === 'Berlin'))
})

test('day and night from the sun: sunrise and sunset in Berlin on 1 November', () => {
  const at = h => solarElevation(52.52, 13.405, Date.UTC(2026, 10, 1) + h * 3.6e6)
  assert.ok(at(5.9) < -0.833 && at(6.1) > -0.833, 'sunrise about 06:00 UTC (published 05:59)')
  assert.ok(at(15.55) > -0.833 && at(15.7) < -0.833, 'sunset about 15:37 UTC (published 15:37)')
  near(at(11.1), 23.3, 0.6, 'noon elevation 90 - 52.5 - 14.3')
  const g = grid({ geographic_anchor_latitude_deg: 52.52, geographic_anchor_longitude_deg: 13.405, origin_east_m: -1, origin_north_m: -1, nx: 2, ny: 2, spacing_east_m: 1, spacing_north_m: 1 })
  const f = h => ({ ...frameOf({ g }), forecast: { ...frameOf({ g }).forecast, start_time_utc: '2026-11-01T00:00:00Z', elapsed_seconds: h * 3600 } })
  assert.equal(isNight(f(3), 0), true); assert.equal(isNight(f(12), 0), false)
})
