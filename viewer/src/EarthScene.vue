<script setup>
// The maps of the Coupled Earth workspace in 3D: the forecast domain as terrain (realistic land, the forecast field
// laid over it, clouds, rain, isobars, winds, pressure systems, cities, warnings and the hurricane) and the climate grid
// as a turning globe (the warming or CO2 field, emitters, hotspots and the CO2 fluxes). Everything moves: the arrows
// glide with the wind, the clouds billow, the hurricane spins and glides along its track, the warning areas pulse;
// the parent steps the forecast hours, days or decades. Hover lights up a cell, click pins it (events to the parent).
import { ref, watch, onMounted, onBeforeUnmount, nextTick } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { planeSurface, globeSurface, surfaceSkirt, surfaceMaterial, skirtMaterial, surfaceSubdivisions, centreHeights, cellAtUv, surfaceColours } from './surface.js'
import { reliefRange, verticalScale, roughness, snowlineFor, treelineFor, strataSpacing } from './terrain.js'
import { cloudPuffs, cloudMesh, precipitationMesh, precipitationIntensity } from './clouds3d.js'
import { arrowField } from './flowArrows.js'
import { watchCurtain, hurricaneBands } from './stormScene.js'
import { maskOutline } from './stormWatch.js'
import { cloudForms, cloudDrawn, isobars, levelMask, globeDir } from './earthScene.js'
import { globeReliefHeight } from './globalRelief.js'
import { textSprite, glyphSprite, softFloor, sceneLights, disposeTree } from './sceneKit.js'

const props = defineProps({
  kind: { type: String, required: true },            // 'forecast' | 'climate'
  grid: { type: Object, required: true },            // forecast: { nx, ny, spacing, lat0 }; climate: { nx, ny, dx, dy }
  land: { type: Array, required: true },
  elevation: { type: Array, default: null },
  colors: { type: Array, default: null },            // CSS colour (or null) per cell: the field of the view
  heights: { type: Array, default: null },           // climate: 0..1 per cell, raised on the globe
  overlays: { type: Object, default: () => ({}) },
  selected: { type: Number, default: null },
  playing: { type: Boolean, default: false },
  canPlay: { type: Boolean, default: false },
  playLabel: { type: String, default: '' },
})
const emit = defineEmits(['hover', 'pick', 'toggle-play'])
const host = ref(null), root = ref(null)
const layers = ref({ clouds: true, wind: true, isobars: true, labels: true, relief: true, turn: true })
const fullscreen = ref(false)
const reduced = typeof window !== 'undefined' && !!window.matchMedia?.('(prefers-reduced-motion: reduce)').matches

let renderer, scene, camera, controls, group, frame, observer, pick = null, uniforms = [], updates = [], surfaceU = null, info = null, lastTime = 0, clock = 0
let lastHurricane = null

function init() {
  scene = new THREE.Scene(); scene.background = new THREE.Color('#09151d'); scene.fog = new THREE.Fog('#09151d', 70, 140)
  camera = new THREE.PerspectiveCamera(42, 1, 0.1, 300)
  renderer = new THREE.WebGLRenderer({ antialias: true }); renderer.setPixelRatio(Math.min(devicePixelRatio, 2))
  renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap
  host.value.appendChild(renderer.domElement)
  const { sun } = sceneLights(scene); scene.userData.sun = sun
  scene.userData.floor = softFloor(60); scene.add(scene.userData.floor)
  controls = new OrbitControls(camera, renderer.domElement); controls.enableDamping = true; controls.minDistance = 6; controls.maxDistance = 120
  controls.addEventListener('start', () => { controls.autoRotate = false })
  observer = new ResizeObserver(resize); observer.observe(host.value); resize()
  const el = renderer.domElement
  el.addEventListener('pointermove', onMove); el.addEventListener('pointerleave', () => emit('hover', null))
  el.addEventListener('pointerdown', e => { downAt = [e.clientX, e.clientY] }); el.addEventListener('pointerup', onUp)
  window.addEventListener('keydown', onKey); document.addEventListener('fullscreenchange', onFullscreen)
  build(); resetCamera()
  const tick = now => {
    frame = requestAnimationFrame(tick)
    if (!root.value || root.value.offsetParent === null && !document.fullscreenElement) { lastTime = 0; return }   // hidden: no work
    const dt = lastTime ? Math.min(0.1, (now - lastTime) / 1000) : 0; lastTime = now
    if (!reduced) clock += dt
    for (const u of uniforms) u.uTime.value = clock
    stepColours(dt)
    for (const f of updates) f(dt)
    controls.update(); renderer.render(scene, camera)
  }
  tick(performance.now())
}
function resize() {
  const r = host.value.getBoundingClientRect(), w = Math.max(1, r.width), h = Math.max(1, r.height)
  renderer.setSize(w, h); camera.aspect = w / h; camera.updateProjectionMatrix()
}
function resetCamera() {
  if (props.kind === 'climate') { camera.position.set(0, 12, 34); controls.target.set(0, 0, 0); controls.autoRotate = layers.value.turn && !reduced; controls.autoRotateSpeed = 0.6 }
  else { camera.position.set(0, 17, 20.5); controls.target.set(0, 0, 1.2); controls.autoRotate = false }
  controls.update()
}

