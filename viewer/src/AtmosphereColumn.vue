<script setup>
// The inspector's Atmosphere tab: the air above the clicked point as a layered 3D column with a metre scale, the cloud
// in it (its genus on top, where it starts, how tall it is, the ceiling), the levels that shape it, and how it builds up
// from the ground to its top. The column can be turned and zoomed; on opening, a parcel of air rises from the ground,
// the cloud grows behind it, and the steps below light up as it passes their heights. Data: atmosphereColumn.js.
import { ref, computed, watch, onMounted, onBeforeUnmount } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { cloudPuffs, cloudMesh, precipitationMesh, precipitationIntensity } from './clouds3d.js'
import { metres, temperatureColour, environmentTemperature, CEILING_COVER } from './atmosphereColumn.js'
import { disposeTree } from './sceneKit.js'

const props = defineProps({
  column: { type: Object, required: true }, columnKey: { type: String, required: true },
  paused: { type: Boolean, default: false },   // the window is minimised or rolled up: no rendering
  idle: { type: Boolean, default: false },     // the window is not focused: no turning, a third of the frame rate
})
const host = ref(null), labelLayer = ref(null)
const front = ref(0), growing = ref(false), focus = ref(null)
const reduced = typeof window !== 'undefined' && !!window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
const cloud = computed(() => props.column.cloud)
const steps = computed(() => props.column.steps)
const summit = computed(() => cloud.value?.top ?? props.column.diagnostics.lcl ?? 2000)
const reached = s => !growing.value || (s.key === 'precipitation' ? front.value >= summit.value - 1 : s.height <= front.value + 1)
const ceilingTip = `Aviation ceiling: the base of the lowest cloud layer covering more than half of the sky (${Math.round(CEILING_COVER * 8)}/8 or more). Here the cloud covers ${'{cover}'} of the sky.`
const ceilingTitle = computed(() => ceilingTip.replace('{cover}', cloud.value && Number.isFinite(cloud.value.fraction) ? `${Math.round(cloud.value.fraction * 100)} %` : 'an unknown share'))
const fmtC = v => (Number.isFinite(v) ? `${v.toFixed(Math.abs(v) < 10 ? 1 : 0)} °C` : '—')

// ---- The 3D column ------------------------------------------------------------------------------------------------------
const KM = 1 / 1000, WIDTH = 6
let renderer, scene, camera, controls, group, frame, observer, uniforms = [], lastTime = 0, clock = 0
let skip = 0, parcel = null, trail = null, cloudGroup = null, focusRing = null, growT = 1, emitAt = 0, ruler = null
// Labels are HTML over the canvas, at a fixed readable size: the metre scale on the column's left edge, the levels on
// its right edge (whichever edges face left and right as the column turns).
let labels = []

function init() {
  renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true }); renderer.setPixelRatio(Math.min(devicePixelRatio, 2))
  renderer.setClearColor(0x000000, 0)
  host.value.appendChild(renderer.domElement)
  scene = new THREE.Scene()
  camera = new THREE.PerspectiveCamera(32, 1, 0.1, 400)
  scene.add(new THREE.AmbientLight(0xdfefff, 1.4))
  const sun = new THREE.DirectionalLight(0xffffff, 1.6); sun.position.set(-8, 14, 10); scene.add(sun); scene.userData.sun = sun
  controls = new OrbitControls(camera, renderer.domElement)
  Object.assign(controls, { enableDamping: true, enablePan: false, autoRotate: !reduced, autoRotateSpeed: 0.7, rotateSpeed: 0.8, zoomSpeed: 0.7 })
  controls.addEventListener('start', () => { controls.autoRotate = false })
  observer = new ResizeObserver(resize); observer.observe(host.value)
  build(); resize(); fit(); grow()
  const tick = now => {
    frame = requestAnimationFrame(tick)
    if (!host.value || props.paused || host.value.offsetParent === null) { lastTime = 0; return }
    if (props.idle && growT >= 1 && (skip = (skip + 1) % 3) !== 0) return
    const dt = lastTime ? Math.min(0.1, (now - lastTime) / 1000) : 0; lastTime = now
    if (!reduced) clock += dt
    for (const u of uniforms) u.uTime.value = clock
    advance(dt)
    controls.update(); renderer.render(scene, camera); placeLabels()
  }
  frame = requestAnimationFrame(tick)
}
function resize() {
  const r = host.value.getBoundingClientRect(), w = Math.max(1, r.width), h = Math.max(1, r.height)
  renderer.setSize(w, h); camera.aspect = w / h
  // The column a little left of the middle, so the level labels fit on its right; it still turns about its own axis.
  camera.setViewOffset(w, h, w * 0.08, 0, w, h)
  camera.updateProjectionMatrix()
}
// Frame the whole column, leaving room for the scale on the left and the level labels on the right.
function fit() {
  const H = props.column.top * KM, d = Math.max(17, H * 1.85 + 7)
  controls.target.set(0, H * 0.46, 0)
  camera.position.set(d * 0.5, H * 0.58, d * 0.86)
  controls.minDistance = 5; controls.maxDistance = d * 2
  controls.update()
}

