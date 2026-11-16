// Animated arrows. Wherever the scene shows a direction and a speed (wind, ocean currents, sea-ice drift, runoff, flow
// directions, the solar wind, escaping ions), the arrows move: each glides forward along its direction, fading to a
// faint minimum before it returns (never away, so the direction can always be read), with a tail that fades towards its
// end so that even a still frame suggests motion. Stronger flow glides faster (and its arrows are already longer), so
// speed is seen, not only read. All arrows of a scene are one instanced mesh animated on the GPU; phases are random, so
// the field shimmers instead of pulsing in step. With reduced motion the arrows stand still, as before.
import * as THREE from 'three'

// Glide cycles per second for a flow strength 0..1 (relative to the strongest arrow of its kind).
export const cyclesPerSecond = rate => 0.25 + 1.1 * Math.min(1, Math.max(0, rate))
// How far an arrow travels in one cycle, in arrow lengths, and its faintest opacity (at the return).
export const GLIDE = 0.8
export const FAINTEST = 0.18
// Opacity over a cycle f in 0..1: rises, holds, fades before it returns to the start, never below FAINTEST.
export function glideAlpha(f) {
  const s = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t) }
  return FAINTEST + (1 - FAINTEST) * s(0, 0.25, f) * (1 - s(0.72, 1, f))
}

// Flow strengths where the call did not give one: the length relative to the longest arrow of the same colour (arrow
// lengths encode the speed in the scene), at least 0.25 so that every arrow visibly moves.
export function arrowRates(arrows) {
  const longest = new Map()
  for (const a of arrows) longest.set(a.color, Math.max(longest.get(a.color) ?? 0, a.length))
  return arrows.map(a => (a.rate ?? null) !== null ? Math.min(1, Math.max(0, a.rate)) : Math.max(0.25, a.length / (longest.get(a.color) || 1)))
}

// A unit arrow along +y: the shaft (part 0) and the head (part 1) as rings with t = 0..1 along each part. The vertex
// shader builds the real shape from each arrow's length, head length and head width.
function unitArrow(segments = 10) {
  const pos = [], part = [], index = []
  const ring = (t, p) => { const start = pos.length / 3; for (let k = 0; k < segments; k++) { const a = k / segments * Math.PI * 2; pos.push(Math.cos(a), t, Math.sin(a)); part.push(p) } return start }
  const tube = (a, b) => { for (let k = 0; k < segments; k++) { const k1 = (k + 1) % segments; index.push(a + k, b + k, a + k1, a + k1, b + k, b + k1) } }
  tube(ring(0, 0), ring(1, 0))                  // shaft
  const headBase = ring(0, 1), tip = ring(1, 1)  // head (the tip ring has radius 0)
  tube(headBase, tip)
  const centre = pos.length / 3; pos.push(0, 0, 0); part.push(1)   // closes the back of the head
  for (let k = 0; k < segments; k++) index.push(centre, headBase + (k + 1) % segments, headBase + k)
  const g = new THREE.BufferGeometry()
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3))
  g.setAttribute('aPart', new THREE.Float32BufferAttribute(part, 1))
  g.setIndex(index)
  return g
}

// arrows: [{ dir (Vector3), origin (Vector3), length, color, headLength, headWidth, rate? }]
export function arrowField(arrows, { moving = true, seed = 1 } = {}) {
  const rates = arrowRates(arrows), n = arrows.length
  const geometry = unitArrow()
  const aShape = new Float32Array(n * 3), aMotion = new Float32Array(n * 2)
  const uniforms = { uTime: { value: 0 }, uMove: { value: moving ? 1 : 0 } }
  const material = new THREE.MeshStandardMaterial({ roughness: 0.5, metalness: 0.05, transparent: true, depthWrite: false })
  material.onBeforeCompile = shader => {
    Object.assign(shader.uniforms, uniforms)
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `#include <common>
attribute float aPart; attribute vec3 aShape; attribute vec2 aMotion; uniform float uTime; uniform float uMove; varying float vGlide; varying float vAlong;`)
      .replace('#include <beginnormal_vertex>', `
float L = aShape.x, H = min(aShape.y, L * 0.6), W = aShape.z, shaftR = max(W * 0.24, L * 0.012);
vec3 objectNormal = aPart < 0.5 ? vec3(position.x, 0.0, position.z) : normalize(vec3(position.x, W / max(H, 1e-4), position.z));
#ifdef USE_TANGENT
vec3 objectTangent = vec3(1.0, 0.0, 0.0);
#endif`)
      .replace('#include <begin_vertex>', `
vec3 transformed = aPart < 0.5 ? vec3(position.x * shaftR, position.y * (L - H), position.z * shaftR)
                               : vec3(position.x * W * (1.0 - position.y), (L - H) + position.y * H, position.z * W * (1.0 - position.y));
// The glide: one cycle per 1 / aMotion.x seconds from phase aMotion.y, centred on the arrow's place.
float f = fract(uTime * aMotion.x + aMotion.y);
transformed.y += (f - 0.5) * L * ${GLIDE.toFixed(2)} * uMove;
vGlide = uMove > 0.5 ? ${FAINTEST.toFixed(2)} + ${(1 - FAINTEST).toFixed(2)} * smoothstep(0.0, 0.25, f) * (1.0 - smoothstep(0.72, 1.0, f)) : 1.0;
// Along the arrow, 0 at the tail to 1 at the tip: the tail fades out (only while moving).
vAlong = aPart < 0.5 ? position.y * (L - H) / max(L, 1e-4) : 1.0;`)
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', '#include <common>\nuniform float uMove; varying float vGlide; varying float vAlong;')
      .replace('#include <color_fragment>', '#include <color_fragment>\ndiffuseColor.a *= vGlide * mix(1.0, 0.3 + 0.7 * smoothstep(0.0, 0.5, vAlong), uMove);\nif (diffuseColor.a < 0.02) discard;')
  }
  material.customProgramCacheKey = () => 'terraflux-flow-arrows'
  const mesh = new THREE.InstancedMesh(geometry, material, Math.max(1, n))
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), up = new THREE.Vector3(0, 1, 0), one = new THREE.Vector3(1, 1, 1), colour = new THREE.Color()
  let s = seed
  const rand = () => ((s = (s * 16807) % 2147483647) / 2147483647)
  arrows.forEach((a, k) => {
    const dir = a.dir.clone().normalize(), speed = cyclesPerSecond(rates[k]), phase = rand()
    q.setFromUnitVectors(up, dir.lengthSq() > 0 ? dir : up)
    m.compose(a.origin, q, one)
    mesh.setMatrixAt(k, m); mesh.setColorAt(k, colour.set(a.color ?? 0xffffff))
    aShape.set([a.length, a.headLength ?? a.length * 0.2, a.headWidth ?? (a.headLength ?? a.length * 0.2) * 0.2], k * 3)
    aMotion.set([speed, phase], k * 2)
  })
  mesh.count = n
  geometry.setAttribute('aShape', new THREE.InstancedBufferAttribute(aShape, 3))
  geometry.setAttribute('aMotion', new THREE.InstancedBufferAttribute(aMotion, 2))
  mesh.frustumCulled = false
  mesh.userData.uniforms = uniforms
  return mesh
}
