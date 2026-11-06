// The 3D parts of the storm watch (stormWatch.js selects what to draw from the engine output):
//   - a glowing, pulsing curtain along each tornado watch boundary;
//   - tornado funnels where the engine flags a tornado as possible: a twisting condensation funnel from the ground to
//     the engine's cloud base (LCL), turning with the helicity's sense, flickering (a possibility, not a sighting);
//   - hurricanes: a disk of spiral rain bands around a clear eye, turning at the storm's wind over its core radius;
//     the eyewall's storm towers are clouds (clouds3d.js) spinning with it.
// Every part carries its uniforms in userData.uniforms (uTime is advanced by the scene).
import * as THREE from 'three'
import { TERRAIN_GLSL } from './surface.js'

// ---- Watch curtain ---------------------------------------------------------------------------------------------------
// segments: [[x0, z0, x1, z1]] in the scene, base(x, z): ground height there, height: curtain height.
export function watchCurtain(segments, base, { height = 0.9, colour = 0xff3b30 } = {}) {
  const pos = [], uv = [], index = []
  let along = 0
  segments.forEach(([x0, z0, x1, z1]) => {
    const k = pos.length / 3, l = Math.hypot(x1 - x0, z1 - z0), b0 = base(x0, z0), b1 = base(x1, z1)
    pos.push(x0, b0, z0, x1, b1, z1, x1, b1 + height, z1, x0, b0 + height, z0)
    uv.push(along, 0, along + l, 0, along + l, 1, along, 1)
    index.push(k, k + 1, k + 2, k, k + 2, k + 3)
    along += l
  })
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3))
  geometry.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2))
  geometry.setIndex(index)
  const uniforms = { uTime: { value: 0 }, uColour: { value: new THREE.Color(colour) } }
  const material = new THREE.ShaderMaterial({
    uniforms, transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
    vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
    fragmentShader: `uniform float uTime; uniform vec3 uColour; varying vec2 vUv;
void main(){
  float rise = pow(1.0 - vUv.y, 1.6);                                   // bright at the ground, fading upwards
  float pulse = 0.6 + 0.4 * sin(uTime * 2.6);                             // the whole watch pulses
  float bands = 0.55 + 0.45 * smoothstep(0.3, 0.7, fract(vUv.x * 2.2 - uTime * 0.6));   // bands march along the boundary
  float edge = smoothstep(0.0, 0.04, vUv.y) * (1.0 - smoothstep(0.96, 1.0, vUv.y)) + (1.0 - smoothstep(0.0, 0.06, vUv.y)) * 0.8;
  gl_FragColor = vec4(uColour, clamp(rise * pulse * bands * 0.75 + edge * 0.35, 0.0, 1.0));
}` })
  const mesh = new THREE.Mesh(geometry, material)
  mesh.renderOrder = 3; mesh.userData.uniforms = uniforms
  return mesh
}

// ---- Tornado funnels -----------------------------------------------------------------------------------------------
// funnels: [{ x, z, ground, top (cloud base y), width (radius at the cloud), spin (rad/s, + counter-clockwise), strength 0..1, seed }]
const FUNNEL_VERTEX = `
attribute vec4 aFunnel;   // x: spin, y: strength, z: seed, w: height
uniform float uTime;
varying vec2 vUv; varying vec4 vFunnel; varying vec3 vWorld;
void main(){
  vUv = uv; vFunnel = aFunnel;
  vec3 p = position;
  // Sway: the funnel's lower part wanders slowly around the point below the cloud.
  float low = 1.0 - uv.y;
  p.x += sin(uTime * 0.9 + aFunnel.z * 6.0) * 0.22 * low * low;
  p.z += cos(uTime * 0.7 + aFunnel.z * 4.0) * 0.22 * low * low;
  vec4 world = modelMatrix * instanceMatrix * vec4(p, 1.0);
  vWorld = world.xyz;
  gl_Position = projectionMatrix * viewMatrix * world;
}`
const FUNNEL_FRAGMENT = `
uniform float uTime;
varying vec2 vUv; varying vec4 vFunnel; varying vec3 vWorld;
${TERRAIN_GLSL}
void main(){
  float a = vUv.x * 6.283185, h = vUv.y;
  // Streaks spiralling up the funnel, turning with the spin.
  float streak = smoothstep(0.35, 0.75, tnoise(vec2(a * 1.6 + h * 5.0 - uTime * vFunnel.x * 2.5, h * 9.0 - uTime * 1.5)));
  float body = mix(0.45, 0.9, streak);
  // Debris at the ground, the condensation funnel above, the cloud base at the top.
  float debris = 1.0 - smoothstep(0.0, 0.22, h);
  vec3 c = mix(vec3(0.62, 0.64, 0.68), vec3(0.4, 0.33, 0.26), debris) * (0.75 + 0.35 * streak);
  // It flickers: the model says a tornado is possible here, not that one is on the ground.
  float flicker = 0.72 + 0.28 * sin(uTime * 1.3 + vFunnel.z * 9.0) * sin(uTime * 0.37 + vFunnel.z * 3.0);
  float alpha = body * (0.55 + 0.4 * vFunnel.y) * flicker * smoothstep(0.0, 0.05, h) * (1.0 - smoothstep(0.92, 1.0, h));
  gl_FragColor = vec4(c, alpha);
}`
export function funnelMesh(funnels) {
  // A tapered tube: narrow at the ground (y = 0), wide at the cloud (y = 1).
  const geometry = new THREE.CylinderGeometry(1, 0.16, 1, 20, 16, true).translate(0, 0.5, 0)
  const n = funnels.length, aFunnel = new Float32Array(n * 4), uniforms = { uTime: { value: 0 } }
  const material = new THREE.ShaderMaterial({ vertexShader: FUNNEL_VERTEX, fragmentShader: FUNNEL_FRAGMENT, uniforms, transparent: true, depthWrite: false, side: THREE.DoubleSide })
  const mesh = new THREE.InstancedMesh(geometry, material, Math.max(1, n)), m = new THREE.Matrix4()
  funnels.forEach((f, i) => {
    const h = Math.max(0.2, f.top - f.ground)
    m.makeScale(f.width, h, f.width).setPosition(f.x, f.ground, f.z); mesh.setMatrixAt(i, m)
    aFunnel.set([f.spin, f.strength, f.seed ?? i * 0.37, h], i * 4)
  })
  mesh.count = n
  geometry.setAttribute('aFunnel', new THREE.InstancedBufferAttribute(aFunnel, 4))
  mesh.frustumCulled = false; mesh.renderOrder = 2
  mesh.userData.uniforms = uniforms
  return mesh
}

