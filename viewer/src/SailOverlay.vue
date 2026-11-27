<script setup>
// The sail-through overlay: a window onto the 3D scene from behind (or beside) the boat - the scene draws the picture
// into this rectangle - with a sailing display over it: time into the passage and UTC, speed through the water and
// over the ground, heading and course over the ground, true and apparent wind, point of sail and tack, the sails and
// the heel, the sea, and the next tack or gybe (played in real time when it comes). A strip of the passage coloured by
// point of sail, with the manoeuvres, scrubs through it. The weather is the run's frame of the time sailed.
import { computed } from 'vue'
import WindBarb from './WindBarb.vue'
import { POINT_OF_SAIL_COLOURS } from './sailRoute.js'

const props = defineProps({
  route: { type: Object, required: true },            // sailRoute(...)
  state: { type: Object, required: true },            // sailStateAt(route, hours)
  rect: { type: Object, required: true },
  playing: { type: Boolean, default: false },
  speed: { type: Number, default: 600 },
  slowTurns: { type: Boolean, default: true },
  mode: { type: String, default: 'chase' },
  frameLabel: { type: String, default: '' },
  northernHemisphere: { type: Boolean, default: true },
})
const emit = defineEmits(['toggle-play', 'restart', 'scrub', 'speed', 'slow-turns', 'mode', 'close'])
const s = computed(() => props.state), r = computed(() => props.route)
const deg3 = d => String(Math.round(((d % 360) + 360) % 360) || 360).padStart(3, '0')
const hm = h => { const m = Math.round(h * 60); return `${Math.floor(m / 60)}:${String(m % 60).padStart(2, '0')}` }
const utc = ms => { const d = new Date(ms); return `${['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d.getUTCDay()]} ${String(d.getUTCHours()).padStart(2, '0')}:${String(d.getUTCMinutes()).padStart(2, '0')}Z` }
const side = a => (a >= 0 ? 'stbd' : 'port')
const strip = computed(() => {
  const H = r.value.hours || 1, X = ms => (ms - r.value.departMs) / 3.6e6 / H * 100
  return { bands: r.value.track.map(t => ({ x: X(t.ms), w: Math.max(0.2, X(t.endMs) - X(t.ms)), colour: POINT_OF_SAIL_COLOURS[t.pointOfSail] ?? '#e8f6ff', key: t.k })), marks: r.value.manoeuvres.map((m, k) => ({ x: X(m.ms), kind: m.kind, key: k })), x: s.value.hours / H * 100 }
})
function scrubStrip(e) { const b = e.currentTarget.getBoundingClientRect(); emit('scrub', Math.max(0, Math.min(1, (e.clientX - b.left) / b.width)) * r.value.hours) }
const SPEEDS = [60, 300, 600, 1800, 3600]
const PHASES = { approach: 'PREPARE', turning: 'TURNING', 'head to wind': 'HEAD TO WIND', 'stern to wind': 'BOOM CROSSING', accelerating: 'ACCELERATING', steady: 'SETTLED' }
</script>

