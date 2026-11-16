import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import {
  metarTime, metarWind, metarVisibility, metarHeight, metarWeather, metarCloud, metarTemperature, metarQnh, flightCategory, CATEGORIES,
  runwayComponents, runwayDesignator, pressureAltitudeFt, densityAltitudeFt, isaTemperatureC, moistDensity, icingBand, flightReport, flightCategorySeries,
} from './aviation.js'
import { validateSequence, demo } from './data.js'
import { frameOf } from './stationTestFrames.js'

const here = dirname(fileURLToPath(import.meta.url)), root = join(here, '..', '..')
const fixture = name => validateSequence(JSON.parse(readFileSync(join(root, 'viewer', 'public', name, 'engine-timeline.json'), 'utf8')))
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)

test('METAR wind: true direction to 10 degrees (north is 360), whole knots, calm and three-digit speeds', () => {
  assert.equal(metarWind(0, 0.4), '00000KT')
  assert.equal(metarWind(275.4, 12.4), '28012KT')
  assert.equal(metarWind(4, 8), '36008KT', 'north is 360, not 000')
  assert.equal(metarWind(355, 8), '36008KT')
  assert.equal(metarWind(90, 5.5), '09006KT')
  assert.equal(metarWind(270, 105), '270105KT')
})

test('METAR visibility rounds down to the ICAO steps; 9999 from 10 km; //// when unknown', () => {
  assert.deepEqual([49, 120, 799, 800, 801, 4999, 5000, 5500, 9999, 10000, 40000].map(metarVisibility), ['0000', '0100', '0750', '0800', '0800', '4900', '5000', '5000', '9000', '9999', '9999'])
  assert.equal(metarVisibility(null), '////'); assert.equal(metarVisibility(NaN), '////')
})

test('METAR groups: cloud heights, temperatures, QNH and time', () => {
  assert.equal(metarHeight(730), '023', '730 m = 2 395 ft');
  assert.equal(metarHeight(3500), '110', 'above 10 000 ft in 1 000 ft steps'); assert.equal(metarHeight(10), '000')
  assert.deepEqual([15.4, 2.5, -0.4, -2.5, -12.6, 0].map(metarTemperature), ['15', '03', 'M00', 'M02', 'M13', '00'])
  assert.equal(metarTemperature(null), '//')
  assert.equal(metarQnh(1012.9), 'Q1012', 'QNH is rounded down'); assert.equal(metarQnh(998.2), 'Q0998'); assert.equal(metarQnh(null), 'Q////')
  assert.equal(metarTime(Date.parse('2026-08-09T06:05:00Z')), '090605Z')
})

test('METAR weather: intensity, descriptor, precipitation, hail and obscuration', () => {
  const wx = (o = {}) => ({ modelled: true, kind: 'rain', intensity: 'moderate', showers: false, thunder: false, hail: 0, freezing: false, ...o })
  assert.deepEqual(metarWeather(wx({ intensity: 'light' })), ['-RA'])
  assert.deepEqual(metarWeather(wx({ intensity: 'violent', showers: true, thunder: true, hail: 2 })), ['+TSRAGR'], 'thunder wins over showers as descriptor')
  assert.deepEqual(metarWeather(wx({ showers: true, hail: 0.3 })), ['SHRAGS'], 'small hail')
  assert.deepEqual(metarWeather(wx({ kind: 'drizzle', intensity: 'light', freezing: true })), ['-FZDZ'])
  assert.deepEqual(metarWeather(wx({ kind: 'sleet', showers: true })), ['SHRASN'])
  assert.deepEqual(metarWeather(wx({ kind: 'snow', intensity: 'heavy' })), ['+SN'])
  assert.deepEqual(metarWeather(wx({ kind: null, thunder: true })), ['TS'], 'thunder without precipitation')
  assert.deepEqual(metarWeather(wx({ intensity: 'light' }), { obscuration: 'mist' }), ['-RA', 'BR'])
  assert.deepEqual(metarWeather({ modelled: false, kind: null, thunder: false }, { obscuration: 'fog' }), ['FG'])
})

