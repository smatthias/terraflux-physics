import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { siteAirports, nearestAirport, runwayPair, prevailingWind } from './airports.js'
import { routePoint, routePoints, planFlight, windTriangle, frameAt, profileAltitude, activeRunway, terrainClearanceFt, planStateAt, climbDescentPoints, hoursAtDistance, flightRules, returnTrip, ruleWindows, RULES, CLIMB_FT_PER_NM, RESERVE_HOURS } from './flightPlan.js'
import { cardContent, shortHazard } from './airportCard.js'
import { flightReport } from './aviation.js'
import { windsAloftField } from './windsAloft.js'
import { validateSequence } from './data.js'
import { frameOf, grid } from './stationTestFrames.js'
import { FT_PER_M, cellLocation } from './station.js'
import { distanceNm, initialCourse } from './geo.js'

const here = dirname(fileURLToPath(import.meta.url)), root = join(here, '..', '..')
const fixture = name => validateSequence(JSON.parse(readFileSync(join(root, 'viewer', 'public', name, 'engine-timeline.json'), 'utf8')))
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
const cached = () => { const c = new WeakMap(); return f => { if (!c.has(f)) c.set(f, windsAloftField(f)); return c.get(f) } }

// ---- Aerodromes -----------------------------------------------------------------------------------------------------
test('runway designators and the prevailing wind', () => {
  assert.deepEqual([90, 270, 4, 360, 175, 184].map(runwayPair), ['09/27', '09/27', '18/36', '18/36', '18/36', '18/36'])
  assert.equal(runwayPair(120), '12/30')
  const g = grid(), frames = [frameOf({ g, u: 5 }), frameOf({ g, u: 5, v: 5 }), frameOf({ g, u: 5, v: -5 })]
  near(prevailingWind(frames, 0).fromDeg, 270, 1e-9, 'a westerly on average'); near(prevailingWind(frames, 0).speed, 5, 1e-9)
})

test('aerodromes are fixed by the run: on land, on low ground, spread out, away from the edge, deterministic', () => {
  const g = grid({ nx: 12, ny: 10 }), n = 120
  const terrain = { values: Array.from({ length: n }, (_, i) => (i % 12 >= 6 ? 2500 : 100 + i % 12 * 10)) }
  const land = Uint8Array.from({ length: n }, (_, i) => (Math.floor(i / 12) === 9 ? 0 : 1))
  const frames = [frameOf({ g, u: 0, v: -6, terrain }), frameOf({ g, u: 0, v: -6, elapsed: 3600, terrain })]
  const a = siteAirports(frames, { land }), b = siteAirports(frames, { land })
  assert.deepEqual(a, b, 'the same run gives the same aerodromes')
  assert.ok(a.length >= 3 && a.length <= 6)
  for (const ap of a) {
    assert.ok(land[ap.i], `${ap.id} on land`); assert.ok(ap.x >= 1 && ap.y >= 1 && ap.x <= 10 && ap.y <= 8, `${ap.id} away from the edge`)
    assert.ok(ap.elevationM < 1000, `${ap.id} on low ground, not on the 2 500 m plateau`); assert.match(ap.id, /^XA\d\d$/)
    assert.equal(ap.runway, '18/36', 'runway along the northerly prevailing wind'); near(ap.elevationFt, ap.elevationM * FT_PER_M, 1e-9)
  }
  const d = (p, q) => Math.hypot(p.x - q.x, p.y - q.y)
  assert.ok(Math.min(...a.flatMap((p, k) => a.slice(k + 1).map(q => d(p, q)))) >= 2, 'spread over the map')
  assert.equal(nearestAirport(a, g, a[1].i).id, a[1].id)
  // The frame shown does not matter: the set is a property of the run.
  assert.deepEqual(siteAirports([...frames].reverse(), { land }).map(x => x.i), a.map(x => x.i))
})

