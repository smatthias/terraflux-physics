<script setup>
// The sailing view's report, full width below the 3D scene: the marine forecast line (wind now and its change over
// the coming frames, sea state, weather, visibility), the warning and Beaufort force, the boat's true and apparent
// wind and point of sail, the sea, current and water, the barometer, a sail-plan hint against the user's own limits,
// hazards, the wind above the boat and the Beaufort force over the run. Numbers from sailing.js and windsAloft.js.
import { computed } from 'vue'
import WindBarb from './WindBarb.vue'
import { compass16 } from './station.js'
import { BEAUFORT_COLOURS as BF_COLOURS } from './sailing.js'

const props = defineProps({
  report: { type: Object, required: true },
  forecast: { type: Object, default: null },        // marineWindForecast(...)
  aloft: { type: Object, default: null },           // winds aloft profile at the station, or null
  probe: { type: Object, default: null },           // pointReadout(...) of the clicked point, or null
  probeSeries: { type: Array, default: () => [] },  // Beaufort force at the clicked point over the run
  boatMoved: { type: Boolean, default: false },     // the boat was moved away from its starting point
  series: { type: Array, default: () => [] },
  seriesIndex: { type: Number, default: 0 },
  frameLabels: { type: Array, default: () => [] },
  northernHemisphere: { type: Boolean, default: true },
})
const emit = defineEmits(['move-boat', 'steer-here', 'clear-point', 'reset-boat', 'settings', 'seek'])
const r = computed(() => props.report)
const cap = s => (s ? s.charAt(0).toUpperCase() + s.slice(1) : s)
const rose = computed(() => {
  const b = r.value.boat, w = r.value.wind, C = 60
  const vec = (deg, len) => [C + Math.sin(deg * Math.PI / 180) * len, C - Math.cos(deg * Math.PI / 180) * len]
  const maxKt = Math.max(8, w.kt, b.aws)
  const arrow = (fromDeg, kt) => { const len = 14 + 32 * Math.min(1, kt / maxKt), [x0, y0] = vec(fromDeg, len + 9), [x1, y1] = vec(fromDeg, 9); return { x0, y0, x1, y1 } }
  const h = b.headingDeg * Math.PI / 180, s = Math.sin(h), c = Math.cos(h), pt = (x, y) => `${(C + x * c - y * s).toFixed(1)},${(C + x * s + y * c).toFixed(1)}`
  return { hull: [pt(0, -15), pt(5.5, -2), pt(4.5, 12), pt(-4.5, 12), pt(-5.5, -2)].join(' '), tw: arrow(w.fromDeg, w.kt), aw: arrow(b.awdFrom, b.aws) }
})
function set(key, e, lo, hi) { const v = Number(e.target.value); if (Number.isFinite(v) && v >= lo && v <= hi) emit('settings', { [key]: v }) }
const side = a => (Math.abs(a) < 1 ? 'ahead' : Math.abs(a) > 179 ? 'astern' : a > 0 ? 'starboard' : 'port')
const visLine = computed(() => { const v = r.value.visibility; return v ? `${cap(v.word)}${v.metres < 1000 ? `, ${Math.round(v.metres / 50) * 50} m` : ''}` : null })
const probeVis = computed(() => { const v = props.probe?.visibility; return v ? `${cap(v.word)}${v.metres < 1000 ? `, ${Math.round(v.metres / 50) * 50} m` : ''}` : null })
const probeHere = computed(() => !!props.probe && props.probe.i === r.value.station.i)
const aloftRows = computed(() => props.aloft ? props.aloft.rows.filter(x => !x.below && (x.level.surface || x.level.ft <= 12000)) : [])
</script>