test('METAR cloud: significant layers, NSC, and solidi when the sky is not modelled', () => {
  const layer = o => ({ modelled: true, layer: { cover: 'BKN', base: 700, convective: null, ...o } })
  assert.deepEqual(metarCloud({ modelled: false, layer: null }), { group: '//////', significant: false })
  assert.deepEqual(metarCloud({ modelled: true, layer: null }), { group: 'NSC', significant: false })
  assert.equal(metarCloud(layer()).group, 'BKN022')
  assert.equal(metarCloud(layer({ convective: 'CB' })).group, 'BKN022CB')
  assert.equal(metarCloud(layer({ base: 3000 })).group, 'NSC', 'layer cloud above 5 000 ft is not operationally significant')
  assert.equal(metarCloud(layer({ base: 3000, cover: 'SCT', convective: 'TCU' })).group, 'SCT098TCU', 'CB/TCU always are')
})

test('FAA flight categories at every boundary; the worse element decides', () => {
  const cat = (c, v) => flightCategory(c, v).name
  assert.equal(cat(3001, 10), 'VFR'); assert.equal(cat(3000, 10), 'MVFR'); assert.equal(cat(1000, 10), 'MVFR'); assert.equal(cat(999, 10), 'IFR')
  assert.equal(cat(500, 10), 'IFR'); assert.equal(cat(499, 10), 'LIFR')
  assert.equal(cat(Infinity, 5.01), 'VFR'); assert.equal(cat(Infinity, 5), 'MVFR'); assert.equal(cat(Infinity, 3), 'MVFR'); assert.equal(cat(Infinity, 2.99), 'IFR')
  assert.equal(cat(Infinity, 1), 'IFR'); assert.equal(cat(Infinity, 0.99), 'LIFR')
  assert.equal(cat(5000, 0.5), 'LIFR'); assert.equal(cat(800, 4), 'IFR')
  const partial = flightCategory(null, 10)
  assert.equal(partial.name, 'VFR'); assert.equal(partial.partial, true); assert.match(partial.basis, /visibility only/)
  assert.equal(flightCategory(null, null).name, CATEGORIES.UNKNOWN.name)
  assert.equal(CATEGORIES.VFR.colour, '#3ddc84'); assert.equal(CATEGORIES.LIFR.colour, '#e040fb')
})

test('runway wind components and designators', () => {
  const c = runwayComponents(270, 20, 270); near(c.head, 20, 1e-9); near(c.cross, 0, 1e-9)
  const x = runwayComponents(270, 20, 360); near(x.head, 0, 1e-9); near(x.cross, -20, 1e-9, 'a west wind on runway 36 comes from the left')
  near(runwayComponents(90, 20, 360).cross, 20, 1e-9, 'from the right')
  const t = runwayComponents(90, 10, 270); near(t.head, -10, 1e-9, 'tailwind')
  const q = runwayComponents(300, 20, 270); near(q.head, 20 * Math.cos(Math.PI / 6), 1e-9); near(q.cross, 10, 1e-9)
  assert.deepEqual([4, 5, 274, 355, 180].map(runwayDesignator), ['36', '01', '27', '36', '18'])
})

test('pressure and density altitude follow the ISA', () => {
  near(pressureAltitudeFt(101325), 0, 1e-6); near(pressureAltitudeFt(69682), 10000, 15, '697 hPa is about 10 000 ft')
  near(densityAltitudeFt(1.225), 0, 1e-6); near(densityAltitudeFt(1.0555), 5000, 20, 'ISA density at 5 000 ft')
  near(isaTemperatureC(5000), 5.1, 1e-9)
  near(moistDensity(101325, 288.15), 1.225, 0.001, 'dry ISA sea level')
  assert.ok(moistDensity(101325, 288.15, 0.01) < moistDensity(101325, 288.15), 'moist air is lighter')
  // Hot day at sea level: density altitude well above the field.
  assert.ok(densityAltitudeFt(moistDensity(101325, 308.15, 0.015)) > 1800)
})

test('icing band: inside cloud between 0 and -20 degC', () => {
  const cu = { genus: 'cumulonimbus', convective: 'CB', base: 1000, top: 9000 }
  const band = icingBand(cu, 20, { baseTemperature: 283.15, topTemperature: 233.15, freezingLevel: 2600 })
  near(band.fromFt, 2600 * 3.2808399, 1e-6, 'from the freezing level'); near(band.toFt, (1000 + 30 / 50 * 8000) * 3.2808399, 1e-6, 'to -20 degC, interpolated')
  assert.match(band.severity, /moderate to severe/); assert.equal(band.estimated, false)
  assert.equal(icingBand({ ...cu, top: 2000 }, 20, { baseTemperature: 283.15, topTemperature: 278.15 }), null, 'a warm cloud does not ice')
  assert.equal(icingBand({ ...cu, base: 8000, top: 9000 }, 20, { baseTemperature: 240, topTemperature: 230 }), null, 'all ice crystals below -20 degC')
  const st = icingBand({ genus: 'stratus', convective: null, base: 200, top: 800 }, -2, null)
  assert.equal(st.estimated, true); near(st.fromFt, 200 * 3.2808399, 1e-6, 'the whole cloud is below freezing'); assert.match(st.severity, /layer cloud/)
  assert.equal(icingBand(null, 0, null), null)
})

