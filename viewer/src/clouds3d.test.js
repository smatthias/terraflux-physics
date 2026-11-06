import test from 'node:test'
import assert from 'node:assert/strict'
import * as THREE from 'three'
import { CLOUD_TYPES } from './cloudFormation.js'
import { rng, cloudPuffs, CLOUD_FORMS, cloudMesh, precipitationMesh, precipitationIntensity } from './clouds3d.js'

const base = 1, top = 4, w = 1
const shape = (type, extra = {}) => cloudPuffs({ type, base, top, w, seed: 7, ...extra })
const extent = ps => ({ bottom: Math.min(...ps.map(p => p.y - p.sy)), top: Math.max(...ps.map(p => p.y + p.sy)), wide: Math.max(...ps.map(p => Math.max(p.sx, p.sz))), tall: Math.max(...ps.map(p => p.sy)) })

test('every cloud genus of the model has a 3D form, and clear sky has none', () => {
  for (const t of CLOUD_TYPES) {
    if (t.key === 'none') { assert.deepEqual(shape('none'), []); continue }
    assert.ok(CLOUD_FORMS[t.key], `${t.key}: described`)
    const ps = shape(t.key)
    assert.ok(ps.length >= 1, `${t.key}: has puffs`)
    assert.ok(ps.every(p => [p.x, p.y, p.z, p.sx, p.sy, p.sz].every(Number.isFinite) && p.sx > 0 && p.sy > 0 && p.opacity > 0 && p.opacity <= 1), `${t.key}: valid`)
    assert.ok(ps.every(p => p.y >= base - 0.01 && p.y <= top + 0.01), `${t.key}: centred between base and top`)
  }
})

test('the forms differ the way the clouds do', () => {
  const cu = shape('cumulus'), tcu = shape('towering_cumulus'), cb = shape('cumulonimbus', { wind: [0, 1] })
  assert.ok(cu.every(p => p.flat === 1), 'cumulus sit on a flat base')
  assert.ok(extent(tcu).top > extent(cu).top, 'towering cumulus grow higher than cumulus')
  const anvil = cb.filter(p => p.y > top - 0.5), tower = cb.filter(p => p.y <= top - 0.5)
  assert.ok(anvil.length >= 4 && Math.max(...anvil.map(p => p.sx)) > Math.max(...tower.map(p => p.sx)), 'the cumulonimbus anvil spreads wider than its tower')
  assert.ok(anvil.reduce((a, p) => a + p.z, 0) / anvil.length > 0.15, 'and drifts downwind')
  assert.ok(tower.some(p => p.shade < 0.4), 'with a dark base')
  const st = shape('stratus'), as = shape('altostratus'), cs = shape('cirrostratus')
  for (const sheet of [st, as, cs]) assert.ok(sheet.every(p => p.sx > 0.6 && p.sy < p.sx * 0.5 + 1.4), 'sheets are wide')
  assert.ok(shape('stratus', { top: 1.1 }).every(p => p.sy < 0.1), 'a thin stratus deck is flat')
  assert.ok(cs[0].opacity < st[0].opacity, 'the cirrostratus veil is fainter than stratus')
  const ci = shape('cirrus', { wind: [1, 0] })
  assert.ok(ci.every(p => p.sx > 8 * p.sy && Math.abs(p.yaw) < 0.4), 'cirrus are thin streaks along the wind')
  const ac = shape('altocumulus')
  // A cumulus is the whole heap of its puffs; each altocumulus cloudlet is one small, flatter cloud of its own.
  const heap = ps => { const cx = ps.reduce((a, p) => a + p.x, 0) / ps.length, cz = ps.reduce((a, p) => a + p.z, 0) / ps.length; return 2 * Math.max(...ps.map(p => Math.hypot(p.x - cx, p.z - cz) + p.sx)) }
  assert.ok(Math.max(...ac.map(p => 2 * p.sx)) < heap(cu) * 0.5, 'altocumulus cloudlets are much smaller than a cumulus heap')
  assert.ok(Math.max(...ac.map(p => p.sy / p.sx)) < Math.min(...cu.map(p => p.sy / p.sx)), 'and flatter')
  assert.ok(shape('nimbostratus').every(p => p.shade < 0.4), 'nimbostratus is dark')
})

test('shapes are stable per cell, vary between cells and grow with the cloud fraction', () => {
  assert.deepEqual(shape('cumulus'), shape('cumulus'))
  const centre = ps => ps.reduce((a, p) => a + p.x, 0) / ps.length
  assert.notEqual(centre(shape('cumulonimbus', { seed: 1 })).toFixed(3), centre(shape('cumulonimbus', { seed: 2 })).toFixed(3), 'clouds sit off the cell centre, differently per cell')
  assert.notDeepEqual(shape('cumulus', { seed: 8 }), shape('cumulus'))
  assert.ok(shape('cumulus', { fraction: 1 }).length > shape('cumulus', { fraction: 0.2 }).length)
  const r = rng(3); for (let k = 0; k < 100; k++) { const v = r(); assert.ok(v >= 0 && v < 1) }
})

test('the cloud and precipitation meshes carry one instance per puff and per shaft', () => {
  const puffs = shape('cumulonimbus').map(p => ({ ...p, base, depth: top - base, storm: true }))
  const mesh = cloudMesh(puffs)
  assert.equal(mesh.count, puffs.length)
  assert.equal(mesh.geometry.attributes.aCloud.count, puffs.length)
  assert.equal(mesh.geometry.attributes.aMisc.getX(puffs.findIndex(p => p.flat)), 3, 'flat base and storm are both encoded')
  assert.ok(mesh.material.fragmentShader.includes('flash') && mesh.userData.uniforms.uTime)
  const rain = precipitationMesh([{ x: 0, z: 0, top: 2, bottom: 0.2, radius: 0.4, intensity: 0.5, snow: false }, { x: 1, z: 0, top: 2, bottom: 0.2, radius: 0.4, intensity: 1, snow: true }])
  assert.equal(rain.count, 2); assert.equal(rain.geometry.attributes.aShaft.getY(1), 1)
  const m = new THREE.Matrix4(); rain.getMatrixAt(0, m); const e = m.elements
  assert.ok(Math.abs(e[5] - 1.8) < 1e-6 && Math.abs(e[13] - 0.2) < 1e-6, 'from the ground up to the cloud base')
  assert.equal(precipitationIntensity(0), 0); assert.equal(precipitationIntensity(40), 1); assert.ok(precipitationIntensity(2) > 0.2 && precipitationIntensity(2) < 0.5)
})