// ---- The wind triangle and the profile ----------------------------------------------------------------------------
test('the wind triangle: head, tail and crosswinds', () => {
  const head = windTriangle(90, 100, 90, 20)
  near(head.gsKt, 80, 1e-9); near(head.wcaDeg, 0, 1e-9); near(head.headKt, 20, 1e-9)
  near(windTriangle(90, 100, 270, 20).gsKt, 120, 1e-9, 'tailwind')
  const cross = windTriangle(0, 100, 90, 20)           // wind from the right on a northerly course
  near(cross.wcaDeg, Math.asin(0.2) * 180 / Math.PI, 1e-9, 'turn right into it'); near(cross.headingDeg, cross.wcaDeg, 1e-9)
  near(cross.gsKt, 100 * Math.cos(Math.asin(0.2)), 1e-9)
  assert.ok(windTriangle(0, 90, 180, 10).headingDeg < 1e-9 || windTriangle(0, 90, 180, 10).headingDeg > 359.9)
  assert.equal(windTriangle(0, 50, 90, 60).possible, false, 'crosswind above the airspeed')
  assert.equal(windTriangle(0, 50, 0, 60).possible, false, 'headwind above the airspeed')
})

test('vertical profile: 300 ft per NM up and down, capped at cruise', () => {
  assert.equal(CLIMB_FT_PER_NM, 300)
  near(profileAltitude(10, 100, 9000, 0, 0), 3000, 1e-9); near(profileAltitude(50, 100, 9000, 0, 0), 9000, 1e-9); near(profileAltitude(95, 100, 9000, 0, 1000), 2500, 1e-9)
  near(profileAltitude(10, 20, 9000, 0, 0), 3000, 1e-9, 'a short route tops out below cruise')
  assert.equal(terrainClearanceFt(2000), 1000); assert.equal(terrainClearanceFt(6000), 2000)
})

test('frames by time: the nearest frame, and beyond the end of the run', () => {
  const frames = [0, 3, 6].map(h => frameOf({ elapsed: h * 3600 })), t0 = Date.parse('2026-11-01T12:00:00Z')
  assert.equal(frameAt(frames, t0 + 1.4 * 3.6e6).index, 0); assert.equal(frameAt(frames, t0 + 1.6 * 3.6e6).index, 1)
  assert.equal(frameAt(frames, t0 + 9 * 3.6e6).beyond, true); assert.equal(frameAt(frames, t0 + 6 * 3.6e6).beyond, false)
})

// ---- The plan -------------------------------------------------------------------------------------------------------
// A 10 x 10 grid of 100 km cells; the wind changes with time so that the timeline matters.
function run({ windsByHour, hours = [0, 1, 2, 3, 4, 5, 6], extra = () => ({}) } = {}) {
  const g = grid({ nx: 10, ny: 10, origin_east_m: -500000, origin_north_m: -500000 })
  return hours.map(h => { const [u, v] = windsByHour(h); return frameOf({ g, u, v, elapsed: h * 3600, coupled: { pressure: Array(100).fill(101325) }, ...extra(h) }) })
}
const ap = (id, x, y, runwayDeg = 90) => ({ id, name: id, i: y * 10 + x, x, y, elevationM: 0, elevationFt: 0, runwayDeg, runway: runwayPair(runwayDeg) })

