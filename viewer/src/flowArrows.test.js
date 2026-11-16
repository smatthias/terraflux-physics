import test from 'node:test'
import assert from 'node:assert/strict'
import * as THREE from 'three'
import { cyclesPerSecond, glideAlpha, GLIDE, FAINTEST, arrowRates, arrowField } from './flowArrows.js'

test('stronger flow glides faster', () => {
  assert.ok(cyclesPerSecond(1) > cyclesPerSecond(0.5) && cyclesPerSecond(0.5) > cyclesPerSecond(0))
  assert.equal(cyclesPerSecond(5), cyclesPerSecond(1)); assert.equal(cyclesPerSecond(-1), cyclesPerSecond(0))
  assert.ok(cyclesPerSecond(0) > 0, 'even weak flow moves')
  assert.ok(GLIDE > 0 && GLIDE < 1)
})

test('an arrow rises, holds and fades over its glide, but never disappears', () => {
  assert.ok(Math.abs(glideAlpha(0) - FAINTEST) < 1e-12); assert.equal(glideAlpha(0.5), 1); assert.ok(Math.abs(glideAlpha(1) - FAINTEST) < 1e-12)
  for (let f = 0; f <= 1; f += 0.01) assert.ok(glideAlpha(f) >= FAINTEST - 1e-12, `readable at ${f.toFixed(2)}`)
  assert.ok(FAINTEST > 0.1 && FAINTEST < 0.35, 'faint but there')
})

test('flow strengths come from the call or from the arrow length within its colour', () => {
  const r = arrowRates([{ color: 1, length: 2 }, { color: 1, length: 1 }, { color: 1, length: 0.1 }, { color: 2, length: 0.5 }, { color: 2, length: 9, rate: 0.3 }])
  assert.deepEqual(r, [1, 0.5, 0.25, 0.5 / 9, 0.3].map((v, k) => (k === 3 ? 0.25 : v)))
})

test('the field has one arrow per arrow, shaped and timed per arrow', () => {
  const arrows = [{ dir: new THREE.Vector3(1, 0, 0), origin: new THREE.Vector3(0, 1, 0), length: 2, color: 0xff0000, headLength: 0.4, headWidth: 0.1 },
    { dir: new THREE.Vector3(0, 0, -1), origin: new THREE.Vector3(3, 1, 0), length: 1, color: 0xff0000 }]
  const mesh = arrowField(arrows)
  assert.equal(mesh.count, 2)
  const shape = mesh.geometry.attributes.aShape, motion = mesh.geometry.attributes.aMotion
  assert.deepEqual([shape.getX(0), shape.getY(0), shape.getZ(0)].map(v => +v.toFixed(4)), [2, 0.4, 0.1])
  assert.deepEqual([shape.getY(1), shape.getZ(1)].map(v => +v.toFixed(4)), [0.2, 0.04], 'ArrowHelper defaults for the head')
  assert.ok(motion.getY(0) !== motion.getY(1), 'random phases: the field shimmers instead of pulsing in step')
  assert.ok(motion.getX(0) > motion.getX(1), 'the longer (stronger) arrow glides faster')
  const m = new THREE.Matrix4(); mesh.getMatrixAt(0, m)
  const up = new THREE.Vector3(0, 1, 0).applyMatrix4(new THREE.Matrix4().extractRotation(m))
  assert.ok(up.distanceTo(new THREE.Vector3(1, 0, 0)) < 1e-6, 'oriented along its direction')
  assert.equal(arrowField(arrows, { moving: false }).userData.uniforms.uMove.value, 0, 'reduced motion: still arrows')
  const shader = { uniforms: {}, vertexShader: THREE.ShaderLib.standard.vertexShader, fragmentShader: THREE.ShaderLib.standard.fragmentShader }
  mesh.material.onBeforeCompile(shader)
  for (const hook of ['float f = fract(uTime * aMotion.x + aMotion.y);', 'vec3 objectNormal = aPart < 0.5']) assert.ok(shader.vertexShader.includes(hook), hook)
  assert.ok(!shader.vertexShader.includes('vec3 transformed = vec3( position );'), 'the shape replaces the default position')
  assert.ok(shader.fragmentShader.includes('diffuseColor.a *= vGlide * mix(1.0, 0.3 + 0.7 * smoothstep(0.0, 0.5, vAlong), uMove)'), 'fading tail only while moving')
})
