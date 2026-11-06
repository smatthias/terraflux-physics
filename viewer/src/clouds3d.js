// Clouds in 3D, shaped by their genus. Every cloudy cell becomes a small group of soft "puffs" (deformed, translucent
// ellipsoids) arranged the way that cloud type looks in the sky: heaps with flat bases, towers, anvils, lumpy layers,
// flat sheets, fields of cloudlets, thin streaks. A shader gives them soft edges, darker bases and a slow billow; storm
// clouds flash. Rain and snow fall from precipitating clouds as animated shafts. The recipes are pure and testable; the
// three.js part turns them into two instanced meshes.
import * as THREE from 'three'

// Deterministic random numbers per cell, so a cloud keeps its shape from frame to frame.
export function rng(seed) {
  let a = (seed * 2654435761) >>> 0
  return () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296 }
}

const WHITE = [0.97, 0.98, 1], GREY = [0.86, 0.88, 0.92], BLUE_GREY = [0.82, 0.87, 0.95], STORM = [0.88, 0.9, 0.95]
// What each genus is, in the inspector's and the legend's words, and where it sits.
export const CLOUD_FORMS = {
  cumulus: { level: 'low', form: 'heap', note: 'fair-weather heaps with flat bases' },
  towering_cumulus: { level: 'low', form: 'tower', note: 'heaps growing upwards' },
  cumulonimbus: { level: 'deep', form: 'anvil', note: 'thunderstorm tower with an anvil' },
  stratocumulus: { level: 'low', form: 'lumpy layer', note: 'a low layer of lumps and rolls' },
  stratus: { level: 'low', form: 'sheet', note: 'a flat grey sheet, like lifted fog' },
  nimbostratus: { level: 'low', form: 'thick layer', note: 'a thick dark layer that rains steadily' },
  altocumulus: { level: 'mid', form: 'cloudlets', note: 'fields of small mid-level cloudlets' },
  altostratus: { level: 'mid', form: 'veil', note: 'a grey-blue mid-level veil' },
  cirrus: { level: 'high', form: 'streaks', note: 'thin ice streaks high up' },
  cirrostratus: { level: 'high', form: 'thin veil', note: 'a thin ice veil high up' },
}

