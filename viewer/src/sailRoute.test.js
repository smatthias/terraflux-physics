import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { sailRoute, sailStateAt, manoeuvreWindows, departureScan, bestDeparture, weatherModel, gridNorthTrue, lay, POINT_OF_SAIL_COLOURS } from './sailRoute.js'
import { optimalVmg, polarSpeed, BOATS } from './polars.js'
import { frameOf, grid } from './stationTestFrames.js'
import { cellLocation, inverseProject, KT_PER_MS, M_PER_NM } from './station.js'
import { distanceNm } from './geo.js'
import { landMaskFromCountries } from './geography.js'
import { waveField } from './waves.js'
import { validateSequence } from './data.js'

const here = dirname(fileURLToPath(import.meta.url))
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
const B40 = BOATS.cruiser40
// A 24 x 24 grid of 10 km cells at 45 N 10 E, hourly for two days; the wind (m/s, east and north) by cell and hour.
const G = grid({ nx: 24, ny: 24, origin_east_m: -120000, origin_north_m: -120000, spacing_east_m: 10000, spacing_north_m: 10000 })
const runOf = (wind, extra = () => ({})) => Array.from({ length: 49 }, (_, h) => frameOf({ g: G, u: i => wind(i, h)[0], v: i => wind(i, h)[1], elapsed: h * 3600, ...extra(h) }))
const at = (east, north) => inverseProject(G, east, north)
const P = (name, east, north) => ({ name, ...at(east, north) })
const westerly = runOf(() => [8, 0])                      // 15.6 kt from the west everywhere
const TWS = 8 * KT_PER_MS

test('the mark on the layline: two legs at the VMG angle, the current side first', () => {
  const [a, b] = lay(0, 1000, 0, 45, 1)                    // dead upwind, wind from the north
  near(a.distanceM, 1000 / Math.SQRT2, 1e-6); near(b.distanceM, 1000 / Math.SQRT2, 1e-6); near(a.headingDeg, 315, 1e-9); near(b.headingDeg, 45, 1e-9)
  const [c, d] = lay(300, 1000, 0, 45, -1); near(c.headingDeg, 45, 1e-9)
  near(Math.sin(c.headingDeg * Math.PI / 180) * c.distanceM + Math.sin(d.headingDeg * Math.PI / 180) * d.distanceM, 300, 1e-6, 'the legs add up to the mark')
  assert.equal(lay(1000, 0, 0, 45, 1), null, 'a mark outside the cone is not beaten to')
})

test('a reach is sailed straight at the polar speed; a beat in tacks at the best VMG; a run in gybes', () => {
  const start = P('A', -80000, 0)
  // Reaching: the mark due north of the start, the wind from the west (a beam reach).
  const reach = sailRoute({ frames: westerly, points: [start, P('N', -80000, 90000)] })
  assert.equal(reach.ok, true, reach.reason); assert.equal(reach.tacks + reach.gybes, 0)
  near(reach.hours, reach.directNm / polarSpeed(B40, TWS, 90), 0.03 * reach.hours, 'distance / polar speed on a beam reach')
  assert.ok(reach.track.every(r => r.pointOfSail === 'beam reach' && r.tack === 'port tack' && r.sails.leeward === 'starboard'))
  // Beating: the mark due west (dead upwind).
  const beat = sailRoute({ frames: westerly, points: [P('E', 80000, 0), P('W', -80000, 0)] }), vmg = optimalVmg(B40, TWS)
  assert.equal(beat.ok, true, beat.reason); assert.ok(beat.tacks >= 1); assert.equal(beat.gybes, 0)
  near(beat.hours, beat.directNm / vmg.upVmg, 0.06 * beat.hours, 'about distance / best upwind VMG')
  assert.ok(beat.track.every(r => Math.abs(r.twa) >= 35), 'never in the no-go zone'); assert.ok(beat.track.every(r => r.pointOfSail === 'close-hauled'))
  assert.ok(beat.sailedNm > beat.directNm * 1.25, 'the zig-zag is longer than the rhumb line')
  // Running: the mark due east (dead downwind): gybing at the best downwind angle beats a dead run.
  const run = sailRoute({ frames: westerly, points: [P('W', -80000, 0), P('E', 80000, 0)] })
  assert.equal(run.ok, true, run.reason); assert.ok(run.gybes >= 1); near(run.hours, run.directNm / vmg.downVmg, 0.06 * run.hours)
  assert.ok(run.hours < run.directNm / polarSpeed(B40, TWS, 180), 'faster than running dead downwind')
  // Weather routing is never slower than the direct route beyond its resolution.
  for (const r of [reach, beat, run]) { assert.equal(r.compare.ok, true); assert.ok(r.hours <= r.compare.hours * 1.03, `${r.hours} vs direct ${r.compare.hours}`) }
  // The log: positions, speeds, headings, sails; the track starts at the start and ends at the mark.
  const first = beat.track[0], lastP = beat.end
  near(first.lat, beat.marks[0].lat, 1e-9); near(distanceNm(lastP, beat.marks[1]), 0, 0.05)
  assert.ok(beat.track.every(r => r.stwKt > 0 && r.sogKt > 0 && r.sails && r.beaufort.force === 5 && POINT_OF_SAIL_COLOURS[r.pointOfSail]))
  near(beat.track.reduce((a, r) => a + r.hours, 0), beat.hours, 1e-9)
})

