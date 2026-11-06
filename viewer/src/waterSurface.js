// The sea as an animated 3D water surface. A dense lattice over the sea carries, per vertex, the wave estimated for its
// place (waves.js): direction, drawn height, drawn wavelength and period. The vertex shader sums four Gerstner
// (trochoidal) waves spread around that direction, each with the deep-water period of its length, so the crests run
// with the waves' direction and speed; the fragment shader adds the sky's reflection, the sun's glitter, the colour of
// the depth, whitecaps on steep crests in strong wind and surf along the coast. Over data colours (the map views) the
// water is glass: only highlights and foam, so the data stays readable underneath.
import * as THREE from 'three'
import { sampleLinear } from './surface.js'
import { TERRAIN_GLSL } from './surface.js'
import { drawnBand } from './waves.js'

// The sea is a sum of fixed components, as a directional wave spectrum is: DIRECTIONS evenly spaced directions times
// BANDS drawn wavelengths. At every place each component is weighted by the spreading function cos^4 of its angle to
// the local wave direction and by how close its band is to the local wavelength. Phases are therefore the same across
// the whole sea (a per-place direction in the phase would bend the crests into fringes where the wind turns), while the
// waves still run in each place's direction, at the deep-water speed of their band. Random phases and slowly drifting
// wave groups keep the pattern from repeating.
export const DIRECTIONS = 8
export const BANDS = [0.55, 1.1, 2.2]
export const SPREADING = 4
// Crest sharpness of the Gerstner waves (0 = sine, 1 = the sharpest crest without looping).
export const STEEPNESS = 0.6

// Vertex spacing so that the lattice stays within a budget and resolves the shortest band.
export function waterSpacing(width, depth, budget = 110000) {
  return Math.max(0.06, Math.sqrt(width * depth / budget))
}

// Weights of the components at a place (the same as the shader): direction weights sum to 1, band weights sum to 1.
export function componentWeights(dirAngle, wavelength) {
  const wd = Array.from({ length: DIRECTIONS }, (_, j) => Math.max(0, Math.cos(j * 2 * Math.PI / DIRECTIONS - dirAngle)) ** SPREADING)
  const wb = BANDS.map(b => Math.exp(-((Math.log(b / Math.max(1e-3, wavelength)) / 0.5) ** 2)))
  const sd = wd.reduce((a, v) => a + v, 0) || 1, sb = wb.reduce((a, v) => a + v, 0) || 1
  return { direction: wd.map(v => v / sd), band: wb.map(v => v / sb) }
}


// Every component gets its own wavelength, up to 20 % off its band's (a sea has a continuous spread of lengths):
// equal lengths in neighbouring directions would interfere into a regular lattice of crests and troughs. Each moves at
// the deep-water speed of its own length. Deterministic, so the sea looks the same every time.
const jitter = (j, b, salt) => { const v = Math.sin(j * 12.9898 + b * 78.233 + salt * 37.719) * 43758.5453; return v - Math.floor(v) }
export const COMPONENT_TABLE = BANDS.flatMap((band, b) => Array.from({ length: DIRECTIONS }, (_, j) => {
  const wavelength = band * (0.8 + 0.4 * jitter(j, b, 1)), d = drawnBand(wavelength)
  return { j, b, angle: j * 2 * Math.PI / DIRECTIONS, wavelength, k: 2 * Math.PI / wavelength, omega: d.omega, phase: jitter(j, b, 2) * 2 * Math.PI }
}))