<template>
  <div class="fly sail-fly" :style="{ left: `${rect.x}px`, top: `${rect.y}px`, width: `${rect.w}px`, height: `${rect.h}px` }" role="region" aria-label="Sail-through of the planned route">
    <div class="fly-top">
      <b>⛵ {{ r.marks[0].name }} → {{ r.marks.at(-1).name }}</b>
      <span class="fly-phase" :style="{ borderColor: POINT_OF_SAIL_COLOURS[s.pointOfSail], color: POINT_OF_SAIL_COLOURS[s.pointOfSail] }">{{ s.pointOfSail.toUpperCase() }}{{ s.motoring ? '' : ` · ${s.tack.toUpperCase()}` }}</span>
      <span class="fly-gafor" :style="{ background: s.warning.colour }" :title="s.warning.label">F{{ s.beaufort.force }}</span>
      <span class="fly-spacer"></span>
      <span class="fly-seg"><button :class="{ on: mode === 'chase' }" @click="emit('mode', 'chase')">Astern</button><button :class="{ on: mode === 'side' }" @click="emit('mode', 'side')">Abeam</button></span>
      <button class="fly-x" title="Close the sail-through" aria-label="Close the sail-through" @click="emit('close')">✕</button>
    </div>

    <div class="fly-left">
      <div><small>STW / SOG</small><b>{{ s.stwKt.toFixed(1) }}</b><em>/ {{ s.sogKt.toFixed(1) }} kt</em></div>
      <div><small>HDG / COG</small><b>{{ deg3(s.headingDeg) }}°</b><em>/ {{ deg3(s.cogDeg) }}°</em></div>
      <div><small>SAILS</small><b class="small">{{ s.motoring ? 'engine' : s.sailShort }}</b><em>heel {{ Math.round(Math.abs(s.heelDeg)) }}° {{ s.heelDeg >= 0 ? 'stbd' : 'port' }}</em></div>
      <div v-if="s.hs > 0.05"><small>SEA</small><b>{{ s.hs.toFixed(1) }} m</b></div>
      <div v-if="s.current && s.current.driftKt >= 0.05"><small>CURRENT</small><b>{{ s.current.driftKt.toFixed(1) }} kt</b><em>set {{ deg3(s.current.setDeg) }}° · {{ ({ ocean: 'model', table: 'table', drift: 'wind drift' })[s.current.source] }}</em></div>
    </div>

    <div class="fly-right">
      <WindBarb :kt="s.tws" :from-deg="s.twdFrom" :size="46" :northern-hemisphere="northernHemisphere"/>
      <div><small>TRUE</small><b>{{ Math.round(s.tws) }} kt</b><em>{{ Math.round(Math.abs(s.twa)) }}° {{ side(s.twa) }}</em></div>
      <div><small>APPARENT</small><b>{{ Math.round(s.aws) }} kt</b><em>{{ Math.round(Math.abs(s.awa)) }}° {{ side(s.awa) }}</em></div>
    </div>

    <div v-if="s.manoeuvre || s.next" class="fly-alert" :class="s.manoeuvre ? 'warning' : 'ahead'">
      <template v-if="s.manoeuvre">{{ s.manoeuvre.kind === 'tack' ? '⤺ TACKING' : '⤻ GYBING' }} · {{ PHASES[s.manoeuvre.phase] ?? s.manoeuvre.phase }} · {{ s.manoeuvre.t.toFixed(0) }} / {{ s.manoeuvre.duration.toFixed(0) }} s{{ slowTurns ? ' · real time' : '' }}</template>
      <template v-else>Next: {{ s.next.kind }} in {{ s.next.inMinutes >= 90 ? hm(s.next.inMinutes / 60) : `${Math.round(s.next.inMinutes)} min` }}</template>
    </div>

    <div class="fly-bottom">
      <div class="fly-controls">
        <button class="fly-play" :title="playing ? 'Pause' : 'Sail'" @click="emit('toggle-play')">{{ playing ? '❚❚' : '▶' }}</button>
        <button title="Back to the start" @click="emit('restart')">⏮</button>
        <select :value="speed" aria-label="Simulation speed" @change="emit('speed', Number($event.target.value))"><option v-for="v in SPEEDS" :key="v" :value="v">×{{ v }}</option></select>
        <label class="sail-fly-slow"><input type="checkbox" :checked="slowTurns" @change="emit('slow-turns', $event.target.checked)"> turns in real time</label>
        <span class="fly-time"><b>T+{{ hm(s.hours) }}</b> / {{ hm(r.hours) }} · {{ utc(s.ms) }}</span>
        <span class="fly-frame" title="The weather shown is this frame of the run">weather {{ frameLabel }}</span>
      </div>
      <div class="sail-fly-strip" role="img" aria-label="The passage by point of sail: click to jump" @click="scrubStrip">
        <i v-for="b in strip.bands" :key="b.key" :style="{ left: `${b.x}%`, width: `${b.w}%`, background: b.colour }"></i>
        <b v-for="m in strip.marks" :key="`m${m.key}`" :class="m.kind" :style="{ left: `${m.x}%` }">{{ m.kind === 'tack' ? 'T' : 'G' }}</b>
        <span class="sail-fly-now" :style="{ left: `${strip.x}%` }"></span>
      </div>
      <input type="range" min="0" :max="r.hours" step="0.0001" :value="s.hours" aria-label="Time into the passage" @input="emit('scrub', Number($event.target.value))">
    </div>
  </div>
</template>
