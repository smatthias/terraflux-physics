import test from 'node:test'
import assert from 'node:assert/strict'
import * as THREE from 'three'
import { waterMesh, waterSpacing, DIRECTIONS, BANDS, SPREADING, STEEPNESS, componentWeights, COMPONENT_TABLE, WATER_VERTEX, WATER_FRAGMENT } from './waterSurface.js'
import { drawnBand, drawnWave, windSea } from './waves.js'

const cells = (nx, ny, seaOf) => {
  const n = nx * ny, f = v => Float32Array.from({ length: n }, (_, i) => (typeof v === 'function' ? v(i) : v))
  return { sea: f(seaOf), amp: f(0.2), len: f(1), dirX: f(1), dirZ: f(0), foam: f(0.5), depth: f(100) }
}

test('the water lattice stays within budget and only covers water', () => {
  assert.ok(waterSpacing(32, 20) >= 0.06)
  const s = waterSpacing(32, 20, 1000); assert.ok((32 / s + 1) * (20 / s + 1) < 1300)
  const nx = 8, ny = 4, half = cells(nx, ny, i => (i % nx < 4 ? 0 : 1))
  const mesh = waterMesh({ nx, ny, dx: 1, dz: 1, y: 0.3, cells: half })
  const pos = mesh.geometry.attributes.position, idx = mesh.geometry.index.array
  assert.ok(idx.length > 0)
  for (let t = 0; t < idx.length; t += 3) assert.ok(Math.max(...[0, 1, 2].map(j => pos.getX(idx[t + j]))) > -0.6, 'no triangles over the land half')
  assert.ok(Array.from({ length: pos.count }, (_, k) => pos.getY(k)).every(y => Math.abs(y - 0.3) < 1e-6), 'at sea level')
  // Triangles face up, so that the water is seen from above.
  const A = new THREE.Vector3().fromBufferAttribute(pos, idx[0]), B = new THREE.Vector3().fromBufferAttribute(pos, idx[1]), C = new THREE.Vector3().fromBufferAttribute(pos, idx[2])
  assert.ok(B.clone().sub(A).cross(C.clone().sub(A)).y > 0)
  assert.equal(waterMesh({ nx, ny, dx: 1, dz: 1, y: 0, cells: cells(nx, ny, 0) }).geometry.index.count, 0, 'no sea, no water')
})

test('per-vertex waves come from the cells, with unit directions', () => {
  const nx = 4, ny = 2, c = cells(nx, ny, 1)
  c.dirX = new Float32Array(8).fill(3); c.dirZ = new Float32Array(8).fill(4)
  const g = waterMesh({ nx, ny, dx: 1, dz: 1, y: 0, cells: c }).geometry
  const d = g.attributes.aDir
  for (let k = 0; k < d.count; k++) assert.ok(Math.abs(Math.hypot(d.getX(k), d.getY(k)) - 1) < 1e-6)
  assert.ok(Math.abs(g.attributes.aAmp.getX(0) - 0.2) < 1e-6 && Math.abs(g.attributes.aDepth.getX(0) - 100) < 1e-4)
})

test('the sea is a fixed directional spectrum: weights follow the local direction and wavelength', () => {
  const w = componentWeights(0, 1.1)
  assert.ok(Math.abs(w.direction.reduce((a, v) => a + v, 0) - 1) < 1e-9 && Math.abs(w.band.reduce((a, v) => a + v, 0) - 1) < 1e-9)
  assert.equal(w.direction.indexOf(Math.max(...w.direction)), 0, 'most energy in the wave direction')
  assert.equal(w.direction[DIRECTIONS / 2], 0, 'none running against it')
  assert.equal(w.band.indexOf(Math.max(...w.band)), 1, 'most energy in the band of the local wavelength')
  const north = componentWeights(Math.PI / 2, 2.2)
  assert.equal(north.direction.indexOf(Math.max(...north.direction)), DIRECTIONS / 4); assert.equal(north.band.indexOf(Math.max(...north.band)), 2)
  assert.ok(SPREADING >= 2 && BANDS.every((b, k) => k === 0 || b > BANDS[k - 1]))
})

test('components have distinct wavelengths (no repeating lattice) and each runs at its own deep-water speed', () => {
  assert.equal(COMPONENT_TABLE.length, DIRECTIONS * BANDS.length)
  for (const b of BANDS.keys()) {
    const lengths = COMPONENT_TABLE.filter(c => c.b === b).map(c => c.wavelength)
    assert.equal(new Set(lengths.map(v => v.toFixed(4))).size, DIRECTIONS, 'no two components of a band share a length')
    assert.ok(lengths.every(v => v >= BANDS[b] * 0.8 - 1e-9 && v <= BANDS[b] * 1.2 + 1e-9))
  }
  const byLength = [...COMPONENT_TABLE].sort((a, c) => a.wavelength - c.wavelength)
  assert.ok(byLength.every((c, k) => k === 0 || c.omega / c.k > byLength[k - 1].omega / byLength[k - 1].k - 1e-12), 'longer components move faster')
  for (const c of COMPONENT_TABLE) assert.ok(WATER_VERTEX.includes(c.omega.toFixed(6)), 'each component in the shader')
})

test('longer bands run faster, at the deep-water speed of the real wave they stand for', () => {
  const speeds = BANDS.map(b => drawnBand(b).omega / (2 * Math.PI / b))
  assert.ok(speeds.every((v, k) => k === 0 || v > speeds[k - 1]), `drawn phase speeds ${speeds}`)
  const real = windSea(15, 300e3), band = drawnBand(drawnWave(real).wavelength)
  assert.ok(Math.abs(band.length - real.length) / real.length < 1e-6, 'the drawn band maps back to the real wavelength')
  assert.ok(Math.abs(band.period - real.tp) / real.tp < 0.02, 'and its deep-water period')
})

test('the shader: global phases, spreading, groups, foam, surf and glass over data', () => {
  assert.ok(STEEPNESS > 0 && STEEPNESS <= 1, 'crests sharpen without looping')
  for (const hook of ['k * dot(D, x) - ', 'pow(max(cos(float(j)', 'float group = ', 'smoothstep(0.3, 0.85, aSea)']) assert.ok(WATER_VERTEX.includes(hook), hook)
  assert.ok(!/dot\(aDir, ?position/.test(WATER_VERTEX), 'no per-place direction inside the phase (that bends crests into fringes)')
  assert.ok(WATER_FRAGMENT.includes('float caps') && WATER_FRAGMENT.includes('float surf') && WATER_FRAGMENT.includes('discard') && WATER_FRAGMENT.includes('float far'))
  const glass = waterMesh({ nx: 2, ny: 2, dx: 1, dz: 1, y: 0, cells: cells(2, 2, 1), glass: true })
  assert.equal(glass.userData.uniforms.uGlass.value, 1); assert.equal(glass.material.depthWrite, false)
})
