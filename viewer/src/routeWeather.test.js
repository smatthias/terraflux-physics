import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { routeWeather, rangesOf, stretchLine, MAX_STRETCHES, GAMET_SIG_CLD_FT } from './routeWeather.js'
import { planFlight } from './flightPlan.js'
import { runwayPair, siteAirports } from './airports.js'
import { windsAloftField } from './windsAloft.js'
import { validateSequence } from './data.js'
import { frameOf, grid } from './stationTestFrames.js'
import { FT_PER_M } from './station.js'

const here = dirname(fileURLToPath(import.meta.url))
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
const ap = (id, x, y) => ({ id, name: id, i: y * 10 + x, x, y, elevationM: 0, elevationFt: 0, runwayDeg: 90, runway: runwayPair(90) })

// A 10 x 10 grid of 100 km cells, hours 0-6. West of column 4 clear; columns 4-5 a thunderstorm line; east of it an
// overcast stratus deck at 300 ft with drizzle and 97 % humidity (low visibility); a 2 000 m ridge in column 8.
function run({ terrain = false } = {}) {
  const g = grid({ nx: 10, ny: 10, origin_east_m: -500000, origin_north_m: -500000 }), n = 100, col = i => i % 10
  const pick = (w, cb, e) => Array.from({ length: n }, (_, i) => (col(i) < 4 ? w : col(i) < 6 ? cb : e))
  const clouds = { type: pick('none', 'cumulonimbus', 'stratus'), base: pick(null, 1200, 300 / FT_PER_M), top: pick(null, 11000, 1500), fraction: pick(0, 0.9, 1), thunderstorm: pick(false, true, false) }
  const precipitation = { type: pick('none', 'rain_showers', 'drizzle'), intensity: pick('none', 'heavy', 'light'), rate: pick(0, 12, 0.3), lightning: pick(false, true, false), hail: pick(0, 0, 0), virga: pick(false, false, false), freezingLevel: Array(n).fill(2500), baseTemperature: Array(n).fill(283), topTemperature: Array(n).fill(250), updraft: Array(n).fill(5) }
  const extra = { clouds, precipitation, coupled: { pressure: Array(n).fill(101325), humidity: pick(60, 80, 97) }, ...(terrain ? { terrain: { values: Array.from({ length: n }, (_, i) => (col(i) === 8 ? 2000 : 0)) } } : {}) }
  return [0, 1, 2, 3, 4, 5, 6].map(h => frameOf({ g, u: 0, v: 0, elapsed: h * 3600, ...extra }))
}