test('a plan flies through the timeline: winds of the frame valid when each leg is flown', () => {
  // A calm first hour, then a 40 kt westerly: eastbound, the tailwind only helps after an hour.
  const frames = run({ windsByHour: h => (h < 1 ? [0, 0] : [20.58, 0]) })
  const A = ap('XA01', 1, 5), B = ap('XA02', 8, 5)
  const p = planFlight({ frames, startIndex: 0, from: A, to: B, airports: [A, B], cruiseFt: 6000, tasKt: 120, aloftFor: () => null })
  assert.equal(p.ok, true)
  const g = frames[0].forecast.grid
  near(p.lengthNm, distanceNm(cellLocation(g, A.i), cellLocation(g, B.i)), 1e-9, 'the great-circle distance')
  near(p.lengthNm, 700000 / 1852, 0.05, 'about the grid distance over a regional grid'); near(p.tcDeg, initialCourse(cellLocation(g, A.i), cellLocation(g, B.i)), 1e-9, 'initial great-circle course')
  near(p.tcDeg + p.finalCourseDeg, 180, 1e-6, 'an east-west great circle is symmetric: it starts poleward of east and ends equatorward'); assert.ok(p.tcDeg < 90 && p.finalCourseDeg > 90)
  assert.equal(p.legs[0].wind.kt, 0, 'the first leg in the calm hour')
  near(p.legs.at(-1).wind.kt, 40, 0.1, 'later legs meet the westerly'); assert.ok(p.legs.at(-1).gsKt > 155)
  const still = planFlight({ frames: run({ windsByHour: () => [0, 0] }), startIndex: 0, from: A, to: B, airports: [A, B], cruiseFt: 6000, tasKt: 120, aloftFor: () => null })
  near(still.hours, still.lengthNm / 120, 1e-9, 'in calm air the time is distance / TAS')
  assert.ok(p.hours < still.hours, 'the tailwind shortens the flight')
  near(p.fuel.trip, p.hours * 35, 1e-9); near(p.fuel.reserve, RESERVE_HOURS * 35, 1e-9)
  near(p.etaMs, p.departMs + p.hours * 3.6e6, 1)
  assert.ok(p.checks.some(c => /surface wind is used/.test(c.text)), 'no winds aloft in this run: said so')
  assert.deepEqual(planFlight({ frames, startIndex: 0, from: A, to: A, aloftFor: () => null }).ok, false)
})

test('destination judged at the ETA with an alternate; thunderstorms and icing on the route are flagged', () => {
  const fill = v => Array(100).fill(v)
  // From hour 2 the east of the map is under cumulonimbus with heavy rain (IFR); the south-west stays clear.
  const storm = h => (h >= 2 ? {
    clouds: { type: Array.from({ length: 100 }, (_, i) => (i % 10 >= 5 ? 'cumulonimbus' : 'none')), base: Array.from({ length: 100 }, (_, i) => (i % 10 >= 5 ? 200 : null)), top: Array.from({ length: 100 }, (_, i) => (i % 10 >= 5 ? 10000 : null)), fraction: Array.from({ length: 100 }, (_, i) => (i % 10 >= 5 ? 0.9 : 0)), thunderstorm: Array.from({ length: 100 }, (_, i) => i % 10 >= 5) },
    precipitation: { type: Array.from({ length: 100 }, (_, i) => (i % 10 >= 5 ? 'rain_showers' : 'none')), intensity: Array.from({ length: 100 }, (_, i) => (i % 10 >= 5 ? 'violent' : 'none')), rate: Array.from({ length: 100 }, (_, i) => (i % 10 >= 5 ? 60 : 0)), lightning: Array.from({ length: 100 }, (_, i) => i % 10 >= 5), hail: fill(0), virga: fill(false), freezingLevel: fill(3500), baseTemperature: fill(283), topTemperature: fill(220), updraft: fill(20) },
  } : { clouds: { type: fill('none'), base: fill(null), top: fill(null), fraction: fill(0), thunderstorm: fill(false) } })
  const frames = run({ windsByHour: () => [0, 0], extra: storm }).map(f => ({ ...f, coupled: { ...f.coupled, humidity: Array(100).fill(70) } }))
  const A = ap('XA01', 1, 5), B = ap('XA02', 8, 5), C = ap('XA03', 1, 1), D = ap('XA04', 8, 8)
  const p = planFlight({ frames, startIndex: 0, from: A, to: B, airports: [A, B, C, D], cruiseFt: 12000, tasKt: 120, aloftFor: () => null })
  assert.equal(p.departure.category.name, 'VFR'); assert.equal(p.destination.category.name !== 'VFR', true, 'under the storm at the ETA')
  assert.equal(p.departure.station.id, 'XA01'); assert.match(p.destination.metar, /^METAR XA02 /)
  assert.equal(p.alternate.airport.id, 'XA03', 'the nearest aerodrome that is VFR/MVFR then')
  const text = p.checks.map(c => `${c.level}:${c.text}`).join('\n')
  assert.match(text, /severe:Thunderstorms \(CB\) on the route/); assert.match(text, /warning:XA02 is (IFR|LIFR) at the ETA.*alternate XA03/)
  assert.match(text, /warning:Icing in cloud at your altitude/)
  assert.ok(p.ranges.some(r => r.kind === 'thunderstorm' && r.fromNm > 150), 'storms only on the eastern part of the route')
  assert.equal(p.checks[0].level, 'severe', 'most serious first')
  // The best level avoids the icing band where it can.
  assert.ok(p.best && !p.best.hazards.includes('icing'))
  assert.equal(p.levels.length, 9); assert.ok(p.section.length >= 3 && p.section.every(s => s.winds.length === 9))
})