// The puffs of one cell's cloud. base and top are scene heights, w the cell size in scene units, wind a horizontal unit
// vector [x, z] (towards where the wind blows). Puff positions are relative to the cell centre.
//   { x, y, z, sx, sy, sz, yaw, shade (brightness of its base), opacity, flat (1: cut flat at the cloud base), tint }
export function cloudPuffs({ type, base, top, fraction = 1, w = 1, wind = [1, 0], seed = 1 }) {
  const r = rng(seed), depth = Math.max(0.05, top - base), f = Math.min(1, Math.max(0.15, fraction))
  const jitter = s => (r() - 0.5) * 2 * s, out = []
  const yawWind = Math.atan2(wind[1], wind[0])
  // Each cell's cloud sits a little off the cell centre, so neighbouring clouds never line up on the grid.
  const ox = jitter(w * 0.22), oz = jitter(w * 0.22)
  const puff = p => out.push({ yaw: 0, shade: 0.7, opacity: 0.9, flat: 0, tint: WHITE, ...p, x: p.x + ox, z: p.z + oz })
  switch (type) {
    case 'cumulus': {
      const n = 4 + Math.round(5 * f), rad = Math.min(w * (0.2 + 0.08 * f), depth * 0.8 + w * 0.08)
      for (let k = 0; k < n; k++) { const s = rad * (0.65 + 0.4 * r()), a = r() * Math.PI * 2, d = Math.sqrt(r()) * w * 0.3; puff({ x: Math.cos(a) * d, z: Math.sin(a) * d, y: base + s * (0.4 + 0.35 * r()) + (1 - d / (w * 0.3)) * Math.min(depth * 0.25, s * 0.6), sx: s, sy: s * 0.85, sz: s, shade: 0.66, flat: 1 }) }
      const crown = Math.min(rad * 1.1, depth * 0.6 + w * 0.08)
      puff({ x: jitter(w * 0.08), z: jitter(w * 0.08), y: Math.min(top - crown * 0.5, base + depth * 0.55 + crown * 0.3), sx: crown, sy: crown * 0.9, sz: crown, shade: 0.75, flat: 1 })
      break
    }
    case 'towering_cumulus': case 'cumulonimbus': {
      const storm = type === 'cumulonimbus', towerTop = (storm ? top - depth * 0.12 : top) - r() * depth * 0.15
      const tiers = Math.max(3, Math.min(storm ? 9 : 7, Math.round((towerTop - base) / (w * 0.3))))
      for (let t = 0; t < tiers; t++) {
        const u = t / Math.max(1, tiers - 1), s = w * (storm ? 0.36 - 0.12 * u : 0.3 - 0.13 * u), each = t === 0 ? 5 : u < 0.5 ? 3 : 2
        for (let k = 0; k < each; k++) { const ss = s * (0.8 + 0.4 * r()); puff({ x: jitter(w * 0.16) + (storm ? wind[0] * u * w * 0.15 : 0), z: jitter(w * 0.16) + (storm ? wind[1] * u * w * 0.15 : 0), y: base + s * 0.5 + (towerTop - base - s) * u, sx: ss, sy: ss * 1.05, sz: ss, shade: storm ? 0.36 : 0.6, tint: storm ? STORM : WHITE, flat: t === 0 ? 1 : 0 }) }
      }
      if (storm) {
        // The anvil: ice spreading out under the tropopause, carried downwind.
        const n = 4 + Math.round(3 * f)
        for (let k = 0; k < n; k++) { const reach = w * (0.3 + 0.9 * r()), s = w * (0.55 + 0.35 * r()); puff({ x: wind[0] * reach + jitter(w * 0.25), z: wind[1] * reach + jitter(w * 0.25), y: top - depth * 0.06 + jitter(depth * 0.02), sx: s, sy: depth * 0.045 + w * 0.07, sz: s * 0.8, yaw: yawWind, shade: 0.82, opacity: 0.86, tint: WHITE }) }
      }
      break
    }
    case 'stratocumulus': {
      const n = 3 + Math.round(3 * f)
      for (let k = 0; k < n; k++) { const s = w * (0.34 + 0.16 * r()); puff({ x: jitter(w * 0.38), z: jitter(w * 0.38), y: base + depth * 0.5, sx: s, sy: Math.max(depth * 0.5, w * 0.12), sz: s * 0.8, yaw: yawWind + jitter(0.4), shade: 0.58, opacity: 0.86, tint: GREY }) }
      break
    }
    case 'stratus': case 'altostratus': case 'cirrostratus': {
      const high = type === 'cirrostratus', mid = type === 'altostratus'
      for (let k = 0; k < 2; k++) puff({ x: jitter(w * 0.15), z: jitter(w * 0.15), y: base + depth * 0.5, sx: w * (0.74 + 0.1 * r()), sy: Math.max(depth * 0.45, w * (high ? 0.025 : 0.05)), sz: w * (0.74 + 0.1 * r()),
        shade: high ? 0.96 : mid ? 0.74 : 0.56, opacity: (high ? 0.2 : mid ? 0.42 : 0.52) * (0.6 + 0.4 * f), tint: mid ? BLUE_GREY : high ? WHITE : GREY })
      break
    }
    case 'nimbostratus': {
      for (let k = 0; k < 3; k++) puff({ x: jitter(w * 0.2), z: jitter(w * 0.2), y: base + depth * (0.3 + 0.4 * r()), sx: w * (0.66 + 0.12 * r()), sy: Math.max(depth * 0.42, w * 0.16), sz: w * (0.66 + 0.12 * r()), shade: 0.3, opacity: 0.9, tint: STORM })
      break
    }
    case 'altocumulus': {
      const side = f > 0.6 ? 3 : 2
      for (let a = 0; a < side; a++) for (let b = 0; b < side; b++) {
        if (r() > 0.35 + 0.65 * f) continue
        const s = w * (0.1 + 0.07 * r())
        puff({ x: ((a + 0.5) / side - 0.5) * w * 0.9 + jitter(w * 0.06), z: ((b + 0.5) / side - 0.5) * w * 0.9 + jitter(w * 0.06), y: base + depth * 0.5 + jitter(Math.min(depth * 0.3, w * 0.05)), sx: s * 1.3, sy: s * 0.6, sz: s * 1.1, yaw: r() * 3, shade: 0.72, opacity: 0.88, flat: 1 })
      }
      break
    }
    case 'cirrus': {
      const n = 2 + Math.round(2 * f)
      for (let k = 0; k < n; k++) puff({ x: jitter(w * 0.35), z: jitter(w * 0.35), y: base + depth * r(), sx: w * (0.45 + 0.3 * r()), sy: w * 0.02, sz: w * 0.05, yaw: yawWind + jitter(0.35), shade: 1, opacity: 0.42, tint: WHITE })
      break
    }
    default: break
  }
  return out
}

