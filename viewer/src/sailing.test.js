import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { nearestSea, marineWarning, WARNINGS, marineVisibility, tendencyWord, apparentWind, pointOfSail, tackOf, steepness, sailingReport, pointReadout, beaufortSeries, marineWindForecast, directionWord, forceWords, timingWord, BEAUFORT_COLOURS } from './sailing.js'
import { waveField } from './waves.js'
import { validateSequence } from './data.js'
import { frameOf } from './stationTestFrames.js'
import { distanceNm, initialCourse } from './geo.js'
import { cellLocation } from './station.js'
import { probeCardContent } from './airportCard.js'

const here = dirname(fileURLToPath(import.meta.url)), root = join(here, '..', '..')
const fixture = name => validateSequence(JSON.parse(readFileSync(join(root, 'viewer', 'public', name, 'engine-timeline.json'), 'utf8')))
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)

test('apparent wind: the true wind minus the boat\'s own motion', () => {
  // Stationary boat: apparent = true.
  const still = apparentWind(0, 10, 45, 0)
  near(still.aws, 10, 1e-9); near(still.awa, -45, 1e-9); near(still.twa, -45, 1e-9)
  // Beating: the apparent wind is stronger and further forward than the true wind.
  const beat = apparentWind(0, 10, 45, 5)
  near(beat.aws, Math.sqrt(100 + 25 + 2 * 10 * 5 * Math.cos(Math.PI / 4)), 1e-9, 'law of cosines'); assert.ok(Math.abs(beat.awa) < 45)
  // Running at the wind's speed: no apparent wind. Faster than a following wind: it comes from ahead.
  near(apparentWind(180, 6, 0, 6).aws, 0, 1e-9)
  const ahead = apparentWind(270, 3, 90, 6)
  near(ahead.aws, 3, 1e-9); near(ahead.awa, 0, 1e-9, 'a boat outrunning a 3 kt tailwind feels 3 kt on the nose')
  // Beam reach: wind from starboard (positive angles).
  const beam = apparentWind(90, 12, 0, 6)
  near(beam.twa, 90, 1e-9); assert.ok(beam.awa > 0 && beam.awa < 90, 'the apparent wind moves forward')
})

test('points of sail and tacks by the true wind angle', () => {
  assert.deepEqual([0, 39, 40, 54, 55, 79, 80, 99, 100, 159, 160, 180].map(pointOfSail), ['in irons (no-go zone)', 'in irons (no-go zone)', 'close-hauled', 'close-hauled', 'close reach', 'close reach', 'beam reach', 'beam reach', 'broad reach', 'broad reach', 'running', 'running'])
  assert.equal(tackOf(60), 'starboard tack'); assert.equal(tackOf(-60), 'port tack'); assert.equal(tackOf(1), 'head to wind'); assert.equal(tackOf(-179), 'dead run')
})

test('marine warnings by the sustained wind, small-craft limits as settings', () => {
  assert.equal(marineWarning(21, 1), WARNINGS.none); assert.equal(marineWarning(22, 1), WARNINGS.sca); assert.equal(marineWarning(10, 2), WARNINGS.sca, 'hazardous seas alone')
  assert.equal(marineWarning(33.9, 0), WARNINGS.sca); assert.equal(marineWarning(34, 0), WARNINGS.gale); assert.equal(marineWarning(48, 0), WARNINGS.storm); assert.equal(marineWarning(64, 0), WARNINGS.hurricane)
  assert.equal(marineWarning(22, 1, { scaKt: 25 }), WARNINGS.none, 'a regional definition can raise the limit')
  assert.equal(marineWarning(10, NaN), WARNINGS.none, 'unknown seas do not trigger')
})

test('shipping-forecast words for visibility and barometric tendency', () => {
  assert.deepEqual([999, 1000, 3703, 3705, 9260, 9300].map(m => marineVisibility(m).word), ['very poor', 'poor', 'poor', 'moderate', 'moderate', 'good'])
  assert.equal(marineVisibility(null), null)
  assert.deepEqual([0.05, 1.0, -1.5, 2, -3.5, 5, -6, 7, -9].map(tendencyWord), ['steady', 'rising slowly', 'falling slowly', 'rising', 'falling', 'rising quickly', 'falling quickly', 'rising very rapidly', 'falling very rapidly'])
  assert.equal(tendencyWord(null), null)
})

