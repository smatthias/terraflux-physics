import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { parseCurrentTable, currentModel, driftFields, tableCoverage, setOf, fromSetDrift, DRIFT } from './currents.js'
import { frameOf, grid } from './stationTestFrames.js'
import { inverseProject, KT_PER_MS, cellLocation } from './station.js'
import { validateSequence } from './data.js'
import { landMaskFromCountries } from './geography.js'
import { sailRoute } from './sailRoute.js'
import { sailingReport, pointReadout } from './sailing.js'

const here = dirname(fileURLToPath(import.meta.url)), pub = join(here, '..', 'public')
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
const T0 = Date.parse('2026-11-01T12:00:00Z')
const G = grid({ nx: 10, ny: 10, origin_east_m: -50000, origin_north_m: -50000, spacing_east_m: 10000, spacing_north_m: 10000 })
const run = (u, v, hours = 24) => Array.from({ length: hours + 1 }, (_, h) => frameOf({ g: G, u, v, elapsed: h * 3600 }))
const at = (east, north) => inverseProject(G, east, north)

test('set and drift: the direction the water flows towards, in knots', () => {
  const [u, v] = fromSetDrift(90, 2); near(u, 2 / KT_PER_MS, 1e-12); near(v, 0, 1e-12); near(setOf(u, v), 90, 1e-9)
  near(setOf(...fromSetDrift(225, 1)), 225, 1e-9)
})

test('CSV tables: u/v, set/drift and knots; stations by name or position; interpolation in time; errors said', () => {
  const p = at(0, 0)
  const csv = `# synthetic: true\n# source: a test\ntime_utc,station,lat,lon,set_deg,drift_kt\n2026-11-01T12:00Z,S1,${p.lat},${p.lon},90,1\n2026-11-01T14:00Z,S1,${p.lat},${p.lon},90,3\n2026-11-01T12:00Z,S2,${p.lat + 0.2},${p.lon},0,1`
  const t = parseCurrentTable(csv, { name: 'test' })
  assert.equal(t.format, 'csv'); assert.equal(t.kind, 'total'); assert.equal(t.synthetic, true); assert.equal(t.source, 'a test')
  assert.deepEqual(t.stations.map(s => s.id), ['S1', 'S2']); assert.deepEqual(t.span, [T0, T0 + 2 * 3.6e6])
  const mid = t.sample(0, T0 + 3.6e6); near(Math.hypot(mid.u, mid.v) * KT_PER_MS, 2, 1e-9, 'halfway between 1 and 3 kt'); near(setOf(mid.u, mid.v), 90, 1e-9)
  assert.equal(t.sample(0, T0 - 1), null, 'nothing outside the series')
  const uv = parseCurrentTable('time,lat,lon,uo,vo\n2026-11-01 12:00,45,10,0.5,-0.5\n', { kind: 'tidal' }); assert.equal(uv.kind, 'tidal'); near(uv.sample(0, T0).v, -0.5, 1e-12)
  assert.equal(parseCurrentTable('# kind: tidal\ntime;lat;lon;u_kt;v_kt\n2026-11-01T12:00Z;45;10;1;0').kind, 'tidal', 'semicolons, knots, a kind in the comments')
  assert.throws(() => parseCurrentTable('lat,lon,u,v\n45,10,1,1'), /No time column/)
  assert.throws(() => parseCurrentTable('time,lat,lon,speed\n2026-11-01T12:00Z,45,10,1'), /No current columns/)
  assert.throws(() => parseCurrentTable('time,lat,lon,u,v\nyesterday,45,10,1,1'), /Unreadable time/)
  assert.throws(() => parseCurrentTable('time,lat,lon,u,v\n2026-11-01T12:00Z,95,10,1,1'), /out of range/)
})

