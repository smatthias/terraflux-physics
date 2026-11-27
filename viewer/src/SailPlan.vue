<script setup>
// Sailing route planning by the forecast in the sailing view: start (the boat or coordinates), waypoints and
// destination; weather routing by isochrones or the direct route; the passage through the run's wind, waves and
// currents with the sails, tacks and gybes; the weather along the route; departure planning; and the sail-through.
// Numbers from sailRoute.js; this component lays them out.
import { computed, ref } from 'vue'
import WxStrip from './WxStrip.vue'
import WeatherIcon from './WeatherIcon.vue'
import { parseCoordinate, formatDM } from './geo.js'
import { compass16 } from './station.js'
import { BOATS } from './polars.js'
import { ROUTE_MODES, POINT_OF_SAIL_COLOURS } from './sailRoute.js'
import { BEAUFORT_COLOURS } from './sailing.js'

const props = defineProps({
  route: { type: Object, default: null },            // sailRoute(...) or { ok: false, reason }
  busy: { type: Boolean, default: false },
  settings: { type: Object, required: true },        // route settings (mode, limits, motor, departure, route)
  sailSettings: { type: Object, required: true },    // boat, reefKt, gennaker
  frameTimes: { type: Array, default: () => [] },
  frameIndex: { type: Number, default: 0 },
  bounds: { type: Object, default: null },
  picking: { type: Boolean, default: false },
  scan: { type: Object, default: null },             // { rows, best, total }
  scanBusy: { type: Boolean, default: false },
  boatWhere: { type: String, default: '' },
  flying: { type: Boolean, default: false },
  northernHemisphere: { type: Boolean, default: true },
})
const emit = defineEmits(['settings', 'sail-settings', 'seek', 'picking', 'scan', 'sail'])
const r = computed(() => (props.route?.ok ? props.route : null))
const WD = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']
const utc = ms => { const d = new Date(ms); return `${String(d.getUTCHours()).padStart(2, '0')}:${String(d.getUTCMinutes()).padStart(2, '0')}Z` }
const when = ms => `${WD[new Date(ms).getUTCDay()]} ${utc(ms)}`
const hm = h => { if (!Number.isFinite(h)) return '—'; const m = Math.round(h * 60); return `${Math.floor(m / 60)}:${String(m % 60).padStart(2, '0')}` }
const deg3 = d => String(Math.round(((d % 360) + 360) % 360) || 360).padStart(3, '0')
const set = (key, value) => emit('settings', { [key]: value })
const num = (key, e, lo, hi) => { const v = Number(e.target.value); if (Number.isFinite(v) && v >= lo && v <= hi) set(key, v) }
// ---- The route editor -------------------------------------------------------------------------------------------------
const list = computed(() => props.settings.route ?? [])
const setRoute = rt => set('route', rt)
const role = k => (k === 0 ? 'START' : k === list.value.length - 1 ? 'DEST' : `WP${k}`)
const resolved = computed(() => list.value.map(it => {
  if (it.kind === 'boat') return { ok: true, text: `the boat · ${props.boatWhere}` }
  try { const q = parseCoordinate(it.text); const inside = !props.bounds || (q.lat >= props.bounds.south && q.lat <= props.bounds.north && q.lon >= props.bounds.west && q.lon <= props.bounds.east); return { ok: true, text: formatDM(q), inside } } catch (err) { return { ok: false, text: err.message } }
}))
const setRow = (k, patch) => { const rt = [...list.value]; rt[k] = { ...rt[k], ...patch }; setRoute(rt) }
const setKind = (k, kind) => setRow(k, kind === 'boat' ? { kind: 'boat' } : { kind: 'coord', name: role(k) === 'DEST' ? 'Destination' : role(k), text: '' })
const move = (k, d) => { const rt = [...list.value], j = k + d; if (j < 0 || j >= rt.length) return; [rt[k], rt[j]] = [rt[j], rt[k]]; setRoute(rt) }
const remove = k => { const rt = [...list.value]; rt.splice(k, 1); setRoute(rt.length >= 2 ? rt : null) }
const addWaypoint = () => { const rt = [...list.value]; rt.splice(Math.max(1, rt.length - 1), 0, { kind: 'coord', name: `WP${rt.length - 1}`, text: '' }); setRoute(rt) }
const reverse = () => setRoute([...list.value].reverse())
const typed = ref('')
function startRoute() { let q; try { q = parseCoordinate(typed.value) } catch { return } setRoute([{ kind: 'boat' }, { kind: 'coord', name: 'Destination', text: `${q.lat.toFixed(4)}, ${q.lon.toFixed(4)}` }]); if (props.settings.departure === null) set('departure', String(props.frameIndex)) }
// ---- Summary ----------------------------------------------------------------------------------------------------------
const verdict = computed(() => {
  if (!r.value) return null
  const c = r.value.checks
  if (c.some(x => x.level === 'severe')) return { level: 'severe', text: 'Not advisable' }
  if (c.some(x => x.level === 'warning')) return { level: 'warning', text: 'Beyond your limits' }
  return { level: 'ok', text: 'Within your limits' }
})
// ---- Weather along the route (over time) ------------------------------------------------------------------------------
const hs = h => (h < 0.1 ? '#1d4660' : h < 0.5 ? '#2a7fa0' : h < 1.25 ? '#3aa3a0' : h < 2.5 ? '#e0c040' : h < 4 ? '#f07a30' : '#d03c6a')
const SAIL_COLOURS = r2 => (!r2 ? '#b0b8bf' : !r2.sailing ? '#e040fb' : r2.main.kind === 'trysail' ? '#ff5252' : r2.headsail.kind === 'gennaker' ? '#4a9dff' : ['#3ddc84', '#ffd166', '#ff9f43', '#ff6b6b'][r2.reef])
const strip = computed(() => {
  if (!r.value) return null
  const tr = r.value.track, t0 = r.value.departMs, H = ms => (ms - t0) / 3.6e6
  const band = (x, colour, title, extra = {}) => ({ from: H(x.ms), to: H(x.endMs), colour, title: `${when(x.ms)}: ${title}`, data: x, ...extra })
  const marks = (every, make) => { const out = [], step = Math.max(1, Math.ceil(tr.length / every)); for (let k = Math.floor(step / 2); k < tr.length; k += step) out.push(make(tr[k])); return out }
  const rows = [
    { label: 'Weather', tall: true, bands: tr.map(x => band(x, 'rgba(127,224,255,0.04)', x.weather ?? '—')), marks: r.value.stretches.map(s => ({ at: H((s.fromMs + s.toMs) / 2), icon: s.symbol, night: s.night, title: s.weather })) },
    { label: 'Beaufort force', bands: tr.map(x => band(x, BEAUFORT_COLOURS[x.beaufort.force], `F${x.beaufort.force}, ${Math.round(x.tws)} kt from ${compass16(x.twdFrom)}`, { text: '' })) },
    { label: 'True wind', tall: true, bands: tr.map(x => band(x, 'rgba(127,224,255,0.04)', `${Math.round(x.tws)} kt from ${deg3(x.twdFrom)}°`)), marks: marks(16, x => ({ at: H(x.ms), barb: { kt: x.tws, fromDeg: x.twdFrom }, title: `${Math.round(x.tws)} kt from ${compass16(x.twdFrom)}` })) },
    { label: 'Point of sail', bands: tr.map(x => band(x, POINT_OF_SAIL_COLOURS[x.pointOfSail] ?? '#e8f6ff', `${x.pointOfSail}${x.motoring ? '' : `, ${x.tack}, TWA ${Math.round(Math.abs(x.twa))}°`}`)) },
    { label: 'Sails', bands: tr.map(x => band(x, SAIL_COLOURS(x.sails), x.sailShort)) },
    { label: 'Boat speed', bands: tr.map(x => band(x, `rgba(61,220,132,${Math.min(0.9, 0.15 + x.sogKt / 10)})`, `${x.stwKt.toFixed(1)} kt through the water, ${x.sogKt.toFixed(1)} kt over the ground`)), marks: marks(10, x => ({ at: H(x.ms), text: x.sogKt.toFixed(1) })) },
    { label: 'Waves', bands: tr.map(x => band(x, hs(x.hs), `${x.hs.toFixed(1)} m${x.wavesFromDeg !== null ? ` from ${compass16(x.wavesFromDeg)}` : ''}`)) },
  ]
  if (tr.some(x => x.current)) rows.push({ label: 'Current', bands: tr.filter(x => x.current).map(x => band(x, `rgba(127,224,255,${Math.min(0.9, 0.1 + x.current.driftKt)})`, `setting ${compass16(x.current.setDeg)} ${x.current.driftKt.toFixed(1)} kt (${({ ocean: 'ocean model', table: 'current table', drift: 'wind drift' })[x.current.source] ?? x.current.source})`)) })
  rows.push({ label: 'Tacks / gybes', bands: r.value.manoeuvres.map(m => ({ from: H(m.ms) - r.value.hours / 300, to: H(m.ms) + r.value.hours / 300, colour: m.kind === 'tack' ? '#ff6b6b' : '#4a9dff', title: `${when(m.ms)}: ${m.kind} ${deg3(m.fromHeadingDeg)}° → ${deg3(m.toHeadingDeg)}°`, data: { frameIndex: r.value.track[m.index].frameIndex } })) })
  const every = [1, 2, 3, 6, 12, 24].find(v => r.value.hours / v <= 8) ?? 48, ticks = []
  for (let h = 0; h <= r.value.hours + 1e-6; h += every) ticks.push({ at: h, label: when(t0 + h * 3.6e6) })
  return { rows, ticks, span: [0, r.value.hours] }
})
const cursor = computed(() => (r.value && props.frameTimes[props.frameIndex] !== undefined ? (props.frameTimes[props.frameIndex] - r.value.departMs) / 3.6e6 : null))
// ---- Departure planning ---------------------------------------------------------------------------------------------
// Bars from the quickest (short) to the slowest (full) passage of the scan.
const scanRange = computed(() => { const h = (props.scan?.rows ?? []).filter(x => x.ok).map(x => x.hours); return h.length ? [Math.min(...h), Math.max(...h)] : [0, 1] })
const barWidth = h => `${15 + 85 * (h - scanRange.value[0]) / ((scanRange.value[1] - scanRange.value[0]) || 1)}%`
</script>