function build() {
  if (group) { scene.remove(group); disposeTree(group) }
  group = new THREE.Group(); scene.add(group); uniforms = []
  const c = props.column, H = c.top * KM, half = WIDTH / 2, cfg = c.config, surfaceK = c.ground.temperatureC + 273.15

  // The ground.
  const groundColour = c.ground.land === 1 || c.ground.land === true ? 0x3b4a36 : c.ground.land === 0 || c.ground.land === false ? 0x173a52 : 0x2c3a41
  const ground = new THREE.Mesh(new THREE.BoxGeometry(WIDTH + 0.8, 0.22, WIDTH + 0.8), new THREE.MeshStandardMaterial({ color: groundColour, roughness: 0.95 }))
  ground.position.y = -0.11; group.add(ground)

  // The layered air: a glass column coloured by the temperature of its layers, a line at every layer boundary.
  const rows = Math.max(2, Math.round(H / 0.1)), geo = new THREE.BoxGeometry(WIDTH, H, WIDTH, 1, rows, 1).translate(0, H / 2, 0)
  const pos = geo.attributes.position, col = new Float32Array(pos.count * 3), tmp = new THREE.Color()
  for (let k = 0; k < pos.count; k++) { const z = pos.getY(k) / KM; tmp.set(temperatureColour(environmentTemperature(cfg, surfaceK, z) - 273.15)); col.set([tmp.r, tmp.g, tmp.b], k * 3) }
  geo.setAttribute('color', new THREE.BufferAttribute(col, 3))
  const back = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.1, side: THREE.BackSide, depthWrite: false })); back.renderOrder = 0; group.add(back)
  const glass = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.07, side: THREE.FrontSide, depthWrite: false })); glass.renderOrder = 3; group.add(glass)
  const square = (y, size, colour, opacity, dashed = false) => {
    const s = size / 2, g = new THREE.BufferGeometry().setFromPoints([[-s, -s], [s, -s], [s, s], [-s, s], [-s, -s]].map(([x, z]) => new THREE.Vector3(x, y, z)))
    const line = new THREE.Line(g, dashed ? new THREE.LineDashedMaterial({ color: colour, transparent: true, opacity, dashSize: 0.18, gapSize: 0.12 }) : new THREE.LineBasicMaterial({ color: colour, transparent: true, opacity }))
    if (dashed) line.computeLineDistances()
    return line
  }
  for (const l of c.layers) if (l.top * KM <= H + 1e-6) group.add(square(l.top * KM, WIDTH, 0xffffff, l.top % 1000 === 0 ? 0.22 : 0.09))
  // Vertical edges of the column.
  const edges = []; for (const [x, z] of [[-half, -half], [half, -half], [half, half], [-half, half]]) edges.push(new THREE.Vector3(x, 0, z), new THREE.Vector3(x, H, z))
  group.add(new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(edges), new THREE.LineBasicMaterial({ color: 0xbfe6ff, transparent: true, opacity: 0.28 })))

  // The temperature profile on the back wall (−70 … +40 °C across it), with the 0 °C line.
  const tx = t => -half + (Math.max(-70, Math.min(40, t)) + 70) / 110 * WIDTH, wall = -half + 0.01
  const profile = []; for (let z = 0; z <= H + 1e-6; z += 0.05) profile.push(new THREE.Vector3(tx(environmentTemperature(cfg, surfaceK, z / KM) - 273.15), z, wall))
  group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(profile), new THREE.LineBasicMaterial({ color: 0xffd166, transparent: true, opacity: 0.95 })))
  const zero = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(tx(0), 0, wall), new THREE.Vector3(tx(0), H, wall)]), new THREE.LineDashedMaterial({ color: 0xb9d8ff, transparent: true, opacity: 0.6, dashSize: 0.15, gapSize: 0.12 }))
  zero.computeLineDistances(); group.add(zero)

  // The metre scale: ticks every 500 m on a vertical edge (moved to the left-most edge every frame), labelled in HTML.
  const ticks = [new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, H, 0)]
  for (let km = 0; km <= H + 1e-6; km += 0.5) { const major = Math.abs(km - Math.round(km)) < 1e-6; ticks.push(new THREE.Vector3(0, km, 0), new THREE.Vector3(major ? -0.4 : -0.2, km, 0)) }
  ruler = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(ticks), new THREE.LineBasicMaterial({ color: 0xdff6ff, transparent: true, opacity: 0.9 })); group.add(ruler)
  clearLabels()
  const step = H > 13 ? 2 : 1
  for (let km = 0; km <= H + 1e-6; km += step) addLabel(metres(km * 1000), 'ruler', km, '#dff6ff')

  // The levels: a ring around the column, labelled at its right-most corner.
  for (const l of c.levels) {
    const y = l.height * KM
    if (y > H + 1e-6) continue
    group.add(square(y, WIDTH + 0.3, new THREE.Color(l.colour), 0.95, l.source === 'derived' || l.key === 'freezing'))
    addLabel(`${l.label} <b>${metres(l.height)}</b>`, 'level', y, l.colour)
  }
  if (c.inversion) {
    const b = c.inversion.bottom * KM, t = c.inversion.top * KM, band = new THREE.Mesh(new THREE.BoxGeometry(WIDTH + 0.02, Math.max(0.02, t - b), WIDTH + 0.02).translate(0, (b + t) / 2, 0), new THREE.MeshBasicMaterial({ color: 0xffb454, transparent: true, opacity: 0.16, depthWrite: false }))
    band.renderOrder = 4; group.add(band)
  }

  // The cloud in its form, grown from its base (scaled from the base up while the parcel rises), and what falls out.
  cloudGroup = null
  if (c.cloud) {
    const b = c.cloud.base * KM, t = c.cloud.top * KM
    const puffs = cloudPuffs({ type: c.cloud.key, base: b, top: t, fraction: c.cloud.fraction ?? 1, w: WIDTH * (c.cloud.key === 'cumulonimbus' ? 0.62 : 0.8), wind: [0.6, 0], seed: 11, bulges: 2 })
      .map(p => ({ ...p, base: b, depth: t - b, storm: c.cloud.thunderstorm, seed: 0.37 }))
    if (puffs.length) {
      const mesh = cloudMesh(puffs, { sun: scene.userData.sun.position, opacity: 1 }); mesh.position.y = -b; uniforms.push(mesh.userData.uniforms)
      cloudGroup = new THREE.Group(); cloudGroup.position.y = b; cloudGroup.add(mesh); group.add(cloudGroup)
    }
    const p = c.precipitation
    if (p && !p.virga && p.rate > 0) { const shaft = precipitationMesh([{ x: 0, z: 0, top: b, bottom: 0, radius: WIDTH * 0.3, intensity: precipitationIntensity(p.rate), snow: p.phase === 'solid' }]); uniforms.push(shaft.userData.uniforms); group.add(shaft) }
  }
  // The rising parcel of air and its trail.
  const glowCanvas = document.createElement('canvas'); glowCanvas.width = glowCanvas.height = 64
  const gctx = glowCanvas.getContext('2d'), grad = gctx.createRadialGradient(32, 32, 0, 32, 32, 32)
  grad.addColorStop(0, 'rgba(255,240,190,1)'); grad.addColorStop(0.3, 'rgba(255,209,102,.8)'); grad.addColorStop(1, 'rgba(255,209,102,0)'); gctx.fillStyle = grad; gctx.fillRect(0, 0, 64, 64)
  parcel = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(glowCanvas), transparent: true, depthWrite: false, blending: THREE.AdditiveBlending })); parcel.scale.set(0.9, 0.9, 1); parcel.renderOrder = 9; group.add(parcel)
  trail = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, 0.001, 0)]), new THREE.LineBasicMaterial({ color: 0xffd166, transparent: true, opacity: 0.75 })); group.add(trail)
  focusRing = square(0, WIDTH + 0.5, 0xffffff, 0); group.add(focusRing)
}