<template>
  <section v-if="!r.available" class="comparison wx-report sailing-weather" aria-label="Sailing report">
    <div class="section-label">SAILING WEATHER · SIMULATED</div>
    <p>{{ r.reason }} Load a run with sea: the hurricane landfall, the ocean, polar or coast examples.</p>
  </section>
  <section v-else class="comparison wx-report sailing-weather" aria-label="Sailing report" :style="{ '--cat': r.warning.colour }">
    <div class="wx-head">
      <div>
        <div class="section-label">SAILING WEATHER · SIMULATED · LIVE · {{ r.time.iso.replace('T', ' ').replace('Z', ' UTC') }}</div>
        <h3>{{ r.station.where }} <small>column {{ r.station.x + 1 }}, row {{ r.station.y + 1 }}<template v-if="r.station.snapped"> · nearest sea, {{ r.station.distanceKm.toFixed(0) }} km from the picked point</template> · the boat stays put: click the map to read a point</small></h3>
      </div>
      <button v-if="boatMoved" class="wx-pin" title="Put the boat back at the centre of the run" @click="emit('reset-boat')">⟲ Boat to start</button>
    </div>

    <div v-if="probe" class="wx-probe" :style="{ '--pt': probe.warning.colour }" aria-label="Clicked point">
      <div class="wx-probe-head">
        <span class="wx-small-label">POINT READ · {{ probe.where }} <small>column {{ probe.x + 1 }}, row {{ probe.y + 1 }}</small></span>
        <span v-if="probe.fromBoat && !probeHere" class="wx-probe-range">{{ probe.fromBoat.nm < 10 ? probe.fromBoat.nm.toFixed(1) : Math.round(probe.fromBoat.nm) }} NM · bearing {{ String(Math.round(probe.fromBoat.bearingDeg) % 360).padStart(3, '0') }}° ({{ compass16(probe.fromBoat.bearingDeg) }}) from the boat</span>
        <span v-else-if="probeHere" class="wx-probe-range">at the boat</span>
        <span class="wx-probe-actions">
          <button v-if="!probeHere && probe.onSea !== false" title="Put the boat at this point: the report below then follows it" @click="emit('move-boat')">⛵ Move the boat here</button>
          <button v-if="probe.fromBoat && !probeHere && probe.fromBoat.nm > 0.05" :title="`Set the boat's heading to the bearing of this point (${Math.round(probe.fromBoat.bearingDeg) % 360}° true)`" @click="emit('steer-here')">➜ Steer {{ String(Math.round(probe.fromBoat.bearingDeg) % 360).padStart(3, '0') }}°</button>
          <button class="wx-probe-x" aria-label="Close the point read" title="Close" @click="emit('clear-point')">✕</button>
        </span>
      </div>
      <div class="wx-probe-grid">
        <div><WindBarb :kt="probe.wind.kt" :from-deg="probe.wind.fromDeg" :size="38" :northern-hemisphere="northernHemisphere"/><span><b>F{{ probe.wind.beaufort.force }}</b> {{ probe.wind.kt < 1 ? 'calm' : `${probe.wind.from16} ${Math.round(probe.wind.kt)} kt` }}<small>{{ probe.wind.beaufort.label }} · {{ probe.warning.label }}</small></span></div>
        <div v-if="probe.onSea === false"><span><b>Land</b><small>no waves or current here</small></span></div>
        <div v-else-if="probe.sea"><span><b>{{ probe.sea.hs.toFixed(1) }} m</b> waves<small>{{ probe.sea.state }}<template v-if="probe.sea.hs >= 0.05"> · from {{ compass16(probe.sea.fromDeg) }}, every {{ probe.sea.tp.toFixed(1) }} s</template></small></span></div>
        <div v-if="probe.current"><span><b>{{ probe.current.driftKt.toFixed(1) }} kt</b> current<small>setting {{ compass16(probe.current.setDeg) }}</small></span></div>
        <div v-if="probe.water"><span><b>{{ probe.water.c.toFixed(1) }} °C</b> water</span></div>
        <div v-if="probe.weather?.words || probeVis"><span><b>{{ probe.weather?.words ? cap(probe.weather.words) : 'Visibility' }}</b><small v-if="probeVis">{{ probe.weather?.words ? 'visibility ' : '' }}{{ probeVis.toLowerCase() }}</small></span></div>
        <div v-if="probe.qnhHpa !== null"><span><b>{{ probe.qnhHpa.toFixed(0) }} hPa</b> QNH</span></div>
        <div v-if="probe.weather?.thunder" class="wx-probe-warn"><span><b>⚡ Thunderstorm</b><small>squalls, sudden wind shifts, lightning</small></span></div>
      </div>
      <div v-if="probeSeries.length > 1" class="wx-strip wx-strip-thin" role="list" aria-label="Beaufort force at the point over the run">
        <button v-for="(f, k) in probeSeries" :key="k" role="listitem" :class="{ now: k === seriesIndex }" :style="{ background: BF_COLOURS[f] }" :title="`${frameLabels[k] ?? `Frame ${k + 1}`}: force ${f} at the point`" :aria-label="`Frame ${k + 1}: force ${f} at the point`" @click="emit('seek', k)">{{ probeSeries.length <= 40 ? f : '' }}</button>
      </div>
    </div>
    <p v-else class="wx-probe-hint">Click the map to read any point — wind, waves, current, water, weather and its distance and bearing from the boat. The boat stays where it is; move it or steer for the point from the read-out.</p>

    <div class="wx-top">
      <div class="wx-forecast">
        <span class="wx-small-label">MARINE FORECAST · NEXT {{ forecast && forecast.horizonHours ? `${Math.round(forecast.horizonHours)} H OF THE RUN` : 'FRAMES: NONE WITHIN 24 H' }}</span>
        <p><b>Wind</b> {{ forecast?.text ?? '—' }}</p>
        <p v-if="r.sea"><b>Sea state</b> {{ cap(r.sea.state) }}.</p>
        <p v-if="r.weather.words"><b>Weather</b> {{ cap(r.weather.words) }}.</p>
        <p v-if="visLine"><b>Visibility</b> {{ visLine }}.</p>
      </div>
      <div class="wx-category"><b>F{{ r.wind.beaufort.force }}</b><span>{{ r.warning.label }}<small>{{ r.wind.from16 }} {{ Math.round(r.wind.kt) }} kt · {{ r.wind.beaufort.label }}<template v-if="r.wind.cyclone"> · {{ r.wind.source }}</template></small></span></div>
      <p class="wx-plan" :class="r.plan.level">{{ r.plan.text }}</p>
    </div>

    <div class="wx-grid">
      <div class="wx-card">
        <div class="wx-small-label">THE BOAT</div>
        <div class="wx-boat">
          <svg viewBox="0 0 120 120" class="wx-rose" role="img" :aria-label="`Boat heading ${Math.round(r.boat.headingDeg)}°, true wind from ${Math.round(r.wind.fromDeg)}°, apparent wind ${Math.round(r.boat.aws)} kt`">
            <defs>
              <marker id="wx-tw" viewBox="0 0 6 6" refX="5" refY="3" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0L6 3L0 6z" fill="#6fa8ff"/></marker>
              <marker id="wx-aw" viewBox="0 0 6 6" refX="5" refY="3" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0L6 3L0 6z" fill="#7fe0ff"/></marker>
            </defs>
            <circle cx="60" cy="60" r="55" class="wx-rose-ring"/>
            <text x="60" y="11" class="wx-rose-n">N</text>
            <polygon :points="rose.hull" class="wx-rose-hull"/>
            <line :x1="rose.tw.x0" :y1="rose.tw.y0" :x2="rose.tw.x1" :y2="rose.tw.y1" stroke="#6fa8ff" stroke-width="2.4" marker-end="url(#wx-tw)"/>
            <line :x1="rose.aw.x0" :y1="rose.aw.y0" :x2="rose.aw.x1" :y2="rose.aw.y1" stroke="#7fe0ff" stroke-width="1.7" stroke-dasharray="3 2" marker-end="url(#wx-aw)"/>
          </svg>
          <div class="wx-boat-in">
            <label>Heading <input type="number" min="0" max="359" step="5" :value="Math.round(r.boat.headingDeg)" aria-label="Boat heading in degrees true" @change="set('headingDeg', $event, 0, 360)"> °T</label>
            <label>Boat speed <input type="number" min="0" max="30" step="0.5" :value="r.boat.speedKt" aria-label="Boat speed in knots" @change="set('boatKt', $event, 0, 30)"> kt</label>
            <p><b>{{ cap(r.boat.pointOfSail) }}</b> · {{ r.boat.tack }}</p>
            <p><span class="tw">True wind</span> {{ Math.round(Math.abs(r.boat.twa)) }}° {{ side(r.boat.twa) }}, {{ Math.round(r.wind.kt) }} kt</p>
            <p><span class="aw">Apparent</span> {{ Math.round(Math.abs(r.boat.awa)) }}° {{ side(r.boat.awa) }}, {{ Math.round(r.boat.aws) }} kt</p>
            <p><small>VMG to windward {{ r.boat.vmgKt.toFixed(1) }} kt</small></p>
          </div>
        </div>
      </div>

      <div class="wx-card">
        <div class="wx-small-label">SEA <small>(wind sea estimated from the wind)</small></div>
        <dl v-if="r.sea" class="wx-dl">
          <dt>Waves</dt><dd>{{ r.sea.hs.toFixed(1) }} m, {{ r.sea.state }} <small v-if="r.sea.hs >= 0.05">every {{ r.sea.tp.toFixed(1) }} s, {{ Math.round(r.sea.length) }} m apart</small></dd>
          <template v-if="r.sea.hs >= 0.05"><dt>From</dt><dd>{{ compass16(r.sea.fromDeg) }} ({{ Math.round(r.sea.fromDeg) }}°) <small>{{ r.sea.regime }}<template v-if="r.sea.regime === 'fetch-limited'">, fetch {{ Math.round(r.sea.fetchKm) }} km</template></small></dd></template>
          <template v-if="r.sea.steepness"><dt>Steepness</dt><dd>{{ r.sea.steepness.label }} <small>H/L₀ {{ r.sea.steepness.value.toFixed(3) }}</small></dd></template>
          <template v-if="r.current"><dt>Current</dt><dd>setting {{ compass16(r.current.setDeg) }}, {{ r.current.driftKt.toFixed(1) }} kt <small v-if="r.current.windAgainstCurrent" class="warn">against the waves</small></dd></template>
          <template v-if="r.water"><dt>Water</dt><dd>{{ r.water.c.toFixed(1) }} °C</dd></template>
        </dl>
        <p v-else class="wx-note">No land/sea mask in this run: waves are not estimated.<template v-if="r.water"> Water {{ r.water.c.toFixed(1) }} °C.</template></p>
        <div class="wx-small-label" style="margin-top:14px">BAROMETER</div>
        <p class="wx-line">{{ r.barometer ? `${r.barometer.qnhHpa.toFixed(0)} hPa` : 'not available' }}
          <small v-if="r.barometer?.tendency">{{ r.barometer.tendency.word }} ({{ r.barometer.tendency.hPaPer3h >= 0 ? '+' : '−' }}{{ Math.abs(r.barometer.tendency.hPaPer3h).toFixed(1) }} hPa / 3 h)</small>
          <small v-else-if="r.barometer?.staticField">prescribed field: no tendency</small></p>
      </div>

      <div class="wx-card">
        <div class="wx-small-label">WIND ABOVE THE BOAT · BARBS</div>
        <table v-if="aloftRows.length" class="wx-mini">
          <tbody><tr v-for="row in [...aloftRows].reverse()" :key="row.level.key"><th>{{ row.level.label }}</th><td class="barb-cell"><WindBarb :kt="row.kt" :from-deg="row.fromDeg" :size="30" :northern-hemisphere="northernHemisphere"/></td><td>{{ row.kt < 2.5 ? 'calm' : `${compass16(row.fromDeg)} ${Math.round(row.kt)} kt` }}</td></tr></tbody>
        </table>
        <p v-else class="wx-note"><WindBarb :kt="r.wind.kt" :from-deg="r.wind.fromDeg" :size="34" :northern-hemisphere="northernHemisphere"/> Surface {{ r.wind.from16 }} {{ Math.round(r.wind.kt) }} kt. No pressure field in this run: the wind aloft is not diagnosed.</p>
        <p v-if="aloftRows.length" class="wx-note">The low-level wind shows where squalls and gusts come from; gusts themselves are not modelled.</p>
      </div>

      <div class="wx-card">
        <div class="wx-small-label">HAZARDS</div>
        <ul v-if="r.hazards.length" class="wx-hazards"><li v-for="(h, k) in r.hazards" :key="k" :class="h.level">{{ h.text }}</li></ul>
        <p v-else class="wx-note">None diagnosed at this point.</p>
        <div class="wx-small-label" style="margin-top:14px">YOUR LIMITS</div>
        <div class="wx-limits">
          <label>Reef at <input type="number" min="0" max="60" step="1" :value="r.settings.reefKt" aria-label="Reefing wind in knots" @change="set('reefKt', $event, 0, 60)"> kt</label>
          <label>Small-craft limit <input type="number" min="0" max="60" step="1" :value="r.settings.scaKt" aria-label="Small-craft wind limit in knots" @change="set('scaKt', $event, 0, 60)"> kt</label>
          <label>and seas <input type="number" min="0" max="15" step="0.5" :value="r.settings.scaSeasM" aria-label="Small-craft sea limit in metres" @change="set('scaSeasM', $event, 0, 15)"> m</label>
        </div>
      </div>
    </div>

    <div v-if="series.length > 1" class="wx-strip-block">
      <div class="wx-small-label">BEAUFORT FORCE AT THE BOAT OVER THE RUN · CLICK A FRAME TO JUMP</div>
      <div class="wx-strip" role="list">
        <button v-for="(f, k) in series" :key="k" role="listitem" :class="{ now: k === seriesIndex }" :style="{ background: BF_COLOURS[f] }" :title="`${frameLabels[k] ?? `Frame ${k + 1}`}: force ${f}`" :aria-label="`Frame ${k + 1}: force ${f}`" @click="emit('seek', k)">{{ series.length <= 40 ? f : '' }}</button>
      </div>
    </div>
    <details class="wx-about"><summary>About this report</summary><ul><li v-for="(n, k) in r.notes" :key="k">{{ n }}</li></ul></details>
  </section>
</template>
