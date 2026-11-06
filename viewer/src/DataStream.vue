<script setup>
// The data stream of the fullscreen scene: what the WebGL scene renders right now, the statistics and a scan of the
// rendered field, the data blocks that feed it, the render pipeline's numbers and a log line per scene rebuild.
import { ref, computed, watch, onMounted, onBeforeUnmount, nextTick } from 'vue'
import { fieldStats, histogram, rowSignal, signalPaths, rampRgb, formatSignal } from './dataStream.js'

const props = defineProps({
  state: { type: Object, required: true },
  values: { type: Array, default: () => [] },
  range: { type: Array, default: () => [0, 1] },
  // The scene's own cell colours as [r, g, b] (sRGB), and the value -> CSS colour mapping when the view has one.
  colors: { type: Array, default: null },
  colorFor: { type: Function, default: null },
  channels: { type: Array, default: () => [] },
  overlays: { type: Array, default: () => [] },
  telemetry: { type: Object, default: () => ({}) },
  log: { type: Array, default: () => [] },
  playing: { type: Boolean, default: false },
  loading: { type: Boolean, default: false },
  paused: { type: Boolean, default: false },
})
const emit = defineEmits(['close', 'select'])

const BINS = 28
const stats = computed(() => fieldStats(props.values))
const lo = computed(() => Number.isFinite(props.range[0]) ? props.range[0] : (stats.value.min ?? 0))
const hi = computed(() => Number.isFinite(props.range[1]) ? props.range[1] : (stats.value.max ?? 1))
// Bar heights are the square root of the share of the fullest bin, so that a field that is mostly one value (e.g. no
// aurora over most of the globe) still shows the rest of its distribution.
const bins = computed(() => histogram(props.values, lo.value, hi.value, BINS).map(Math.sqrt))
const binColor = k => (props.colorFor ? props.colorFor(lo.value + (k + 0.5) / BINS * (hi.value - lo.value)) : '#7fe0ff')
const coverage = computed(() => (stats.value.cells ? stats.value.valid / stats.value.cells * 100 : 0))
const meanMarker = computed(() => (stats.value.mean === null || !(hi.value > lo.value) ? null : Math.min(100, Math.max(0, (stats.value.mean - lo.value) / (hi.value - lo.value) * 100))))

// Scan: a sweep through the grid rows (south to north) with the values of the scanned row as a trace. It reads the
// same array the scene draws, so the trace is the data on screen, not a decoration.
const nx = computed(() => props.state.nx || 0), ny = computed(() => props.state.ny || 0)
const row = ref(0)
let sweep = null
const reducedMotion = typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
function startSweep() { stopSweep(); sweep = setInterval(() => { if (ny.value) row.value = (row.value + 1) % ny.value }, reducedMotion ? 900 : 140) }
function stopSweep() { clearInterval(sweep); sweep = null }
const scanRow = computed(() => (props.state.selected !== null && props.state.selected !== undefined && nx.value ? Math.floor(props.state.selected / nx.value) : Math.min(row.value, Math.max(0, ny.value - 1))))
const locked = computed(() => props.state.selected !== null && props.state.selected !== undefined)
const trace = computed(() => signalPaths(rowSignal(props.values, nx.value, scanRow.value), lo.value, hi.value, 100, 36, 3))
const sweepTop = computed(() => (ny.value ? (ny.value - 1 - scanRow.value) / ny.value * 100 : 0))
const probe = computed(() => {
  const i = props.state.selected
  if (i === null || i === undefined || !nx.value) return null
  return { left: ((i % nx.value) + 0.5) / nx.value * 100, top: (ny.value - 1 - Math.floor(i / nx.value) + 0.5) / ny.value * 100, x: nx.value > 1 ? (i % nx.value) / (nx.value - 1) * 100 : 50 }
})

