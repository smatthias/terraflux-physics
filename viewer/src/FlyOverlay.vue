<script setup>
// The fly-through overlay: a window onto the 3D scene from the aircraft (chase or cockpit view; the scene itself draws
// the picture into this rectangle) with a flight display over it. Time into the flight, UTC, altitude and vertical
// speed, heading against track (the wind correction), ground and true airspeed, wind barb and components, outside air
// temperature, the hazard now and the next one ahead, and a mini profile of the route to scrub through the flight.
// The weather it shows is the weather of the time flown (the parent steps the run's frames with the flight).
import { computed } from 'vue'
import WindBarb from './WindBarb.vue'
import { HAZARD_INFO } from './flightPlan.js'
import { GAFOR, UNKNOWN_GAFOR } from './gafor.js'

const props = defineProps({
  plan: { type: Object, required: true },
  state: { type: Object, required: true },            // planStateAt(plan, hours)
  rect: { type: Object, required: true },             // { x, y, w, h } in px within the viewport
  playing: { type: Boolean, default: false },
  speed: { type: Number, default: 300 },              // seconds of flight per second
  mode: { type: String, default: 'chase' },
  frameLabel: { type: String, default: '' },
  northernHemisphere: { type: Boolean, default: true },
})
const emit = defineEmits(['toggle-play', 'restart', 'scrub', 'speed', 'mode', 'close'])
const s = computed(() => props.state), p = computed(() => props.plan)
const dir3 = d => String(Math.round(d / 10) * 10 || 360).padStart(3, '0')   // winds: to 10 degrees, as reported
const deg3 = d => String(Math.round(((d % 360) + 360) % 360) || 360).padStart(3, '0')   // headings and courses: to the degree
const hm = h => { const m = Math.round(h * 60); return `${Math.floor(m / 60)}:${String(m % 60).padStart(2, '0')}` }
const utc = ms => { const d = new Date(ms); return `${String(d.getUTCHours()).padStart(2, '0')}:${String(d.getUTCMinutes()).padStart(2, '0')}Z` }
const alt = ft => `${(Math.round(ft / 10) * 10).toLocaleString('en-US')}`
// Mini profile: distance across, altitude up, with the hazard stretches and the aircraft.
const MW = 300, MH = 44
const mini = computed(() => {
  const L = p.value.lengthNm || 1, top = Math.max(p.value.cruiseFt, ...p.value.profile.map(q => q.terrainFt)) * 1.15 || 1
  const X = d => d / L * MW, Y = ft => MH - 4 - ft / top * (MH - 10)
  return {
    path: [`0,${Y(p.value.from.elevationFt)}`, ...p.value.profile.map(q => `${X(q.distNm).toFixed(1)},${Y(q.altFt).toFixed(1)}`), `${MW},${Y(p.value.to.elevationFt)}`].join(' '),
    terrain: [`0,${MH}`, ...p.value.profile.map(q => `${X(q.distNm).toFixed(1)},${Y(q.terrainFt).toFixed(1)}`), `${MW},${MH}`].join(' '),
    ranges: p.value.ranges.filter(r => r.kind !== 'cloud').map(r => ({ x: X(r.fromNm), w: Math.max(2, X(r.toNm) - X(r.fromNm)), colour: HAZARD_INFO[r.kind].colour, key: `${r.kind}${r.fromNm}` })),
    x: X(s.value.distNm), y: Y(s.value.altFt),
  }
})
function scrubMini(e) { const r = e.currentTarget.getBoundingClientRect(); emit('scrub', Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * p.value.hours) }
const SPEEDS = [60, 300, 900, 1800]
</script>

