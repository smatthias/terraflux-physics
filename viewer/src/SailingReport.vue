<script setup>
// The sailing report over the 3D view: warning level and Beaufort force, a wind diagram of the boat with its true and
// apparent wind, the boat's point of sail, the sea, current and water, weather, visibility and barometer, a sail-plan
// hint against the user's own limits, hazards, and the Beaufort force over the run. Follows the simulation live; the
// numbers come from sailing.js.
import { computed, ref } from 'vue'
import { compass16 } from './station.js'

const props = defineProps({
  report: { type: Object, required: true },
  pinned: { type: Boolean, default: false },
  series: { type: Array, default: () => [] },     // Beaufort force per frame
  seriesIndex: { type: Number, default: 0 },
  frameLabels: { type: Array, default: () => [] },
})
const emit = defineEmits(['close', 'toggle-pin', 'settings', 'seek'])
const r = computed(() => props.report)
const collapsed = ref(false)

// The wind diagram (north up): the hull along its heading, the true wind (blue) and apparent wind (cyan) blowing in
// toward the boat from their directions, each with its length by speed.
const rose = computed(() => {
  const b = r.value.boat, w = r.value.wind, C = 55
  const vec = (deg, len) => [C + Math.sin(deg * Math.PI / 180) * len, C - Math.cos(deg * Math.PI / 180) * len]
  const maxKt = Math.max(8, w.kt, b.aws)
  const arrow = (fromDeg, kt) => { const len = 14 + 30 * Math.min(1, kt / maxKt), [x0, y0] = vec(fromDeg, len + 8), [x1, y1] = vec(fromDeg, 8); return { x0, y0, x1, y1 } }
  const h = b.headingDeg * Math.PI / 180, s = Math.sin(h), c = Math.cos(h), pt = (x, y) => `${(C + x * c - y * s).toFixed(1)},${(C + x * s + y * c).toFixed(1)}`
  return { hull: [pt(0, -14), pt(5, -2), pt(4, 11), pt(-4, 11), pt(-5, -2)].join(' '), tw: arrow(w.fromDeg, w.kt), aw: arrow(b.awdFrom, b.aws) }
})
const num = (e, lo, hi) => { const v = Number(e.target.value); return Number.isFinite(v) && v >= lo && v <= hi ? v : null }
function set(key, e, lo, hi) { const v = num(e, lo, hi); if (v !== null) emit('settings', { [key]: v }) }
const side = a => (Math.abs(a) < 1 ? 'ahead' : Math.abs(a) > 179 ? 'astern' : a > 0 ? 'starboard' : 'port')
const BF_COLOURS = ['#20404f', '#22506a', '#246685', '#2a7fa0', '#3aa3a0', '#5fbf7f', '#a8cf55', '#e0c040', '#f0a030', '#f07a30', '#e8503a', '#d03c6a', '#b040c0']
</script>