test('terrain clearance in cruise: a level below the ridge is flagged, higher ones are not', () => {
  const terrain = { values: Array.from({ length: 100 }, (_, i) => (i % 10 === 5 ? 2000 : 0)) }   // a 2 000 m (6 562 ft) ridge
  const frames = run({ windsByHour: () => [0, 0], extra: () => ({ terrain }) })
  const A = ap('XA01', 1, 5), B = ap('XA02', 8, 5)
  const low = planFlight({ frames, startIndex: 0, from: A, to: B, airports: [A, B], cruiseFt: 6000, tasKt: 120, aloftFor: () => null })
  assert.ok(low.checks.some(c => c.level === 'severe' && /below terrain clearance/.test(c.text)))
  const high = planFlight({ frames, startIndex: 0, from: A, to: B, airports: [A, B], cruiseFt: 12000, tasKt: 120, aloftFor: () => null })
  assert.ok(!high.checks.some(c => /terrain/.test(c.text)), '12 000 ft clears 6 562 ft + 2 000 ft')
  assert.ok(low.levels.find(l => l.level.key === '6000').hazards.includes('terrain') && !low.levels.find(l => l.level.key === '12000').hazards.includes('terrain'))
})

test('engine fixtures: plans on the coupled runs use the winds aloft and stay consistent', () => {
  for (const name of ['clouds', 'alps', 'supercell', 'hurricane']) {
    const frames = fixture(name), d = frames[0]
    const land = d.surface?.land ? Uint8Array.from(d.surface.land, l => (l ? 1 : 0)) : null
    const aps = siteAirports(frames, { land })
    assert.ok(aps.length >= 3, `${name}: aerodromes`)
    const p = planFlight({ frames, startIndex: 1, from: aps[0], to: aps[1], airports: aps, cruiseFt: 9000, tasKt: 140, aloftFor: cached() })
    assert.equal(p.ok, true, `${name}: ${p.reason}`); assert.equal(p.aloft, true)
    near(p.legs.reduce((s, l) => s + l.distNm, 0), p.lengthNm, 1e-6, `${name}: legs add up`)
    near(p.legs.reduce((s, l) => s + l.hours, 0), p.hours, 1e-9, `${name}: leg times add up`)
    assert.ok(p.legs.every(l => Number.isFinite(l.gsKt) && l.gsKt > 0))
    assert.ok(p.best === null || p.levels.includes(p.best))
    // The active runway is the end into wind.
    const r = flightReport(frames[1], aps[0].i, { runwayDeg: activeRunway(frames[1], aps[0]), station: aps[0] })
    assert.ok(r.runway.head >= -1e-9, `${name}: no tailwind on the runway in use`)
  }
})