function clearLabels() { for (const l of labels) l.el.remove(); labels = [] }
function addLabel(html, kind, y, colour) {
  const el = document.createElement('div'); el.className = `atmo-label ${kind}`; el.innerHTML = html; el.style.color = colour
  labelLayer.value?.appendChild(el); labels.push({ el, kind, y })
}
// Each frame: the scale on the left-most vertical edge, the level labels at the right-most corner of their ring,
// pushed apart where they would overlap.
const v3 = new THREE.Vector3(), CORNERS = [[-1, -1], [1, -1], [1, 1], [-1, 1]]
function screen(x, y, z) { v3.set(x, y, z).project(camera); const el = host.value; return { x: (v3.x + 1) / 2 * el.clientWidth, y: (1 - v3.y) / 2 * el.clientHeight, z: v3.z } }
function placeLabels() {
  if (!labels.length || !host.value) return
  const half = WIDTH / 2, H = props.column.top * KM
  const mid = CORNERS.map(([a, b]) => screen(a * (half + 0.15), H * 0.5, b * (half + 0.15)))
  const left = CORNERS[mid.reduce((k, p, j) => (p.x < mid[k].x ? j : k), 0)], right = CORNERS[mid.reduce((k, p, j) => (p.x > mid[k].x ? j : k), 0)]
  if (ruler) ruler.position.set(left[0] * (half + 0.45), 0, left[1] * (half + 0.45))
  const placed = []
  for (const l of labels) {
    if (l.kind === 'ruler') { const p = screen(left[0] * (half + 0.45) - 0.45, l.y, left[1] * (half + 0.45)); l.el.style.transform = `translate(${p.x.toFixed(1)}px,${p.y.toFixed(1)}px) translate(-100%,-50%)`; continue }
    const p = screen(right[0] * (half + 0.15), l.y, right[1] * (half + 0.15)); placed.push({ l, x: p.x + 8, y: p.y })
  }
  // Bottom to top, at least 14 px apart.
  placed.sort((a, b) => b.y - a.y)
  for (let k = 1; k < placed.length; k++) if (placed[k].y > placed[k - 1].y - 14) placed[k].y = placed[k - 1].y - 14
  for (const p of placed) p.l.el.style.transform = `translate(${p.x.toFixed(1)}px,${p.y.toFixed(1)}px) translate(0,-50%)`
}