test('NOAA CO-OPS current predictions: flood and ebb along the major axis, the slack-and-maximum curve, the position', () => {
  const cp = [{ Time: '2026-11-01 12:00', Velocity_Major: 1.5, meanFloodDir: 40, meanEbbDir: 220, Bin: '1', Depth: '10' }, { Time: '2026-11-01 13:00', Velocity_Major: -0.5, meanFloodDir: 40, meanEbbDir: 220, Bin: '1', Depth: '10' }]
  const t = parseCurrentTable(JSON.stringify({ lat: 45, lng: 10, current_predictions: { units: 'knots', cp } }))
  assert.equal(t.format, 'noaa'); assert.equal(t.kind, 'tidal', 'predictions are the tidal stream only')
  const a = t.sample(0, T0); near(setOf(a.u, a.v), 40, 1e-9); near(Math.hypot(a.u, a.v) * KT_PER_MS, 1.5, 1e-9, 'flood')
  const b = t.sample(0, T0 + 3.6e6); near(setOf(b.u, b.v), 220, 1e-9); near(Math.hypot(b.u, b.v) * KT_PER_MS, 0.5, 1e-9, 'ebb')
  assert.throws(() => parseCurrentTable(JSON.stringify({ current_predictions: { cp } })), /no station position/)
  near(parseCurrentTable(JSON.stringify({ current_predictions: { cp } }), { position: { lat: 45, lon: 10 } }).stations[0].lat, 45, 0, 'the position given')
  const cms = parseCurrentTable(JSON.stringify({ lat: 45, lng: 10, current_predictions: { units: 'cm/s', cp: [{ ...cp[0], Velocity_Major: 100 }] } }))
  near(Math.hypot(cms.sample(0, T0).u, cms.sample(0, T0).v), 1, 1e-9, '100 cm/s')
  // MAX_SLACK: slack 12:00, max flood 2 kt 15:00, slack 18:00: a sine curve between the events.
  const slack = parseCurrentTable(JSON.stringify({ lat: 45, lng: 10, current_predictions: { cp: [
    { Time: '2026-11-01 12:00', Type: 'slack', Velocity_Major: 0, meanFloodDir: 40, meanEbbDir: 220 },
    { Time: '2026-11-01 15:00', Type: 'flood', Velocity_Major: 2, meanFloodDir: 40, meanEbbDir: 220 },
    { Time: '2026-11-01 18:00', Type: 'slack', Velocity_Major: 0, meanFloodDir: 40, meanEbbDir: 220 }] } }))
  const kt = ms => { const s = slack.sample(0, ms); return Math.hypot(s.u, s.v) * KT_PER_MS }
  near(kt(T0), 0, 1e-9); near(kt(T0 + 1.5 * 3.6e6), 2 * Math.sin(Math.PI / 4), 1e-9); near(kt(T0 + 3 * 3.6e6), 2, 1e-9); near(kt(T0 + 4.5 * 3.6e6), 2 * Math.sin(Math.PI / 4), 1e-9)
})

test('tidal stream atlas: hours from high water, rates between neaps and springs by the range', () => {
  const doc = { schema: 'terraflux.tidal-stream/v1', reference_port: { name: 'Port', spring_range_m: 4, neap_range_m: 2, high_waters: [{ time: '2026-11-01T12:00Z', range_m: 4 }, { time: '2026-11-02T00:25Z', range_m: 3 }] },
    diamonds: [{ id: 'A', lat: 45, lon: 10, hours: [{ hour: -6, set_deg: 350, spring_kt: 2, neap_kt: 1 }, { hour: 0, set_deg: 10, spring_kt: 0, neap_kt: 0 }, { hour: 6, set_deg: 190, spring_kt: 2, neap_kt: 1 }] }] }
  const t = parseCurrentTable(JSON.stringify(doc))
  assert.equal(t.format, 'tidal-stream'); assert.equal(t.kind, 'tidal'); assert.equal(t.stations[0].id, 'A')
  const s = (ms) => { const x = t.sample(0, ms); return { kt: Math.hypot(x.u, x.v) * KT_PER_MS, set: setOf(x.u, x.v) } }
  near(s(T0).kt, 0, 1e-9, 'slack at high water'); near(s(T0 - 3 * 3.6e6).kt, 1, 1e-9, 'springs, halfway between -6 and HW')
  near(s(T0 - 3 * 3.6e6).set, 0, 1e-6, 'the set interpolated across north (350 -> 10)')
  near(s(T0 + 12.4167 * 3.6e6 + 6 * 3.6e6 - 1).kt, 1.5, 1e-3, 'range 3 m: halfway between neaps (1 kt) and springs (2 kt)')
  assert.equal(t.sample(0, T0 - 7 * 3.6e6), null, 'more than 6.5 h from any high water')
  assert.throws(() => parseCurrentTable(JSON.stringify({ ...doc, reference_port: { ...doc.reference_port, spring_range_m: 1 } })), /spring_range_m > neap_range_m/)
  assert.throws(() => parseCurrentTable(JSON.stringify({ diamonds: [] })), /high_waters/)
  assert.throws(() => parseCurrentTable('{"foo":1}'), /Unknown JSON/)
})