test('the weather card over an aerodrome: identifier, category, wind and a short hazard', () => {
  const frames = fixture('hurricane'), d = frames[2], sw = d.storms.env.storm_wind_speed_m_s
  let eye = 0; sw.forEach((v, j) => { if (v > sw[eye]) eye = j })
  const airport = { id: 'XA09', name: 'Test field', i: eye, runwayDeg: 90, runway: '09/27' }
  const r = flightReport(d, eye, { runwayDeg: 90, station: airport }), c = cardContent(r, airport, { selected: true })
  assert.equal(c.id, 'XA09'); assert.equal(c.category, r.category.name); assert.equal(c.colour, r.category.colour); assert.equal(c.selected, true)
  assert.match(c.wind, /^\d{3}° \d+ kt$/); assert.equal(c.runway, `RWY ${r.runway.designator}`)
  assert.match(c.lines[0], /^vis \d+ m \+TSRAGR/); assert.match(c.lines[1], /^BKN\d{3}CB · ceiling \d+ ft$/); assert.match(c.lines[2], /^\d+\/\d+ °C · Q\d{4}$/)
  assert.ok(c.hazard && c.hazard.length <= 30)
  assert.equal(shortHazard('Thunderstorm (CB): severe turbulence'), 'Thunderstorm'); assert.equal(shortHazard('Icing moderate to severe (convective cloud) in cloud'), 'Icing moderate to severe')
  assert.equal(shortHazard(null), null)
  const bare = flightReport(validateSequence(JSON.parse(readFileSync(join(root, 'viewer', 'public', 'ocean', 'engine-timeline.json'), 'utf8')))[0], 0, { station: airport })
  const b = cardContent(bare, airport)
  assert.deepEqual([b.lines[0], b.lines[1].startsWith('cloud —'), b.lines[2].endsWith('QNH —')], ['vis —', true, true], 'missing elements say so')
})

test('flying the plan: position, altitude, vertical speed, phase and the frame of the weather met', () => {
  // Calm first hour, then a westerly: the aircraft flies east through two frames.
  const frames = run({ windsByHour: h => (h < 1 ? [0, 0] : [20.58, 0]) })
  const A = ap('XA01', 1, 5), B = ap('XA02', 8, 5)
  const p = planFlight({ frames, startIndex: 0, from: A, to: B, airports: [A, B], cruiseFt: 6000, tasKt: 120, aloftFor: () => null })
  const start = planStateAt(p, 0), end = planStateAt(p, p.hours + 1)
  assert.equal(start.distNm, 0); assert.equal(start.altFt, 0); assert.equal(start.phase, 'climb'); assert.equal(start.frameIndex, 0)
  near(start.verticalFpm, 300 * start.gsKt / 60, 1, '300 ft per NM at the ground speed')
  assert.equal(end.done, true); near(end.distNm, p.lengthNm, 1e-9); assert.equal(end.phase, 'descent'); near(end.hours, p.hours, 1e-12)
  const { toc, tod } = climbDescentPoints(p)
  near(toc.distNm, 20, 1e-9, '6 000 ft at 300 ft/NM'); near(tod.distNm, p.lengthNm - 20, 1e-9)
  const cruise = planStateAt(p, (toc.hours + tod.hours) / 2)
  assert.equal(cruise.phase, 'cruise'); near(cruise.altFt, 6000, 1e-9); near(cruise.verticalFpm, 0, 1e-9)
  assert.ok(cruise.frameIndex >= 1, 'later in the flight the weather of a later frame')
  near(hoursAtDistance(p, p.lengthNm), p.hours, 1e-12); near(hoursAtDistance(p, 0), 0, 1e-12)
  // Monotone in time: further and never earlier.
  let last = -1; for (let k = 0; k <= 20; k++) { const st = planStateAt(p, p.hours * k / 20); assert.ok(st.distNm >= last); last = st.distNm }
  near(planStateAt(p, p.hours / 3).ms, p.departMs + p.hours / 3 * 3.6e6, 1)
})