const VERTEX = `
attribute float aSea; attribute float aAmp; attribute float aLen; attribute vec2 aDir; attribute float aFoam; attribute float aDepth;
uniform float uTime;
varying vec3 vPos; varying vec3 vNormal; varying float vSea; varying float vFoam; varying float vCrest; varying float vDepth; varying float vAmp;
${TERRAIN_GLSL}
const float PI2 = 6.283185307;
void main(){
  vec3 p = position, n = vec3(0.0, 1.0, 0.0);
  vec2 x = position.xz;
  float fade = smoothstep(0.3, 0.85, aSea), theta = atan(aDir.y, aDir.x), crest = 0.0, total = 1e-5;
  float wd[${DIRECTIONS}]; float sd = 0.0;
  for (int j = 0; j < ${DIRECTIONS}; j++) { wd[j] = pow(max(cos(float(j) * ${(2 * Math.PI / DIRECTIONS).toFixed(6)} - theta), 0.0), ${SPREADING.toFixed(1)}); sd += wd[j]; }
  float wb[${BANDS.length}]; float sb = 0.0;
${BANDS.map((b, k) => `  wb[${k}] = exp(-pow(log(${b.toFixed(3)} / max(aLen, 1e-3)) / 0.5, 2.0)); sb += wb[${k}];`).join('\n')}
  // Wave groups: the height swells and fades in slowly drifting sets.
  float group = 0.6 + 0.8 * tnoise(x * 0.16 + aDir * uTime * 0.06);
${COMPONENT_TABLE.map(c => `  {
    float A = aAmp * fade * group * sqrt(wd[${c.j}] / max(sd, 1e-5) * wb[${c.b}] / max(sb, 1e-5));
    if (A > 1e-5) {
      vec2 D = vec2(${Math.cos(c.angle).toFixed(6)}, ${Math.sin(c.angle).toFixed(6)});
      float k = ${c.k.toFixed(6)}, Q = min(1.0, ${STEEPNESS.toFixed(2)} / max(k * aAmp * 3.0, 1e-5));
      float f = k * dot(D, x) - ${c.omega.toFixed(6)} * uTime + ${c.phase.toFixed(4)}, c = cos(f), s = sin(f);
      p.x += Q * A * D.x * c; p.z += Q * A * D.y * c; p.y += A * s;
      n.x -= D.x * k * A * c; n.z -= D.y * k * A * c; n.y -= Q * k * A * s;
      crest += A * s; total += A;
    }
  }`).join('\n')}
  vPos = (modelMatrix * vec4(p, 1.0)).xyz; vNormal = normalize(n);
  vSea = aSea; vFoam = aFoam; vCrest = crest / total * 2.0; vDepth = aDepth; vAmp = aAmp;
  gl_Position = projectionMatrix * viewMatrix * vec4(vPos, 1.0);
}`

const FRAGMENT = `
uniform vec3 uSun; uniform float uTime; uniform float uGlass;
varying vec3 vPos; varying vec3 vNormal; varying float vSea; varying float vFoam; varying float vCrest; varying float vDepth; varying float vAmp;
${TERRAIN_GLSL}
void main(){
  vec2 q = vPos.xz;
  // The same noisy shoreline as the land's, so water and land meet without a seam.
  float n2 = tfbm(q * 3.1 + 7.1), n3 = tfbm(q * 13.0 + 2.3);
  if (vSea + (n2 - 0.5) * 0.5 + (n3 - 0.5) * 0.12 < 0.5) discard;
  // Far away the fine ripples and the glitter fade, so the sea does not shimmer into moiré.
  float far = smoothstep(22.0, 60.0, length(cameraPosition - vPos));
  vec3 n = normalize(mix(vNormal + vec3(tnoise(q * 9.0 + uTime * 0.7) - 0.5, 0.0, tnoise(q * 9.0 - uTime * 0.6 + 4.0) - 0.5) * 0.18, vec3(0.0, 1.0, 0.0), far * 0.6));
  vec3 v = normalize(cameraPosition - vPos), sun = normalize(uSun);
  float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
  float glitter = pow(max(dot(reflect(-sun, n), v), 0.0), 140.0) * 1.8 + pow(max(dot(reflect(-sun, n), v), 0.0), 18.0) * 0.12;
  vec3 shallow = vec3(0.09, 0.4, 0.47), deep = vec3(0.02, 0.16, 0.26), abyss = vec3(0.01, 0.07, 0.14);
  vec3 water = mix(mix(shallow, deep, smoothstep(2.0, 60.0, vDepth)), abyss, smoothstep(200.0, 3000.0, vDepth));
  vec3 sky = vec3(0.56, 0.69, 0.8);
  glitter *= 1.0 - far * 0.8;
  vec3 c = mix(water * (0.6 + 0.4 * max(dot(n, sun), 0.0)), sky, fresnel * 0.65) + glitter;
  // Whitecaps on the steep crests where the wind is strong, surf where the waves reach the coast.
  float caps = vFoam * smoothstep(0.5, 0.95, vCrest) * smoothstep(0.35, 0.7, tfbm(q * 6.0 + uTime * 0.3));
  float surf = (1.0 - smoothstep(0.5, 0.85, vSea)) * smoothstep(0.0, 0.02, vAmp) * smoothstep(0.3, 0.65, tfbm(q * 7.0 - uTime * 0.5 + n2 * 2.0));
  float foam = clamp(caps + surf, 0.0, 1.0);
  c = mix(c, vec3(0.94, 0.97, 1.0), foam);
  float body = mix(0.62, 0.94, smoothstep(1.0, 40.0, vDepth));
  float alpha = mix(body, clamp(0.12 + fresnel * 0.55 + glitter + foam * 0.9, 0.0, 0.95), uGlass);
  gl_FragColor = vec4(c, alpha);
}`

