<script setup>
// The flight weather report over the 3D view: a METAR-style line, the flight category, a decoded briefing, runway wind,
// performance, hazards and the category over the run at this station. It follows the simulation live (every frame, the
// picked point, the settings). The numbers come from aviation.js; this component only lays them out.
import { computed, ref } from 'vue'
import { CATEGORIES } from './aviation.js'
import { FT_PER_M } from './station.js'

const props = defineProps({
  report: { type: Object, required: true },
  pinned: { type: Boolean, default: false },
  series: { type: Array, default: () => [] },     // flight category name per frame
  seriesIndex: { type: Number, default: 0 },
  frameLabels: { type: Array, default: () => [] },
})
const emit = defineEmits(['close', 'toggle-pin', 'settings', 'seek'])

const r = computed(() => props.report)
const collapsed = ref(false)
const round = (v, step = 1) => Math.round(v / step) * step
const signed = v => `${v >= 0 ? '' : '−'}${Math.abs(Math.round(v))}`
const ft = v => `${round(v, 100).toLocaleString('en-US')} ft`
const visText = computed(() => {
  const v = r.value.visibility
  if (!v) return 'not available'
  if (v.metres >= 10000) return '10 km or more (≥ 6 SM)'
  return `${v.metres < 5000 ? round(v.metres, 50) : round(v.metres / 1000, 1)}${v.metres < 5000 ? ' m' : ' km'} (${v.sm < 3 ? v.sm.toFixed(1) : Math.round(v.sm)} SM)`
})
const cloudText = computed(() => {
  const c = r.value.cloud
  if (!c.modelled) return 'not modelled in this run'
  if (!c.layer) return 'clear sky'
  const l = c.layer
  return `${l.cover} (${l.oktas}/8) ${l.abbreviation} base ${ft(l.base * FT_PER_M)}${l.top !== null ? `, tops ${ft(l.top * FT_PER_M)}` : ''}`
})
const ceilingText = computed(() => {
  const c = r.value.cloud.ceilingFt
  return c === null ? 'unknown' : c === Infinity ? 'none' : ft(c)
})
const headText = computed(() => {
  const h = r.value.runway.head
  return Math.abs(h) < 0.5 ? 'no head or tail wind' : h > 0 ? `headwind ${Math.round(h)} kt` : `tailwind ${Math.round(-h)} kt`
})
const crossText = computed(() => {
  const c = r.value.runway.cross
  return Math.abs(c) < 0.5 ? 'no crosswind' : `crosswind ${Math.round(Math.abs(c))} kt from the ${c > 0 ? 'right' : 'left'}`
})
function setRunway(e) {
  const v = Number(e.target.value)
  if (Number.isFinite(v) && v >= 1 && v <= 36) emit('settings', { runwayDeg: Math.round(v) * 10 })
}
function setLimit(e) {
  const v = Number(e.target.value)
  if (Number.isFinite(v) && v >= 0 && v <= 60) emit('settings', { crosswindLimitKt: v })
}
const stripColour = name => (Object.values(CATEGORIES).find(c => c.name === name) ?? CATEGORIES.UNKNOWN).colour
async function copyMetar() { try { await navigator.clipboard?.writeText(r.value.metar) } catch { /* clipboard unavailable: the text stays selectable */ } }
</script>