// The build-up: the parcel rises from the ground to the top of the cloud (or to its condensation level under a clear
// sky) in about five seconds; the cloud grows from its base behind it.
function grow() { growT = reduced ? 1 : 0; growing.value = !reduced; front.value = reduced ? summit.value : 0; emitAt = 0; place(reduced ? 1 : 0) }
const ease = t => (t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2)
function place(t) {
  const c = props.column, h = ease(t) * summit.value, y = h * KM
  if (parcel) { parcel.position.set(0, y, 0); parcel.material.opacity = t >= 1 ? 0 : 1 }
  if (trail) { trail.geometry.setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, Math.max(0.001, y), 0)]); trail.material.opacity = t >= 1 ? 0.25 : 0.75 }
  if (cloudGroup && c.cloud) {
    const g = Math.max(0, Math.min(1, (h - c.cloud.base) / Math.max(1, c.cloud.thickness)))
    cloudGroup.visible = g > 0.001
    cloudGroup.scale.set(0.55 + 0.45 * g, Math.max(0.02, g), 0.55 + 0.45 * g)
  }
  return h
}
function advance(dt) {
  if (growT < 1) {
    growT = Math.min(1, growT + dt / 5)
    const h = place(growT)
    emitAt -= dt
    if (emitAt <= 0 || growT >= 1) { front.value = h; emitAt = 0.08 }
    if (growT >= 1) growing.value = false
  }
  if (focusRing) {
    const target = focus.value === null ? 0 : 0.5 + 0.4 * Math.sin(clock * 4)
    focusRing.material.opacity += (target - focusRing.material.opacity) * Math.min(1, dt * 8)
  }
}
function replay() { focus.value = null; grow() }
function focusStep(s) {
  if (focus.value === s.key) { focus.value = null; return }
  focus.value = s.key
  if (focusRing) focusRing.position.y = (s.key === 'precipitation' ? Math.max(0.05, (cloud.value?.base ?? 0) * KM / 2) : s.height * KM)
  if (growT < 1) { growT = 1; place(1); front.value = summit.value; growing.value = false }
}