// ---- Building -----------------------------------------------------------------------------------------------------------
// Two layers: the base (the terrain or the globe, rebuilt when the grid, the relief or the heights change) and the
// overlays (clouds, winds, isobars, labels, storms; rebuilt when the parent steps a frame). A new field only recolours
// the base, blending from the old colours, so playing the forecast does not rebuild the terrain.
const css = c => (c ? new THREE.Color(c) : null)
let base = null, sheet = null, where = null, colourTween = null
let dirty = { base: false, colours: false, overlays: false }, scheduled = false
function schedule(part) {
  dirty[part] = true
  if (scheduled) return
  scheduled = true
  queueMicrotask(() => { scheduled = false; sync() })
}
function sync() {
  if (!renderer) return
  const d = dirty; dirty = { base: false, colours: false, overlays: false }
  if (d.base || !base) { buildBase(); buildOverlays(); return }
  if (d.colours) recolour()
  if (d.overlays) buildOverlays()
}
function build() { dirty = { base: false, colours: false, overlays: false }; buildBase(); buildOverlays() }

function baseColours(n, landColour, seaColour) {
  const out = new Float32Array(n * 3), c = new THREE.Color()
  for (let i = 0; i < n; i++) {
    const field = css(props.colors?.[i])
    if (field) c.copy(field); else c.set(props.land[i] ? landColour : seaColour)
    out.set([c.r, c.g, c.b], i * 3)
  }
  return out
}
const LAND = { forecast: ['#3a4a34', '#0d2433'], climate: ['#3d5238', '#0f2c44'] }
function recolour() {
  if (!sheet || !info) return
  const attr = sheet.geometry.attributes.color, target = surfaceColours(sheet.geometry, baseColours(info.nx * info.ny, ...LAND[props.kind]), info.nx, info.ny, info.wrap)
  if (reduced) { attr.array.set(target); attr.needsUpdate = true; colourTween = null; return }
  colourTween = { from: Float32Array.from(attr.array), target, t: 0 }
}
function stepColours(dt) {
  if (!colourTween || !sheet) return
  const tw = colourTween, attr = sheet.geometry.attributes.color, a = attr.array
  tw.t = Math.min(1, tw.t + dt / 0.45)
  const e = tw.t * tw.t * (3 - 2 * tw.t)
  for (let k = 0; k < a.length; k++) a[k] = tw.from[k] + (tw.target[k] - tw.from[k]) * e
  attr.needsUpdate = true
  if (tw.t >= 1) colourTween = null
}

function buildBase() {
  if (!renderer) return
  if (base) { scene.remove(base); disposeTree(base) }
  base = new THREE.Group(); scene.add(base); pick = null; sheet = null; colourTween = null
  if (props.kind === 'climate') globeBase(); else forecastBase()
  syncHighlight()
}
function buildOverlays() {
  if (!renderer || !where) return
  if (group) { scene.remove(group); disposeTree(group) }
  group = new THREE.Group(); scene.add(group); uniforms = []; updates = []
  if (props.kind === 'climate') globeOverlays(); else forecastOverlays()
}