test('wave steepness classes; the fully developed sea is a typical wind sea', () => {
  const U = 15, hs = 0.243 * U * U / 9.81, tp = 7.14 * U / 9.81
  const pm = steepness(hs, tp)
  near(pm.value, 0.0299, 0.0005); assert.equal(pm.label, 'typical wind sea')
  assert.equal(steepness(1, 12).label, 'long and gentle'); assert.equal(steepness(2, 4).label, 'steep and short'); assert.equal(steepness(0.01, 2), null)
})

test('the station snaps to the nearest sea cell; no mask means unknown, no sea means nowhere to sail', () => {
  const g = { nx: 4, ny: 1, spacing_east_m: 1000, spacing_north_m: 1000 }
  assert.deepEqual(nearestSea([0, 0, 1, 1], g, 0), { i: 2, snapped: true, distanceM: 2000, known: true })
  assert.deepEqual(nearestSea([0, 0, 1, 1], g, 3), { i: 3, snapped: false, distanceM: 0, known: true })
  assert.deepEqual(nearestSea(null, g, 1), { i: 1, snapped: false, distanceM: 0, known: false })
  assert.equal(nearestSea([0, 0, 0, 0], g, 1), null)
  const r = sailingReport(frameOf(), 4, { sea: new Uint8Array(9) })
  assert.equal(r.available, false); assert.match(r.reason, /no sea/)
})

// A 5 x 1 strip of sea with land to the west: a westerly wind blowing offshore over it.
function strip(over = {}) {
  const g = { nx: 5, ny: 1, origin_east_m: 0, origin_north_m: 0, spacing_east_m: 20000, spacing_north_m: 20000, geographic_anchor_longitude_deg: -5, geographic_anchor_latitude_deg: 50, coordinate_system: 'local_azimuthal_equidistant_m', array_order: 'row_major_y_then_x' }
  const sea = Uint8Array.from([0, 1, 1, 1, 1])
  const frame = frameOf({ g, u: 12, v: 0, t: 285, ...over })
  const f = frame.forecast.fields
  const waves = waveField({ sea, nx: 5, ny: 1, dxm: 20000, dym: 20000, u: f.wind_east.values, v: f.wind_north.values })
  return { frame, sea, waves }
}

test('a sailing report: wind, boat, sea, plan and notes', () => {
  const { frame, sea, waves } = strip()
  const r = sailingReport(frame, 0, { sea, waves, settings: { headingDeg: 0, boatKt: 6 } })
  assert.equal(r.available, true); assert.equal(r.station.snapped, true); assert.equal(r.station.i, 1); near(r.station.distanceKm, 20, 1e-9)
  near(r.wind.fromDeg, 270, 1e-9); assert.equal(r.wind.from16, 'W'); assert.equal(r.wind.beaufort.force, 6); assert.equal(r.warning, WARNINGS.sca, '23 kt')
  assert.equal(r.boat.pointOfSail, 'beam reach'); assert.equal(r.boat.tack, 'port tack', 'wind from the west over the port side of a boat heading north')
  assert.ok(r.sea.hs > 0.3 && r.sea.regime === 'fetch-limited', 'a young sea 20 km off the lee shore'); near(r.sea.fromDeg, 270, 1e-6)
  assert.match(r.plan.text, /Beyond your small-craft limits/)
  assert.equal(r.current, null); assert.equal(r.water, null); assert.equal(r.barometer, null); assert.equal(r.visibility, null)
  assert.ok(r.notes.some(n => /Moved to the nearest sea cell, 20 km/.test(n)) && r.notes.some(n => /not for navigation/.test(n)))
  // The user's own limits change the plan.
  assert.match(sailingReport(frame, 1, { sea, waves, settings: { scaKt: 30, scaSeasM: 4 } }).plan.text, /Above your reefing wind \(18 kt\)/)
  assert.match(sailingReport(frame, 1, { sea, waves, settings: { scaKt: 30, scaSeasM: 4, reefKt: 25 } }).plan.text, /Within the limits set/)
})

