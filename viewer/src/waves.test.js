import test from 'node:test'
import assert from 'node:assert/strict'
import { G, wavenumber, windSea, seaState, fetchField, cycloneWind, waveField, drawnWave, drawnBand, OPEN_FETCH_M, CYCLONE_FETCH_M, DRAWN_SPEED } from './waves.js'

const close = (a, b, rel = 0.02) => Math.abs(a - b) <= rel * Math.abs(b)

test('dispersion: deep water L = g T^2 / 2 pi, shallow water c = sqrt(g h)', () => {
  const T = 10, k = wavenumber(T)
  assert.ok(close(2 * Math.PI / k, G * T * T / (2 * Math.PI), 1e-9), 'deep')
  const h = 2, kS = wavenumber(T, h), c = (2 * Math.PI / T) / kS
  assert.ok(close(c, Math.sqrt(G * h), 0.03), `shallow ${c}`)
  const kM = wavenumber(T, 30), w = 2 * Math.PI / T
  assert.ok(Math.abs(G * kM * Math.tanh(kM * 30) - w * w) < 1e-9, 'intermediate depth solves the relation')
  assert.ok(kM > k && kM < kS, 'waves shorten towards the shore')
})

test('wind seas grow with fetch to the fully developed sea, and break in the shallows', () => {
  const U = 15
  const short = windSea(U, 10e3), long = windSea(U, 200e3), ocean = windSea(U, 5000e3)
  assert.ok(short.hs < long.hs && long.hs <= ocean.hs && short.tp < long.tp)
  assert.equal(short.regime, 'fetch-limited')
  assert.equal(ocean.regime, 'fully developed')
  assert.ok(close(ocean.hs, 0.243 * U * U / G, 1e-9) && close(ocean.tp, 7.14 * U / G, 1e-9), 'Pierson–Moskowitz limit')
  assert.ok(close(windSea(10, 100e3).hs, 1.6e-3 * 10 * Math.sqrt(100e3 / G), 1e-9), 'JONSWAP height')
  const shallow = windSea(25, 400e3, 3)
  assert.equal(shallow.regime, 'depth-limited'); assert.ok(close(shallow.hs, 1.8, 1e-9))
  assert.ok(shallow.speed < ocean.speed, 'slower in shallow water')
  assert.ok(close(ocean.group, ocean.speed / 2, 1e-6), 'deep-water group speed is half the phase speed')
  assert.equal(windSea(0.2, 100e3).regime, 'calm'); assert.equal(windSea(10, 0).hs, 0)
  // A familiar check: a 20 m/s gale over 500 km raises about 7 m waves at 12–14 s, below the fully developed 14.6 s.
  const gale = windSea(20, 500e3)
  assert.ok(gale.hs > 5 && gale.hs < 8 && gale.tp > 11 && gale.tp < 14.6, `gale ${gale.hs.toFixed(1)} m ${gale.tp.toFixed(1)} s`)
})

test('the sea state follows the Douglas scale', () => {
  assert.equal(seaState(0), 'calm (glassy)'); assert.equal(seaState(0.3), 'smooth'); assert.equal(seaState(2), 'moderate')
  assert.equal(seaState(3.2), 'rough'); assert.equal(seaState(7), 'high'); assert.equal(seaState(20), 'phenomenal')
})

test('fetch is the open water upwind, to the coast or the map edge plus the open-ocean allowance', () => {
  // 6 x 1: land in column 0, sea elsewhere; wind towards the east.
  const sea = Uint8Array.from([0, 1, 1, 1, 1, 1]), dirs = Array.from({ length: 6 }, () => [1, 0])
  const f = fetchField({ sea, nx: 6, ny: 1, dxm: 1000, dym: 1000, dirs })
  assert.equal(f[0], 0)
  assert.ok(f[5] > f[3] && f[3] > f[1], 'more fetch further from the upwind coast')
  assert.ok(Math.abs(f[5] - 4500) < 600, `about 4.5 km from the coast: ${f[5]}`)
  const west = fetchField({ sea, nx: 6, ny: 1, dxm: 1000, dym: 1000, dirs: dirs.map(() => [-1, 0]) })
  assert.ok(west[1] >= OPEN_FETCH_M, 'wind from the open east: the path leaves the map over the sea')
  assert.equal(fetchField({ sea, nx: 6, ny: 1, dxm: 1000, dym: 1000, dirs: dirs.map(() => [0, 0]) })[3], 0, 'no wind, no fetch')
})