test('flying the plan: the hazard around the aircraft and the next one ahead', () => {
  const terrain = { values: Array.from({ length: 100 }, (_, i) => (i % 10 === 5 ? 2000 : 0)) }
  const frames = run({ windsByHour: () => [0, 0], extra: () => ({ terrain }) })
  const A = ap('XA01', 1, 5), B = ap('XA02', 8, 5)
  const p = planFlight({ frames, startIndex: 0, from: A, to: B, airports: [A, B], cruiseFt: 6000, tasKt: 120, aloftFor: () => null })
  const r = p.ranges.find(x => x.kind === 'terrain')
  const before = planStateAt(p, hoursAtDistance(p, r.fromNm - 30))
  assert.equal(before.ahead.kind, 'terrain'); near(before.ahead.inNm, 30, 0.5); near(before.ahead.inMinutes, 15, 0.5, '30 NM at 120 kt')
  const inside = planStateAt(p, hoursAtDistance(p, (r.fromNm + r.toNm) / 2))
  assert.deepEqual(inside.hazards, ['terrain'])
})

test('planning by coordinates: great-circle legs through waypoints, checkpoints in the log, the path on the grid', () => {
  const frames = run({ windsByHour: () => [0, 0] }), g = frames[0].forecast.grid
  const A = ap('XA01', 1, 5), dep = cellLocation(g, A.i), wp = cellLocation(g, 5 * 10 + 5), dest = cellLocation(g, 2 * 10 + 8)
  const p = planFlight({ frames, startIndex: 0, waypoints: [{ airport: A, name: 'XA01' }, { name: 'MID', lat: wp.lat, lon: wp.lon }, { name: 'END', lat: dest.lat, lon: dest.lon }], airports: [A], cruiseFt: 6000, tasKt: 120, aloftFor: () => null })
  assert.equal(p.ok, true, p.reason)
  assert.deepEqual(p.points.map(q => q.id), ['XA01', 'MID', 'END']); assert.equal(p.points[0].isAirport, true); assert.equal(p.points[2].isAirport, false)
  near(p.lengthNm, distanceNm(dep, wp) + distanceNm(wp, dest), 1e-9, 'the sum of the great-circle legs')
  near(p.legsNm[0], distanceNm(dep, wp), 1e-9); near(p.cumNm[1], p.legsNm[0], 1e-12)
  near(p.tcDeg, initialCourse(dep, wp), 1e-9)
  // The log's checkpoints: the route points, TOC and TOD, in order along the route.
  const names = p.legs.map(l => l.fromName).concat(p.legs.at(-1).toName).filter(Boolean)
  for (const n of ['XA01', 'TOC', 'MID', 'TOD', 'END']) assert.ok(names.includes(n), `checkpoint ${n}`)
  const order = ['XA01', 'TOC', 'MID', 'TOD', 'END'].map(n => names.indexOf(n)); assert.deepEqual([...order].sort((a, b) => a - b), order)
  near(p.legs.reduce((a, l) => a + l.distNm, 0), p.lengthNm, 1e-9); near(p.legs.reduce((a, l) => a + l.hours, 0), p.hours, 1e-9)
  // The path runs through the waypoint; positions between are interpolated.
  const mid = routePoint(p, p.cumNm[1]); near(mid.lat, wp.lat, 1e-9); near(mid.lon, wp.lon, 1e-9)
  const start = routePoint(p, 0); near(start.lat, dep.lat, 1e-9)
  // The segments carry their true and grid-frame courses; the turn at MID shows in both.
  const before = p.segments.find(x => x.leg === 0), after = p.segments.find(x => x.leg === 1)
  assert.ok(Math.abs(after.trueCourseDeg - before.trueCourseDeg) > 20 && Math.abs(after.gridCourseDeg - before.gridCourseDeg) > 20)
  assert.ok(p.checks.some(c => /coordinate, not an aerodrome/.test(c.text)))
  // A destination outside the simulated area is refused with a reason.
  const out = planFlight({ frames, startIndex: 0, waypoints: [{ airport: A }, { name: 'FAR', lat: 10, lon: 10 }], aloftFor: () => null })
  assert.equal(out.ok, false); assert.match(out.reason, /FAR .* lies outside the simulated area/)
  assert.throws(() => routePoints(frames[0], [{ name: 'X' }]), /has no position/)
  const same = planFlight({ frames, startIndex: 0, waypoints: [{ airport: A }, { name: 'HERE', lat: dep.lat, lon: dep.lon }], aloftFor: () => null })
  assert.equal(same.ok, false); assert.match(same.reason, /the same place/)
})