test('the weather at every route point, read when the aircraft passes it', () => {
  const frames = run(), A = ap('XA01', 1, 5), B = ap('XA02', 8, 5)
  const plan = planFlight({ frames, startIndex: 0, from: A, to: B, airports: [A, B], cruiseFt: 6000, tasKt: 120, aloftFor: () => null })
  const w = routeWeather(plan, frames)
  assert.equal(w.samples.length, plan.segments.length, 'one sample per route segment')
  w.samples.forEach((s, k) => {
    const seg = plan.segments[k]
    assert.equal(s.frameIndex, seg.frameIndex, 'the frame valid when the aircraft gets there'); assert.equal(s.startMs, seg.startMs)
    assert.ok(k === 0 || s.startMs > w.samples[k - 1].startMs, 'in the order flown')
    assert.equal(typeof s.night, 'boolean')
  })
  // West: clear and VFR; the storm line: CB and thunder; east: IFR/LIFR under the 300 ft deck in drizzle.
  const west = w.samples.find(s => s.distNm < 100), storm = w.samples.find(s => s.hazards.includes('thunderstorm')), east = w.samples.at(-2)
  assert.equal(west.surface.symbol, 'clear'); assert.equal(west.surface.category, 'VFR'); assert.equal(west.surface.cloud, null)
  assert.equal(storm.surface.cloud.convective, 'CB'); assert.ok(['thunder', 'hail'].includes(storm.surface.symbol))
  assert.ok(['IFR', 'LIFR'].includes(east.surface.category)); near(east.surface.ceilingAglFt, 300, 1); assert.equal(east.surface.symbol, 'drizzle')
  assert.ok(east.surface.visibilityM < 5000, 'humid drizzle: visibility under 5 km'); near(east.surface.qnhHpa, 1013.25, 1e-9)
  // In cloud (IMC) only where the aircraft is inside the deck (300-4 900 ft): in the descent, not at 6 000 ft.
  for (const s of w.samples.filter(x => x.surface.cloud?.genus === 'stratus')) assert.equal(s.hazards.includes('cloud'), s.altFt >= 300 && s.altFt <= 1500 * FT_PER_M + 1, `${Math.round(s.altFt)} ft`)
  // Stretches: contiguous, in order, at most 12, each with its worst weather.
  assert.ok(w.stretches.length >= 3 && w.stretches.length <= MAX_STRETCHES)
  near(w.stretches[0].fromNm, 0, 1e-9); near(w.stretches.at(-1).toNm, plan.lengthNm, 1e-6)
  w.stretches.forEach((s, k) => { if (k) near(s.fromNm, w.stretches[k - 1].toNm, 1e-9) })
  const last = w.stretches.at(-1)
  assert.ok(['IFR', 'LIFR'].includes(last.category)); assert.equal(last.cover, 'OVC'); assert.equal(stretchLine(last).replace(/vis [\d.]+ (km|m)/, 'vis X'), 'light drizzle, mist · OVC St 300–4900 ft · vis X')
  assert.equal(w.worst.symbol === 'thunder' || w.worst.symbol === 'hail', true, 'the most significant symbol of the route')
  assert.ok(['IFR', 'LIFR'].includes(w.worst.category))
  // The GAMET summary.
  const G = Object.fromEntries(w.gamet.map(x => [x.key, x]))
  assert.deepEqual(Object.keys(G), ['SFC WIND', 'SFC VIS', 'SIGWX', 'MT OBSC', 'SIG CLD', 'ICE', 'TURB', 'WIND/T', 'CLD', 'FZLVL', 'MNM QNH'])
  assert.equal(G['SFC WIND'].level, 'nil', 'calm')
  assert.equal(G.SIGWX.level, 'severe'); assert.match(G.SIGWX.text, /TS at \d+–\d+ NM/); assert.match(G.SIGWX.text, /heavy rain/)
  assert.equal(G['SIG CLD'].level, 'severe'); assert.match(G['SIG CLD'].text, new RegExp(`BKN/OVC below ${GAMET_SIG_CLD_FT} ft AGL at \\d+–\\d+ NM \\(down to 300 ft\\)`)); assert.match(G['SIG CLD'].text, /CB at/)
  assert.match(G['SFC VIS'].text, /km at|m at/); assert.equal(G['MT OBSC'].level, 'nil', 'flat')
  assert.equal(G['MNM QNH'].text, '1013 hPa'); assert.match(G.FZLVL.text, /8,200 ft AMSL/); assert.equal(G['TURB'].level, 'severe', 'turbulence in and near thunderstorms')
  assert.equal(routeWeather({ ok: false }, frames), null)
})

test('mountains obscured where the deck sits on the ridge; ranges of a test along the samples', () => {
  const frames = run({ terrain: true }), A = ap('XA01', 6, 5), B = ap('XA02', 9, 5)
  const plan = planFlight({ frames, startIndex: 0, from: A, to: B, airports: [A, B], cruiseFt: 12000, tasKt: 120, aloftFor: () => null })
  const w = routeWeather(plan, frames), mt = w.gamet.find(x => x.key === 'MT OBSC')
  assert.equal(mt.level, 'warning'); assert.match(mt.text, /Cloud on the high ground at \d+–\d+ NM/)
  assert.ok(w.samples.some(s => s.hazards.includes('mtobsc')))
  const r = rangesOf([{ fromNm: 0, toNm: 1, v: 1 }, { fromNm: 1, toNm: 2, v: 1 }, { fromNm: 2, toNm: 3, v: 0 }, { fromNm: 3, toNm: 4, v: 1 }], s => s.v)
  assert.deepEqual(r.map(x => [x.fromNm, x.toNm, x.items.length]), [[0, 2, 2], [3, 4, 1]])
})

test('engine fixture: the weather along a route over Germany', () => {
  const frames = validateSequence(JSON.parse(readFileSync(join(here, '..', 'public', 'gafor-germany', 'engine-timeline.json'), 'utf8')))
  const aps = siteAirports(frames, {}), cache = new WeakMap(), aloftFor = f => { if (!cache.has(f)) cache.set(f, windsAloftField(f)); return cache.get(f) }
  const plan = planFlight({ frames, startIndex: 4, from: aps[0], to: aps[1], airports: aps, cruiseFt: 4500, tasKt: 110, aloftFor })
  const w = routeWeather(plan, frames)
  assert.ok(w.samples.length >= 12 && w.stretches.length >= 1)
  assert.ok(w.samples.every(s => s.surface.visibilityM > 0 && s.surface.qnhHpa > 950 && s.surface.qnhHpa < 1050 && Number.isFinite(s.aloft.wind.kt)))
  assert.ok(w.samples.every(s => s.aloft.wind === plan.segments[s.k].wind), 'the winds aloft of the plan')
  assert.equal(w.gamet.length, 11)
})