<template>
  <div class="fly" :style="{ left: `${rect.x}px`, top: `${rect.y}px`, width: `${rect.w}px`, height: `${rect.h}px` }" role="region" aria-label="Fly-through of the planned route">
    <div class="fly-top">
      <b>✈ {{ p.from.id }} → {{ p.to.id }}</b>
      <span class="fly-phase" :class="s.phase">{{ s.phase.toUpperCase() }}</span>
      <span v-if="s.inCloud" class="fly-imc">IMC</span>
      <span class="fly-gafor" :style="{ background: (GAFOR[s.gafor] ?? UNKNOWN_GAFOR).colour }" :title="`GAFOR here: ${(GAFOR[s.gafor] ?? UNKNOWN_GAFOR).name}`">{{ s.gafor }}</span>
      <span class="fly-spacer"></span>
      <span class="fly-seg"><button :class="{ on: mode === 'chase' }" @click="emit('mode', 'chase')">Chase</button><button :class="{ on: mode === 'cockpit' }" @click="emit('mode', 'cockpit')">Cockpit</button></span>
      <button class="fly-x" title="Close the fly-through" aria-label="Close the fly-through" @click="emit('close')">✕</button>
    </div>

    <div class="fly-left">
      <div><small>ALT</small><b>{{ alt(s.altFt) }}</b><em>ft {{ s.verticalFpm > 50 ? `↑ ${Math.round(s.verticalFpm)}` : s.verticalFpm < -50 ? `↓ ${Math.round(-s.verticalFpm)}` : '' }}{{ Math.abs(s.verticalFpm) > 50 ? ' fpm' : '' }}</em></div>
      <div><small>GS / TAS</small><b>{{ Math.round(s.gsKt) }}</b><em>/ {{ s.tasKt }} kt</em></div>
      <div><small>HDG / TRK</small><b>{{ deg3(s.headingDeg) }}°</b><em>/ {{ deg3(s.tcDeg) }}° · {{ s.wcaDeg >= 0 ? 'R' : 'L' }}{{ Math.round(Math.abs(s.wcaDeg)) }}°</em></div>
      <div><small>OAT</small><b>{{ Math.round(s.tempC) }}°</b><em>ISA{{ s.isaDevC >= 0 ? '+' : '−' }}{{ Math.round(Math.abs(s.isaDevC)) }}</em></div>
    </div>

    <div class="fly-right">
      <WindBarb :kt="s.wind.kt" :from-deg="s.wind.fromDeg" :size="46" :northern-hemisphere="northernHemisphere"/>
      <div><small>WIND</small><b>{{ dir3(s.wind.fromDeg) }}/{{ Math.round(s.wind.kt) }}</b><em :class="s.headKt > 0 ? 'warn' : 'good'">{{ s.headKt > 0 ? `head ${Math.round(s.headKt)}` : `tail ${Math.round(-s.headKt)}` }} · cross {{ Math.round(Math.abs(s.crossKt)) }}</em></div>
    </div>

    <div v-if="s.hazards.length || s.ahead" class="fly-alert" :class="s.hazards.length ? HAZARD_INFO[s.hazards[0]].level : 'ahead'">
      <template v-if="s.hazards.length">IN {{ s.hazards.map(h => HAZARD_INFO[h].label).join(' · ') }}</template>
      <template v-else>⚠ {{ HAZARD_INFO[s.ahead.kind].label }} in {{ Math.round(s.ahead.inNm) }} NM · {{ Math.max(0, Math.round(s.ahead.inMinutes)) }} min</template>
    </div>

    <div class="fly-bottom">
      <div class="fly-controls">
        <button class="fly-play" :title="playing ? 'Pause' : 'Fly'" @click="emit('toggle-play')">{{ playing ? '❚❚' : '▶' }}</button>
        <button title="Back to the departure" @click="emit('restart')">⏮</button>
        <select :value="speed" aria-label="Simulation speed" @change="emit('speed', Number($event.target.value))"><option v-for="v in SPEEDS" :key="v" :value="v">×{{ v }}</option></select>
        <span class="fly-time"><b>T+{{ hm(s.hours) }}</b> / {{ hm(p.hours) }} · {{ utc(s.ms) }} · {{ Math.round(s.distNm) }} / {{ Math.round(p.lengthNm) }} NM</span>
        <span class="fly-frame" :title="'The weather shown is this frame of the run'">weather {{ frameLabel }}</span>
      </div>
      <svg class="fly-mini" :viewBox="`0 0 ${MW} ${MH}`" preserveAspectRatio="none" @click="scrubMini" role="img" aria-label="Route profile: click to jump">
        <rect v-for="r in mini.ranges" :key="r.key" :x="r.x" :width="r.w" y="0" height="4" :fill="r.colour"/>
        <polygon :points="mini.terrain" fill="#3d4c37"/>
        <polyline :points="mini.path" fill="none" stroke="#e8f1f4" stroke-width="1.3" vector-effect="non-scaling-stroke"/>
        <rect :x="0" y="0" :width="mini.x" :height="MH" fill="rgba(127,224,255,0.08)"/>
        <circle :cx="mini.x" :cy="mini.y" r="3.2" fill="#ffd166" stroke="#09151d"/>
      </svg>
      <input type="range" min="0" :max="p.hours" step="0.001" :value="s.hours" aria-label="Time into the flight" @input="emit('scrub', Number($event.target.value))">
    </div>
  </div>
</template>