test('wind drift: 2.5 % of the wind, to the right of it in the northern hemisphere, building up over hours', () => {
  const frames = run(10, 0, 48), d = driftFields(frames)           // 10 m/s towards the east
  near(d[0].u[0], DRIFT.factor * 10 * Math.cos(DRIFT.deflectionDeg * Math.PI / 180), 1e-6, 'the first frame: from rest at the wind of the frame')
  near(setOf(d[48].u[5], d[48].v[5]), 90 + DRIFT.deflectionDeg, 1e-4, 'turned 20 degrees to the right of the wind (towards ESE)')
  near(Math.hypot(d[48].u[5], d[48].v[5]), 0.25, 1e-6)
  // A wind that starts at hour 12: the drift builds with the lag.
  const late = Array.from({ length: 25 }, (_, h) => frameOf({ g: G, u: h >= 12 ? 10 : 0, v: 0, elapsed: h * 3600 })), dl = driftFields(late)
  near(Math.hypot(dl[18].u[0], dl[18].v[0]), 0.25 * (1 - Math.exp(-1)), 0.01, 'about 6 h after the wind began: 63 %')
  const south = driftFields([frameOf({ g: { ...G, geographic_anchor_latitude_deg: -30 }, u: 10, v: 0 })]); near(setOf(south[0].u[0], south[0].v[0]), 90 - DRIFT.deflectionDeg, 1e-4, 'to the left in the south')
  const sea = new Uint8Array(100).fill(1); sea[3] = 0; assert.equal(driftFields(frames, { sea })[5].u[3], 0, 'none on land')
})

test('the current model: ocean model first, then tables (total replaces, tidal adds the drift), then wind drift', () => {
  const frames = run(10, 0), c0 = at(0, 0)
  const rows = Array.from({ length: 13 }, (_, k) => `${new Date(T0 - 12 * 3.6e6 + k * 3 * 3.6e6).toISOString()},${c0.lat},${c0.lon},0,2`).join('\n')
  const total = parseCurrentTable(`time,lat,lon,set_deg,drift_kt\n${rows}`)
  const m = currentModel(frames, { tables: [total] })
  const x = m.at(0, 0, T0 + 3.6e6); assert.equal(x.source, 'table'); near(x.v, 2 / KT_PER_MS, 1e-6, 'at the station the table is the current'); near(x.u, 0, 1e-6)
  const far = m.at(40000, 40000, T0 + 3.6e6); assert.equal(far.source, 'drift', 'beyond the radius (30 km): the drift')
  const mid = m.at(15000, 0, T0 + 3.6e6); assert.equal(mid.source, 'table'); assert.ok(mid.v < x.v && mid.u > 0, 'blended with the drift towards the edge of the radius')
  const tidal = { ...total, kind: 'tidal' }, mt = currentModel(frames, { tables: [tidal] }), y = mt.at(0, 0, T0 + 6 * 3.6e6), d = mt.at(40000, 40000, T0 + 6 * 3.6e6)
  near(y.v, 2 / KT_PER_MS + d.v, 1e-6, 'a tidal stream plus the wind drift'); near(y.u, d.u, 1e-6)
  assert.equal(currentModel(frames, { tables: [total], settings: { drift: false } }).at(40000, 40000, T0), null, 'nothing known there')
  // An ocean model wins.
  const ocean = { mask: Array(100).fill(1), u: Array(100).fill(0.3), v: Array(100).fill(0) }
  const mo = currentModel(run(10, 0).map(f => ({ ...f, ocean })), { tables: [total] }), o = mo.at(0, 0, T0)
  assert.equal(o.source, 'ocean'); near(o.u, 0.3, 1e-9)
  // The field of a frame and a reading for the reports.
  const f = m.field(1), i = cellLocation(G, 55) && 55
  assert.equal(f.source.length, 100); assert.ok([...f.source].every(s => s === 2 || s === 3))
  const r = m.reading(0, T0); assert.equal(r.source, 'drift'); assert.match(r.sourceLabel, /wind drift/); near(r.setDeg, 110, 1e-3)
  assert.equal(m.stations.length, 1); assert.ok(i)
  assert.deepEqual(tableCoverage(total, frames), { inside: 1, total: 1, overlapHours: 24, runHours: 24 })
  assert.equal(parseCurrentTable(`time,lat,lon,u,v\n${new Date(T0).toISOString()},${c0.lat},${c0.lon},1,0\n${new Date(T0 + 12 * 3.6e6).toISOString()},${c0.lat},${c0.lon},1,0`).sample(0, T0 + 6 * 3.6e6), null, 'no interpolation across a gap of more than 6 h')
})

