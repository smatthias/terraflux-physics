<script setup>
// The inspector: a translucent window in the style of a macOS window that opens at a clicked data point and explains
// it in sentences. It springs open from its point and wobbles like jelly while it is dragged (wobbly.js). The sentences type themselves out; values are gaps that fill in as the typing reaches them, and the
// graphics of a section draw themselves once its text is typed. A tether line keeps pointing at the data point while the
// camera moves. Drag the title bar to move the window; the traffic lights close, minimise and enlarge it.
// Where the data has a vertical column (cloud formation or column diagnostics), the window opens on an Atmosphere tab
// (AtmosphereColumn.vue): the air above the point in 3D with the cloud and how it builds up; the second tab explains
// the value as before.
import { ref, computed, watch, onMounted, onBeforeUnmount, nextTick } from 'vue'
import { typePlan, typedParts, lineStart, lineEnd, typingRate } from './typewriter.js'
import { compass8, formatNumber } from './explain.js'
import { createWobble, popFrom, matrixCss } from './wobbly.js'
import AtmosphereColumn from './AtmosphereColumn.vue'
import { linkArrow, edgePoint, placeWindow } from './workspace.js'

const props = defineProps({
  content: { type: Object, required: true },
  typeKey: { type: String, required: true },
  anchor: { type: Object, default: null },            // { x, y, visible } in viewport pixels
  bounds: { type: Object, required: true },           // { left, top, right, bottom } free area in viewport pixels
  series: { type: Array, default: () => [] },         // [{ value, label }] over the frames
  seriesIndex: { type: Number, default: 0 },
  wind: { type: Object, default: null },              // { speed, fromDeg, toDeg, from }
  neighbourhood: { type: Array, default: () => [] },  // 5 x 5 CSS colours (null: outside / no data), row 0 = north
  scaleStops: { type: Array, default: null },         // CSS colours of the colour scale, low to high
  unit: { type: String, default: '' },
  column: { type: Object, default: null },            // the atmosphere above the point (atmosphereColumn.js), or null
  // The workspace: the window's colour (its arrow, its marker, its taskbar button), stacking, focus, and whether it is
  // minimised to the taskbar (dock: where its button is, in the same pixels as the anchor).
  colour: { type: String, default: '#ffd166' },
  z: { type: Number, default: 9 },
  focused: { type: Boolean, default: true },
  minimised: { type: Boolean, default: false },
  dock: { type: Object, default: null },
  avoid: { type: Function, default: () => [] },       // where the other open windows are ([{ x, y, w, h }]): a window goes beside them
})
const emit = defineEmits(['close', 'minimise', 'focus', 'rect'])
const uid = `io${Math.random().toString(36).slice(2, 8)}`

// ---- Typing ----------------------------------------------------------------------------------------------------------
const plan = computed(() => typePlan([[{ v: props.content.value }], props.content.verdict, ...props.content.sections.flatMap(s => s.lines)]))
const hero = computed(() => plan.value.lines[0]), verdict = computed(() => plan.value.lines[1])
const sectionLines = computed(() => { let k = 2; return props.content.sections.map(s => s.lines.map(() => plan.value.lines[k++])) })
const revealed = ref(0)
const reduced = typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
let raf = 0, last = 0
function run(now) {
  const dt = last ? (now - last) / 1000 : 0; last = now
  revealed.value = Math.min(plan.value.total, revealed.value + typingRate(plan.value.total) * dt)
  if (revealed.value < plan.value.total) raf = requestAnimationFrame(run)
}
function retype() { cancelAnimationFrame(raf); last = 0; revealed.value = reduced ? Infinity : 0; if (!reduced) raf = requestAnimationFrame(run) }
function finish() { cancelAnimationFrame(raf); revealed.value = Infinity }
const typing = computed(() => revealed.value < plan.value.total)
const parts = t => typedParts(t, revealed.value)
const caretIn = t => { const r = Math.floor(revealed.value); return typing.value && r >= t.start && r < t.end }
const reached = lines => lines.length && revealed.value >= Math.min(...lines.map(lineStart))
const drawn = lines => revealed.value >= Math.max(...lines.map(lineEnd))