// ---- Hurricanes ------------------------------------------------------------------------------------------------------
// The rain bands of one storm, a disk of radius `gale` (scene units) around its centre at height y: logarithmic spiral
// arms curving in towards the eyewall, a dense central overcast, a clear eye of radius `eye`, all turning at `spin`.
const BANDS_FRAGMENT = `
uniform float uTime; uniform float uSpin; uniform float uEye; uniform float uCore; uniform float uGale; uniform vec3 uSun;
varying vec2 vLocal; varying vec3 vWorld;
${TERRAIN_GLSL}
void main(){
  float r = length(vLocal), theta = atan(vLocal.y, vLocal.x);
  if (r > uGale) discard;
  float turned = theta - uSpin * uTime;
  // Four logarithmic spiral arms, winding inwards (the sense follows the spin).
  float arms = 0.5 + 0.5 * cos(4.0 * turned + sign(uSpin) * 3.2 * log(max(r, 1e-3) / uCore));
  float ragged = tnoise(vec2(turned * 3.0, r * 3.0) + 5.0) * 0.6 + tnoise(vLocal * 6.0 + uTime * 0.05) * 0.4;
  float overcast = 1.0 - smoothstep(uCore * 1.1, uCore * 2.6, r);
  float cloud = clamp(max(overcast, smoothstep(0.45, 0.85, arms * 0.75 + ragged * 0.45)) * (1.0 - smoothstep(uGale * 0.65, uGale, r)), 0.0, 1.0);
  cloud *= smoothstep(uEye * 0.8, uEye * 1.25, r);                      // the clear eye
  float light = 0.78 + 0.22 * ragged;
  vec3 c = mix(vec3(0.72, 0.76, 0.82), vec3(0.98, 0.99, 1.0), light);
  gl_FragColor = vec4(c, cloud * 0.94);
}`
export function hurricaneBands({ gale, core, eye, spin, y }) {
  const geometry = new THREE.CircleGeometry(gale, 128, 0, Math.PI * 2).rotateX(-Math.PI / 2)
  const uniforms = { uTime: { value: 0 }, uSpin: { value: spin }, uEye: { value: eye }, uCore: { value: core }, uGale: { value: gale }, uSun: { value: new THREE.Vector3(-30, 19, 10).normalize() } }
  const material = new THREE.ShaderMaterial({ uniforms, transparent: true, depthWrite: false, side: THREE.DoubleSide, fragmentShader: BANDS_FRAGMENT,
    // vLocal: the position in the storm's plane, x east and y north (the scene's -z).
    // The central dense overcast bulges up over the core and dips into the eye; the eyewall towers rise above it as a rim.
    vertexShader: `uniform float uGale; uniform float uCore; uniform float uEye; varying vec2 vLocal; varying vec3 vWorld;
void main(){ vLocal = vec2(position.x, -position.z); float r = length(vLocal);
  vec3 p = position; p.y += pow(max(0.0, 1.0 - r / uGale), 2.0) * 0.45 * uCore - (1.0 - smoothstep(uEye * 0.6, uEye * 1.4, r)) * 0.5 * uCore;
  vec4 w = modelMatrix * vec4(p, 1.0); vWorld = w.xyz; gl_Position = projectionMatrix * viewMatrix * w; }` })
  const mesh = new THREE.Mesh(geometry, material)
  mesh.position.y = y; mesh.renderOrder = 3
  mesh.userData.uniforms = uniforms
  return mesh
}
export const STORM_SHADERS = { FUNNEL_VERTEX, FUNNEL_FRAGMENT, BANDS_FRAGMENT }