// ---- Flight rules and the return trip ---------------------------------------------------------------------------------
// Out on day 1, back on day 3: the run has hours 0–6 and 48–54. Skies by hour: 'clear', 'low' (overcast stratus with
// its base at 300 ft: X-ray, no VFR airspace), 'thin' (overcast at 900 ft: Mike, a band of 400 ft over the 500 ft
// minimum height), 'cb' (a thunderstorm line across the middle of the map).
function skyRun(skyAt) {
  const fill = v => Array(100).fill(v), FT = 1 / FT_PER_M
  const sky = kind => kind === 'clear' ? { type: fill('none'), base: fill(null), top: fill(null), fraction: fill(0), thunderstorm: fill(false) }
    : kind === 'low' ? { type: fill('stratus'), base: fill(300 * FT), top: fill(3000 * FT), fraction: fill(1), thunderstorm: fill(false) }
    : kind === 'thin' ? { type: fill('stratus'), base: fill(900 * FT), top: fill(4000 * FT), fraction: fill(1), thunderstorm: fill(false) }
    : { type: Array.from({ length: 100 }, (_, i) => (i % 10 === 4 || i % 10 === 5 ? 'cumulonimbus' : 'none')), base: Array.from({ length: 100 }, (_, i) => (i % 10 === 4 || i % 10 === 5 ? 1200 : null)), top: Array.from({ length: 100 }, (_, i) => (i % 10 === 4 || i % 10 === 5 ? 11000 : null)), fraction: Array.from({ length: 100 }, (_, i) => (i % 10 === 4 || i % 10 === 5 ? 0.8 : 0)), thunderstorm: Array.from({ length: 100 }, (_, i) => i % 10 === 4 || i % 10 === 5) }
  return run({ windsByHour: () => [0, 0], hours: [0, 1, 2, 3, 4, 5, 6, 48, 49, 50, 51, 52, 53, 54], extra: h => ({ clouds: sky(skyAt(h)) }) })
    .map(f => ({ ...f, coupled: { ...f.coupled, humidity: Array(100).fill(60) } }))
}
const A = ap('XA01', 1, 5), B = ap('XA02', 8, 5)
const planAt = (frames, startIndex, extra = {}) => planFlight({ frames, startIndex, from: A, to: B, airports: [A, B], cruiseFt: 4500, tasKt: 120, aloftFor: () => null, ...extra })

test('flight rules: VFR under a clear sky, IFR required under low overcast, not advisable through thunderstorms', () => {
  const clear = skyRun(() => 'clear'), r = flightRules(planAt(clear, 0), clear)
  assert.equal(r.rule.key, 'VFR', r.reasons.map(x => x.text).join('; '))
  assert.equal(r.band.continuous, true); assert.equal(r.band.ceilingFt, 10000, 'no cloud: the band reaches the FL100 cap')
  assert.ok(r.ends.every(e => e.gafor === 'C'), 'Charlie at both ends')
  const low = skyRun(() => 'low'), l = flightRules(planAt(low, 0), low)
  assert.equal(l.rule.key, 'IFR')
  assert.ok(l.reasons.some(x => /XA01 is X-ray at departure/.test(x.text)) && l.reasons.some(x => /XA02 is X-ray at the ETA/.test(x.text)))
  assert.ok(l.reasons.some(x => /No VFR airspace at 0–\d+ NM.*IFR required/.test(x.text)), 'the stretch without VFR airspace is named')
  const cb = skyRun(() => 'cb'), c = flightRules(planAt(cb, 0), cb)
  assert.equal(c.rule.key, 'NOGO'); assert.match(c.reasons[0].text, /^Thunderstorms at \d+–\d+ NM: neither VFR nor IFR/)
  assert.equal(flightRules({ ok: false }, clear), null)
})

