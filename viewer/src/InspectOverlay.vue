<script setup>
// The inspector: a translucent window in the style of a macOS window that opens at a clicked data point and explains
// it in sentences. The sentences type themselves out; values are gaps that fill in as the typing reaches them, and the
// graphics of a section draw themselves once its text is typed. A tether line keeps pointing at the data point while the
// camera moves. Drag the title bar to move the window; the traffic lights close, minimise and enlarge it.
import { ref, computed, watch, onMounted, onBeforeUnmount, nextTick } from 'vue'
import { typePlan, typedParts, lineStart, lineEnd, typingRate } from './typewriter.js'
import { compass8, formatNumber } from './explain.js'

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
})
const emit = defineEmits(['close'])
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
const dragged = ref(false), minimised = ref(false), expanded = ref(false)
const width = computed(() => (expanded.value ? 460 : 360))
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v))
function place() {
  if (dragged.value || !props.anchor) return
  const b = props.bounds, a = props.anchor, w = width.value, h = Math.min(size.value.h, b.bottom - b.top)
  let x = a.x + 40
  if (x + w > b.right - 8) x = a.x - 40 - w
  pos.value = { x: clamp(x, b.left + 8, Math.max(b.left + 8, b.right - w - 8)), y: clamp(a.y - Math.min(140, h / 2), b.top + 8, Math.max(b.top + 8, b.bottom - h - 8)) }
}
let drag = null
function startDrag(e) {
  if (e.button !== 0 || e.target.closest('.mac-lights')) return
  drag = { x: e.clientX - pos.value.x, y: e.clientY - pos.value.y }
  e.currentTarget.setPointerCapture(e.pointerId)
}
function moveDrag(e) {
  if (!drag) return
  const b = props.bounds
  pos.value = { x: clamp(e.clientX - drag.x, b.left - width.value + 80, b.right - 80), y: clamp(e.clientY - drag.y, b.top, b.bottom - 40) }
  dragged.value = true
}
function endDrag() { drag = null }
const maxHeight = computed(() => Math.max(160, props.bounds.bottom - props.bounds.top - 16))

// The tether from the data point to the nearest point of the window's edge.
const tether = computed(() => {
  const a = props.anchor
  if (!a?.visible) return null
  const x0 = pos.value.x, y0 = pos.value.y, x1 = x0 + width.value, y1 = y0 + (minimised.value ? 34 : size.value.h)
  const ex = clamp(a.x, x0, x1), ey = clamp(a.y, y0, y1)
  if (Math.hypot(ex - a.x, ey - a.y) < 6) return null
  return { ax: a.x, ay: a.y, ex, ey, mx: (a.x + ex) / 2, my: Math.min(a.y, ey) - 18 }
})

let observer = null
onMounted(() => {
  // Only the height matters (placement, tether); reacting to the width would re-render the window on every frame of its
  // width transition, and re-applying the inline width restarts that transition so it would never get anywhere.
  observer = new ResizeObserver(() => { if (win.value && Math.abs(win.value.offsetHeight - size.value.h) > 0.5) size.value = { w: size.value.w, h: win.value.offsetHeight } })
  if (win.value) { observer.observe(win.value); size.value = { w: win.value.offsetWidth, h: win.value.offsetHeight } }
  place(); retype()
})
onBeforeUnmount(() => { cancelAnimationFrame(raf); observer?.disconnect() })
watch(() => props.typeKey, () => { dragged.value = dragged.value && !!drag; minimised.value = false; nextTick(place); retype() })
watch(expanded, () => nextTick(place))

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
  <svg class="mac-tether" aria-hidden="true">
    <path v-if="tether" :d="`M${tether.ax},${tether.ay} Q${tether.mx},${tether.my} ${tether.ex},${tether.ey}`"/>
    <circle v-if="anchor?.visible" class="mac-tether-dot" :cx="anchor.x" :cy="anchor.y" r="5"/>
    <circle v-if="anchor?.visible" class="mac-tether-pulse" :cx="anchor.x" :cy="anchor.y" r="5"/>
  </svg>
  <div ref="win" class="mac-window" :class="{ minimised, expanded, typing }" role="dialog" :aria-label="`${content.title}: ${content.value} ${content.unit}`"
       :style="{ left: `${pos.x}px`, top: `${pos.y}px`, width: `${width}px`, maxHeight: `${maxHeight}px`, transformOrigin: anchor && anchor.x < pos.x ? '0 40%' : '100% 40%' }">
    <div class="mac-titlebar" @pointerdown="startDrag" @pointermove="moveDrag" @pointerup="endDrag" @pointercancel="endDrag" @dblclick="e => { if (!e.target.closest('.mac-lights')) minimised = !minimised }">
      <div class="mac-lights">
        <button class="mac-light close" aria-label="Close" title="Close (Esc)" @click="emit('close')"><svg viewBox="0 0 10 10"><path d="M3 3l4 4M7 3l-4 4"/></svg></button>
        <button class="mac-light min" :aria-label="minimised ? 'Expand' : 'Minimise'" :title="minimised ? 'Expand' : 'Minimise'" @click="minimised = !minimised"><svg viewBox="0 0 10 10"><path d="M2.5 5h5"/></svg></button>
        <button class="mac-light zoom" :aria-label="expanded ? 'Smaller' : 'Larger'" :title="expanded ? 'Smaller' : 'Larger'" @click="expanded = !expanded"><svg viewBox="0 0 10 10"><path d="M5 2.5v5M2.5 5h5"/></svg></button>
      </div>
      <div class="mac-title">{{ content.title }}</div>
    </div>
    <div v-show="!minimised" class="mac-body" :style="{ maxHeight: `${maxHeight - 34}px` }" @click="finish">
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