<template>
  <section class="station-report flight-report" aria-label="Flight weather report" :style="{ '--cat': r.category.colour }">
    <header class="sr-head">
      <div>
        <span class="sr-eyebrow">✈ FLIGHT WX · SIMULATED · LIVE</span>
        <strong>ZZZZ <small>{{ r.station.where }} · col {{ r.station.x + 1 }}, row {{ r.station.y + 1 }}<template v-if="r.station.elevationM !== null"> · elev {{ ft(Math.max(0, r.station.elevationM) * FT_PER_M) }}</template></small></strong>
        <small class="sr-time">Valid {{ r.time.iso.replace('T', ' ').replace('Z', ' UTC') }}</small>
      </div>
      <div class="sr-actions">
        <button :aria-expanded="!collapsed" :title="collapsed ? 'Show the full report' : 'Collapse to the key line'" @click="collapsed = !collapsed">{{ collapsed ? '▸' : '▾' }}</button>
        <button :class="{ on: pinned }" :aria-pressed="pinned" :title="pinned ? 'Unpin: follow the picked point' : 'Pin this station (picking other points will not move it)'" @click="emit('toggle-pin')">📌</button>
        <button title="Close the flight weather report" aria-label="Close the flight weather report" @click="emit('close')">✕</button>
      </div>
    </header>

    <div class="sr-metar" title="METAR-style report (WMO FM 15 / ICAO Annex 3 form). Click to copy.">
      <code @click="copyMetar">{{ r.metar }}</code>
    </div>

    <div class="sr-category">
      <b>{{ r.category.name }}</b>
      <span>{{ r.category.label }}<small>{{ r.category.partial ? `Based on ${r.category.basis}` : 'Ceiling and visibility (FAA categories)' }}</small></span>
    </div>

    <div v-show="!collapsed" class="sr-body">
    <dl class="sr-grid">
      <dt>Wind</dt><dd>{{ r.wind.kt < 0.5 ? 'calm' : `${String(Math.round(r.wind.fromDeg / 10) * 10 || 360).padStart(3, '0')}° true, ${Math.round(r.wind.kt)} kt` }} <small>{{ r.wind.speed.toFixed(1) }} m/s · {{ r.wind.beaufort.label }}<template v-if="r.wind.source !== 'engine wind field'"> · {{ r.wind.source }}</template></small></dd>
      <dt>Visibility</dt><dd>{{ visText }} <small v-if="r.visibility">estimated</small></dd>
      <dt>Weather</dt><dd>{{ r.weather.words ?? 'not modelled in this run' }}</dd>
      <dt>Cloud</dt><dd>{{ cloudText }}</dd>
      <dt>Ceiling</dt><dd>{{ ceilingText }}</dd>
      <dt>Temp / dew</dt><dd>{{ r.temperature.c.toFixed(1) }} °C / {{ r.temperature.dewC === null ? '—' : `${r.temperature.dewC.toFixed(1)} °C` }} <small v-if="r.temperature.spread !== null">spread {{ r.temperature.spread.toFixed(1) }} K</small></dd>
      <dt>QNH</dt><dd>{{ r.pressure.qnhHpa === null ? 'not available' : `${Math.floor(r.pressure.qnhHpa)} hPa` }} <small v-if="r.pressure.inHg !== null">{{ r.pressure.inHg.toFixed(2) }} inHg</small></dd>
    </dl>

    <div class="sr-block">
      <div class="sr-label">RUNWAY WIND</div>
      <div class="sr-runway">
        <label>Runway <input type="number" min="1" max="36" step="1" :value="Number(r.runway.designator)" aria-label="Runway designator (heading ÷ 10)" @change="setRunway"></label>
        <span class="sr-rwy-calc"><b>{{ headText }}</b><b :class="{ warn: Math.abs(r.runway.cross) > r.settings.crosswindLimitKt }">{{ crossText }}</b><small>Into wind: runway {{ r.runway.best }} · headings true</small></span>
      </div>
      <label class="sr-inline">Crosswind limit <input type="number" min="0" max="60" step="1" :value="r.settings.crosswindLimitKt" aria-label="Crosswind limit in knots" @change="setLimit"> kt</label>
    </div>

    <div v-if="r.performance" class="sr-block">
      <div class="sr-label">PERFORMANCE</div>
      <dl class="sr-grid compact">
        <dt>Pressure alt.</dt><dd>{{ ft(r.performance.pressureAltitudeFt) }}</dd>
        <dt>Density alt.</dt><dd>{{ ft(r.performance.densityAltitudeFt) }} <small>ISA {{ r.performance.isaDeviationC >= 0 ? '+' : '−' }}{{ Math.abs(r.performance.isaDeviationC).toFixed(0) }} °C</small></dd>
      </dl>
    </div>

    <div class="sr-block">
      <div class="sr-label">HAZARDS</div>
      <ul v-if="r.hazards.length" class="sr-hazards"><li v-for="(h, k) in r.hazards" :key="k" :class="h.level">{{ h.text }}</li></ul>
      <p v-else class="sr-none">None diagnosed at this point.</p>
    </div>

    <div v-if="series.length > 1" class="sr-block">
      <div class="sr-label">CATEGORY OVER THE RUN · CLICK TO JUMP</div>
      <div class="sr-strip" role="list">
        <button v-for="(c, k) in series" :key="k" role="listitem" :class="{ now: k === seriesIndex }" :style="{ background: stripColour(c) }" :title="`${frameLabels[k] ?? `Frame ${k + 1}`}: ${c}`" :aria-label="`Frame ${k + 1}: ${c}`" @click="emit('seek', k)"></button>
      </div>
    </div>

    <details class="sr-notes"><summary>About this report</summary><ul><li v-for="(n, k) in r.notes" :key="k">{{ n }}</li></ul></details>
    </div>
  </section>
</template>