test('currents: set and drift, and wind against current steepens the sea', () => {
  const ocean = (u) => ({ mask: [0, 1, 1, 1, 1], u: Array(5).fill(u), v: Array(5).fill(0), temperature: Array(5).fill(283.15), thermal: null })
  const against = strip({ ocean: ocean(-0.6) })
  const r = sailingReport(against.frame, 3, { sea: against.sea, waves: against.waves })
  near(r.current.setDeg, 270, 1e-9, 'setting west'); near(r.current.driftKt, 1.17, 0.01); assert.equal(r.current.windAgainstCurrent, true)
  assert.match(r.hazards.map(h => h.text).join('\n'), /Wind against current/)
  assert.ok(r.water.coldShock); near(r.water.c, 10, 1e-9); assert.match(r.hazards.map(h => h.text).join('\n'), /Water 10\.0 °C: cold-water shock/)
  const withIt = strip({ ocean: ocean(0.6) })
  assert.equal(sailingReport(withIt.frame, 3, { sea: withIt.sea, waves: withIt.waves }).current.windAgainstCurrent, false)
})

test('the barometer: tendency words, a falling-quickly warning and a static prescribed field', () => {
  const mk = (h, anomaly, pressure) => strip({ elapsed: h * 3600, coupled: { pressure, humidity: Array(5).fill(80) }, storms: { list: [], env: { storm_pressure_anomaly_pa: Array(5).fill(anomaly) } } }).frame
  const p = Array(5).fill(101000), { sea, waves } = strip()
  const frames = [mk(0, 0, p), mk(3, -500, p)]
  const r = sailingReport(frames[1], 2, { sea, waves, frames, index: 1 })
  near(r.barometer.tendency.hPaPer3h, -5, 1e-9); assert.equal(r.barometer.tendency.word, 'falling quickly')
  assert.match(r.hazards.map(h => h.text).join('\n'), /Barometer falling quickly \(-5\.0 hPa in 3 h\)/)
  const still = [mk(0, 0, p), mk(3, 0, p)]
  const s = sailingReport(still[1], 2, { sea, waves, frames: still, index: 1 })
  assert.equal(s.barometer.tendency, null); assert.equal(s.barometer.staticField, true); assert.ok(s.notes.some(n => /prescribed and constant in time/.test(n)))
  assert.equal(s.visibility.word, 'good')
})

test('engine fixture: the ocean run reports currents, sea temperature and a developed sea', () => {
  const frames = fixture('ocean'), k = 10, d = frames[k], g = d.forecast.grid
  const sea = Uint8Array.from(d.ocean.mask, m => (m ? 1 : 0)), f = d.forecast.fields
  const waves = waveField({ sea, nx: g.nx, ny: g.ny, dxm: g.spacing_east_m, dym: g.spacing_north_m, u: f.wind_east.values, v: f.wind_north.values })
  const r = sailingReport(d, 0, { sea, waves, frames, index: k })
  assert.equal(r.available, true); assert.ok(sea[r.station.i]); assert.equal(r.station.snapped, !sea[0])
  assert.ok(r.current && Number.isFinite(r.current.driftKt) && r.water && Number.isFinite(r.water.c))
  assert.ok(r.sea && r.sea.hs >= 0 && typeof r.sea.state === 'string')
  const series = beaufortSeries(frames, r.station.i)
  assert.equal(series.length, frames.length); assert.ok(series.every(b => Number.isInteger(b) && b >= 0 && b <= 12))
})