// A rich synthetic frame: coupled state, a cloud layer and precipitation at cell 0.
function richFrame(over = {}) {
  const n = 9, fill = x => Array(n).fill(x)
  return frameOf({
    u: -5.1444, v: 0, t: 288.15,                    // 10 kt from the east
    coupled: { pressure: fill(101330), humidity: fill(70), density: fill(1.22), specificHumidity: fill(0.007) },
    clouds: { type: fill('stratocumulus'), base: fill(600), top: fill(1400), fraction: fill(0.6), thunderstorm: fill(false) },
    precipitation: { type: fill('rain'), intensity: fill('light'), rate: fill(1), lightning: fill(false), hail: fill(0), virga: fill(false), freezingLevel: fill(2500), baseTemperature: fill(283), topTemperature: fill(278), updraft: fill(0) },
    ...over,
  })
}

test('a full report: the METAR line, its decode and the runway wind', () => {
  const r = flightReport(richFrame(), 0, { runwayDeg: 90 })
  assert.equal(r.metar, 'METAR ZZZZ 011200Z 09010KT 9999 -RA BKN019 15/10 Q1013 RMK SIMULATED VIS EST', 'light rain (1 mm/h) leaves about 11 km')
  assert.equal(r.cloud.ceilingFt, 600 * 3.2808399); assert.equal(r.category.name, 'MVFR'); assert.equal(r.category.partial, false)
  near(r.runway.head, 10, 0.01); near(r.runway.cross, 0, 0.01); assert.equal(r.runway.best, '09'); assert.equal(r.runway.designator, '09')
  assert.equal(r.weather.words, 'light rain'); near(r.temperature.dewC, 9.6, 0.1)
  assert.ok(r.performance && Math.abs(r.performance.densityAltitudeFt - densityAltitudeFt(1.22)) < 1e-9, 'the engine density on a flat sea-level field')
  assert.equal(r.icing, null, 'the cloud is above 0 degC'); assert.deepEqual(r.hazards, [])
  assert.ok(r.notes.some(n => /not for flight planning/.test(n)) && r.notes.some(n => /Gusts are not modelled/.test(n)))
})

test('CAVOK only when the sky is modelled clear, visibility is 10 km or more and nothing falls', () => {
  const clear = richFrame({ clouds: { type: Array(9).fill('none'), base: Array(9).fill(null), top: Array(9).fill(null), fraction: Array(9).fill(0), thunderstorm: Array(9).fill(false) }, precipitation: undefined })
  const r = flightReport(clear, 0)
  assert.match(r.metar, /^METAR ZZZZ 011200Z 09010KT CAVOK 15\/10 Q1013 RMK SIMULATED VIS EST$/); assert.equal(r.category.name, 'VFR'); assert.equal(r.cloud.ceilingFt, Infinity)
  // Without a cloud model neither CAVOK nor NSC may be claimed.
  const unknown = flightReport(richFrame({ clouds: undefined, precipitation: undefined }), 0)
  assert.match(unknown.metar, / 9999 \/\/\/\/\/\/ 15\/10 /); assert.equal(unknown.category.partial, true)
})