// The minimap: one pixel per cell in the scene's own colours (the legend ramp if they are not available), drawn into a
// canvas that is scaled up without smoothing.
const map = ref(null)
function drawMap() {
  const canvas = map.value
  if (!canvas || !nx.value || !ny.value) return
  canvas.width = nx.value; canvas.height = ny.value
  const ctx = canvas.getContext('2d')
  if (!ctx) return
  const image = ctx.createImageData(nx.value, ny.value), span = hi.value - lo.value
  for (let y = 0; y < ny.value; y++) for (let x = 0; x < nx.value; x++) {
    const i = y * nx.value + x, v = props.values[i], o = ((ny.value - 1 - y) * nx.value + x) * 4
    const scene = props.colors?.[i]
    if (scene) { image.data.set([scene[0], scene[1], scene[2], 255], o); continue }
    if (v === null || v === undefined || !Number.isFinite(v)) { image.data.set([14, 30, 40, 255], o); continue }
    const [r, g, b] = rampRgb(span > 0 ? (v - lo.value) / span : 0.5)
    image.data.set([r, g, b, 255], o)
  }
  ctx.putImageData(image, 0, 0)
}
watch(() => [props.values, props.colors, lo.value, hi.value, nx.value, ny.value], () => nextTick(drawMap))
function pickCell(e) {
  if (!nx.value || !ny.value) return
  const r = e.currentTarget.getBoundingClientRect()
  const x = Math.min(nx.value - 1, Math.max(0, Math.floor((e.clientX - r.left) / r.width * nx.value)))
  const y = Math.min(ny.value - 1, Math.max(0, ny.value - 1 - Math.floor((e.clientY - r.top) / r.height * ny.value)))
  emit('select', y * nx.value + x)
}

const fps = computed(() => props.telemetry.fps ?? null)
const pulse = ref(false)
watch(() => props.log[0]?.id, () => { pulse.value = false; requestAnimationFrame(() => { pulse.value = true }) })
const fmt = formatSignal
const fmtInt = v => (Number.isFinite(v) ? Math.round(v).toLocaleString('en-US') : '—')

watch(() => props.paused, p => (p ? stopSweep() : startSweep()))
onMounted(() => { drawMap(); if (!props.paused) startSweep() })
onBeforeUnmount(stopSweep)
</script>