// A new point: frame it and let the air rise again. The same point in another frame: redraw, keep the view.
watch(() => props.columnKey, () => { if (!renderer) return; focus.value = null; build(); fit(); grow() })
const signature = computed(() => { const c = props.column; return [c.cloud?.key, c.cloud?.base, c.cloud?.top, c.ground.temperatureC, c.top, ...c.levels.map(l => `${l.key}${l.height}`), c.precipitation?.rate].join('|') })
watch(signature, () => { if (!renderer) return; build(); place(growT) })
watch(() => props.idle, idle => { if (controls && idle) controls.autoRotate = false })
onMounted(init)
onBeforeUnmount(() => {
  cancelAnimationFrame(frame); observer?.disconnect(); controls?.dispose()
  clearLabels(); if (group) disposeTree(group)
  renderer?.dispose(); renderer?.forceContextLoss?.()
})
</script>

<template>
  <div class="atmo">
    <div class="atmo-head">
      <span class="atmo-badge" :style="{ background: cloud ? cloud.color : 'rgba(255,209,102,.18)', color: cloud && ['#d9e8f2', '#eef5fa', '#d7e6ef', '#c5d5e0', '#b7c9d6'].includes(cloud.color) ? '#14202a' : '#fff' }">{{ cloud ? cloud.abbreviation : '☀' }}</span>
      <div><h3>{{ column.title }}</h3><p>{{ column.subtitle }}</p></div>
    </div>
    <div v-if="cloud" class="atmo-stats">
      <div><small>STARTS AT</small><b>{{ metres(cloud.base) }}</b></div>
      <div><small>TOP</small><b>{{ metres(cloud.top) }}</b></div>
      <div><small>TALL</small><b>{{ metres(cloud.thickness) }}</b></div>
      <div :title="ceilingTitle"><small>CEILING</small><b>{{ cloud.ceiling !== null ? metres(cloud.ceiling) : 'none' }}</b></div>
    </div>
    <div v-else class="atmo-stats">
      <div><small>GROUND</small><b>{{ fmtC(column.ground.temperatureC) }}</b></div>
      <div><small>HUMIDITY</small><b>{{ column.ground.humidityPct !== null ? `${Math.round(column.ground.humidityPct)} %` : '—' }}</b></div>
      <div><small>WOULD CONDENSE</small><b>{{ column.diagnostics.lcl !== null ? metres(column.diagnostics.lcl) : '—' }}</b></div>
      <div><small>CAPE</small><b>{{ Number.isFinite(column.diagnostics.cape) ? `${Math.round(column.diagnostics.cape)} J/kg` : '—' }}</b></div>
    </div>
    <div class="atmo-stage">
      <div ref="host" class="atmo-canvas" aria-label="The atmosphere above this point as a 3D column; drag to turn"></div>
      <div ref="labelLayer" class="atmo-labels" aria-hidden="true"></div>
      <div class="atmo-altimeter" :class="{ on: growing }" aria-hidden="true"><small>PARCEL</small>{{ metres(front) }}</div>
      <button class="atmo-replay" title="Let the air rise again" @click="replay">↻ Build up</button>
      <small class="atmo-hint">drag to turn · scroll to zoom · heights to scale, width not · <i class="atmo-key"></i> temperature on the back wall (−70 … +40 °C)</small>
    </div>
    <ol class="atmo-steps" aria-label="How it builds up, from the ground to the top">
      <li v-for="s in steps" :key="s.key" :class="{ on: reached(s), focus: focus === s.key }" tabindex="0" @click="focusStep(s)" @keydown.enter="focusStep(s)">
        <span class="atmo-h">{{ s.key === 'precipitation' ? '↓ ground' : metres(s.height) }}</span>
        <div><b>{{ s.title }}</b><p>{{ s.text }}</p></div>
      </li>
    </ol>
    <p class="atmo-source">Heights above the ground. Cloud genus, base and top{{ column.precipitation ? ', freezing level, precipitation' : '' }} and the condensation levels are {{ column.synthetic ? 'from a synthetic mock' : 'engine output' }}; the layer temperatures are the column model's starting profile ({{ (column.config.environment_lapse_rate_k_m * 1000).toFixed(1) }} °C/km{{ column.inversion ? ', capping inversion' : '' }}, tropopause {{ metres(column.tropopause) }}){{ column.freezing?.derived ? ', from which the freezing level is read' : '' }}. LCL: lifting condensation level · LFC: level of free convection · EL: equilibrium level. The build-up is textbook meteorology applied to these values; the cloud's shape is drawn from its genus.</p>
  </div>
</template>
