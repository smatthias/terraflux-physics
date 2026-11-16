import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import {
  windsAloftField, gradientOf, smoothField, planeFit, thermalBands, fbCode, strongestAloft, FB_LEVELS, ALL_LEVELS, OMEGA, GRAVITY,
  BOUNDARY_LAYER_M, TROPOPAUSE_M, BUOYANCY_FREQUENCY,
} from './windsAloft.js'
import { validateSequence } from './data.js'
import { frameOf, grid } from './stationTestFrames.js'
import { FT_PER_M, KT_PER_MS } from './station.js'

const here = dirname(fileURLToPath(import.meta.url)), root = join(here, '..', '..')
const fixture = name => validateSequence(JSON.parse(readFileSync(join(root, 'viewer', 'public', name, 'engine-timeline.json'), 'utf8')))
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
const ftOf = m => m * FT_PER_M

// A 9 x 9 grid of 100 km cells at 45 N with prescribed pressure and temperature fields.
const big = grid({ nx: 9, ny: 9, origin_east_m: -450000, origin_north_m: -450000 })
const f45 = 2 * OMEGA * Math.sin(Math.PI / 4)
const field = (fn, g = big) => Array.from({ length: g.nx * g.ny }, (_, i) => fn((i % g.nx - (g.nx - 1) / 2) * g.spacing_east_m, (Math.floor(i / g.nx) - (g.ny - 1) / 2) * g.spacing_north_m, i))

test('levels: the winds-aloft (FB) levels above the 10 m surface wind', () => {
  assert.deepEqual(FB_LEVELS.map(l => l.key), ['3000', '6000', '9000', '12000', 'FL180', 'FL240', 'FL300', 'FL340', 'FL390'])
  assert.equal(ALL_LEVELS[0].key, 'SFC'); assert.equal(FB_LEVELS[4].label, 'FL180'); assert.equal(FB_LEVELS[0].label, '3,000 ft')
})

test('gradients use the engine stencil: central inside, one-sided at the edges', () => {
  const g = grid({ nx: 3, ny: 2 }), v = [0, 1, 4, 10, 11, 14]
  const d = gradientOf(v, g)
  near(d.east[0], 1e-5, 1e-18); near(d.east[1], 2e-5, 1e-18); near(d.east[2], 3e-5, 1e-18)
  near(d.north[0], 1e-4, 1e-18); near(d.north[4], 1e-4, 1e-18)
})

test('without a pressure field there are no winds aloft, and the report says why', () => {
  const w = windsAloftField(frameOf())
  assert.equal(w.available, false); assert.match(w.reason, /no pressure field/)
})

test('geostrophic wind at the top of the boundary layer: low to the left of the wind in the north', () => {
  // Pressure falling northward by 1 hPa per 100 km: a westerly geostrophic wind u = -(1/(rho f)) dp/dy.
  const f = frameOf({ g: big, coupled: { pressure: field((x, y) => 101300 - y * 1e-3) } })
  const w = windsAloftField(f), top = w.at(40, ftOf(BOUNDARY_LAYER_M))
  near(top.u, 1e-3 / (1.2 * f45), 1e-9, 'u_g'); near(top.v, 0, 1e-9); near(top.fromDeg, 270, 1e-6)
  // In the boundary layer: a straight blend from the 10 m wind (calm here) to the geostrophic wind.
  near(w.at(40, ftOf(500)).u, top.u / 2, 1e-9)
  assert.equal(w.at(40, 0).kt, 0, 'the surface level is the engine\'s 10 m wind')
  // The severe kernel's density is used when the run has one.
  const dense = windsAloftField({ ...f, severe: { config: { geostrophic_air_density_kg_m3: 2.4 } } })
  near(dense.at(40, ftOf(1000)).u, top.u / 2, 1e-9)
})

test('a synoptic (planar) temperature gradient gives the engine\'s thermal wind through the troposphere', () => {
  // Colder to the north by 1 K per 100 km: westerlies increasing with height, dV/dz = (g/(f T)) k x grad T.
  const T = field((x, y) => 288 - y * 1e-5)
  const f = frameOf({ g: big, t: i => T[i], coupled: { pressure: Array(81).fill(101325) } })
  const w = windsAloftField(f), i = 40, rate = GRAVITY / (f45 * T[i]) * 1e-5
  for (const z of [1000, 3000, 6000, 10000]) near(w.at(i, ftOf(z)).u, rate * z, 1e-9, `thermal wind at ${z} m`)
  near(w.at(i, ftOf(TROPOPAUSE_M + 1000)).u, rate * TROPOPAUSE_M, 1e-9, 'constant above the tropopause')
  // Temperatures aloft: the ISA lapse rate from the surface temperature.
  near(w.at(i, ftOf(5000)).temperatureK, T[i] - 32.5, 1e-9)
  assert.ok(w.at(i, ftOf(9000)).kt > 50 && w.at(i, ftOf(9000)).kt < 70, `a realistic jet for a realistic gradient (${w.at(i, ftOf(9000)).kt.toFixed(0)} kt)`)
})