// ---- Placement, dragging, traffic lights -------------------------------------------------------------------------------
const win = ref(null), pos = ref({ x: 0, y: 0 }), size = ref({ w: 360, h: 320 })
const dragged = ref(false), rolled = ref(false), expanded = ref(false)   // rolled: folded up to its title bar (double-click)
// Tabs: the atmosphere above the point (the default where the data has one) and the explanation of the value.
const tab = ref(props.column ? 'atmosphere' : 'explain')
const width = computed(() => Math.max(280, Math.min(expanded.value ? 480 : tab.value === 'atmosphere' && props.column ? 440 : 360, props.bounds.right - props.bounds.left - 16)))
function showTab(t) { if (tab.value === t) return; tab.value = t; if (t === 'explain') retype(); nextTick(place) }
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v))
function place() {
  if (dragged.value || !props.anchor) return
  const b = props.bounds, h = Math.min(size.value.h, b.bottom - b.top)
  pos.value = placeWindow({ anchor: props.anchor, bounds: b, w: width.value, h, avoid: props.avoid() })
}
// Dragging: anywhere on the window except its controls and scrollbar; it becomes a drag after 4 px, so a click still
// clicks. The wobble is applied straight to the element's transform every frame, without re-rendering the window.
const wobble = createWobble(), wobbly = !reduced
let drag = null, wobbleFrame = 0, wobbleLast = 0
function wobbleTick(now) {
  wobble.step(wobbleLast ? (now - wobbleLast) / 1000 : 0); wobbleLast = now
  if (!drag?.moving && wobble.resting()) { wobble.settle(); if (win.value) win.value.style.transform = ''; wobbleFrame = 0; wobbleLast = 0; return }
  if (win.value) win.value.style.transform = matrixCss(wobble.matrix())
  wobbleFrame = requestAnimationFrame(wobbleTick)
}
const wobbleRun = () => { if (wobbly && !wobbleFrame) wobbleFrame = requestAnimationFrame(wobbleTick) }
function startDrag(e) {
  if (!props.focused) emit('focus')
  if (e.button !== 0 || e.target.closest('button, a, input, select, .mac-lights, .atmo-canvas, .atmo-steps li')) return
  const body = e.target.closest('.mac-body')
  if (body && e.clientX > body.getBoundingClientRect().left + body.clientWidth) return   // the scrollbar
  drag = { x: e.clientX, y: e.clientY, offset: [e.clientX - pos.value.x, e.clientY - pos.value.y], moving: false }
  // Follow the gesture on the document: a quick first move may leave the window before the drag has begun, and a
  // pointer capture taken up front would steal the click that shows all the text at once.
  window.addEventListener('pointermove', moveDrag); window.addEventListener('pointerup', endDrag); window.addEventListener('pointercancel', endDrag)
}
function moveDrag(e) {
  if (!drag) return
  if (!drag.moving) {
    if (Math.hypot(e.clientX - drag.x, e.clientY - drag.y) < 4) return
    drag.moving = true; win.value?.classList.add('dragging')
    wobble.resize(win.value.offsetWidth, win.value.offsetHeight); wobble.grab(drag.x - win.value.getBoundingClientRect().left, drag.y - win.value.getBoundingClientRect().top)
  }
  const b = props.bounds, before = pos.value
  pos.value = { x: clamp(e.clientX - drag.offset[0], b.left - width.value + 80, b.right - 80), y: clamp(e.clientY - drag.offset[1], b.top, b.bottom - 40) }
  wobble.move(pos.value.x - before.x, pos.value.y - before.y)
  dragged.value = true; wobbleRun()
}
function endDrag() {
  window.removeEventListener('pointermove', moveDrag); window.removeEventListener('pointerup', endDrag); window.removeEventListener('pointercancel', endDrag)
  if (drag?.moving) { wobble.release(); win.value?.classList.remove('dragging'); wobbleRun() }
  drag = null
}
// Spring open from the data point.
function popOpen() {
  if (!wobbly || !win.value || !props.anchor) return
  const w = win.value.offsetWidth, h = win.value.offsetHeight
  wobble.resize(w, h); wobble.kick(popFrom(w, h, props.anchor.x - pos.value.x, props.anchor.y - pos.value.y, 0.12)); wobbleRun()
}
const maxHeight = computed(() => Math.max(160, props.bounds.bottom - props.bounds.top - 16))