function forecastBase() {
  const g = props.grid, nx = g.nx, ny = g.ny, count = nx * ny
  const scale = 32 / Math.max(nx, ny) / g.spacing, dx = g.spacing * scale, dz = dx, w = dx
  const elev = props.elevation ?? new Array(count).fill(0), relief = reliefRange(elev), vs = verticalScale(relief.range, scale)
  // Lower than in the observatory: a 210 km cell is wide, single-cell mountains would stand as needles.
  const heightOf = i => 0.15 + (layers.value.relief ? Math.max(0, elev[i]) * vs * 0.7 : 0)
  const heights = Float32Array.from({ length: count }, (_, i) => heightOf(i))
  const water = Float32Array.from(props.land, l => (l ? 0 : 1)), aElev = Float32Array.from(elev, (e, i) => (props.land[i] ? e : -200))
  const extras = { aElev, aWater: water, aIce: new Float32Array(count), aSnow: new Float32Array(count) }
  const rough = roughness({ elevations: elev, nx, ny, water })
  const reliefUnits = relief.range * vs * 0.7
  const opts = { nx, ny, dx, dz, heights, colors: baseColours(count, ...LAND.forecast), values: null }
  const geometry = planeSurface({ ...opts, s: surfaceSubdivisions(nx, ny, 90000), extras, occlusion: Math.max(1e-3, reliefUnits * 0.05), detail: layers.value.relief ? { amplitude: rough.map(r => r * reliefUnits * 0.07), seed: 11 } : null })
  const top = centreHeights(geometry, nx, ny)
  const snow = snowlineFor(g.lat0 ?? 40)
  const material = surfaceMaterial({ nx, ny, terrain: { dataMix: props.colors ? 0.62 : 0.05, snowline: snow, treeline: treelineFor(snow), strata: strataSpacing(relief.range) } })
  surfaceU = material.userData.uniforms
  sheet = new THREE.Mesh(geometry, material); sheet.castShadow = sheet.receiveShadow = true; base.add(sheet)
  const side = new THREE.Mesh(surfaceSkirt(geometry, 0), skirtMaterial({ terrain: true, strata: 3 })); side.receiveShadow = true; base.add(side)
  pick = new THREE.Mesh(planeSurface({ ...opts, s: 2 }), new THREE.MeshBasicMaterial({ visible: false })); base.add(pick)
  scene.userData.sun.castShadow = true; scene.userData.floor.position.y = -0.06
  info = { nx, ny, wrap: false }
  const X = x => (x - nx / 2) * dx, Z = y => -(y - ny / 2) * dz
  where = { nx, ny, dx, dz, w, top, X, Z,
    cellX: i => X(i % nx + 0.5), cellZ: i => Z(Math.floor(i / nx) + 0.5),
    groundAt: (x, y) => top[Math.min(ny - 1, Math.max(0, Math.floor(y))) * nx + Math.min(nx - 1, Math.max(0, Math.floor(x)))] ?? 0.15,
    ceiling: Math.max(...top) + 0.4 }
}

