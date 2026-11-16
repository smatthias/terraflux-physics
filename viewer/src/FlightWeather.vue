<script setup>
// The flight weather view's report, full width below the 3D scene: the METAR-style line and flight category of the
// station, the categories over the map, the decoded observation, the winds aloft at every level (barbs, FB code,
// temperature, shear), runway wind, performance, hazards and the category over the run. Follows the simulation live.
// Numbers from aviation.js and windsAloft.js; this component only lays them out.
import { computed } from 'vue'
import WindBarb from './WindBarb.vue'
import WeatherIcon from './WeatherIcon.vue'
import { SYMBOLS } from './weatherLayers.js'
import { CATEGORIES } from './aviation.js'
import { FT_PER_M } from './station.js'
import { CAT_SHEAR_KT_PER_1000FT } from './windsAloft.js'

const props = defineProps({
  report: { type: Object, required: true },
  aloft: { type: Object, default: null },          // windsAloftField(...).profile(i), or null
  aloftNote: { type: String, default: '' },         // why there are no winds aloft, or a caveat
  strongest: { type: Object, default: null },       // strongestAloft(...) over the map
  shares: { type: Object, default: null },          // categoryShares(...)
  airports: { type: Array, default: () => [] },     // [{ airport, report }] of every aerodrome
  selectedId: { type: String, default: null },
  series: { type: Array, default: () => [] },
  seriesIndex: { type: Number, default: 0 },
  frameLabels: { type: Array, default: () => [] },
  northernHemisphere: { type: Boolean, default: true },
})
const emit = defineEmits(['select-airport', 'settings', 'seek'])
const r = computed(() => props.report)
const selectedSymbol = computed(() => props.airports.find(a => a.airport.id === props.selectedId)?.symbol ?? null)
const round = (v, step = 1) => Math.round(v / step) * step
const ft = v => `${round(v, 100).toLocaleString('en-US')} ft`
const dir3 = d => String(Math.round(d / 10) * 10 || 360).padStart(3, '0')
const visText = computed(() => {
  const v = r.value.visibility
  if (!v) return 'not available'
  if (v.metres >= 10000) return '10 km or more (≥ 6 SM)'
  return `${v.metres < 5000 ? `${round(v.metres, 50)} m` : `${round(v.metres / 1000, 1)} km`} (${v.sm < 3 ? v.sm.toFixed(1) : Math.round(v.sm)} SM)`
})
const cloudText = computed(() => {
  const c = r.value.cloud
  if (!c.modelled) return 'not modelled in this run'
  if (!c.layer) return 'clear sky'
  const l = c.layer
  return `${l.cover} (${l.oktas}/8) ${l.abbreviation}, base ${ft(l.base * FT_PER_M)}${l.top !== null ? `, tops ${ft(l.top * FT_PER_M)}` : ''}`
})
const ceilingText = computed(() => { const c = r.value.cloud.ceilingFt; return c === null ? 'unknown' : c === Infinity ? 'none' : ft(c) })
const headText = computed(() => { const h = r.value.runway.head; return Math.abs(h) < 0.5 ? 'no head or tail wind' : h > 0 ? `headwind ${Math.round(h)} kt` : `tailwind ${Math.round(-h)} kt` })
const crossText = computed(() => { const c = r.value.runway.cross; return Math.abs(c) < 0.5 ? 'no crosswind' : `crosswind ${Math.round(Math.abs(c))} kt from the ${c > 0 ? 'right' : 'left'}` })
const shareBars = computed(() => props.shares ? ['VFR', 'MVFR', 'IFR', 'LIFR', '—'].map(k => ({ k, pct: props.shares[k] ?? 0, colour: (k === '—' ? CATEGORIES.UNKNOWN : CATEGORIES[k]).colour })).filter(b => b.pct > 0) : [])
// Highest level at the top and the surface at the bottom, like a sounding.
const rows = computed(() => props.aloft ? [...props.aloft.rows].reverse() : [])
function setLimit(e) { const v = Number(e.target.value); if (Number.isFinite(v) && v >= 0 && v <= 60) emit('settings', { crosswindLimitKt: v }) }
const stripColour = name => (Object.values(CATEGORIES).find(c => c.name === name) ?? CATEGORIES.UNKNOWN).colour
</script>