// The arrow from the window's nearest edge to its data point, in the window's colour.
const tether = computed(() => {
  const a = props.anchor
  if (!a?.visible || props.minimised) return null
  const from = edgePoint({ x: pos.value.x, y: pos.value.y, w: width.value, h: rolled.value ? 34 : size.value.h }, a)
  if (from.inside) return null
  return linkArrow(from, a)
})
// Minimising flies the window to its taskbar button: it shrinks about its top-left corner (the wobble's origin), so that
// corner moves onto the button.
const dockShift = computed(() => {
  const d = props.dock
  if (!props.minimised || !d) return {}
  return { '--dock-dx': `${(d.x - pos.value.x).toFixed(0)}px`, '--dock-dy': `${(d.y - pos.value.y).toFixed(0)}px` }
})

let observer = null
onMounted(() => {
  // Only the height matters (placement, tether); reacting to the width would re-render the window on every frame of its
  // width transition, and re-applying the inline width restarts that transition so it would never get anywhere.
  observer = new ResizeObserver(() => { if (win.value && Math.abs(win.value.offsetHeight - size.value.h) > 0.5) size.value = { w: size.value.w, h: win.value.offsetHeight } })
  if (win.value) { observer.observe(win.value); size.value = { w: win.value.offsetWidth, h: win.value.offsetHeight } }
  place(); retype(); nextTick(popOpen)
})
onBeforeUnmount(() => { clearTimeout(replace); cancelAnimationFrame(raf); cancelAnimationFrame(wobbleFrame); observer?.disconnect(); endDrag() })
watch(() => props.typeKey, () => { dragged.value = dragged.value && !!drag; rolled.value = false; tab.value = props.column ? 'atmosphere' : 'explain'; nextTick(() => { place(); popOpen() }); retype() })
watch(() => !!props.column, has => { if (!has) tab.value = 'explain' })
watch(expanded, () => nextTick(place))
// Report where the window is, so that the next one can go beside it.
watch(() => [pos.value.x, pos.value.y, width.value, size.value.h, rolled.value], () => emit('rect', { x: pos.value.x, y: pos.value.y, w: width.value, h: rolled.value ? 34 : size.value.h }), { immediate: true })
// The free area changed (fullscreen on or off, a panel opened): place the window again, or keep a moved one inside.
// Several windows: the focused one first, the others after it in stacking order, each beside those already placed.
let replace = 0
watch(() => [props.bounds.left, props.bounds.top, props.bounds.right, props.bounds.bottom].join(), () => {
  if (!dragged.value) { clearTimeout(replace); replace = setTimeout(place, props.focused ? 0 : 60 + Math.max(0, 20 - props.z) * 50); return }
  const b = props.bounds
  pos.value = { x: clamp(pos.value.x, b.left - width.value + 80, b.right - 80), y: clamp(pos.value.y, b.top, b.bottom - 40) }
})