function forecastOverlays() {
  const { nx, ny, dx, dz, w, top, X, Z, cellX, cellZ, groundAt, ceiling } = where, o = props.overlays, L = layers.value
  // Clouds from the forecast's cover, rain and thunder chance, every second cell (each drawn twice as wide), and rain.
  if (L.clouds && o.clouds) {
    const forms = cloudForms(o.clouds), cloudScale = 6 / 12000, puffs = [], shafts = []
    for (let y = 0; y < ny; y += 2) for (let x = 0; x < nx; x += 2) {
      const i = y * nx + x, c = forms[i]
      if (!c || !cloudDrawn(i, o.clouds.cover[i])) continue
      const ground = top[i] + 0.2, baseY = ground + c.base * cloudScale, topY = ground + c.top * cloudScale
      const u = o.wind?.u[i] ?? 1, v = o.wind?.v[i] ?? 0, s = Math.hypot(u, v), wind = s > 0.1 ? [u / s, -v / s] : [1, 0]
      for (const q of cloudPuffs({ type: c.type, base: baseY, top: topY, fraction: c.fraction, w: w * 2, wind, seed: i + 1 })) puffs.push({ ...q, x: q.x + cellX(i) + w / 2, z: q.z + cellZ(i) - w / 2, base: baseY, depth: topY - baseY, storm: c.type === 'cumulonimbus', seed: (i * 0.618) % 1 })
      const rain = o.clouds.precip?.[i]
      if (rain > 0.5) shafts.push({ x: cellX(i) + w / 2, z: cellZ(i) - w / 2, top: baseY, bottom: top[i], radius: w * 0.6, intensity: precipitationIntensity(rain), snow: false })
    }
    if (puffs.length) { const m = cloudMesh(puffs, { sun: scene.userData.sun.position, opacity: 0.55 }); uniforms.push(m.userData.uniforms); group.add(m) }
    if (shafts.length) { const m = precipitationMesh(shafts); uniforms.push(m.userData.uniforms); group.add(m) }
  }
  // Isobars, floating above the relief, labelled on the major lines.
  if (L.isobars && o.pressure) {
    for (const line of isobars(o.pressure, nx, ny, 4)) {
      const pts = line.segments.flatMap(([a, b]) => [new THREE.Vector3(X(a[0]), ceiling, Z(a[1])), new THREE.Vector3(X(b[0]), ceiling, Z(b[1]))])
      group.add(new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(pts), new THREE.LineBasicMaterial({ color: line.major ? 0xf3f7f9 : 0xa9bcc6, transparent: true, opacity: line.major ? 0.9 : 0.5 })))
      if (line.major && L.labels) { const [a] = line.segments[Math.floor(line.segments.length / 2)], t = textSprite(String(line.level), '#f3f7f9', 0.42); t.position.set(X(a[0]), ceiling + 0.3, Z(a[1])); group.add(t) }
    }
  }
  // The wind: arrows gliding with it, longer and faster where it is stronger.
  if (L.wind && o.wind) {
    const arrows = [], stride = 2, speeds = o.wind.u.map((u, i) => Math.hypot(u, o.wind.v[i])), strongest = Math.max(1, ...speeds)
    for (let y = 1; y < ny; y += stride) for (let x = 1; x < nx; x += stride) {
      const i = y * nx + x, s = speeds[i]
      if (s < 0.5) continue
      arrows.push({ dir: new THREE.Vector3(o.wind.u[i], 0, -o.wind.v[i]), origin: new THREE.Vector3(cellX(i), top[i] + 0.35, cellZ(i)), length: w * (0.45 + 1.3 * Math.sqrt(s / strongest)), color: 0xdfe9f2, headLength: w * 0.3, headWidth: w * 0.12, rate: s / strongest })
    }
    if (arrows.length) { const m = arrowField(arrows, { moving: !reduced }); uniforms.push(m.userData.uniforms); group.add(m) }
  }
  // Warnings: pulsing curtains around the orange and red areas.
  if (o.warnings) for (const [level, colour] of [[2, 0xffa726], [3, 0xff3b30]]) {
    const mask = levelMask(o.warnings.levels, level)
    if (!mask.some(Boolean)) continue
    const seg = maskOutline(mask, nx, ny).map(([x0, y0, x1, y1]) => [X(x0), Z(y0), X(x1), Z(y1)])
    const curtain = watchCurtain(seg, (x, z) => groundAt(x / dx + nx / 2, -z / dz + ny / 2) + 0.02, { height: level === 3 ? 1 : 0.6, colour }); uniforms.push(curtain.userData.uniforms); group.add(curtain)
  }
  // Pressure systems and cities.
  if (L.labels) {
    for (const s of o.systems ?? []) { const t = glyphSprite(s.kind === 'low' ? 'L' : 'H', s.kind === 'low' ? '#ff6b6b' : '#64b5f6', 1.6); t.position.set(X(s.x), ceiling + 0.9, Z(s.y)); group.add(t) }
    for (const c of o.cities ?? []) {
      const y = groundAt(c.x, c.y), pin = new THREE.Mesh(new THREE.SphereGeometry(0.09, 10, 8), new THREE.MeshBasicMaterial({ color: c.highlight ? 0xffd166 : 0xf4f8fa }))
      pin.position.set(X(c.x), y + 0.1, Z(c.y)); group.add(pin)
      const t = textSprite(c.name, c.highlight ? '#ffd166' : '#eef5f8', 0.5); t.position.set(X(c.x), y + 0.55, Z(c.y)); group.add(t)
    }
  }
  // The hurricane: the track plume (members by category, the truth run white), the members now, and the ensemble's
  // storm spinning, gliding from where it was drawn last to its position at this hour.
  for (const t of o.tracks ?? []) {
    if (t.points.length < 2) continue
    // Members coloured by their category along the way; the truth run white and dashed.
    const pts = t.points.map(p => new THREE.Vector3(X(p.x), 0.5 + (t.truth ? 0.05 : 0), Z(p.y))), c = new THREE.Color()
    const geo = new THREE.BufferGeometry().setFromPoints(pts)
    geo.setAttribute('color', new THREE.Float32BufferAttribute(t.points.flatMap(p => c.set(t.truth ? '#ffffff' : p.colour ?? t.colour ?? '#ffd166').toArray()), 3))
    const line = new THREE.Line(geo, t.truth ? new THREE.LineDashedMaterial({ vertexColors: true, dashSize: 0.35, gapSize: 0.2, transparent: true, opacity: 0.95 }) : new THREE.LineBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.5 }))
    if (t.truth) line.computeLineDistances()
    group.add(line)
  }
  for (const m of o.members ?? []) { const dot = new THREE.Mesh(new THREE.SphereGeometry(0.12, 10, 8), new THREE.MeshBasicMaterial({ color: new THREE.Color(m.colour ?? '#ffd166') })); dot.position.set(X(m.x), 0.55, Z(m.y)); group.add(dot) }
  const h = o.hurricane
  if (h) {
    const hg = new THREE.Group(), gale = Math.max(1.2, h.gale * dx), core = Math.max(0.35, gale * 0.28), eye = core * 0.45, spin = (h.northern === false ? -1 : 1) * (0.4 + h.wind / 60)
    const bands = hurricaneBands({ gale, core, eye, spin, y: 1.6 }); uniforms.push(bands.userData.uniforms); hg.add(bands)
    const wall = []
    for (let k = 0; k < 14; k++) { const a = k / 14 * Math.PI * 2, r = (eye + core) / 2; for (const q of cloudPuffs({ type: 'towering_cumulus', base: 0.4, top: 2.6, fraction: 1, w: Math.max(0.2, core * 0.35), seed: 97 + k })) wall.push({ ...q, x: q.x + Math.cos(a) * r, z: q.z - Math.sin(a) * r, base: 0.4, depth: 2.2, storm: true, seed: (k * 0.173) % 1, spin: { x: 0, z: 0, omega: spin } }) }
    const wm = cloudMesh(wall, { sun: scene.userData.sun.position }); uniforms.push(wm.userData.uniforms); hg.add(wm)
    if (L.labels) { const t = textSprite(h.label, h.colour ?? '#ffd166', 0.7); t.position.y = 3.6; hg.add(t) }
    const target = [X(h.x), Z(h.y)], from = lastHurricane ?? target
    hg.position.set(from[0], 0, from[1])
    if (!reduced && (from[0] !== target[0] || from[1] !== target[1])) { let t = 0; updates.push(dt => { if (t >= 1) return; t = Math.min(1, t + dt / 0.9); const e = 1 - (1 - t) ** 2; hg.position.set(from[0] + (target[0] - from[0]) * e, 0, from[1] + (target[1] - from[1]) * e) }) }
    else hg.position.set(target[0], 0, target[1])
    lastHurricane = target
    group.add(hg)
  } else lastHurricane = null
}