<template>
  <section class="comparison wx-report sail-plan" aria-label="Sailing route planning">
    <div class="wx-head">
      <div>
        <div class="section-label">SAILING ROUTE · PLANNED BY THE FORECAST · SIMULATED WEATHER · NOT FOR NAVIGATION</div>
        <h3 v-if="r">{{ r.marks.map(m => m.name).join(' → ') }} <small>{{ ROUTE_MODES[r.mode] }} · {{ BOATS[sailSettings.boat]?.label }} · departs {{ when(r.departMs) }}</small></h3>
        <h3 v-else>Plan a passage <small>from the boat, through the run's wind, waves and currents</small></h3>
      </div>
      <div class="fp-head-actions">
        <span v-if="busy" class="sp-busy">routing…</span>
        <button v-if="r" class="fp-fly" :disabled="flying" @click="emit('sail', 0)">{{ flying ? '⛵ Sailing…' : '⛵ Sail the route in 3D' }}</button>
        <div v-if="verdict" class="wx-verdict" :class="verdict.level">{{ verdict.text }}</div>
      </div>
    </div>

    <div class="fp-form">
      <label>Routing <select :value="settings.mode" aria-label="Routing" @change="set('mode', $event.target.value)"><option v-for="(label, k) in ROUTE_MODES" :key="k" :value="k">{{ label }}</option></select></label>
      <label>Boat <select :value="sailSettings.boat" aria-label="Boat" @change="emit('sail-settings', { boat: $event.target.value })"><option v-for="b in BOATS" :key="b.key" :value="b.key">{{ b.label }}</option></select></label>
      <label>Departure <select :value="settings.departure ?? String(frameIndex)" aria-label="Departure time" @change="set('departure', $event.target.value)"><option v-for="(ms, k) in frameTimes" :key="k" :value="String(k)">{{ when(ms) }} ({{ new Date(ms).toISOString().slice(5, 10) }})</option></select></label>
      <label title="Routing avoids winds and seas above these (and the report flags them)"><input type="checkbox" :checked="settings.avoidLimits" @change="set('avoidLimits', $event.target.checked)"> Avoid wind above <input type="number" min="5" max="60" step="1" :value="settings.maxWindKt" aria-label="Wind limit in knots" @change="num('maxWindKt', $event, 5, 60)"> kt, seas above <input type="number" min="0.5" max="10" step="0.5" :value="settings.maxSeasM" aria-label="Sea limit in metres" @change="num('maxSeasM', $event, 0.5, 10)"> m</label>
      <label title="The engine is used when the boat would make less than this even on a beam reach"><input type="checkbox" :checked="settings.motor" @change="set('motor', $event.target.checked)"> Motor below <input type="number" min="0.5" max="6" step="0.5" :value="settings.motorBelowKt" aria-label="Motor below (kt)" @change="num('motorBelowKt', $event, 0.5, 6)"> kt at <input type="number" min="2" max="10" step="0.5" :value="settings.motorKt" aria-label="Motoring speed (kt)" @change="num('motorKt', $event, 2, 10)"> kt</label>
    </div>

    <div class="fp-route">
      <div class="wx-small-label">ROUTE · THE BOAT OR COORDINATES (DECIMAL, DEGREES-MINUTES OR ICAO 5430N01030E)</div>
      <template v-if="list.length">
        <div v-for="(it, k) in list" :key="k" class="fp-row" :class="{ bad: !resolved[k]?.ok || resolved[k]?.inside === false }">
          <span class="fp-role">{{ role(k) }}</span>
          <select :value="it.kind" :aria-label="`${role(k)}: the boat or coordinates`" @change="setKind(k, $event.target.value)"><option value="boat">The boat</option><option value="coord">Coordinates…</option></select>
          <template v-if="it.kind === 'coord'">
            <input class="fp-name" :value="it.name" maxlength="14" :aria-label="`${role(k)} name`" placeholder="Name" @change="setRow(k, { name: $event.target.value })">
            <input class="fp-coord" :value="it.text" :aria-label="`${role(k)} coordinates`" placeholder="54.55, 10.45" @change="setRow(k, { text: $event.target.value })">
          </template>
          <span class="fp-pos"><template v-if="resolved[k]?.ok">{{ resolved[k].text }}<em v-if="resolved[k].inside === false"> · outside the simulated area</em></template><em v-else>{{ resolved[k]?.text }}</em></span>
          <span class="fp-row-tools">
            <button :disabled="k === 0" title="Move up" aria-label="Move up" @click="move(k, -1)">↑</button>
            <button :disabled="k === list.length - 1" title="Move down" aria-label="Move down" @click="move(k, 1)">↓</button>
            <button title="Remove" aria-label="Remove" @click="remove(k)">✕</button>
          </span>
        </div>
      </template>
      <div v-else class="sp-start">
        <span>From the boat to</span>
        <input class="fp-coord" v-model="typed" aria-label="Destination coordinates" placeholder="55.10, 14.50" @keydown.enter="startRoute">
        <button @click="startRoute">Plan</button>
        <span class="wx-note">or click a point on the map and press <b>🧭 Route here</b> in its read-out, or pick it below.</span>
      </div>
      <div class="fp-route-tools">
        <button v-if="list.length" @click="addWaypoint">+ Add waypoint</button>
        <button :class="{ on: picking }" :aria-pressed="picking" @click="emit('picking', !picking)">{{ picking ? '◉ Click the map to add points — done' : list.length ? '⊕ Pick waypoints on the map' : '⊕ Pick the destination on the map' }}</button>
        <button v-if="list.length" @click="reverse">⇄ Reverse (the way back)</button>
        <span v-if="bounds" class="fp-bounds">Simulated area {{ bounds.south.toFixed(1) }}–{{ bounds.north.toFixed(1) }}° N, {{ bounds.west.toFixed(1) }}–{{ bounds.east.toFixed(1) }}° E</span>
      </div>
    </div>

    <p v-if="route && !route.ok" class="wx-note fp-error">{{ route.reason }}</p>
    <template v-if="r">
      <div class="fp-summary">
        <div><small>TIME UNDER WAY</small><b>{{ hm(r.hours) }}</b><span>off {{ when(r.departMs) }} · in {{ when(r.etaMs) }}</span></div>
        <div><small>DISTANCE</small><b>{{ Math.round(r.sailedNm) }} NM</b><span>sailed · {{ Math.round(r.directNm) }} NM direct</span></div>
        <div><small>SPEED</small><b>{{ r.avgSogKt.toFixed(1) }} kt</b><span>over the ground · {{ r.vmcKt.toFixed(1) }} kt towards the marks</span></div>
        <div><small>MANOEUVRES</small><b>{{ r.tacks }} · {{ r.gybes }}</b><span>tacks · gybes<template v-if="r.motoringHours > 0.05"> · motoring {{ hm(r.motoringHours) }}</template></span></div>
        <div><small>WIND · SEA (MAX)</small><b>{{ Math.round(r.maxTws) }} kt</b><span>seas {{ r.maxHs.toFixed(1) }} m · {{ r.worstWarning.label.toLowerCase() }}</span></div>
        <div v-if="r.compare"><small>AGAINST THE DIRECT ROUTE</small><template v-if="r.compare.ok"><b :class="r.compare.savedHours > 0.05 ? 'good' : ''">{{ r.compare.savedHours > 0.05 ? `−${hm(r.compare.savedHours)}` : 'no gain' }}</b><span>direct {{ hm(r.compare.hours) }}, {{ Math.round(r.compare.sailedNm) }} NM, {{ r.compare.tacks }} tacks, {{ r.compare.gybes }} gybes</span></template><template v-else><b>—</b><span>{{ r.compare.reason }}</span></template></div>
      </div>

      <ul v-if="r.checks.length" class="wx-hazards"><li v-for="(c, k) in r.checks" :key="k" :class="c.level">{{ c.text }}</li></ul>

      <div class="wx-card sp-weather">
        <div class="wx-small-label">WEATHER ALONG THE ROUTE · AT THE TIME OF PASSAGE · CLICK FOR ITS FRAME</div>
        <WxStrip :span="strip.span" :rows="strip.rows" :ticks="strip.ticks" :cursor="cursor" :northern-hemisphere="northernHemisphere" @pick="b => emit('seek', b.data.frameIndex)"/>
        <div class="sp-legend"><span v-for="(c, k) in POINT_OF_SAIL_COLOURS" :key="k"><i :style="{ background: c }"></i>{{ k }}</span></div>
        <table class="rw-table">
          <thead><tr><th>Time</th><th></th><th>Heading · wind</th><th>Point of sail</th><th>Sails</th><th>Speed</th><th>Sea</th><th>Weather</th></tr></thead>
          <tbody>
            <tr v-for="(s, k) in r.stretches" :key="k" @click="emit('seek', r.track.find(x => x.ms >= s.fromMs)?.frameIndex ?? 0)">
              <th>{{ when(s.fromMs) }}<small>{{ hm(s.hours) }} · {{ Math.round(s.fromNm) }}–{{ Math.round(s.toNm) }} NM</small></th>
              <td><WeatherIcon :symbol="s.symbol" :night="!!s.night" :size="24"/></td>
              <td>{{ deg3(s.headingDeg) }}° · {{ compass16(s.twdFrom) }} {{ Math.round(s.twsMin) }}{{ Math.round(s.twsMax) !== Math.round(s.twsMin) ? `–${Math.round(s.twsMax)}` : '' }} kt<small>F{{ s.forceMax }} · {{ s.warning.label }}</small></td>
              <td><span class="sp-pos" :style="{ background: POINT_OF_SAIL_COLOURS[s.pointOfSail] }">{{ s.pointOfSail }}</span><small v-if="!s.motoring">{{ s.tack }}<template v-if="s.manoeuvres"> · {{ s.manoeuvres }} {{ s.manoeuvres === 1 ? 'turn' : 'turns' }}</template></small></td>
              <td>{{ s.sailShort }}</td>
              <td>{{ s.sogKt.toFixed(1) }} kt</td>
              <td>{{ s.hsMax.toFixed(1) }} m</td>
              <td class="rw-words">{{ s.weather ?? '—' }}</td>
            </tr>
          </tbody>
        </table>
      </div>

      <div class="fp-tables">
        <div class="wx-card">
          <div class="wx-small-label">TACKS AND GYBES · ▶ WATCHES IT IN THE SAIL-THROUGH (REAL TIME)</div>
          <table v-if="r.manoeuvres.length" class="rw-table">
            <thead><tr><th>Time</th><th>Turn</th><th>Heading</th><th>Wind</th><th>Lost</th><th></th></tr></thead>
            <tbody>
              <tr v-for="(m, k) in r.manoeuvres" :key="k">
                <th>{{ when(m.ms) }}</th>
                <td :style="{ color: m.kind === 'tack' ? '#ff9f9f' : '#9fc8ff' }">{{ m.kind === 'tack' ? '⤺ tack' : '⤻ gybe' }}<small v-if="m.warnings.length" class="warn">⚠ {{ m.warnings[0] }}</small></td>
                <td>{{ deg3(m.fromHeadingDeg) }}° → {{ deg3(m.toHeadingDeg) }}°</td>
                <td>{{ Math.round(m.tws) }} kt {{ compass16(m.twdFrom) }}</td>
                <td>{{ Math.round(m.sim.summary.timeLostS) }} s<small>{{ m.sim.summary.boatLengthsLost.toFixed(1) }} lengths</small></td>
                <td><button class="fp-link" @click="emit('sail', Math.max(0, (m.ms - r.departMs) / 3.6e6 - 0.02))">▶ watch</button></td>
              </tr>
            </tbody>
          </table>
          <p v-else class="wx-note">No tack or gybe: the marks can be fetched on one side of the wind all the way.</p>
        </div>
        <div class="wx-card">
          <div class="wx-small-label">WHEN TO LEAVE · THE PASSAGE FOR EVERY DEPARTURE WITHIN 48 H</div>
          <button class="sp-scan" :disabled="scanBusy" @click="emit('scan')">{{ scanBusy ? `Routing ${scan?.rows.length ?? 0} / ${scan?.total ?? '…'}…` : scan ? 'Scan again' : 'Scan departure times' }}</button>
          <div v-if="scan?.rows.length" class="sp-scan-list">
            <button v-for="x in scan.rows" :key="x.index" class="sp-scan-row" :class="{ best: x.index === scan.best, now: String(x.index) === String(settings.departure ?? frameIndex), bad: x.ok && !x.within, fail: !x.ok }" :title="x.ok ? `${when(x.departMs)}: ${hm(x.hours)} under way, wind up to ${Math.round(x.maxTws)} kt, seas ${x.maxHs.toFixed(1)} m, ${x.tacks} tacks, ${x.gybes} gybes` : x.reason" @click="set('departure', String(x.index))">
              <span>{{ when(x.departMs) }}</span>
              <i :style="{ width: x.ok ? barWidth(x.hours) : '0', background: x.ok ? (x.within ? '#3ddc84' : '#ff9f43') : 'transparent' }"></i>
              <b>{{ x.ok ? hm(x.hours) : '—' }}</b><small>{{ x.ok ? `${Math.round(x.maxTws)} kt · ${x.tacks}T ${x.gybes}G` : 'no route' }}</small>
            </button>
          </div>
          <p class="wx-note">★ The best departure: the shortest passage within your limits (orange: beyond them). Click one to depart then.</p>
        </div>
      </div>
      <p class="wx-note">{{ r.mode === 'routed' ? `Weather routing by isochrones: every ${r.stepMinutes} min, headings 10° apart plus the best VMG headings; the farthest point per 3° sector is kept; land${settings.avoidLimits ? ' and your limits are' : ' is'} avoided. ` : 'Direct: the legs as planned, beating or running in tacks or gybes inside a corridor of up to 10 NM around the rhumb line where a mark cannot be fetched. ' }}The boat sails its polar (generic), slowed in head seas; a tack or gybe costs the time of its simulation. Wind from the engine run (interpolated in space and time), waves from the wind-sea estimate. Simulated: not for navigation.</p>
    </template>
  </section>
</template>
