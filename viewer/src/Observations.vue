<script setup>
import { computed, ref, onMounted, onBeforeUnmount } from 'vue'
import { parseObservations, observationSummary, observationAge, observationPlot, reflectivityColour } from './observations.js'
const document = ref(null), error = ref(''), index = ref(0), selected = ref(null), now = ref(Date.now())
let timer
onMounted(() => { timer = setInterval(() => { now.value = Date.now() }, 30000) })
onBeforeUnmount(() => clearInterval(timer))
const frame = computed(() => document.value?.frames[index.value] ?? null)
const summary = computed(() => frame.value ? observationSummary(frame.value) : null)
const plot = computed(() => frame.value ? observationPlot(frame.value) : null)
const age = computed(() => frame.value ? observationAge(frame.value.ms, now.value) : null)
const badge = computed(() => document.value?.source.source_kind === 'synthetic_fixture' ? 'SYNTHETIC FIXTURE' : 'IMPORTED OBSERVATIONS')
function accept(records) { const next = parseObservations(records); document.value = next; index.value = 0; selected.value = null; error.value = '' }
async function load(event) {
  const file = event.target.files?.[0]
  if (!file) return
  try {
    if (file.size > 64000000) throw new Error('Observation file exceeds 64 MB')
    accept(JSON.parse(await file.text()))
  } catch (err) { error.value = err.message }
  event.target.value = ''
}
async function example() {
  try {
    const response = await fetch(`${import.meta.env.BASE_URL}observations/mrms-synthetic.json`)
    if (!response.ok) throw new Error('Could not load observation example')
    accept(await response.json())
  } catch (err) { error.value = err.message }
}
</script>
<template>
  <section class="observations-workspace">
    <h1>Radar observations</h1>
    <p>Inspect imported radar reflectivity by valid time. Observation data stays separate from the simulation. Files do not refresh automatically.</p>
    <div class="observation-actions">
      <label>Open decoded observation JSON <input type="file" accept=".json,application/json" @change="load"></label>
      <button @click="example">Load synthetic MRMS-format example</button>
      <button v-if="document" @click="document = null; selected = null; error = ''">Clear observations</button>
    </div>
    <p v-if="error" role="alert">{{ error }}</p>
    <template v-if="document && frame">
      <p><strong>{{ badge }}</strong> · {{ document.source.source_id }} · {{ document.source.product_id || 'Product unspecified' }} · {{ document.source.vertical_reference }}</p>
      <p><strong>{{ frame.time }}</strong> · {{ age.label }} · {{ Math.abs(age.minutes).toFixed(1) }} minutes {{ age.minutes < 0 ? 'ahead' : 'old' }}</p>
      <label v-if="document.frames.length > 1">Valid time <input type="range" min="0" :max="document.frames.length - 1" v-model.number="index" @input="selected = null"> {{ index + 1 }} / {{ document.frames.length }}</label>
      <p>{{ summary.decoded }} decoded · {{ summary.no_data }} no data · {{ summary.ambiguous }} ambiguous · {{ summary.total }} reported points.
        <template v-if="summary.min !== null">Range {{ summary.min }}…{{ summary.max }} dBZ.</template>
      </p>
      <p class="hint">Geographic scatter preview · north up, east right. Gaps are unknown; points are not interpolated.
        <template v-if="plot.sampled">Showing {{ plot.points.length }} sampled points; counts cover all reported points.</template>
      </p>
      <svg class="observation-map" viewBox="0 0 1000 500" role="img" aria-label="Radar observation locations, longitude left to right and latitude bottom to top">
        <rect width="1000" height="500" fill="#0b1d2a" />
        <circle v-for="(p, k) in plot.points" :key="k" :cx="p.x" :cy="p.y" r="4" :fill="reflectivityColour(p.value)" :stroke="p.status === 'decoded' ? 'none' : '#d4dde2'" @click="selected = p">
          <title>{{ p.lat }}, {{ p.lon }} · {{ p.value === null ? p.status : p.value + ' dBZ' }}</title>
        </circle>
      </svg>
      <p>{{ plot.west.toFixed(3) }}°…{{ plot.east.toFixed(3) }}° longitude · {{ plot.south.toFixed(3) }}°…{{ plot.north.toFixed(3) }}° latitude</p>
      <p>Colour key: &lt;0 blue-grey · 0–20 blue · 20–35 green · 35–50 yellow · 50–65 orange · ≥65 purple dBZ. Missing/ambiguous: outlined grey.</p>
      <label>Inspect a displayed point
        <select :value="selected ? plot.points.indexOf(selected) : -1" @change="selected = plot.points[+$event.target.value] ?? null">
          <option value="-1">Choose coordinates</option>
          <option v-for="(p,k) in plot.points" :key="k" :value="k">{{ p.lat }}, {{ p.lon }}</option>
        </select>
      </label>
      <p v-if="selected">{{ selected.lat }}°, {{ selected.lon }}° · {{ selected.value === null ? selected.status : selected.value + ' dBZ' }} · {{ selected.reason }}</p>
      <p class="hint">Reflectivity is radar signal, not measured surface rainfall. Unreported locations have unknown coverage. The 15-minute age label is a display threshold, not a provider guarantee.</p>
    </template>
    <p v-else>Decode a bounded NOAA MRMS crop with tools/decode_noaa_mrms.py, then open its JSON here.</p>
  </section>
</template>
<style scoped>
.observations-workspace { padding: 2rem; color: #d6e6ef; max-width: 1300px; margin: auto; }
h1 { font-size: 2rem; }
p { line-height: 1.6; }
.observation-actions { display:flex; flex-wrap:wrap; gap:1rem; align-items:center; }
.observation-map { width:100%; max-height:65vh; border:1px solid #355064; }
.observation-map circle { cursor:pointer; }
[role=alert] { color:#ffb3a7; }
select { max-width: 100%; }
</style>