const GLOBE_R = 9
function globeBase() {
  const g = props.grid, nx = g.nx, ny = g.ny, count = nx * ny, R = GLOBE_R
  const raise = i => (layers.value.relief && props.elevation ? globeReliefHeight(props.elevation[i], R) : (props.land[i] ? 0.06 : 0)) + (layers.value.relief && props.heights ? Math.max(0, Math.min(1, props.heights[i] ?? 0)) * 1.6 : 0)
  const opts = { nx, ny, heights: Float32Array.from({ length: count }, (_, i) => raise(i)), colors: baseColours(count, ...LAND.climate), values: null, R, centre: new THREE.Vector3(0, 0, 0), dir: globeDir }
  const material = surfaceMaterial({ nx, ny, wrap: true }); surfaceU = material.userData.uniforms
  sheet = new THREE.Mesh(globeSurface({ ...opts, s: 4 }), material); base.add(sheet)
  pick = new THREE.Mesh(globeSurface({ ...opts, s: 1 }), new THREE.MeshBasicMaterial({ visible: false })); base.add(pick)
  scene.userData.sun.castShadow = false; scene.userData.floor.position.y = -R - 2.5   // below the globe, not through it
  info = { nx, ny, wrap: true }
  // The atmosphere's rim.
  const rim = new THREE.Mesh(new THREE.SphereGeometry(R * 1.06, 64, 48), new THREE.ShaderMaterial({ transparent: true, depthWrite: false, side: THREE.BackSide,
    vertexShader: 'varying vec3 vN; varying vec3 vV; void main(){ vec4 w = modelMatrix * vec4(position,1.0); vN = normalize(mat3(modelMatrix) * normal); vV = normalize(cameraPosition - w.xyz); gl_Position = projectionMatrix * viewMatrix * w; }',
    fragmentShader: 'varying vec3 vN; varying vec3 vV; void main(){ float f = pow(1.0 - abs(dot(vN, vV)), 3.0); gl_FragColor = vec4(0.45, 0.75, 1.0, f * 0.7); }' }))
  base.add(rim)
  where = { R, at: (lat, lon, lift) => { const d = globeDir(lat, lon); return new THREE.Vector3(d[0], d[1], d[2]).multiplyScalar(R + lift) } }
  controls.autoRotate = layers.value.turn && !reduced
}