test('engine fixture: at the hurricane\'s strongest winds the report is a storm warning with a falling barometer', () => {
  const frames = fixture('hurricane'), k = 2, d = frames[k], sw = d.storms.env.storm_wind_speed_m_s
  let eye = 0; sw.forEach((v, j) => { if (v > sw[eye]) eye = j })
  const sea = Uint8Array.from(d.surface.land, l => (l ? 0 : 1))
  const r = sailingReport(d, eye, { sea, frames, index: k })
  assert.ok(['storm', 'hurricane', 'gale'].includes(r.warning.key)); assert.equal(r.plan.level, 'severe')
  assert.match(r.hazards[0].text, /^Inside tropical cyclone #\d+/); assert.ok(r.barometer.tendency.hPaPer3h < 0, 'the storm is deepening over the point')
})

test('marine forecast words: directions, named forces, timing', () => {
  assert.deepEqual([0, 44, 46, 135, 225, 290, 338].map(directionWord), ['north', 'northeast', 'northeast', 'southeast', 'southwest', 'west', 'north'])
  assert.deepEqual([5, 7, 8, 9, 10, 11, 12].map(forceWords), ['5', '7', 'gale 8', 'severe gale 9', 'storm 10', 'violent storm 11', 'hurricane force 12'])
  assert.deepEqual([3, 6, 7, 12, 13].map(timingWord), ['imminent', 'imminent', 'soon', 'soon', 'later'])
  assert.equal(BEAUFORT_COLOURS.length, 13)
})

test('the marine wind forecast reads the coming frames: veering, backing, increasing, decreasing', () => {
  // Wind (from, m/s) at hours 0, 3, 9, 18 at every cell.
  const run = winds => winds.map(([h, from, speed]) => { const a = from * Math.PI / 180; return frameOf({ elapsed: h * 3600, u: -speed * Math.sin(a), v: -speed * Math.cos(a) }) })
  assert.equal(marineWindForecast(run([[0, 225, 9], [3, 226, 9], [18, 228, 9.5]]), 0, 0).text, 'Southwest 5.')
  assert.equal(marineWindForecast(run([[0, 225, 9], [9, 270, 12], [18, 290, 18]]), 0, 0).text, 'Southwest 5, veering west gale 8 soon.')
  assert.equal(marineWindForecast(run([[0, 270, 9], [3, 250, 9], [18, 200, 9]]), 0, 0).text, 'West 5, backing south later.')
  assert.equal(marineWindForecast(run([[0, 180, 6], [3, 180, 12], [6, 180, 12]]), 0, 0).text, 'South 4, increasing 6 imminent.')
  assert.equal(marineWindForecast(run([[0, 180, 12], [18, 180, 6]]), 0, 0).text, 'South 6, decreasing 4 later.')
  assert.equal(marineWindForecast(run([[0, 0, 0], [9, 180, 12]]), 0, 0).text, 'Calm, becoming south 6 soon.', 'a calm has no direction to veer from')
  assert.equal(marineWindForecast(run([[0, 0, 0.2], [9, 180, 0.3]]), 0, 0).text, 'Calm.')
  assert.equal(marineWindForecast(run([[0, 90, 9], [18, 90, 0.2]]), 0, 0).text, 'East 5, becoming calm later.')
  const last = run([[0, 90, 5], [6, 90, 5]])
  const r = marineWindForecast(last, 1, 0)
  assert.equal(r.text, 'East 3.'); assert.equal(r.change, null); assert.equal(r.horizonHours, 0, 'nothing ahead at the end of the run')
  assert.equal(marineWindForecast(run([[0, 90, 5], [48, 270, 20]]), 0, 0).text, 'East 3.', 'frames beyond 24 h are not used')
})

test('reading a point: wind, sea and the distance and bearing from the boat, without moving the boat', () => {
  const { frame, sea, waves } = strip(), g = frame.forecast.grid
  const boat = sailingReport(frame, 1, { sea, waves }).station
  const p = pointReadout(frame, 4, { sea, waves, boat })
  assert.equal(p.onSea, true); assert.equal(p.i, 4); assert.equal(p.wind.beaufort.force, 6); assert.equal(p.wind.from16, 'W')
  assert.equal(p.warning, WARNINGS.sca); assert.ok(p.sea.hs > sailingReport(frame, 1, { sea, waves }).sea.hs, 'a longer fetch: higher waves further out')
  near(p.fromBoat.nm, distanceNm(cellLocation(g, 1), cellLocation(g, 4)), 1e-9); near(p.fromBoat.nm, 60000 / 1852, 0.05, 'three 20 km cells east')
  near(p.fromBoat.bearingDeg, initialCourse(cellLocation(g, 1), cellLocation(g, 4)), 1e-9); near(p.fromBoat.bearingDeg, 90, 0.5, 'due east')
  assert.equal(boat.i, 1, 'the boat has not moved')
  assert.deepEqual(p.weather, sailingReport(frame, 4, { sea, waves }).weather, 'the same weather as the report would give there')
  // A point on land: wind, no sea; without a boat there is no range.
  const land = pointReadout(frame, 0, { sea, waves })
  assert.equal(land.onSea, false); assert.equal(land.sea, null); assert.equal(land.fromBoat, null); assert.equal(land.current, null)
  // The card over the point in the scene.
  const card = probeCardContent(p)
  assert.equal(card.force, 'F6'); assert.equal(card.wind, 'W 23 kt'); assert.match(card.head, /^32 NM · 090° from ⛵$/); assert.match(card.lines[0], /^waves \d\.\d m · \d+ s$/)
  assert.equal(probeCardContent(land).lines[0], 'land'); assert.equal(probeCardContent(land).head, 'point')
})