// ---- Graphics ----------------------------------------------------------------------------------------------------------
const dist = computed(() => {
  const d = props.content.distribution
  if (!d) return null
  const X = v => 14 + (v - d.lo) / (d.hi - d.lo) * 272
  return { ...d, x: X(d.value), mean: X(d.mean), lo: formatNumber(d.lo), hi: formatNumber(d.hi) }
})
const stops = computed(() => (props.scaleStops?.length ? props.scaleStops : ['#35505e', '#7fa9b8', '#dff6ff']))
const spark = computed(() => {
  const pts = props.series.map((p, k) => ({ ...p, k })).filter(p => p.value !== null && p.value !== undefined && Number.isFinite(p.value))
  if (pts.length < 2) return null
  const lo = Math.min(...pts.map(p => p.value)), hi = Math.max(...pts.map(p => p.value)), n = props.series.length - 1 || 1
  const X = k => 6 + k / n * 288, Y = v => 56 - (hi > lo ? (v - lo) / (hi - lo) : 0.5) * 46
  const line = pts.map(p => `${X(p.k).toFixed(1)},${Y(p.value).toFixed(1)}`).join(' ')
  const cur = pts.find(p => p.k === props.seriesIndex)
  return { line, area: `6,60 ${line} ${X(pts.at(-1).k).toFixed(1)},60`, cur: cur ? { x: X(cur.k), y: Y(cur.value), label: cur.label } : null,
    first: props.series[0]?.label ?? '', last: props.series.at(-1)?.label ?? '', lo: formatNumber(lo), hi: formatNumber(hi) }
})
const cats = computed(() => {
  const list = props.content.categories
  if (!list?.length) return null
  let x = 4
  return list.map(c => { const w = c.share / 100 * 292, seg = { ...c, x, w }; x += w; return seg })
})
const here = computed(() => cats.value?.find(c => c.current) ?? null)
const around = computed(() => props.content.around)
const arrow = computed(() => (around.value && !around.value.flat && !around.value.peak && !around.value.trough ? around.value.towardDeg : null))
const delay = k => `${(Math.abs((k % 5) - 2) + Math.abs(Math.floor(k / 5) - 2)) * 45}ms`
</script>