function globeOverlays() {
  const { at } = where, o = props.overlays
  for (const m of o.markers ?? []) {
    const p = at(m.lat, m.lon, 0.25), normal = p.clone().normalize()
    if (m.kind === 'ring') {
      const ring = new THREE.Mesh(new THREE.RingGeometry(m.size * 0.85, m.size, 40), new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.85, side: THREE.DoubleSide, depthWrite: false }))
      ring.position.copy(p); ring.lookAt(p.clone().add(normal)); group.add(ring)
      // A pulse expanding from the emitter.
      const pulse = ring.clone(); pulse.material = ring.material.clone(); group.add(pulse)
      const phase = Math.random()
      updates.push(() => { const f = (clock * 0.5 + phase) % 1; pulse.scale.setScalar(1 + f * 1.6); pulse.material.opacity = 0.6 * (1 - f) })
    } else { const pin = glyphSprite('▲', m.colour ?? '#ffd166', 0.55); pin.material.depthTest = true; pin.position.copy(at(m.lat, m.lon, 0.45)); group.add(pin) }
    // Labels hide behind the globe (depth-tested, unlike the labels over the forecast map).
    if (m.label && layers.value.labels) { const t = textSprite(m.label, '#eef5f8', 0.62); t.material.depthTest = true; t.position.copy(at(m.lat, m.lon, 1.3)); group.add(t) }
  }
  // CO2 fluxes: arrows rising from the sources, sinking into the sinks.
  if (o.fluxes?.length && layers.value.wind) {
    const arrows = o.fluxes.map(f => { const p = at(f.lat, f.lon, f.up ? 0.15 : 1.2), n = p.clone().normalize(); return { dir: f.up ? n : n.clone().negate(), origin: p, length: 0.5 + f.strength * 0.9, color: f.up ? 0xff8a65 : 0x64d2ff, headLength: 0.25, headWidth: 0.1, rate: f.strength } })
    const m = arrowField(arrows, { moving: !reduced }); uniforms.push(m.userData.uniforms); group.add(m)
  }
}

// ---- Hover, pick, highlight ----------------------------------------------------------------------------------------------
const raycaster = new THREE.Raycaster(), ndc = new THREE.Vector2()
let downAt = null, hoverCell = null
function cellAt(e) {
  if (!pick || !info) return null
  const r = renderer.domElement.getBoundingClientRect()
  ndc.set((e.clientX - r.left) / r.width * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1); raycaster.setFromCamera(ndc, camera)
  const hit = raycaster.intersectObject(pick, false)[0]
  return hit?.uv ? cellAtUv(hit.uv.x, hit.uv.y, info.nx, info.ny) : null
}
function onMove(e) { const k = cellAt(e); if (k !== hoverCell) { hoverCell = k; emit('hover', k); syncHighlight() } }
function onUp(e) { if (!downAt || Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]) > 5) { downAt = null; return } downAt = null; emit('pick', cellAt(e)) }
function syncHighlight() {
  if (!surfaceU || !info) return
  const set = (k, pos, on) => { if (k === null || k === undefined) { surfaceU[on].value = 0; return } surfaceU[pos].value.set(k % info.nx, Math.floor(k / info.nx)); surfaceU[on].value = 1 }
  set(hoverCell, 'uHover', 'uHoverOn'); set(props.selected, 'uSel', 'uSelOn')
}