test('land is avoided by the routing; the direct route says when it runs aground', () => {
  // An island in the middle, blocking the straight line from west to east.
  const sea = Uint8Array.from({ length: 24 * 24 }, (_, i) => { const x = i % 24, y = Math.floor(i / 24); return x >= 10 && x <= 13 && y >= 8 && y <= 15 ? 0 : 1 })
  const frames = runOf(() => [0, 8]), points = [P('W', -90000, 0), P('E', 90000, 0)]       // a beam reach from the south
  const r = sailRoute({ frames, points, sea })
  assert.equal(r.ok, true, r.reason)
  const m = weatherModel(frames, { sea })
  for (const row of r.track) for (let f = 0; f <= 1; f += 0.1) assert.ok(m.isSea(row.e + (row.e1 - row.e) * f, row.n + (row.n1 - row.n) * f), 'every point of the track on the water')
  assert.ok(r.sailedNm > r.directNm * 1.02, 'around the island')
  assert.equal(r.compare.ok, false); assert.match(r.compare.reason, /runs onto land/)
  assert.match(sailRoute({ frames, points: [P('W', -90000, 0), P('Island', 0, 0)], sea }).reason, /Island is on land/)
  assert.match(sailRoute({ frames, points: [P('W', -90000, 0), { name: 'Far', lat: 10, lon: 10 }], sea }).reason, /Far lies outside the simulated area/)
  assert.match(sailRoute({ frames, points: [P('W', -90000, 0), P('W2', -90000, 50)], sea }).reason, /the same place/)
})

test('your limits: a band of strong wind is sailed round, or the route says it cannot keep them', () => {
  // 30 kt in the middle rows (y 10-13), 12 kt elsewhere, from the west; start south, mark north (a beam reach).
  const frames = runOf(i => { const y = Math.floor(i / 24), x = i % 24; return [y >= 10 && y <= 13 && x >= 4 ? 15.4 : 6.2, 0] })
  const points = [P('S', 60000, -90000), P('N', 60000, 90000)]
  const r = sailRoute({ frames, points, settings: { maxWindKt: 22 } })
  assert.equal(r.ok, true, r.reason); assert.equal(r.relaxed, false)
  assert.ok(r.track.every(row => row.tws <= 22.5), 'kept out of the 30 kt band (through its western end)')
  const everywhere = runOf(() => [15.4, 0]), s = sailRoute({ frames: everywhere, points, settings: { maxWindKt: 22 } })
  assert.equal(s.ok, true); assert.equal(s.relaxed, true); assert.ok(s.checks.some(c => /cannot be kept on any route/.test(c.text)))
  assert.ok(s.checks.some(c => c.level === 'warning' && /Beyond your limits/.test(c.text)))
})

test('calm: the engine; current: course and speed over the ground differ from heading and speed through the water', () => {
  const calm = sailRoute({ frames: runOf(() => [0.3, 0]), points: [P('A', -50000, 0), P('B', 50000, 0)] })
  assert.equal(calm.ok, true); near(calm.motoringHours, calm.hours, 1e-6); near(calm.avgSogKt, 5.5, 0.05); assert.ok(calm.checks.some(c => /Motoring/.test(c.text)))
  assert.ok(calm.track.every(r => r.sailShort === 'engine' && r.pointOfSail === 'motoring'))
  assert.equal(sailRoute({ frames: runOf(() => [0.3, 0]), points: [P('A', -50000, 0), P('B', 50000, 0)], settings: { motor: false, stepMinutes: 60 } }).ok, false, 'no wind and no engine: no route')
  // A 1 m/s current setting east while reaching north.
  const ocean = { mask: Array(576).fill(1), u: Array(576).fill(1), v: Array(576).fill(0) }
  const r = sailRoute({ frames: runOf(() => [8, 0], () => ({ ocean })), points: [P('A', -60000, -60000), P('B', 0, 60000)] })
  assert.equal(r.ok, true, r.reason)
  const row = r.track[1]; assert.ok(row.current && row.current.driftKt > 1.9 && row.current.driftKt < 2); const crab = ((row.headingDeg - row.cogDeg + 540) % 360) - 180; assert.ok(crab < -3, `heading left of the course into a current setting east (${crab.toFixed(1)}°)`); assert.ok(row.sogKt > row.stwKt * Math.cos(crab * Math.PI / 180), 'the current helps along the course')
})

