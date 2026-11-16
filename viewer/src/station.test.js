import test from 'node:test'
import assert from 'node:assert/strict'
import { grid, frameOf } from './stationTestFrames.js'
import {
  KT_PER_MS, EARTH_RADIUS_M, angleDiff, cellLocation, validTime, centreCell, surfaceWind, beaufort, compass16, saturationVapourPressure, dewPoint,
  relativeHumidity, pressures, cloudLayer, coverOf, presentWeather, weatherWords, estimateVisibility, pressureTendency, cycloneHazard,
} from './station.js'

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
test('cell locations invert the grid\'s azimuthal-equidistant projection exactly', () => {
  const g = grid()
  // The centre cell of this grid sits on the anchor.
  const c = cellLocation(g, 4)
  assert.equal(c.east, 0); assert.equal(c.north, 0); near(c.lat, 45, 1e-12); near(c.lon, 10, 1e-12)
  // 100 km due north: the latitude grows by the arc, the longitude stays.
  const n = cellLocation(g, 7)
  near(n.lat, 45 + 100000 / EARTH_RADIUS_M * 180 / Math.PI, 1e-9, 'north'); near(n.lon, 10, 1e-9)
  // Forward projection of the result (as observation_grid.cpp) gives the cell centre back.
  const e = cellLocation(g, 2), f = Math.PI / 180, lat = e.lat * f, lat0 = 45 * f, dl = (e.lon - 10) * f
  const cosc = Math.sin(lat0) * Math.sin(lat) + Math.cos(lat0) * Math.cos(lat) * Math.cos(dl), c2 = Math.acos(cosc), k = c2 / Math.sin(c2)
  near(EARTH_RADIUS_M * k * Math.cos(lat) * Math.sin(dl), e.east, 1e-3, 'east'); near(EARTH_RADIUS_M * k * (Math.cos(lat0) * Math.sin(lat) - Math.sin(lat0) * Math.cos(lat) * Math.cos(dl)), e.north, 1e-3, 'north')
  assert.equal(centreCell(g), 4); assert.equal(centreCell(grid({ nx: 4, ny: 2 })), 6)
})

test('valid time is start plus elapsed; angles wrap', () => {
  assert.equal(new Date(validTime(frameOf({ elapsed: 5400 }))).toISOString(), '2026-11-01T13:30:00.000Z')
  assert.equal(angleDiff(10, 350), 20); assert.equal(angleDiff(350, 10), -20); assert.equal(angleDiff(180, 0), 180)
  assert.equal(compass16(0), 'N'); assert.equal(compass16(359), 'N'); assert.equal(compass16(247.5), 'WSW'); assert.equal(compass16(90), 'E')
})

test('wind direction is where it blows from, in degrees true', () => {
  const west = surfaceWind(frameOf({ u: 10 }), 0)
  near(west.fromDeg, 270, 1e-9, 'a wind blowing east comes from the west'); near(west.kt, 10 * KT_PER_MS, 1e-9)
  near(surfaceWind(frameOf({ v: -5 }), 0).fromDeg, 0, 1e-9, 'blowing south: a north wind')
  near(surfaceWind(frameOf({ u: -3, v: -3 }), 0).fromDeg, 45, 1e-9, 'north-east wind')
  assert.equal(surfaceWind(frameOf(), 0).speed, 0)
})