// ---- Fullscreen (F, or the button) --------------------------------------------------------------------------------------
function toggleFullscreen() { if (document.fullscreenElement) document.exitFullscreen?.().catch(() => {}); else root.value?.requestFullscreen?.().catch(() => {}) }
function onFullscreen() { fullscreen.value = document.fullscreenElement === root.value; nextTick(resize) }
function onKey(e) {
  if (!root.value || root.value.offsetParent === null && !fullscreen.value) return
  const t = e.target, tag = String(t?.tagName ?? '').toUpperCase()
  if (e.ctrlKey || e.metaKey || e.altKey || e.repeat || t?.isContentEditable || ['INPUT', 'SELECT', 'TEXTAREA'].includes(tag)) return
  if (e.key === 'f' || e.key === 'F') { e.preventDefault(); toggleFullscreen() }
  else if (e.key === ' ' && props.canPlay && !['BUTTON', 'SUMMARY', 'A'].includes(tag)) { e.preventDefault(); emit('toggle-play') }
}

watch(() => [props.kind, props.grid, props.land, props.elevation, props.heights], () => schedule('base'))
watch(() => props.colors, () => schedule('colours'))
watch(() => props.overlays, () => schedule('overlays'))
watch(() => props.kind, () => { lastHurricane = null; nextTick(resetCamera) })
watch(() => layers.value.relief, () => schedule('base'))
watch(() => [layers.value.clouds, layers.value.wind, layers.value.isobars, layers.value.labels], () => schedule('overlays'))
watch(() => layers.value.turn, on => { if (props.kind === 'climate') controls.autoRotate = on && !reduced })
watch(() => props.selected, syncHighlight)
onMounted(init)
onBeforeUnmount(() => {
  cancelAnimationFrame(frame); observer?.disconnect(); controls?.dispose()
  window.removeEventListener('keydown', onKey); document.removeEventListener('fullscreenchange', onFullscreen)
  if (group) disposeTree(group); if (base) disposeTree(base); renderer?.dispose()
})
defineExpose({ resetCamera })
</script>

<template>
  <div ref="root" class="earth-scene" :class="{ fullscreen }">
    <div ref="host" class="earth-scene-host" aria-label="3D map"></div>
    <!-- Buttons drop the focus after a click: Space then plays instead of pressing the last button again. -->
    <div class="earth-scene-tools" @click="$event.target.closest?.('button')?.blur()">
      <button v-if="canPlay" class="primary" :title="`${playing ? 'Pause' : 'Play'} (Space)`" @click="emit('toggle-play')">{{ playing ? '❚❚' : '▶' }} {{ playLabel }}</button>
      <template v-if="kind === 'forecast'">
        <button :class="{ on: layers.clouds }" title="Clouds and rain from the forecast's cover, rain and thunder chance" @click="layers.clouds = !layers.clouds">☁ Clouds</button>
        <button :class="{ on: layers.wind }" title="The wind as gliding arrows" @click="layers.wind = !layers.wind">➔ Wind</button>
        <button :class="{ on: layers.isobars }" title="Isobars every 4 hPa" @click="layers.isobars = !layers.isobars">≋ Isobars</button>
        <button :class="{ on: layers.relief }" title="Terrain relief" @click="layers.relief = !layers.relief">⛰ Relief</button>
      </template>
      <template v-else>
        <button :class="{ on: layers.turn }" title="Turn the globe" @click="layers.turn = !layers.turn">⟳ Turn</button>
        <button v-if="heights" :class="{ on: layers.relief }" title="Raise the globe by the value" @click="layers.relief = !layers.relief">▲ Height by value</button>
        <button v-if="overlays.fluxes?.length" :class="{ on: layers.wind }" title="CO₂ rising from the sources and sinking into the sinks" @click="layers.wind = !layers.wind">⇅ Fluxes</button>
      </template>
      <button :class="{ on: layers.labels }" title="Labels" @click="layers.labels = !layers.labels">Aa</button>
      <button title="Reset the camera" @click="resetCamera">↺</button>
      <button :title="fullscreen ? 'Leave fullscreen (F)' : 'Fullscreen (F)'" @click="toggleFullscreen"><svg class="icon" viewBox="0 0 16 16" aria-hidden="true"><path :d="fullscreen ? 'M5 1v4H1M15 5h-4V1M11 15v-4h4M1 11h4v4' : 'M1 5V1h4M11 1h4v4M15 11v4h-4M5 15H1v-4'"/></svg></button>
    </div>
    <slot />
  </div>
</template>