// ---- three.js --------------------------------------------------------------------------------------------------------
export const CLOUD_VERTEX = `
attribute vec4 aCloud;   // x: base y, y: depth, z: shade at the base, w: opacity
attribute vec3 aTint;
attribute vec2 aMisc;    // x: flat base + 2 * storm (0..3), y: seed
attribute vec3 aSpin;    // rotation about a vertical axis: centre x, z (mesh coordinates) and angular speed (rad/s, + counter-clockwise from above)
uniform float uTime;
varying vec3 vWorld; varying vec3 vNormalW; varying vec3 vSphereW; varying vec4 vCloud; varying vec3 vTint; varying vec2 vMisc;
float h3(vec3 p){ p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float n3(vec3 x){ vec3 i = floor(x), f = fract(x); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(h3(i), h3(i + vec3(1,0,0)), f.x), mix(h3(i + vec3(0,1,0)), h3(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(h3(i + vec3(0,0,1)), h3(i + vec3(1,0,1)), f.x), mix(h3(i + vec3(0,1,1)), h3(i + vec3(1,1,1)), f.x), f.y), f.z); }
// Cauliflower bumps in three octaves that billow slowly; q is a point of the unit sphere.
float bumps(vec3 q){
  return n3(q * 2.2 + aMisc.y * 11.0 + vec3(0.0, uTime * 0.08, 0.0)) * 0.55 + n3(q * 4.7 + aMisc.y * 3.0 - uTime * 0.05) * 0.3 + n3(q * 9.5 + aMisc.y * 7.0) * 0.15;
}
vec3 lumpy(vec3 q){ return q * (1.0 + (bumps(q) - 0.45) * 0.55); }
void main(){
  vec3 q = normalize(position), p = lumpy(q);
  // The normal of the lumpy surface from two nearby points, so the lumps are lit, not only outlined.
  vec3 t1 = normalize(cross(q, abs(q.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0))), t2 = cross(q, t1);
  vec3 nl = normalize(cross(lumpy(normalize(q + t1 * 0.09)) - p, lumpy(normalize(q + t2 * 0.09)) - p));
  if (dot(nl, q) < 0.0) nl = -nl;
  // Spin (supercell updrafts, hurricane eyewalls): turn the puff about its storm's vertical axis.
  float sa = aSpin.z * uTime, cs = cos(sa), sn = sin(sa);
  mat3 spin = mat3(cs, 0.0, -sn, 0.0, 1.0, 0.0, sn, 0.0, cs);
  vec4 local = instanceMatrix * vec4(p, 1.0);
  local.xz = vec2(aSpin.x, aSpin.y) + (spin * vec3(local.x - aSpin.x, 0.0, local.z - aSpin.y)).xz;
  vec4 world = modelMatrix * local;
  vWorld = world.xyz;
  mat3 m = mat3(modelMatrix) * spin * mat3(instanceMatrix);
  vNormalW = normalize(m * nl); vSphereW = normalize(m * q);
  vCloud = aCloud; vTint = aTint; vMisc = aMisc;
  gl_Position = projectionMatrix * viewMatrix * world;
}`
export const CLOUD_FRAGMENT = `
uniform vec3 uSun; uniform float uTime; uniform float uFlash; uniform float uOpacity;
varying vec3 vWorld; varying vec3 vNormalW; varying vec3 vSphereW; varying vec4 vCloud; varying vec3 vTint; varying vec2 vMisc;
void main(){
  vec3 n = normalize(vNormalW), v = normalize(cameraPosition - vWorld), sun = normalize(uSun);
  // Dark only near the base: tall towers are bright in the sun above their lower part.
  float rise = smoothstep(vCloud.x, vCloud.x + min(vCloud.y * 0.45, 1.2) + 0.05, vWorld.y);
  float ndl = max(dot(n, sun), 0.0), edge = clamp(abs(dot(normalize(vSphereW), v)), 0.0, 1.0);
  // Sunlit tops, cool blue shadows, darker bases, and a bright rim where the sun shines through the thin edge.
  float light = 0.7 + 0.32 * ndl + 0.2 * pow(1.0 - edge, 2.0) * max(dot(-v, sun) * 0.5 + 0.5, 0.0);
  vec3 c = vTint * light * mix(vCloud.z, 1.0, rise);
  c = mix(c * vec3(0.86, 0.9, 1.0), c, ndl);
  // Lightning: a storm cloud lights up now and then, each at its own moments.
  float storm = step(1.5, vMisc.x), flatBase = step(0.5, mod(vMisc.x, 2.0));
  float tick = floor(uTime * 9.0 + vMisc.y * 37.0);
  float flash = uFlash * storm * step(0.975, fract(sin(tick * 12.9898 + vMisc.y * 78.233) * 43758.5453)) * (1.0 - rise * 0.5);
  c += vec3(0.75, 0.72, 1.0) * flash;
  // Soft silhouettes: a puff fades out towards its outline, so overlapping puffs merge into one cloud.
  // Both the lumpy and the round outline fade, so no lump shows a hard edge.
  float alpha = uOpacity * vCloud.w * smoothstep(0.0, 0.45, edge) * smoothstep(0.0, 0.5, abs(dot(n, v)));
  if (flatBase > 0.5) alpha *= smoothstep(vCloud.x - 0.03, vCloud.x + 0.06, vWorld.y);   // flat base
  if (alpha < 0.01) discard;
  gl_FragColor = vec4(c, alpha);
}`