// cells: per-cell Float32Arrays sea (0..1), amp, len, dirX, dirZ (scene xz direction the waves travel), foam,
// depth (m). The lattice covers the domain (nx dx by ny dz, centred at the origin) at height y.
export function waterMesh({ nx, ny, dx, dz, y, cells, glass = false, sun = new THREE.Vector3(-30, 19, 10) }) {
  const width = nx * dx, depth = ny * dz, step = waterSpacing(width, depth)
  const cols = Math.ceil(width / step) + 1, rows = Math.ceil(depth / step) + 1, n = cols * rows
  const position = new Float32Array(n * 3), attr = { aSea: 1, aAmp: 1, aLen: 1, aDir: 2, aFoam: 1, aDepth: 1 }
  const arrays = Object.fromEntries(Object.entries(attr).map(([k, size]) => [k, new Float32Array(n * size)]))
  for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
    const k = r * cols + c, x = -width / 2 + c * width / (cols - 1), z = depth / 2 - r * depth / (rows - 1)
    position.set([x, y, z], k * 3)
    const fx = x / dx + nx / 2 - 0.5, fy = -z / dz + ny / 2 - 0.5
    const s = (a, comp = 0, stride = 1) => sampleLinear(a, stride, comp, nx, ny, fx, fy)
    arrays.aSea[k] = s(cells.sea); arrays.aAmp[k] = s(cells.amp); arrays.aLen[k] = s(cells.len)
    const ex = s(cells.dirX), ez = s(cells.dirZ), l = Math.hypot(ex, ez) || 1
    arrays.aDir[k * 2] = ex / l; arrays.aDir[k * 2 + 1] = ez / l
    arrays.aFoam[k] = s(cells.foam); arrays.aDepth[k] = s(cells.depth)
  }
  // Only the quads that touch water.
  const index = []
  for (let r = 0; r < rows - 1; r++) for (let c = 0; c < cols - 1; c++) {
    const a = r * cols + c, b = a + 1, d = a + cols, e = d + 1
    if (Math.max(arrays.aSea[a], arrays.aSea[b], arrays.aSea[d], arrays.aSea[e]) < 0.2) continue
    index.push(a, b, d, b, e, d)   // counter-clockwise seen from above: the water faces up
  }
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(position, 3))
  for (const [k, size] of Object.entries(attr)) geometry.setAttribute(k, new THREE.BufferAttribute(arrays[k], size))
  geometry.setIndex(n > 65535 ? new THREE.Uint32BufferAttribute(index, 1) : new THREE.Uint16BufferAttribute(index, 1))
  const uniforms = { uTime: { value: 0 }, uSun: { value: sun.clone().normalize() }, uGlass: { value: glass ? 1 : 0 } }
  const material = new THREE.ShaderMaterial({ vertexShader: VERTEX, fragmentShader: FRAGMENT, uniforms, transparent: true, depthWrite: !glass })
  const mesh = new THREE.Mesh(geometry, material)
  mesh.frustumCulled = false; mesh.renderOrder = 0.5
  mesh.userData.uniforms = uniforms
  return mesh
}
export const WATER_VERTEX = VERTEX, WATER_FRAGMENT = FRAGMENT