<template>
  <section class="comparison wx-report flight-weather" aria-label="Flight weather report" :style="{ '--cat': r.category.colour }">
    <div class="wx-head">
      <div>
        <div class="section-label">AERODROME WEATHER · SIMULATED · LIVE · {{ r.time.iso.replace('T', ' ').replace('Z', ' UTC') }}</div>
        <h3>{{ r.station.id }}<template v-if="r.station.name"> · {{ r.station.name }}</template> <small>{{ r.station.where }} · column {{ r.station.x + 1 }}, row {{ r.station.y + 1 }}<template v-if="r.station.elevationM !== null"> · elevation {{ ft(Math.max(0, r.station.elevationM) * FT_PER_M) }}</template> · synthetic aerodrome, fixed for this run</small></h3>
      </div>
    </div>
    <div v-if="airports.length" class="fw-airports" role="list" aria-label="Aerodromes">
      <button v-for="a in airports" :key="a.airport.id" role="listitem" :class="{ on: a.airport.id === selectedId }" :style="{ '--c': a.report.category.colour }" :title="a.report.metar" @click="emit('select-airport', a.airport.id)">
        <WeatherIcon :symbol="a.symbol" :size="30"/><b>{{ a.report.category.name }}</b><span>{{ a.airport.id }}<small>{{ a.airport.name }} · RWY {{ a.airport.runway }}</small></span>
      </button>
    </div>

    <div class="wx-top">
      <div class="wx-metar"><span class="wx-small-label">METAR-STYLE REPORT (FM 15 FORM)</span><code>{{ r.metar }}</code></div>
      <div class="wx-category"><WeatherIcon :symbol="selectedSymbol" :size="54"/><b>{{ r.category.name }}</b><span><template v-if="selectedSymbol">{{ SYMBOLS[selectedSymbol].label }} · </template>{{ r.category.label }}<small>{{ r.category.partial ? `Based on ${r.category.basis}` : 'Ceiling and visibility (FAA categories)' }}</small></span></div>
      <div v-if="shareBars.length" class="wx-shares">
        <span class="wx-small-label">ACROSS THE MAP</span>
        <div class="wx-sharebar"><i v-for="b in shareBars" :key="b.k" :style="{ width: `${b.pct}%`, background: b.colour }" :title="`${b.k}: ${b.pct.toFixed(0)} %`"></i></div>
        <small><template v-for="(b, k) in shareBars" :key="b.k">{{ k ? ' · ' : '' }}<span :style="{ color: b.colour }">{{ b.k }}</span> {{ b.pct.toFixed(0) }} %</template></small>
      </div>
    </div>

    <div class="wx-grid">
      <div class="wx-card">
        <div class="wx-small-label">OBSERVATION (DECODED)</div>
        <dl class="wx-dl">
          <dt>Wind</dt><dd><WindBarb :kt="r.wind.kt" :from-deg="r.wind.fromDeg" :size="30" :northern-hemisphere="northernHemisphere"/> {{ r.wind.kt < 0.5 ? 'calm' : `${dir3(r.wind.fromDeg)}° true, ${Math.round(r.wind.kt)} kt` }} <small>{{ r.wind.speed.toFixed(1) }} m/s · {{ r.wind.beaufort.label }}<template v-if="r.wind.cyclone"> · {{ r.wind.source }}</template></small></dd>
          <dt>Visibility</dt><dd>{{ visText }} <small v-if="r.visibility">estimated</small></dd>
          <dt>Weather</dt><dd>{{ r.weather.words ?? 'not modelled in this run' }}</dd>
          <dt>Cloud</dt><dd>{{ cloudText }}</dd>
          <dt>Ceiling</dt><dd>{{ ceilingText }}</dd>
          <dt>Temp / dew</dt><dd>{{ r.temperature.c.toFixed(1) }} °C / {{ r.temperature.dewC === null ? '—' : `${r.temperature.dewC.toFixed(1)} °C` }} <small v-if="r.temperature.spread !== null">spread {{ r.temperature.spread.toFixed(1) }} K</small></dd>
          <dt>QNH</dt><dd>{{ r.pressure.qnhHpa === null ? 'not available' : `${Math.floor(r.pressure.qnhHpa)} hPa` }} <small v-if="r.pressure.inHg !== null">{{ r.pressure.inHg.toFixed(2) }} inHg</small></dd>
        </dl>
      </div>

      <div class="wx-card wx-aloft">
        <div class="wx-small-label">WINDS ALOFT AT THE STATION · BARBS: ▲ 50 KT · ― 10 KT · ╴ 5 KT</div>
        <table v-if="rows.length">
          <thead><tr><th>Level</th><th>Wind</th><th></th><th>Temp</th><th>FB code</th><th title="Vertical wind shear to the level above">Shear ↑</th></tr></thead>
          <tbody>
            <tr v-for="row in rows" :key="row.level.key" :class="{ below: row.below, sfc: row.level.surface }">
              <th>{{ row.level.label }}</th>
              <td class="barb-cell"><WindBarb v-if="!row.below" :kt="row.kt" :from-deg="row.fromDeg" :size="34" :northern-hemisphere="northernHemisphere"/></td>
              <td>{{ row.below ? 'below ground' : row.kt < 2.5 ? 'calm' : `${dir3(row.fromDeg)}° / ${Math.round(row.kt)} kt` }}</td>
              <td>{{ row.temperatureC === null ? '' : `${row.temperatureC.toFixed(0)} °C` }}</td>
              <td><code>{{ row.fb ?? '' }}</code></td>
              <td :class="{ warn: row.shearKtPer1000ft !== null && row.shearKtPer1000ft >= CAT_SHEAR_KT_PER_1000FT }">{{ row.shearKtPer1000ft === null ? '' : `${row.shearKtPer1000ft.toFixed(1)} kt/1000 ft` }}</td>
            </tr>
          </tbody>
        </table>
        <p v-if="aloftNote" class="wx-note">{{ aloftNote }}</p>
        <p v-if="strongest" class="wx-note">Strongest wind aloft on the map: <b>{{ Math.round(strongest.kt) }} kt</b> from {{ dir3(strongest.fromDeg) }}° at {{ strongest.level.label }} (column {{ strongest.x + 1 }}, row {{ strongest.y + 1 }}).</p>
        <p v-if="rows.length" class="wx-note">Shear of {{ CAT_SHEAR_KT_PER_1000FT }} kt per 1 000 ft or more (orange) is a common rule of thumb for clear-air turbulence. Winds aloft are diagnosed from the engine's pressure and temperature gradients (geostrophic and thermal wind); the engine has no upper-air model.</p>
      </div>

      <div class="wx-card">
        <div class="wx-small-label">RUNWAY WIND</div>
        <div class="wx-runway">
          <span class="fw-rwy">RWY {{ r.runway.designator }}<small>in use</small></span>
          <span><b>{{ headText }}</b><b :class="{ warn: Math.abs(r.runway.cross) > r.settings.crosswindLimitKt }">{{ crossText }}</b><small>The runway end with the most headwind · runway along the site's prevailing wind · headings true</small></span>
        </div>
        <label class="wx-inline">Crosswind limit <input type="number" min="0" max="60" step="1" :value="r.settings.crosswindLimitKt" aria-label="Crosswind limit in knots" @change="setLimit"> kt</label>
        <template v-if="r.performance">
          <div class="wx-small-label" style="margin-top:14px">PERFORMANCE</div>
          <dl class="wx-dl">
            <dt>Pressure alt.</dt><dd>{{ ft(r.performance.pressureAltitudeFt) }}</dd>
            <dt>Density alt.</dt><dd>{{ ft(r.performance.densityAltitudeFt) }} <small>ISA {{ r.performance.isaDeviationC >= 0 ? '+' : '−' }}{{ Math.abs(r.performance.isaDeviationC).toFixed(0) }} °C</small></dd>
          </dl>
        </template>
      </div>

      <div class="wx-card">
        <div class="wx-small-label">HAZARDS</div>
        <ul v-if="r.hazards.length" class="wx-hazards"><li v-for="(h, k) in r.hazards" :key="k" :class="h.level">{{ h.text }}</li></ul>
        <p v-else class="wx-note">None diagnosed at this point.</p>
      </div>
    </div>

    <div v-if="series.length > 1" class="wx-strip-block">
      <div class="wx-small-label">FLIGHT CATEGORY AT THE STATION OVER THE RUN · CLICK A FRAME TO JUMP</div>
      <div class="wx-strip" role="list">
        <button v-for="(c, k) in series" :key="k" role="listitem" :class="{ now: k === seriesIndex }" :style="{ background: stripColour(c) }" :title="`${frameLabels[k] ?? `Frame ${k + 1}`}: ${c}`" :aria-label="`Frame ${k + 1}: ${c}`" @click="emit('seek', k)">{{ series.length <= 40 ? c : '' }}</button>
      </div>
    </div>
    <details class="wx-about"><summary>About this report</summary><ul><li v-for="(n, k) in r.notes" :key="k">{{ n }}</li></ul></details>
  </section>
</template>
