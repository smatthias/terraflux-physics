import test from 'node:test'
import assert from 'node:assert/strict'
import * as THREE from 'three'
import { watchCurtain, funnelMesh, hurricaneBands, STORM_SHADERS } from './stormScene.js'

test('the watch curtain stands on the ground along every boundary segment', () => {
  const mesh = watchCurtain([[0, 0, 1, 0], [1, 0, 1, 1]], (x, z) => 0.2 + x * 0.1, { height: 0.5 })
  const pos = mesh.geometry.attributes.position
  assert.equal(pos.count, 8); assert.equal(mesh.geometry.index.count, 12)
  assert.ok(Math.abs(pos.getY(0) - 0.2) < 1e-6 && Math.abs(pos.getY(3) - 0.7) < 1e-6, 'from the ground up by its height')
  assert.ok(mesh.material.fragmentShader.includes('sin(uTime * 2.6)'), 'it pulses')
})

test('funnels reach from the ground to the cloud base, one instance each, spinning and flickering', () => {
  const mesh = funnelMesh([{ x: 1, z: 2, ground: 0.2, top: 1.4, width: 0.3, spin: 1.2, strength: 0.8 }, { x: 3, z: 2, ground: 0.2, top: 1, width: 0.2, spin: -1, strength: 0.3 }])
  assert.equal(mesh.count, 2)
  const m = new THREE.Matrix4(); mesh.getMatrixAt(0, m)
  assert.ok(Math.abs(m.elements[5] - 1.2) < 1e-6 && Math.abs(m.elements[13] - 0.2) < 1e-6, 'height = cloud base - ground, standing on the ground')
  assert.equal(mesh.geometry.attributes.aFunnel.getX(1), -1, 'the spin sense is kept')
  assert.ok(STORM_SHADERS.FUNNEL_FRAGMENT.includes('flicker') && STORM_SHADERS.FUNNEL_FRAGMENT.includes('uTime * vFunnel.x'))
})

test('hurricane bands: a disk to the gale radius with the eye, core and spin of the storm', () => {
  const mesh = hurricaneBands({ gale: 5, core: 0.8, eye: 0.35, spin: 0.9, y: 3 })
  mesh.geometry.computeBoundingSphere()
  assert.ok(Math.abs(mesh.geometry.boundingSphere.radius - 5) < 1e-6 && mesh.position.y === 3)
  const u = mesh.userData.uniforms
  assert.equal(u.uSpin.value, 0.9); assert.equal(u.uEye.value, 0.35); assert.equal(u.uCore.value, 0.8)
  assert.ok(STORM_SHADERS.BANDS_FRAGMENT.includes('log(max(r, 1e-3) / uCore)'), 'logarithmic spiral arms')
  assert.ok(STORM_SHADERS.BANDS_FRAGMENT.includes('sign(uSpin)'), 'the arms wind with the spin sense')
})