// puffs: [{ x, y, z (scene), sx, sy, sz, yaw, shade, opacity, flat, tint, base, depth, storm, seed }]
// opacity: 1 in the field explorer; lower over composed maps, so the map stays readable under a storm's anvils.
export function cloudMesh(puffs, { sun = new THREE.Vector3(-30, 19, 10), opacity = 1 } = {}) {
  // Finer puffs while there are not too many of them (a hurricane of storm towers has thousands).
  const n = puffs.length, geometry = new THREE.IcosahedronGeometry(1, n < 2500 ? 3 : 2)
  const aCloud = new Float32Array(n * 4), aTint = new Float32Array(n * 3), aMisc = new Float32Array(n * 2), aSpin = new Float32Array(n * 3)
  const uniforms = { uTime: { value: 0 }, uSun: { value: sun.clone().normalize() }, uFlash: { value: 1 }, uOpacity: { value: opacity } }
  const material = new THREE.ShaderMaterial({ vertexShader: CLOUD_VERTEX, fragmentShader: CLOUD_FRAGMENT, uniforms, transparent: true, depthWrite: false })
  const mesh = new THREE.InstancedMesh(geometry, material, Math.max(1, n))
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), up = new THREE.Vector3(0, 1, 0)
  puffs.forEach((p, i) => {
    m.compose(new THREE.Vector3(p.x, p.y, p.z), q.setFromAxisAngle(up, -p.yaw), new THREE.Vector3(p.sx, p.sy, p.sz))
    mesh.setMatrixAt(i, m)
    aCloud.set([p.base, p.depth, p.shade, p.opacity], i * 4); aTint.set(p.tint, i * 3)
    aMisc.set([(p.flat ? 1 : 0) + (p.storm ? 2 : 0), p.seed ?? i * 0.137], i * 2)
    if (p.spin) aSpin.set([p.spin.x, p.spin.z, p.spin.omega], i * 3)
  })
  mesh.count = n
  geometry.setAttribute('aCloud', new THREE.InstancedBufferAttribute(aCloud, 4))
  geometry.setAttribute('aTint', new THREE.InstancedBufferAttribute(aTint, 3))
  geometry.setAttribute('aMisc', new THREE.InstancedBufferAttribute(aMisc, 2))
  geometry.setAttribute('aSpin', new THREE.InstancedBufferAttribute(aSpin, 3))
  mesh.frustumCulled = false; mesh.renderOrder = 2
  mesh.userData.uniforms = uniforms
  return mesh
}