test('inside a tropical cyclone the storm wind replaces a weaker background, circling the eye', () => {
  const g = grid(), storm = { id: 7, east_m: 0, north_m: 0, category: 'category_2', max_wind_m_s: 45 }
  const f = frameOf({ g, u: 2, storms: { list: [storm], env: { storm_wind_speed_m_s: [30, 30, 30, 30, 0, 30, 30, 30, 30] } } })
  const east = surfaceWind(f, 5)                            // 100 km east of the eye, northern hemisphere
  assert.equal(east.source, 'tropical cyclone #7'); assert.equal(east.cyclone, storm); near(east.speed, 30, 1e-9)
  assert.ok(east.v > 0 && east.u < 0, 'counter-clockwise: northward east of the eye, spiralling in')
  assert.equal(surfaceWind(f, 4).source, 'engine wind field', 'the background wins where the storm is calmer')
  assert.equal(cycloneHazard(east).level, 'severe'); assert.match(cycloneHazard(east).text, /Inside tropical cyclone #7 \(category 2, 87 kt at its core\): 58 kt here/)
  const weak = surfaceWind(frameOf({ g, storms: { list: [storm], env: { storm_wind_speed_m_s: Array(9).fill(6) } } }), 0)
  assert.equal(cycloneHazard(weak).level, 'caution', 'the outer circulation is not hurricane-strength')
  assert.equal(cycloneHazard(west()), null)
  function west() { return surfaceWind(frameOf({ u: 10 }), 0) }
})

test('Beaufort force follows the WMO speed limits', () => {
  assert.deepEqual([0.4, 0.5, 3.3, 3.4, 10.7, 10.8, 17.1, 17.2, 32.6, 32.7, 60].map(s => beaufort(s).force), [0, 1, 2, 3, 5, 6, 7, 8, 11, 12, 12])
  assert.equal(beaufort(9).label, 'fresh breeze')
})

test('dew point inverts the engine\'s Magnus form', () => {
  near(saturationVapourPressure(288.15), 1702, 1, 'e_s at 15 degC')
  near(dewPoint(293.15, 50) - 273.15, 9.26, 0.01, '20 degC at 50 %')
  near(dewPoint(283.15, 100), 283.15, 1e-9, 'saturated air: dew point = temperature')
  near(dewPoint(283.15, 130), 283.15, 1e-9, 'supersaturated engine states are treated as saturated')
  assert.equal(dewPoint(283.15, null), null); assert.equal(dewPoint(283.15, 0), null)
  // The dew point brings the vapour pressure back: e_s(Td) = RH e_s(T).
  near(saturationVapourPressure(dewPoint(300, 37)), 0.37 * saturationVapourPressure(300), 1e-6)
})

test('pressures: QNH from the coupled field plus a storm anomaly; station pressure from the terrain', () => {
  assert.deepEqual(pressures(frameOf(), 0), { qnh: null, station: null, stationSource: null, stormAnomaly: 0 })
  const p = Array(9).fill(101325)
  const flat = pressures(frameOf({ coupled: { pressure: p } }), 0)
  assert.equal(flat.qnh, 101325); assert.equal(flat.station, 101325); assert.equal(flat.stationSource, 'sea-level field')
  const storm = pressures(frameOf({ coupled: { pressure: p }, storms: { list: [], env: { storm_pressure_anomaly_pa: Array(9).fill(-3000) } } }), 0)
  assert.equal(storm.qnh, 98325, 'the storm deepens the barometer')
  const hill = pressures(frameOf({ coupled: { pressure: p }, terrain: { values: Array(9).fill(1000) } }), 0)
  near(hill.station, 89875, 5, 'ISA pressure at 1 000 m'); assert.match(hill.stationSource, /ISA/)
  const oro = pressures(frameOf({ coupled: { pressure: p }, terrain: { values: Array(9).fill(1000) }, orography: { surfacePressure: Array(9).fill(90000) } }), 0)
  assert.equal(oro.station, 90000, 'the orography kernel\'s surface pressure wins')
  assert.equal(pressures(frameOf({ atmosphere: { pressure: Array(9).fill(1008) } }), 0).qnh, 100800, 'the synthetic display carries hPa')
  assert.equal(relativeHumidity(frameOf({ coupled: { humidity: Array(9).fill(55) } }), 0), 55)
  assert.equal(relativeHumidity(frameOf(), 0), null)
})

test('cloud layers: unknown without a cloud model, METAR cover from the fraction in oktas', () => {
  assert.deepEqual(cloudLayer(frameOf(), 0), { modelled: false, layer: null })
  const clouds = { type: ['none', 'cumulonimbus', 'stratus', 'cirrus', 'cumulus', 'none', 'none', 'none', 'none'], base: [null, 700, 150, 8000, 1200, null, null, null, null], top: [null, 11000, 700, 9000, 2000, null, null, null, null], fraction: [0, 0.9, 0.9, 0.3, 0.3, 0, 0, 0, 0], thunderstorm: [false, true, false, false, false, false, false, false, false] }
  const f = frameOf({ clouds })
  assert.deepEqual(cloudLayer(f, 0), { modelled: true, layer: null })
  const cb = cloudLayer(f, 1).layer
  assert.equal(cb.cover, 'BKN'); assert.equal(cb.oktas, 7); assert.equal(cb.convective, 'CB'); assert.equal(cb.abbreviation, 'Cb'); assert.equal(cb.thunderstorm, true)
  assert.equal(cloudLayer(f, 3).layer.cover, 'FEW'); assert.equal(cloudLayer(f, 3).layer.convective, null)
  assert.deepEqual([0, 1, 2, 3, 4, 5, 7, 8].map(coverOf), [null, 'FEW', 'FEW', 'SCT', 'SCT', 'BKN', 'BKN', 'OVC'])
})

test('present weather: a neutral description, freezing below 0 degC, words for it', () => {
  assert.equal(presentWeather(frameOf(), 0).modelled, false)
  const n = 9, fill = x => Array(n).fill(x)
  const precipitation = { type: ['rain_showers', 'drizzle', 'snow', 'none', 'rain', 'sleet', 'none', 'none', 'none'], intensity: ['heavy', 'light', 'moderate', 'none', 'light', 'moderate', 'none', 'none', 'none'], rate: [20, 0.3, 1, 0, 1, 2, 0, 0, 0], lightning: [true, false, false, false, false, false, false, false, false], hail: [1.2, 0, 0, 0, 0, 0, 0, 0, 0], virga: fill(false), freezingLevel: fill(3000), baseTemperature: fill(280), topTemperature: fill(230), updraft: fill(0) }
  const f = frameOf({ precipitation, t: i => (i === 1 || i === 4 ? 270 : 290) })
  const storm = presentWeather(f, 0)
  assert.deepEqual([storm.kind, storm.showers, storm.thunder, storm.hail, storm.freezing], ['rain', true, true, 1.2, false])
  assert.equal(weatherWords(storm), 'heavy rain showers with thunder and hail')
  assert.equal(presentWeather(f, 1).freezing, true, 'drizzle at -3 degC freezes on contact'); assert.equal(weatherWords(presentWeather(f, 1)), 'light freezing drizzle')
  assert.equal(presentWeather(f, 2).freezing, false, 'snow is not "freezing"')
  assert.equal(weatherWords(presentWeather(f, 3)), 'dry'); assert.equal(weatherWords(presentWeather(frameOf(), 0)), null, 'nothing may be said without a model')
  assert.equal(weatherWords(presentWeather(f, 5)), 'moderate sleet (rain and snow)')
  assert.equal(weatherWords(presentWeather(f, 3), { obscuration: 'mist' }), 'mist')
})

test('visibility is estimated only from what the run carries, and drops with rain, snow, humidity and fog', () => {
  assert.equal(estimateVisibility({}), null, 'no humidity, no precipitation: no estimate')
  const dry = estimateVisibility({ rh: 50 })
  assert.ok(dry.metres > 30000 && dry.estimated && dry.obscuration === null && dry.cause === null)
  const rain = r => estimateVisibility({ rh: 80, weather: { modelled: true, kind: 'rain', rate: r } }).metres
  assert.ok(rain(1) > rain(10) && rain(10) > rain(50), 'heavier rain, lower visibility')
  assert.ok(rain(10) > 2000 && rain(10) < 6000 && rain(50) < 2000, `heavy rain: a few km, violent rain under 2 km (${rain(10)}, ${rain(50)})`)
  const snow = estimateVisibility({ rh: 80, weather: { modelled: true, kind: 'snow', rate: 1 } }).metres
  assert.ok(snow < rain(1) / 3, 'snow cuts visibility far more than rain of the same water rate')
  assert.equal(estimateVisibility({ rh: 95 }).obscuration, null, '95 % RH: haze, still over 5 km')
  assert.equal(estimateVisibility({ rh: 98 }).obscuration, 'mist')
  assert.equal(estimateVisibility({ rh: 99.5 }).obscuration, 'fog')
  assert.equal(estimateVisibility({ rh: 150 }).metres, estimateVisibility({ rh: 99.5 }).metres, 'supersaturation is capped')
  const fog = estimateVisibility({ rh: 90, cloudBase: 20 })
  assert.ok(fog.metres < 200 && fog.obscuration === 'fog', 'cloud on the ground is fog')
  const shower = estimateVisibility({ rh: 99, weather: { modelled: true, kind: 'rain', rate: 40 } })
  assert.equal(shower.cause, 'humidity'); assert.deepEqual(shower.basis, ['humidity', 'precipitation'])
})

test('pressure tendency per 3 h from the timeline; a prescribed static field is flagged', () => {
  const pressure = Array(9).fill(101300), mk = (h, p, anomaly = 0) => frameOf({ elapsed: h * 3600, coupled: { pressure: p }, storms: { list: [], env: { storm_pressure_anomaly_pa: Array(9).fill(anomaly) } } })
  const frames = [mk(0, pressure), mk(1, pressure, -100), mk(2, pressure, -250), mk(3, pressure, -400), mk(6, pressure, -400)]
  assert.equal(pressureTendency(frames, 0, 0), null, 'no tendency at the first frame')
  const t = pressureTendency(frames, 3, 0)
  near(t.hPaPer3h, -4, 1e-9, 'the frame 3 h before'); assert.equal(t.basisHours, 3); assert.equal(t.static, false)
  const s = pressureTendency(frames, 4, 0)
  assert.equal(s.hPaPer3h, 0); assert.equal(s.static, true, 'unchanged, shared prescribed array: not a measured "steady"')
  const coarse = [mk(0, [...pressure]), mk(24, pressure.map(v => v - 600))]
  near(pressureTendency(coarse, 1, 0).hPaPer3h, -0.75, 1e-9, 'a coarse run scales the change to 3 h'); assert.equal(pressureTendency(coarse, 1, 0).basisHours, 24)
  assert.equal(pressureTendency([frameOf(), frameOf({ elapsed: 60 })], 1, 0), null, 'no pressure field')
})