test('the shipped example tables: the Baltic straits (CSV) and the German Bight tidal streams, both marked synthetic', () => {
  const baltic = parseCurrentTable(readFileSync(join(pub, 'currents', 'baltic-straits-example.csv'), 'utf8'), { name: 'Baltic straits' })
  assert.equal(baltic.synthetic, true); assert.equal(baltic.kind, 'total'); assert.equal(baltic.stations.length, 5)
  const frames = validateSequence(JSON.parse(readFileSync(join(pub, 'baltic-sailing', 'engine-timeline.json'), 'utf8')))
  const cov = tableCoverage(baltic, frames); assert.equal(cov.inside, 5); near(cov.overlapHours, cov.runHours, 1e-9, 'covers the whole run')
  const world = JSON.parse(readFileSync(join(pub, 'geo', 'countries-110m.geojson'), 'utf8')), g = frames[0].forecast.grid
  const sea = Uint8Array.from(landMaskFromCountries(world, g.nx * g.ny, k => cellLocation(g, k)), v => (v ? 0 : 1))
  const m = currentModel(frames, { sea, tables: [baltic] }), f = m.field(10)
  assert.ok([...f.source].some(s => s === 2) && [...f.source].some(s => s === 3), 'table currents near the straits, wind drift elsewhere')
  for (let k = 0; k < f.u.length; k++) if (!sea[k]) assert.equal(f.source[k], 0, 'no current on land')
  const bight = parseCurrentTable(readFileSync(join(pub, 'currents', 'german-bight-tidal-stream-example.json'), 'utf8'))
  assert.equal(bight.synthetic, true); assert.equal(bight.kind, 'tidal'); assert.equal(bight.stations.length, 4)
  const germany = validateSequence(JSON.parse(readFileSync(join(pub, 'gafor-germany', 'engine-timeline.json'), 'utf8')))
  const gc = tableCoverage(bight, germany); assert.equal(gc.inside, 4); assert.ok(gc.overlapHours >= 72)
})

test('the currents reach the report, the point read-out and the routing', () => {
  // A westerly (15.6 kt): a wind drift of 0.2 m/s setting ESE everywhere; the mark north of the start (a beam reach).
  const frames = Array.from({ length: 25 }, (_, h) => frameOf({ g: grid({ nx: 24, ny: 24, origin_east_m: -120000, origin_north_m: -120000, spacing_east_m: 10000, spacing_north_m: 10000 }), u: 8, v: 0, elapsed: h * 3600 }))
  const g = frames[0].forecast.grid, m = currentModel(frames), currentAt = i => m.reading(i, T0 + 12 * 3.6e6)
  const r = sailingReport(frames[12], 300, { currentAt })
  assert.equal(r.current.source, 'drift'); near(r.current.setDeg, 110, 1e-3); near(r.current.driftKt, 0.2 * KT_PER_MS, 1e-6); assert.ok(r.notes.some(n => /wind drift estimated/.test(n)))
  const p = pointReadout(frames[12], 301, { currentAt }); assert.equal(p.current.source, 'drift')
  const P = (e, n) => inverseProject(g, e, n)
  const route = sailRoute({ frames, points: [{ name: 'S', ...P(0, -80000) }, { name: 'N', ...P(0, 80000) }], currents: m, compare: false })
  assert.equal(route.ok, true, route.reason)
  const row = route.track[2]; assert.equal(row.current.source, 'drift'); near(row.current.driftKt, 0.2 * KT_PER_MS, 0.02)
  const crab = ((row.headingDeg - row.cogDeg + 540) % 360) - 180; assert.ok(crab < -1, `heading up into the set (${crab.toFixed(1)}°)`)
  const still = sailRoute({ frames, points: [{ name: 'S', ...P(0, -80000) }, { name: 'N', ...P(0, 80000) }], compare: false })
  assert.equal(still.track[2].current, null, 'without the current model: no current (no ocean)')
})