// Rain and snow below precipitating clouds: open cylinders with falling streaks (rain) or drifting flakes (snow).
// shafts: [{ x, z, top, bottom, radius, intensity (0..1), snow (bool) }]
export const SHAFT_VERTEX = `
attribute vec2 aShaft;   // x: intensity, y: snow (0/1)
varying vec2 vUv; varying vec2 vShaft; varying float vHeight;
void main(){ vUv = uv; vShaft = aShaft; vec4 world = modelMatrix * instanceMatrix * vec4(position, 1.0); vHeight = instanceMatrix[1][1]; gl_Position = projectionMatrix * viewMatrix * world; }`
export const SHAFT_FRAGMENT = `
uniform float uTime;
varying vec2 vUv; varying vec2 vShaft; varying float vHeight;
float hash(float x){ return fract(sin(x * 91.345) * 47453.5453); }
void main(){
  float snow = vShaft.y, lanes = snow > 0.5 ? 22.0 : 46.0;
  float lane = floor(vUv.x * lanes), u = fract(vUv.x * lanes);
  float speed = snow > 0.5 ? 0.35 : 2.4;
  float y = vUv.y * vHeight * (snow > 0.5 ? 1.6 : 0.9) + uTime * speed + hash(lane) * 7.0;
  float cell = fract(y);
  float mark = snow > 0.5
    ? (1.0 - smoothstep(0.12, 0.3, length(vec2(u - 0.5 + 0.25 * sin(y * 3.0 + lane), (cell - 0.5) * 1.6))))
    : smoothstep(0.0, 0.08, u) * (1.0 - smoothstep(0.1, 0.2, u)) * smoothstep(0.0, 0.5, cell) * (1.0 - smoothstep(0.55, 0.8, cell));
  float fade = smoothstep(0.0, 0.15, vUv.y) * (1.0 - smoothstep(0.82, 1.0, vUv.y));
  float a = mark * fade * (0.25 + 0.75 * vShaft.x) * (snow > 0.5 ? 0.9 : 0.7) + fade * 0.05 * vShaft.x;
  if (a < 0.01) discard;
  gl_FragColor = vec4(snow > 0.5 ? vec3(0.96, 0.98, 1.0) : vec3(0.66, 0.8, 0.95), a);
}`
export function precipitationMesh(shafts) {
  const geometry = new THREE.CylinderGeometry(1, 1, 1, 14, 1, true).translate(0, 0.5, 0), n = shafts.length
  const aShaft = new Float32Array(n * 2), uniforms = { uTime: { value: 0 } }
  const material = new THREE.ShaderMaterial({ vertexShader: SHAFT_VERTEX, fragmentShader: SHAFT_FRAGMENT, uniforms, transparent: true, depthWrite: false, side: THREE.DoubleSide })
  const mesh = new THREE.InstancedMesh(geometry, material, Math.max(1, n)), m = new THREE.Matrix4()
  shafts.forEach((s, i) => {
    const h = Math.max(0.05, s.top - s.bottom)
    m.makeScale(s.radius, h, s.radius).setPosition(s.x, s.bottom, s.z); mesh.setMatrixAt(i, m)
    aShaft.set([s.intensity, s.snow ? 1 : 0], i * 2)
  })
  mesh.count = n
  geometry.setAttribute('aShaft', new THREE.InstancedBufferAttribute(aShaft, 2))
  mesh.frustumCulled = false; mesh.renderOrder = 1
  mesh.userData.uniforms = uniforms
  return mesh
}

// How strongly it rains, for the shafts: 0.1 mm/h faint … 40 mm/h and more at full strength.
export const precipitationIntensity = rate => (Number.isFinite(rate) && rate > 0 ? Math.min(1, Math.log1p(rate) / Math.log1p(40)) : 0)