test('hazards: thunderstorm, hail, icing, freezing rain, crosswind, tailwind, shear and mountain waves', () => {
  const n = 9, fill = x => Array(n).fill(x)
  const storm = richFrame({
    t: 271.15,
    clouds: { type: fill('cumulonimbus'), base: fill(500), top: fill(10000), fraction: fill(0.9), thunderstorm: fill(true) },
    precipitation: { type: fill('rain'), intensity: fill('heavy'), rate: fill(15), lightning: fill(true), hail: fill(2.5), virga: fill(false), freezingLevel: fill(0), baseTemperature: fill(268), topTemperature: fill(220), updraft: fill(25) },
    severe: { shear1: fill(14), possible: fill(true) },
    orography: { cross: fill(15), obstacle: fill(800), foehn: fill(true), surfacePressure: fill(95000) },
  })
  const r = flightReport(storm, 0, { runwayDeg: 360, crosswindLimitKt: 5 })
  assert.match(r.metar, / \+TSRA(GR)? BKN016CB M02\/M0\d /)
  const text = r.hazards.map(h => `${h.level}:${h.text}`).join('\n')
  for (const needle of [/severe:Thunderstorm \(CB\)/, /severe:Hail up to 2\.5 cm/, /severe:Icing moderate to severe/, /severe:Tornado possible/, /warning:Strong low-level shear: 27 kt/, /warning:Mountain waves possible: 29 kt/, /caution:Foehn/, /warning:Crosswind 10 kt on runway 36 exceeds the 5 kt limit/]) assert.match(text, needle)
  assert.equal(r.hazards[0].level, 'severe', 'most serious first')
  assert.equal(r.weather.freezing, true, 'liquid rain at -2 degC freezes on contact'); assert.match(text, /severe:Freezing rain/)
  const fz = flightReport(richFrame({ t: 271.15 }), 0)
  assert.match(fz.metar, / -FZRA /); assert.match(fz.hazards.map(h => h.text).join(), /Freezing rain: severe airframe icing/)
  const tail = flightReport(richFrame({ u: 10.3 }), 0, { runwayDeg: 90 })
  assert.match(tail.hazards.map(h => h.text).join(), /Tailwind 20 kt on runway 09: runway 27 is into wind/)
})

test('a run without humidity, pressure, cloud or precipitation prints solidi, not guesses', () => {
  const [frame] = validateSequence(demo())
  const r = flightReport(frame, 0)
  assert.match(r.metar, /^METAR ZZZZ 011300Z \d{5}KT \/\/\/\/ \/\/\/\/\/\/ \d\d\/\/\/ Q\/\/\/\/ RMK SIMULATED$/)
  assert.equal(r.category.name, CATEGORIES.UNKNOWN.name); assert.equal(r.visibility, null); assert.equal(r.performance, null); assert.equal(r.weather.words, null)
  assert.ok(!/NSC|CAVOK|9999/.test(r.metar), 'no clear sky is claimed')
})

test('engine fixture: the hurricane eyewall reads as a severe, low-pressure, low-IFR station', () => {
  const frames = fixture('hurricane'), d = frames[2], sw = d.storms.env.storm_wind_speed_m_s
  let eye = 0; sw.forEach((v, j) => { if (v > sw[eye]) eye = j })
  const r = flightReport(d, eye)
  assert.match(r.metar, /^METAR ZZZZ \d{6}Z \d{5}KT \d{4} \+TSRAGR BKN\d{3}CB \d\d\/\d\d Q0\d{3} RMK SIMULATED VIS EST TC WIND$/)
  assert.ok(r.pressure.qnhHpa < 1000, 'the storm anomaly deepens the QNH'); assert.equal(r.category.name, 'LIFR')
  assert.ok(r.wind.cyclone && r.wind.kt >= 34); assert.match(r.hazards[0].text, /^Inside tropical cyclone #\d+/)
  const series = flightCategorySeries(frames, eye)
  assert.equal(series.length, frames.length); assert.ok(series.every(c => ['VFR', 'MVFR', 'IFR', 'LIFR', '—'].includes(c)))
})

test('engine fixture: the Alps use the terrain surface pressure and a terrain-following temperature', () => {
  const frames = fixture('alps'), d = frames[3], g = d.forecast.grid
  let peak = 0; d.terrain.values.forEach((v, j) => { if (v > d.terrain.values[peak]) peak = j })
  const r = flightReport(d, peak)
  assert.equal(r.performance.stationSource, 'terrain surface pressure (orography kernel)')
  near(r.performance.stationHpa, d.orography.surfacePressure[peak] / 100, 1e-9)
  assert.ok(r.performance.pressureAltitudeFt > 6000, `a ${Math.round(d.terrain.values[peak])} m summit`)
  assert.ok(r.station.elevationM > 2000); assert.equal(r.station.x, peak % g.nx)
  // Frame 0 of this run is supersaturated: treated as saturated, and said so.
  const f0 = flightReport(frames[0], peak)
  if (f0.temperature.rh > 100) assert.ok(f0.notes.some(n => /supersaturated/.test(n)))
})