<template>
  <section v-if="!r.available" class="station-report sailing-report" aria-label="Sailing report">
    <header class="sr-head">
      <div><span class="sr-eyebrow">⛵ SAILING · SIMULATED · LIVE</span><strong>No sea in this run</strong></div>
      <div class="sr-actions"><button title="Close the sailing report" aria-label="Close the sailing report" @click="emit('close')">✕</button></div>
    </header>
    <p class="sr-none">{{ r.reason }} Load a run with sea (hurricane landfall, the ocean, polar or coast examples).</p>
  </section>
  <section v-else class="station-report sailing-report" aria-label="Sailing report" :style="{ '--cat': r.warning.colour }">
    <header class="sr-head">
      <div>
        <span class="sr-eyebrow">⛵ SAILING · SIMULATED · LIVE</span>
        <strong>{{ r.station.where }} <small>col {{ r.station.x + 1 }}, row {{ r.station.y + 1 }}<template v-if="r.station.snapped"> · nearest sea, {{ r.station.distanceKm.toFixed(0) }} km from the pick</template></small></strong>
        <small class="sr-time">Valid {{ r.time.iso.replace('T', ' ').replace('Z', ' UTC') }}</small>
      </div>
      <div class="sr-actions">
        <button :aria-expanded="!collapsed" :title="collapsed ? 'Show the full report' : 'Collapse to the key line'" @click="collapsed = !collapsed">{{ collapsed ? '▸' : '▾' }}</button>
        <button :class="{ on: pinned }" :aria-pressed="pinned" :title="pinned ? 'Unpin: follow the picked point' : 'Pin this position (picking other points will not move it)'" @click="emit('toggle-pin')">📌</button>
        <button title="Close the sailing report" aria-label="Close the sailing report" @click="emit('close')">✕</button>
      </div>
    </header>

    <div class="sr-category">
      <b>F{{ r.wind.beaufort.force }}</b>
      <span>{{ r.warning.label }}<small>{{ r.wind.from16 }} {{ Math.round(r.wind.kt) }} kt · {{ r.wind.beaufort.label }}<template v-if="r.wind.source !== 'engine wind field'"> · {{ r.wind.source }}</template></small></span>
    </div>

    <div v-show="!collapsed" class="sr-body">
    <div class="sr-boat">
      <svg viewBox="0 0 110 110" class="sr-rose" role="img" :aria-label="`Boat heading ${Math.round(r.boat.headingDeg)}°, true wind from ${Math.round(r.wind.fromDeg)}°, apparent wind ${Math.round(r.boat.aws)} kt from ${Math.round(Math.abs(r.boat.awa))}° ${side(r.boat.awa)}`">
        <defs>
          <marker id="sr-tw" viewBox="0 0 6 6" refX="5" refY="3" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0L6 3L0 6z" fill="#6fa8ff"/></marker>
          <marker id="sr-aw" viewBox="0 0 6 6" refX="5" refY="3" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0L6 3L0 6z" fill="#7fe0ff"/></marker>
        </defs>
        <circle cx="55" cy="55" r="50" class="sr-rose-ring"/>
        <text x="55" y="10" class="sr-rose-n">N</text>
        <polygon :points="rose.hull" class="sr-rose-hull"/>
        <line :x1="rose.tw.x0" :y1="rose.tw.y0" :x2="rose.tw.x1" :y2="rose.tw.y1" stroke="#6fa8ff" stroke-width="2.2" marker-end="url(#sr-tw)"/>
        <line :x1="rose.aw.x0" :y1="rose.aw.y0" :x2="rose.aw.x1" :y2="rose.aw.y1" stroke="#7fe0ff" stroke-width="1.6" stroke-dasharray="3 2" marker-end="url(#sr-aw)"/>
      </svg>
      <div class="sr-boat-in">
        <label>Heading <input type="number" min="0" max="359" step="5" :value="Math.round(r.boat.headingDeg)" aria-label="Boat heading in degrees true" @change="set('headingDeg', $event, 0, 360)">°T</label>
        <label>Boat speed <input type="number" min="0" max="30" step="0.5" :value="r.boat.speedKt" aria-label="Boat speed in knots" @change="set('boatKt', $event, 0, 30)"> kt</label>
        <p><b>{{ r.boat.pointOfSail }}</b> · {{ r.boat.tack }}</p>
        <p><span class="tw">True</span> {{ Math.round(Math.abs(r.boat.twa)) }}° {{ side(r.boat.twa) }}, {{ Math.round(r.wind.kt) }} kt</p>
        <p><span class="aw">Apparent</span> {{ Math.round(Math.abs(r.boat.awa)) }}° {{ side(r.boat.awa) }}, {{ Math.round(r.boat.aws) }} kt</p>
        <p><small>VMG to windward {{ r.boat.vmgKt.toFixed(1) }} kt</small></p>
      </div>
    </div>

    <p class="sr-plan" :class="r.plan.level">{{ r.plan.text }}</p>

    <div class="sr-block">
      <div class="sr-label">SEA <small>(wind sea estimated from the wind)</small></div>
      <dl v-if="r.sea" class="sr-grid compact">
        <dt>Waves</dt><dd>{{ r.sea.hs.toFixed(1) }} m, {{ r.sea.state }} <small v-if="r.sea.hs >= 0.05">every {{ r.sea.tp.toFixed(1) }} s, {{ Math.round(r.sea.length) }} m apart</small></dd>
        <dt v-if="r.sea.hs >= 0.05">From</dt><dd v-if="r.sea.hs >= 0.05">{{ compass16(r.sea.fromDeg) }} ({{ Math.round(r.sea.fromDeg) }}°) <small>{{ r.sea.regime }}<template v-if="r.sea.regime === 'fetch-limited'">, fetch {{ Math.round(r.sea.fetchKm) }} km</template></small></dd>
        <dt v-if="r.sea.steepness">Steepness</dt><dd v-if="r.sea.steepness">{{ r.sea.steepness.label }} <small>H/L₀ {{ r.sea.steepness.value.toFixed(3) }}</small></dd>
        <template v-if="r.current"><dt>Current</dt><dd>setting {{ compass16(r.current.setDeg) }}, {{ r.current.driftKt.toFixed(1) }} kt <small v-if="r.current.windAgainstCurrent" class="warn">against the waves</small></dd></template>
        <template v-if="r.water"><dt>Water</dt><dd>{{ r.water.c.toFixed(1) }} °C</dd></template>
      </dl>
      <p v-else class="sr-none">No land/sea mask in this run: waves are not estimated.<template v-if="r.water"> Water {{ r.water.c.toFixed(1) }} °C.</template></p>
    </div>

    <div class="sr-block">
      <div class="sr-label">WEATHER · VISIBILITY · BAROMETER</div>
      <dl class="sr-grid compact">
        <dt>Weather</dt><dd>{{ r.weather.words ?? 'not modelled in this run' }}</dd>
        <dt>Visibility</dt><dd>{{ r.visibility ? r.visibility.word : 'not available' }} <small v-if="r.visibility">{{ r.visibility.metres >= 10000 ? '> 5 NM' : r.visibility.metres < 1000 ? `${Math.round(r.visibility.metres / 50) * 50} m` : `${r.visibility.nm.toFixed(1)} NM` }} · estimated</small></dd>
        <dt>Barometer</dt><dd>{{ r.barometer ? `${r.barometer.qnhHpa.toFixed(0)} hPa` : 'not available' }} <small v-if="r.barometer?.tendency">{{ r.barometer.tendency.word }} ({{ r.barometer.tendency.hPaPer3h >= 0 ? '+' : '−' }}{{ Math.abs(r.barometer.tendency.hPaPer3h).toFixed(1) }} hPa / 3 h)</small><small v-else-if="r.barometer?.staticField">prescribed, no tendency</small></dd>
      </dl>
    </div>

    <div class="sr-block">
      <div class="sr-label">HAZARDS</div>
      <ul v-if="r.hazards.length" class="sr-hazards"><li v-for="(h, k) in r.hazards" :key="k" :class="h.level">{{ h.text }}</li></ul>
      <p v-else class="sr-none">None diagnosed at this point.</p>
    </div>

    <div v-if="series.length > 1" class="sr-block">
      <div class="sr-label">BEAUFORT OVER THE RUN · CLICK TO JUMP</div>
      <div class="sr-strip" role="list">
        <button v-for="(f, k) in series" :key="k" role="listitem" :class="{ now: k === seriesIndex }" :style="{ background: BF_COLOURS[f] }" :title="`${frameLabels[k] ?? `Frame ${k + 1}`}: force ${f}`" :aria-label="`Frame ${k + 1}: force ${f}`" @click="emit('seek', k)">{{ series.length <= 30 ? f : '' }}</button>
      </div>
    </div>

    <details class="sr-notes">
      <summary>Your limits and about this report</summary>
      <div class="sr-limits">
        <label>Reef at <input type="number" min="0" max="60" step="1" :value="r.settings.reefKt" aria-label="Reefing wind in knots" @change="set('reefKt', $event, 0, 60)"> kt</label>
        <label>Small-craft limit <input type="number" min="0" max="60" step="1" :value="r.settings.scaKt" aria-label="Small-craft wind limit in knots" @change="set('scaKt', $event, 0, 60)"> kt</label>
        <label>and seas <input type="number" min="0" max="15" step="0.5" :value="r.settings.scaSeasM" aria-label="Small-craft sea limit in metres" @change="set('scaSeasM', $event, 0, 15)"> m</label>
      </div>
      <ul><li v-for="(n, k) in r.notes" :key="k">{{ n }}</li></ul>
    </details>
    </div>
  </section>
</template>