test('a shallow, small temperature anomaly stays near the ground (quasi-geostrophic depth f L / N)', () => {
  const blob = field((x, y) => 288 + 4 * Math.exp(-(x * x + y * y) / (2 * 100e3 ** 2)))
  const f = frameOf({ g: big, t: i => blob[i], coupled: { pressure: Array(81).fill(101325) } })
  const w = windsAloftField(f), i = 41                      // next to the centre: a strong local gradient
  const rate = Math.hypot(...(() => { const g = gradientOf(blob, big); return [g.east[i], g.north[i]] })()) * GRAVITY / (f45 * blob[i])
  const low = w.at(i, ftOf(200)).speed, high = w.at(i, ftOf(10000)).speed
  near(low, rate * 200, rate * 200 * 0.1, 'near the ground the local thermal wind, as the engine kernel')
  assert.ok(high < rate * 10000 * 0.35, `aloft far less than the local gradient extrapolated (${high.toFixed(1)} vs ${(rate * 10000).toFixed(1)} m/s)`)
  // The bands add back up to the field: residual bands + plane = temperature.
  const bands = thermalBands(blob, big, f45), plane = planeFit(blob, big)
  assert.equal(bands[0].depth, Infinity); near(bands[1].depth, f45 * 100e3 / BUOYANCY_FREQUENCY, 1e-6)
  const smooth = smoothField(blob.map((t, k) => t - plane.at(k)), big, 1600e3)
  assert.ok(smooth.every(Number.isFinite))
})

test('no Coriolis force at the equator: the wind neither turns nor grows with height', () => {
  const g = grid({ geographic_anchor_latitude_deg: 0 })
  const w = windsAloftField(frameOf({ g, u: 5, v: 1, coupled: { pressure: field((x, y) => 101300 - y * 1e-3, g) } }))
  assert.equal(w.rotating, false); assert.match(w.note, /No Coriolis/)
  near(w.at(4, 30000).u, 5, 1e-9); near(w.at(4, 30000).v, 1, 1e-9)
})

test('the profile follows the FB rules near the ground and flags clear-air-turbulence shear', () => {
  const T = field((x, y) => 288 - y * 2e-5)
  const f = frameOf({ g: big, t: i => T[i], coupled: { pressure: Array(81).fill(101325) }, terrain: { values: Array(81).fill(1200) } })
  const p = windsAloftField(f).profile(40)
  near(p.elevationFt, 1200 * FT_PER_M, 1e-9)
  const row = k => p.rows.find(r => r.level.key === k)
  assert.equal(row('3000').below, true, '3 000 ft is within 1 500 ft of a 3 937 ft station'); assert.equal(row('3000').fb, null)
  assert.equal(row('6000').below, false); assert.equal(row('6000').temperatureC, null, 'no temperature within 2 500 ft of the station')
  assert.notEqual(row('9000').temperatureC, null)
  assert.match(row('FL340').fb, /^\d{6}$/, 'unsigned temperature above FL240')
  assert.match(row('FL240').fb, /^\d{4}[+-]\d\d$/)
  assert.ok(row('FL180').shearKtPer1000ft > 3, 'a strong gradient shears the wind')
  assert.equal(p.strongest.level.key, 'FL390')
})

test('FB codes: tens of degrees and knots, +50 for 100-199 kt, 9900 light and variable, temperatures by level', () => {
  assert.equal(fbCode(270, 14, null, 3000), '2714')
  assert.equal(fbCode(275, 25, 0, 6000), '2825+00')
  assert.equal(fbCode(230, 145, -53, 39000), '734553')
  assert.equal(fbCode(10, 105, -21, 24000), '5105-21')
  assert.equal(fbCode(90, 3, -5, 9000), '9900-05')
  assert.equal(fbCode(355, 40, 5, 12000), '3640+05')
  assert.equal(fbCode(300, 230, -60, 34000), '809960', '200 kt or more is coded 199')
  assert.equal(fbCode(90, 20, 10, 3000), '0920', 'no temperature at 3 000 ft')
})

test('engine fixture: where the temperature is smooth the 0-6 km shear equals the severe kernel\'s deep shear', () => {
  const frames = fixture('supercell'), diffs = []
  for (const d of frames) {
    const w = windsAloftField(d), fl = d.forecast.fields
    for (let i = 0; i < fl.wind_east.values.length; i++) {
      const top = w.at(i, ftOf(6000)), s6 = Math.hypot(top.u - fl.wind_east.values[i], top.v - fl.wind_north.values[i])
      diffs.push(Math.abs(s6 - d.severe.shear6[i]))
    }
  }
  diffs.sort((a, b) => a - b)
  assert.ok(diffs[Math.floor(diffs.length / 2)] < 0.5, `median difference ${diffs[Math.floor(diffs.length / 2)]} m/s`)
  assert.ok(diffs[Math.floor(diffs.length / 4)] < 1e-3, 'a quarter of the cells agree to a millimetre per second')
})

test('engine fixtures: winds aloft stay within observed jet-stream speeds on every coupled run', () => {
  for (const name of ['coupled', 'clouds', 'hurricane', 'alps', 'supercell']) {
    const frames = fixture(name)
    for (const d of [frames[1], frames.at(-1)]) {
      const w = windsAloftField(d), best = strongestAloft(w, d.forecast.grid)
      assert.ok(best.kt < 250, `${name}: strongest ${best.kt.toFixed(0)} kt at ${best.level.key}`)
    }
  }
  const hurricane = fixture('hurricane')[2], sw = hurricane.storms.env.storm_wind_speed_m_s
  let eye = 0; sw.forEach((v, j) => { if (v > sw[eye]) eye = j })
  const p = windsAloftField(hurricane).profile(eye)
  assert.ok(p.rows[0].kt > p.rows.at(-1).kt, 'the warm-core cyclone weakens with height')
  near(p.rows[0].kt, sw[eye] * KT_PER_MS, 1e-6, 'at the surface the storm wind')
})