test('flight rules: Mike and a band thinner than 500 ft between terrain clearance and cloud are marginal VFR', () => {
  const thin = skyRun(() => 'thin'), r = flightRules(planAt(thin, 0), thin)
  assert.equal(r.rule.key, 'MARGINAL', r.reasons.map(x => x.text).join('; '))
  assert.ok(r.segments.every(x => x.band >= 0 && x.band < 500))
  assert.ok(r.reasons.some(x => /Marginal VFR at 0–\d+ NM: GAFOR Mike/.test(x.text)) && r.reasons.some(x => /XA01 is Mike at departure/.test(x.text)))
})

test('the return trip on a later day: reversed route at its own time, a verdict for both ways', () => {
  // Monday morning clear, low cloud from 03 h; Wednesday low cloud until 50 h, clear after.
  const frames = skyRun(h => (h < 3 || h >= 50 ? 'clear' : 'low'))
  const C = ap('XA03', 4, 5), wp = [{ airport: A, name: 'XA01' }, { airport: C, name: 'XA03' }], common = { airports: [A, C], cruiseFt: 4500, tasKt: 120, aloftFor: () => null }
  const out = planFlight({ frames, startIndex: 0, waypoints: wp, ...common })
  assert.ok(out.ok && out.etaMs < Date.parse('2026-11-01T12:00:00Z') + 3 * 3.6e6, 'the outbound flight lands before the cloud comes in')
  const wedClear = returnTrip({ frames, outbound: out, returnIndex: frames.findIndex(f => f.forecast.elapsed_seconds === 51 * 3600), waypoints: wp, ...common })
  assert.equal(wedClear.verdict.rule.key, 'VFR'); assert.match(wedClear.verdict.text, /VFR both ways/)
  assert.deepEqual(wedClear.back.plan.points.map(p => p.id), ['XA03', 'XA01'], 'the return flies the route backwards')
  near(wedClear.stayHours, 51 - out.hours, 1e-6, 'hours on the ground between arrival and the return')
  const wedEarly = returnTrip({ frames, outbound: out, returnIndex: frames.findIndex(f => f.forecast.elapsed_seconds === 48 * 3600), waypoints: wp, ...common })
  assert.equal(wedEarly.back.rules.rule.key, 'IFR'); assert.equal(wedEarly.verdict.rule.key, 'IFR'); assert.match(wedEarly.verdict.text, /^Outbound: VFR possible · return: IFR required$/)
  const tooSoon = returnTrip({ frames, outbound: out, returnIndex: 0, waypoints: wp, ...common })
  assert.equal(tooSoon.verdict.rule.key, 'NOGO'); assert.match(tooSoon.verdict.text, /before the outbound flight arrives/)
  assert.equal(returnTrip({ frames, outbound: { ok: false }, returnIndex: 9, waypoints: wp, ...common }), null)
  // The windows of Wednesday: IFR until 50 h, VFR from then.
  const wed = frames.map((f, k) => k).filter(k => frames[k].forecast.elapsed_seconds >= 48 * 3600)
  const windows = ruleWindows({ frames, indices: wed, waypoints: wp, ...common })
  assert.deepEqual(windows.map(w => w.rule.key), ['IFR', 'IFR', 'VFR', 'VFR', 'VFR', 'VFR', 'VFR'])
  assert.ok(windows.every(w => w.ms === Date.parse('2026-11-01T12:00:00Z') + frames[w.index].forecast.elapsed_seconds * 1000))
  assert.equal(Object.keys(RULES).join(), 'VFR,MARGINAL,IFR,NOGO')
})
