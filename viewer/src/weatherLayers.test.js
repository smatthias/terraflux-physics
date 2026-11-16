import test from 'node:test'
import assert from 'node:assert/strict'
import { weatherSymbol, weatherColumn, cloudFamily, SYMBOLS, LAYER_COLOURS } from './weatherLayers.js'
import { drawWeatherIcon } from './weatherIcons.js'
import { windsAloftField } from './windsAloft.js'
import { frameOf, grid } from './stationTestFrames.js'
import { FT_PER_M } from './station.js'

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
const n = 9, fill = v => Array(n).fill(v)
// One cell per situation: the cloud (type, base, top, fraction) and what falls (type, intensity, rate, lightning, hail).
function scene(over = {}) {
  const rows = [
    ['none', null, null, 0, 'none', 'none', 0, false, 0],
    ['cumulus', 1200, 2000, 0.3, 'none', 'none', 0, false, 0],
    ['altocumulus', 3000, 3800, 0.5, 'none', 'none', 0, false, 0],
    ['stratocumulus', 700, 1400, 0.6, 'none', 'none', 0, false, 0],
    ['nimbostratus', 500, 5000, 1.0, 'rain', 'moderate', 4, false, 0],
    ['cumulonimbus', 600, 11000, 0.9, 'rain_showers', 'heavy', 20, true, 2],
    ['nimbostratus', 400, 4000, 1.0, 'snow', 'moderate', 1, false, 0],
    ['stratus', 150, 600, 0.9, 'drizzle', 'light', 0.3, false, 0],
    ['cumulus', 1000, 3000, 0.3, 'rain_showers', 'moderate', 5, false, 0],
  ]
  const col = k => rows.map(r => r[k])
  return frameOf({
    t: 283.15,
    coupled: { pressure: fill(101325), humidity: fill(70) },
    clouds: { type: col(0), base: col(1), top: col(2), fraction: col(3), thunderstorm: rows.map(r => r[0] === 'cumulonimbus') },
    precipitation: { type: col(4), intensity: col(5), rate: col(6), lightning: col(7), hail: col(8), virga: fill(false), freezingLevel: fill(1500), baseTemperature: fill(280), topTemperature: rows.map(r => (r[0] === 'cumulonimbus' ? 215 : 258)), updraft: fill(0) },
    ...over,
  })
}

test('a symbol tells the weather at a glance: thunder and hail first, then what falls, then fog, then the sky', () => {
  const f = scene()
  assert.deepEqual([...Array(9).keys()].map(i => weatherSymbol(f, i)), ['clear', 'few', 'partly', 'cloudy', 'rain', 'hail', 'snow', 'drizzle', 'showers'])
  for (const k of Object.keys(SYMBOLS)) assert.ok(SYMBOLS[k].label)
  // Without hail, a thunderstorm; overcast from 8 oktas.
  const g = scene(); g.precipitation.hail[5] = 0; g.clouds.fraction[3] = 1
  assert.equal(weatherSymbol(g, 5), 'thunder'); assert.equal(weatherSymbol(g, 3), 'overcast')
  // Humid air with no precipitation: fog or mist.
  const fog = scene({ coupled: { pressure: fill(101325), humidity: fill(99.5) } })
  assert.equal(weatherSymbol(fog, 0), 'fog')
  const mist = scene({ coupled: { pressure: fill(101325), humidity: fill(98) } })
  assert.equal(weatherSymbol(mist, 0), 'mist')
  // A run with no cloud or precipitation model has nothing to show.
  assert.equal(weatherSymbol(frameOf(), 0), null)
})

test('the column over a point: freezing level, cloud, icing, thunderstorm and turbulent layers in feet above sea level', () => {
  const f = { ...scene(), terrain: { values: fill(300) } }, elevFt = 300 * FT_PER_M
  const cb = weatherColumn(f, 5)
  near(cb.elevFt, elevFt, 1e-9); near(cb.freezingFt, elevFt + 1500 * FT_PER_M, 1e-9, 'the kernel freezing level over the ground')
  near(cb.cloud.baseFt, elevFt + 600 * FT_PER_M, 1e-9); near(cb.cloud.topFt, elevFt + 11000 * FT_PER_M, 1e-9)
  assert.ok(cb.icing && cb.icing.fromFt >= cb.cloud.baseFt && cb.icing.toFt <= cb.cloud.topFt, 'icing inside the cloud')
  near(cb.thunder.topFt, cb.cloud.topFt, 1e-9, 'the storm column reaches the cloud top')
  assert.equal(cloudFamily(cb.cloud, cb.elevFt), 'cb')
  const clear = weatherColumn(f, 0)
  assert.equal(clear.cloud, null); assert.equal(clear.icing, null); assert.equal(clear.thunder, null); assert.deepEqual(clear.turbulence, [])
  assert.equal(cloudFamily(weatherColumn(f, 3).cloud, elevFt), 'low'); assert.equal(cloudFamily(weatherColumn(f, 2).cloud, elevFt), 'middle')
  // Without a precipitation kernel the freezing level comes from the surface temperature at 6.5 K/km.
  const warm = frameOf({ t: 288.15 })
  near(weatherColumn(warm, 0).freezingFt, 15 / 0.0065 * FT_PER_M, 1e-6)
  near(weatherColumn(frameOf({ t: 263.15 }), 0).freezingFt, 0, 1e-9, 'below freezing at the ground')
  for (const k of ['low', 'middle', 'high', 'cb', 'icing', 'turbulence', 'thunder', 'freezing']) assert.match(LAYER_COLOURS[k], /^#[0-9a-f]{6}$/)
})

test('turbulent layers come from the winds-aloft shear', () => {
  const g = grid({ nx: 9, ny: 9, origin_east_m: -450000, origin_north_m: -450000 })
  const T = Array.from({ length: 81 }, (_, i) => 288 - (Math.floor(i / 9) - 4) * 100000 * 4e-5)     // a strong planar gradient: strong shear
  const f = frameOf({ g, t: i => T[i], coupled: { pressure: Array(81).fill(101325) } })
  const c = weatherColumn(f, 40, windsAloftField(f))
  assert.ok(c.turbulence.length > 0 && c.turbulence.every(t => t.shear >= 6 && t.toFt > t.fromFt))
  const calm = frameOf({ g, coupled: { pressure: Array(81).fill(101325) } })
  assert.deepEqual(weatherColumn(calm, 40, windsAloftField(calm)).turbulence, [])
})

test('every symbol can be painted', () => {
  const calls = []
  const ctx = new Proxy({}, { get: (o, k) => (k in o ? o[k] : typeof k === 'string' ? (...args) => { calls.push(k); return { addColorStop() {} } } : undefined), set: (o, k, v) => { o[k] = v; return true } })
  for (const key of [...Object.keys(SYMBOLS), 'unknown']) { const before = calls.length; drawWeatherIcon(ctx, key, 64); assert.ok(calls.length > before + 2, `${key} draws something`) }
})