<template>
  <div class="data-stream hud-panel" role="complementary" aria-label="Data stream: what the scene renders">
    <div class="hud-panel-head">
      <span class="stream-title"><i class="live" :class="{ pulse, idle: !playing && !loading }"></i>DATA STREAM</span>
      <span class="stream-status">{{ loading ? 'RECEIVING…' : playing ? 'PLAYBACK' : 'LIVE' }}<template v-if="fps !== null"> · {{ fps }} FPS</template></span>
      <button class="hud-icon" title="Hide the data stream ( ] )" aria-label="Hide the data stream" @click="emit('close')">]</button>
    </div>

    <section class="stream-block">
      <div class="stream-label">01 · NOW RENDERING</div>
      <div class="stream-target">
        <strong>{{ state.layerTitle }}</strong>
        <small>{{ state.layerSubtitle }}</small>
      </div>
      <dl class="stream-kv">
        <dt>VIEW</dt><dd>{{ state.viewLabel }}</dd>
        <dt>SOURCE</dt><dd :title="state.source">{{ state.source }}</dd>
        <template v-if="state.variant && state.variant !== 'baseline'"><dt>VARIANT</dt><dd>{{ state.variant }}</dd></template>
        <dt>FRAME</dt><dd>{{ state.frame + 1 }} / {{ state.frames }} <em>{{ state.frameLabel }}</em></dd>
        <dt>VALID</dt><dd>{{ state.validTime }}</dd>
        <dt>GRID</dt><dd>{{ state.nx }} × {{ state.ny }} <em>{{ fmtInt(state.nx * state.ny) }} cells</em></dd>
      </dl>
      <div v-if="state.frames > 1" class="stream-progress" aria-hidden="true"><i :style="{ width: `${(state.frame + 1) / state.frames * 100}%` }"></i></div>
    </section>

    <section class="stream-block">
      <div class="stream-label">02 · FIELD SIGNAL <span>{{ state.unit || 'no unit' }}</span></div>
      <div class="stream-stats">
        <span><small>MIN</small><b>{{ fmt(stats.min) }}</b></span>
        <span><small>MEAN</small><b>{{ fmt(stats.mean) }}</b></span>
        <span><small>MAX</small><b>{{ fmt(stats.max) }}</b></span>
      </div>
      <svg class="stream-hist" viewBox="0 0 100 30" preserveAspectRatio="none" role="img" :aria-label="`Distribution of ${state.layerTitle} over the colour scale`"><title>Distribution over the colour scale (bar height ∝ √cells); dashed line: mean</title>
        <rect v-for="(b, k) in bins" :key="k" :x="k * 100 / BINS + 0.4" :width="100 / BINS - 0.8" :y="30 - Math.max(b * 28, b > 0 ? 1 : 0.3)" :height="Math.max(b * 28, b > 0 ? 1 : 0.3)" :fill="binColor(k)" :opacity="b > 0 ? 0.95 : 0.25"/>
        <line v-if="meanMarker !== null" :x1="meanMarker" :x2="meanMarker" y1="0" y2="30" class="mean-line"/>
      </svg>
      <div class="stream-scale"><span>{{ fmt(lo) }}</span><span>colour scale</span><span>{{ fmt(hi) }}</span></div>
      <div class="stream-coverage"><span>DATA COVERAGE</span><i><b :style="{ width: `${coverage}%` }"></b></i><span>{{ fmtInt(stats.valid) }}/{{ fmtInt(stats.cells) }}</span></div>
    </section>

    <section class="stream-block stream-scan-block">
      <div class="stream-label">03 · GRID SCAN <span>{{ locked ? 'LOCKED ON PROBE' : `ROW ${String(scanRow + 1).padStart(3, '0')}/${ny}` }}</span></div>
      <div class="stream-scan">
        <div class="stream-map" title="Click to probe a cell" @click="pickCell">
          <canvas ref="map" aria-hidden="true"></canvas>
          <i class="sweep" :class="{ locked }" :style="{ top: `${sweepTop}%`, height: `${100 / (ny || 1)}%` }"></i>
          <i v-if="probe" class="probe" :style="{ left: `${probe.left}%`, top: `${probe.top}%` }"></i>
        </div>
        <svg class="stream-scope" viewBox="0 0 100 36" preserveAspectRatio="none" aria-hidden="true">
          <line v-for="g in [9, 18, 27]" :key="g" x1="0" x2="100" :y1="g" :y2="g" class="grid-line"/>
          <polyline v-for="(p, k) in trace" :key="k" :points="p" class="trace"/>
          <line v-if="probe" :x1="probe.x" :x2="probe.x" y1="0" y2="36" class="probe-line"/>
        </svg>
      </div>
      <p v-if="state.selected !== null && state.selected !== undefined" class="stream-probe">PROBE {{ state.selectedLabel }} → <b>{{ fmt(state.selectedValue, 4) }}</b> {{ state.unit }}</p>
    </section>

    <section class="stream-block">
      <div class="stream-label">04 · CHANNELS <span>{{ channels.length }} IN FRAME</span></div>
      <div class="stream-chips">
        <span v-for="c in channels" :key="c.key" class="chip" :class="{ active: c.active, base: c.base }" :title="c.active ? 'Feeds the rendered layer' : c.base ? 'Base grid of every layer' : 'Loaded, not used by this layer'">{{ c.label }}</span>
      </div>
      <div v-if="overlays.length" class="stream-chips overlays">
        <span v-for="o in overlays" :key="o" class="chip overlay">{{ o }}</span>
      </div>
    </section>

    <section class="stream-block">
      <div class="stream-label">05 · PIPELINE <span :title="telemetry.renderer === '2d' ? 'WebGL is unavailable: the scene is drawn as a 2D grid' : `${fmtInt(telemetry.objects)} scene objects`">{{ telemetry.renderer === '2d' ? '2D FALLBACK · NO WEBGL' : telemetry.resolution ? `${telemetry.resolution} PX` : 'WEBGL' }}</span></div>
      <div class="stream-telemetry">
        <div><small>FPS</small><b>{{ fps ?? '—' }}</b></div>
        <div><small>DRAW CALLS</small><b>{{ fmtInt(telemetry.calls) }}</b></div>
        <div><small>TRIANGLES</small><b>{{ fmtInt(telemetry.triangles) }}</b></div>
        <div><small>LAST BUILD</small><b>{{ telemetry.buildMs === null || telemetry.buildMs === undefined ? '—' : `${telemetry.buildMs.toFixed(1)} ms` }}</b></div>
      </div>
    </section>

    <section class="stream-block stream-log-block">
      <div class="stream-label">06 · EVENT STREAM <span>{{ log.length }}</span></div>
      <ol class="stream-log" role="log" aria-live="off">
        <li v-for="e in log" :key="e.id" :class="`tag-${e.tag.toLowerCase()}`">
          <time>{{ e.time }}</time><b>{{ e.tag }}</b><span :title="e.text">{{ e.text }}</span><em v-if="e.meta">{{ e.meta }}</em>
        </li>
      </ol>
    </section>
  </div>
</template>