test('the sail-through: position and sails at any time, turns in their real-time windows; departure planning', () => {
  const beat = sailRoute({ frames: westerly, points: [P('E', 80000, 0), P('W', -80000, 0)] })
  const s0 = sailStateAt(beat, 0), s1 = sailStateAt(beat, beat.hours)
  near(s0.e, beat.track[0].e, 1e-6); near(s1.e, beat.track.at(-1).e1, 1e-6); assert.equal(s1.done, true)
  assert.ok(s0.sails && s0.boomDeg !== 0); assert.ok(s0.next && s0.next.kind === 'tack')
  const w = manoeuvreWindows(beat); assert.equal(w.length, beat.manoeuvres.length)
  const mid = (w[0][0] + w[0][1]) / 2, st = sailStateAt(beat, (mid - beat.departMs) / 3.6e6)
  assert.equal(st.manoeuvre.kind, 'tack'); assert.ok(st.manoeuvre.t > 0 && st.manoeuvre.t < st.manoeuvre.duration)
  const before = sailStateAt(beat, (w[0][0] - 5000 - beat.departMs) / 3.6e6), after = sailStateAt(beat, (w[0][1] + 5000 - beat.departMs) / 3.6e6)
  assert.ok(Math.sign(before.boomDeg) === -Math.sign(after.boomDeg), 'the boom on the other side after the tack')
  // Departures hourly: the same steady wind, the same passage; the best is one of them.
  const frames = runOf((i, h) => [8, h >= 24 ? 8 : 0]), points = [P('A', -60000, -60000), P('B', 60000, 60000)]
  const scan = departureScan({ frames, indices: [0, 12, 24, 30], points })
  assert.equal(scan.rows.length, 4); assert.ok(scan.rows.every(r => r.ok))
  assert.ok(scan.rows[2].hours !== scan.rows[0].hours, 'the wind veers and strengthens on day 2: another passage')
  assert.equal(scan.best, bestDeparture(scan.rows)); assert.equal(scan.best, scan.rows.reduce((b, r) => (r.hours < b.hours ? r : b)).index)
  near(gridNorthTrue(G, 0, 0), 0, 1e-6, 'no convergence at the anchor'); assert.ok(Math.abs(gridNorthTrue(G, 100000, 0)) > 0.3, 'convergence east of it')
})

test('engine fixture: a passage across the western Baltic and back, through the forecast', () => {
  const frames = validateSequence(JSON.parse(readFileSync(join(here, '..', 'public', 'baltic-sailing', 'engine-timeline.json'), 'utf8')))
  const world = JSON.parse(readFileSync(join(here, '..', 'public', 'geo', 'countries-110m.geojson'), 'utf8'))
  const g = frames[0].forecast.grid, n = g.nx * g.ny, sea = Uint8Array.from(landMaskFromCountries(world, n, i => cellLocation(g, i)), v => (v ? 0 : 1))
  const cache = new WeakMap(), wavesFor = f => { if (!cache.has(f)) { const F = f.forecast.fields; cache.set(f, { sea, ...waveField({ sea, nx: g.nx, ny: g.ny, dxm: g.spacing_east_m, dym: g.spacing_north_m, originEast: g.origin_east_m, originNorth: g.origin_north_m, u: F.wind_east.values, v: F.wind_north.values }) }) } return cache.get(f) }
  assert.equal(frames.length, 29); assert.ok(sea.some(Boolean) && sea.some(v => !v), 'the country outlines give land and sea')
  const kiel = { name: 'Kiel Bight', lat: 54.55, lon: 10.45 }, bornholm = { name: 'Bornholm', lat: 55.15, lon: 14.55 }
  const start = frames.findIndex(f => f.forecast.elapsed_seconds === 18 * 3600)             // Friday 06 UTC
  const out = sailRoute({ frames, startIndex: start, points: [kiel, bornholm], sea, wavesFor })
  assert.equal(out.ok, true, out.reason)
  assert.ok(out.hours > 15 && out.hours < 30, `${out.hours} h for ~140 NM`); assert.ok(out.maxTws < 22 && out.maxHs < 2, 'a moderate south-westerly')
  assert.ok(out.track.every(r => ['broad reach', 'running', 'beam reach'].includes(r.pointOfSail)), 'off the wind eastbound')
  const m = weatherModel(frames, { sea })
  assert.ok(out.track.every(r => m.isSea(r.e, r.n) && m.isSea(r.e1, r.n1)), 'on the water all the way')
  const back = sailRoute({ frames, startIndex: start + 8, points: [bornholm, kiel], sea, wavesFor })
  assert.equal(back.ok, true, back.reason); assert.ok(back.tacks >= 1, 'beating home'); assert.ok(back.hours > out.hours, 'slower upwind')
  assert.ok(back.track.some(r => r.pointOfSail === 'close-hauled') && back.track.some(r => r.sails.reef >= 0))
  assert.ok(back.manoeuvres.every(x => x.sim.samples.length > 10))
})