test('cyclone winds circle the eye the right way round and spiral in', () => {
  const storm = { east_m: 0, north_m: 0 }
  const [ue, vn] = cycloneWind(100e3, 0, storm, 30, true)   // east of the eye, northern hemisphere
  assert.ok(vn > 0 && ue < 0, 'east of the eye: blowing north and slightly in (counter-clockwise)')
  assert.ok(Math.abs(Math.hypot(ue, vn) - 30) < 1e-9)
  const [us, vs] = cycloneWind(100e3, 0, storm, 30, false)
  assert.ok(vs < 0, 'southern hemisphere: clockwise')
  assert.deepEqual(cycloneWind(0, 0, storm, 30), [0, 0], 'calm in the eye')
})

test('the wave field: waves run downwind, grow away from the coast, vanish on land and under ice', () => {
  const nx = 8, ny = 2, sea = Uint8Array.from({ length: nx * ny }, (_, i) => (i % nx === 0 ? 0 : 1))
  const u = new Array(nx * ny).fill(15), v = new Array(nx * ny).fill(0)
  const f = waveField({ sea, nx, ny, dxm: 20e3, dym: 20e3, u, v })
  assert.equal(f.hs[0], 0, 'no waves on land')
  assert.ok(f.hs[7] > f.hs[1], 'higher downwind')
  assert.deepEqual([f.dirEast[3], f.dirNorth[3]], [1, 0], 'travelling with the wind')
  const iced = waveField({ sea, nx, ny, dxm: 20e3, dym: 20e3, u, v, ice: new Array(nx * ny).fill(0.9) })
  assert.equal(iced.hs[7], 0); assert.equal(iced.regime[7], 'damped by sea ice')
  const shallow = waveField({ sea, nx, ny, dxm: 20e3, dym: 20e3, u, v, depth: new Array(nx * ny).fill(2) })
  assert.ok(shallow.hs[7] <= 1.2 + 1e-6 && shallow.regime[7] === 'depth-limited', 'breaking limit 0.6 h (float32)')
  // A cyclone: its wind takes over where it is stronger, with a capped effective fetch.
  const storm = waveField({ sea: new Uint8Array(25).fill(1), nx: 5, ny: 5, dxm: 50e3, dym: 50e3, originEast: -125e3, originNorth: -125e3,
    u: new Array(25).fill(5), v: new Array(25).fill(0), storms: [{ east_m: 0, north_m: 0 }], stormSpeed: new Array(25).fill(40) })
  assert.ok(storm.cyclone[13] === 1 && storm.dirNorth[13] > 0.5, 'east of the eye the waves run north')
  const cap = 1.6e-3 * 40 * Math.sqrt(CYCLONE_FETCH_M / G)
  assert.ok(storm.hs.every(h => h <= cap + 1e-6) && Math.max(...storm.hs) > 8, `hurricane seas near ${cap.toFixed(1)} m, not the unbounded 39 m`)
})

test('drawn waves keep their order: longer, higher and faster waves look it', () => {
  const small = windSea(6, 50e3), big = windSea(20, 500e3), a = drawnWave(small), b = drawnWave(big)
  assert.ok(b.wavelength > a.wavelength && b.amplitude > a.amplitude)
  const drawnSpeed = d => d.wavelength / d.period
  assert.ok(Math.abs(drawnSpeed(a) - DRAWN_SPEED * small.speed) < 1e-9 && Math.abs(drawnSpeed(b) - DRAWN_SPEED * big.speed) < 1e-9, 'crest speed in proportion to the real phase speed')
  assert.ok(drawnSpeed(b) > drawnSpeed(a), 'the bigger, faster sea is seen to be faster')
  assert.ok(b.wavelength <= 2.6 && a.wavelength >= 0.35 && b.amplitude <= 0.45)
  assert.equal(drawnWave({ hs: 0, tp: 0, length: 0 }).amplitude, 0)
  assert.ok(drawnWave({ hs: 0.05, tp: 2, length: 6 }).amplitude < drawnWave({ hs: 0.3, tp: 2, length: 6 }).amplitude * 0.3, 'a glassy sea is nearly flat')
  // Across the whole range of drawn lengths, longer bands always move faster (no crawling long waves).
  let last = 0
  for (let w = 0.35; w <= 2.6; w += 0.05) { const v = drawnBand(w).omega / (2 * Math.PI / w); assert.ok(v > last, `monotone at ${w.toFixed(2)}`); last = v }
})