<template>
  <svg class="mac-tether" :class="{ unfocused: !focused }" :style="{ zIndex: z - 1, '--accent': colour }" aria-hidden="true">
    <template v-if="tether"><path class="mac-arrow" :d="tether.path"/><polygon class="mac-arrow-head" :points="tether.head"/></template>
    <template v-if="anchor?.visible && !minimised">
      <circle class="mac-tether-dot" :cx="anchor.x" :cy="anchor.y" r="5"/>
      <circle v-if="focused" class="mac-tether-pulse" :cx="anchor.x" :cy="anchor.y" r="5"/>
    </template>
  </svg>
  <div ref="win" class="mac-window" :class="{ rolled, expanded, typing, unfocused: !focused, 'to-dock': minimised }" @pointerdown="startDrag" role="dialog" :aria-label="`${content.title}: ${content.value} ${content.unit}`"
       :style="{ left: `${pos.x}px`, top: `${pos.y}px`, width: `${width}px`, maxHeight: `${maxHeight}px`, zIndex: z, '--accent': colour, ...dockShift }" :aria-hidden="minimised ? 'true' : null" :inert="minimised ? true : null">
    <div class="mac-titlebar" @dblclick="e => { if (!e.target.closest('.mac-lights')) rolled = !rolled }">
      <div class="mac-lights">
        <button class="mac-light close" aria-label="Close" title="Close (Esc)" @click="emit('close')"><svg viewBox="0 0 10 10"><path d="M3 3l4 4M7 3l-4 4"/></svg></button>
        <button class="mac-light min" aria-label="Minimise to the taskbar" title="Minimise to the taskbar" @click="emit('minimise')"><svg viewBox="0 0 10 10"><path d="M2.5 5h5"/></svg></button>
        <button class="mac-light zoom" :aria-label="expanded ? 'Smaller' : 'Larger'" :title="expanded ? 'Smaller' : 'Larger'" @click="expanded = !expanded"><svg viewBox="0 0 10 10"><path d="M5 2.5v5M2.5 5h5"/></svg></button>
      </div>
      <div class="mac-title"><i class="mac-accent" :style="{ background: colour }"></i>{{ content.title }}</div>
    </div>
    <div v-if="column && !rolled" class="mac-tabs" role="tablist">
      <button role="tab" :aria-selected="tab === 'atmosphere'" :class="{ on: tab === 'atmosphere' }" @click="showTab('atmosphere')">Atmosphere</button>
      <button role="tab" :aria-selected="tab === 'explain'" :class="{ on: tab === 'explain' }" @click="showTab('explain')">{{ content.title.length > 26 ? 'This value' : content.title }}</button>
    </div>
    <div v-if="column && tab === 'atmosphere'" v-show="!rolled" class="mac-body atmo-body" :style="{ maxHeight: `${maxHeight - 68}px` }">
      <AtmosphereColumn :column="column" :column-key="typeKey" :paused="minimised || rolled" :idle="!focused" />
    </div>
    <div v-else v-show="!rolled" class="mac-body" :style="{ maxHeight: `${maxHeight - (column ? 68 : 34)}px` }" @click="finish">
      <div class="mac-hero">
        <span class="mac-value"><template v-for="(t, j) in hero" :key="j"><span>{{ parts(t).shown }}</span><span class="ghost" :class="{ gap: !parts(t).done }">{{ parts(t).ghost }}</span></template></span>
        <span class="mac-unit" :class="{ on: revealed >= hero[0]?.end }">{{ content.unit }}</span>
      </div>
      <p class="mac-verdict">
        <template v-for="(t, j) in verdict" :key="j">
          <span :class="{ tv: t.value, [`tone-${t.tone}`]: t.tone }"><span>{{ parts(t).shown }}</span><i v-if="caretIn(t)" class="caret"></i><span class="ghost" :class="{ gap: t.value && !parts(t).done }">{{ parts(t).ghost }}</span></span>
        </template>
      </p>
      <section v-for="(sec, k) in content.sections" :key="sec.id" class="mac-section" :class="`sec-${sec.id}`">
        <h4 :class="{ on: reached(sectionLines[k]) }">{{ sec.heading }}</h4>
        <div class="mac-section-body">
          <div class="mac-text">
            <p v-for="(line, n) in sectionLines[k]" :key="n">
              <template v-for="(t, j) in line" :key="j">
                <span :class="{ tv: t.value }"><span>{{ parts(t).shown }}</span><i v-if="caretIn(t)" class="caret"></i><span class="ghost" :class="{ gap: t.value && !parts(t).done }">{{ parts(t).ghost }}</span></span>
              </template>
            </p>
          </div>

          <!-- Where the value sits on the map's colour scale, with the average -->
          <svg v-if="sec.graphic === 'distribution' && dist" class="mac-graphic g-dist" :class="{ on: drawn(sectionLines[k]) }" viewBox="0 0 300 58" role="img" :aria-label="`Value ${content.value} ${unit} between ${dist.lo} and ${dist.hi}`">
            <defs><linearGradient :id="`${uid}-scale`" x1="0" x2="1"><stop v-for="(c, j) in stops" :key="j" :offset="j / (stops.length - 1)" :stop-color="c"/></linearGradient></defs>
            <rect x="14" y="18" width="272" height="8" rx="4" :fill="`url(#${uid}-scale)`"/>
            <line :x1="dist.mean" :x2="dist.mean" y1="13" y2="31" class="g-mean"/><text :x="dist.mean" y="10" text-anchor="middle" class="g-small">average</text>
            <g class="g-marker" :style="{ transform: `translateX(${drawn(sectionLines[k]) ? dist.x - 14 : 0}px)` }"><circle cx="14" cy="22" r="7"/><circle cx="14" cy="22" r="2.5" class="g-core"/></g>
            <text x="14" y="44" class="g-label">{{ dist.lo }}</text><text x="286" y="44" text-anchor="end" class="g-label">{{ dist.hi }}</text>
            <text x="14" y="55" class="g-small">← {{ dist.less }}</text><text x="286" y="55" text-anchor="end" class="g-small">{{ dist.more }} →</text>
          </svg>

          <!-- For classes: how much of the map each class covers, this point's class outlined -->
          <svg v-if="sec.graphic === 'categories' && cats" class="mac-graphic g-cats" :class="{ on: drawn(sectionLines[k]) }" viewBox="0 0 300 34" role="img" aria-label="Share of each class">
            <rect v-for="(c, j) in cats" :key="j" :x="c.x" y="6" :width="Math.max(0.5, c.w - 1.5)" height="12" rx="3" :fill="c.color ?? '#7fa9b8'" :class="{ current: c.current }" :style="{ animationDelay: `${j * 60}ms` }"><title>{{ c.label }} · {{ Math.round(c.share) }} %</title></rect>
            <text v-if="here" :x="Math.min(286, Math.max(14, here.x + here.w / 2))" y="31" text-anchor="middle" class="g-small">▲ this point</text>
          </svg>

          <!-- The 5 x 5 neighbourhood, north up, with the direction of increase -->
          <svg v-if="sec.graphic === 'neighbourhood' && neighbourhood.length === 25" class="mac-graphic g-near" :class="{ on: drawn(sectionLines[k]) }" viewBox="0 0 112 112" role="img" aria-label="The neighbourhood of the point, north up">
            <text x="56" y="8" text-anchor="middle" class="g-small">N</text>
            <rect v-for="(c, j) in neighbourhood" :key="j" :x="8 + (j % 5) * 19.5" :y="12 + Math.floor(j / 5) * 19.5" width="17" height="17" rx="4.5" :fill="c ?? '#1b2a33'" :class="{ centre: j === 12 }" :style="{ animationDelay: delay(j) }"/>
            <g v-if="arrow !== null" class="g-arrow" :transform="`rotate(${arrow} 56 60)`"><line x1="56" y1="60" x2="56" y2="28"/><path d="M50 34 L56 24 L62 34"/></g>
          </svg>

          <!-- The value here over the whole run, the current frame marked -->
          <svg v-if="sec.graphic === 'series' && spark" class="mac-graphic g-spark" :class="{ on: drawn(sectionLines[k]) }" viewBox="0 0 300 78" role="img" aria-label="This point over the run">
            <defs><linearGradient :id="`${uid}-area`" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="#7fe0ff" stop-opacity=".35"/><stop offset="1" stop-color="#7fe0ff" stop-opacity="0"/></linearGradient></defs>
            <polygon :points="spark.area" :fill="`url(#${uid}-area)`" class="g-area"/>
            <polyline :points="spark.line" pathLength="1" class="g-line"/>
            <template v-if="spark.cur"><line :x1="spark.cur.x" :x2="spark.cur.x" y1="6" y2="60" class="g-now"/><circle :cx="spark.cur.x" :cy="spark.cur.y" r="4" class="g-dot"/></template>
            <text x="6" y="73" class="g-small">{{ spark.first }}</text><text x="294" y="73" text-anchor="end" class="g-small">{{ spark.last }}</text>
            <text x="150" y="73" text-anchor="middle" class="g-small">range here {{ spark.lo }} – {{ spark.hi }} {{ unit }}</text>
          </svg>

          <!-- Where the wind comes from and where it blows to -->
          <svg v-if="sec.graphic === 'compass' && wind" class="mac-graphic g-compass" :class="{ on: drawn(sectionLines[k]) }" viewBox="0 0 90 90" role="img" :aria-label="`Wind from the ${wind.from}`">
            <circle cx="45" cy="45" r="34" class="g-ring"/>
            <text x="45" y="8" text-anchor="middle" class="g-small">N</text><text x="86" y="48" text-anchor="middle" class="g-small">E</text><text x="45" y="89" text-anchor="middle" class="g-small">S</text><text x="4" y="48" text-anchor="middle" class="g-small">W</text>
            <g class="g-wind" :transform="`rotate(${wind.toDeg} 45 45)`"><line x1="45" y1="74" x2="45" y2="22"/><path d="M38 30 L45 17 L52 30 Z"/><circle cx="45" cy="74" r="3"/></g>
            <title>Blowing from the {{ wind.from }} towards the {{ compass8(wind.toDeg) }}</title>
          </svg>
        </div>
      </section>
      <p v-if="typing" class="mac-skip">Click to show everything at once</p>
    </div>
  </div>
</template>
