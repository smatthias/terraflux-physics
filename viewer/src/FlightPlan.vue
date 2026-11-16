<script setup>
// Flight planning in the flight weather view: choose departure, destination, cruise level, true airspeed, fuel burn
// and departure time; the plan (flightPlan.js) flies the route through the simulation's timeline. Shown here: the
// verdict and checks, departure and destination weather (the destination at the ETA), an alternate, the route's
// vertical cross-section with clouds, icing, freezing level, terrain and wind barbs at every level, the navigation
// log and the best cruise level. Numbers from flightPlan.js; this component lays them out.
import { computed } from 'vue'
import WindBarb from './WindBarb.vue'
import WeatherIcon from './WeatherIcon.vue'
import { barbSvg, barbColour } from './barbs.js'
import { FB_LEVELS } from './windsAloft.js'
import { HAZARD_INFO, RULES } from './flightPlan.js'
import { GAFOR, UNKNOWN_GAFOR } from './gafor.js'
import { parseCoordinate, formatDM, formatIcao, distanceNm, initialCourse, finalCourse } from './geo.js'
import { cellLocation } from './station.js'

const props = defineProps({
  plan: { type: Object, default: null },
  airports: { type: Array, required: true },
  settings: { type: Object, required: true },       // { fromId, toId, cruiseFt, tasKt, fuelPerHour, departure }
  frameLabels: { type: Array, default: () => [] },
  northernHemisphere: { type: Boolean, default: true },
  flying: { type: Boolean, default: false },
  bounds: { type: Object, default: null },           // the simulated area { south, north, west, east }
  picking: { type: Boolean, default: false },
  frameTimes: { type: Array, default: () => [] },      // valid time (ms) of every frame
  rules: { type: Object, default: null },              // flightRules(plan) of the outbound
  returnData: { type: Object, default: null },         // returnTrip(...)
  returnIndex: { type: Number, default: null },
  windows: { type: Object, default: null },            // { outbound: { day, windows }, back: { day, windows } }
})
const emit = defineEmits(['settings', 'seek', 'select-airport', 'fly', 'picking'])
const p = computed(() => props.plan)
const dir3 = d => String(Math.round(d / 10) * 10 || 360).padStart(3, '0')   // winds: to 10 degrees, as reported
const deg3 = d => String(Math.round(((d % 360) + 360) % 360) || 360).padStart(3, '0')   // headings and courses: to the degree
const hm = h => { if (!Number.isFinite(h)) return '—'; const m = Math.round(h * 60); return `${Math.floor(m / 60)}:${String(m % 60).padStart(2, '0')}` }
const utc = ms => { const d = new Date(ms); return `${String(d.getUTCHours()).padStart(2, '0')}:${String(d.getUTCMinutes()).padStart(2, '0')}Z` }
const day = ms => new Date(ms).toISOString().slice(0, 10)
// ---- The route editor: aerodromes or coordinates, departure first, destination last ---------------------------------
const route = computed(() => props.settings.route ?? [])
const setRoute = r => emit('settings', { route: r })
const rowRole = k => (k === 0 ? 'DEP' : k === route.value.length - 1 ? 'DEST' : `WP${k}`)
// What each row resolves to: its position (or the reason it cannot be read), in chart and ICAO formats.
const resolved = computed(() => route.value.map((it, k) => {
  if (it.kind === 'airport') { const a = props.airports.find(x => x.id === it.id); return a ? { ok: true, name: a.id, lat: a.lat, lon: a.lon, airport: a } : { ok: false, error: `Aerodrome ${it.id} is not in this run` } }
  try { const q = parseCoordinate(it.text); const inside = !props.bounds || (q.lat >= props.bounds.south && q.lat <= props.bounds.north && q.lon >= props.bounds.west && q.lon <= props.bounds.east); return { ok: true, name: it.name || rowRole(k), ...q, inside } } catch (err) { return { ok: false, error: err.message } }
}))
const routeLegs = computed(() => { const r = resolved.value, out = []; for (let k = 0; k < r.length - 1; k++) if (r[k].ok && r[k + 1].ok) out.push({ from: r[k].name, to: r[k + 1].name, nm: distanceNm(r[k], r[k + 1]), initial: initialCourse(r[k], r[k + 1]), final: finalCourse(r[k], r[k + 1]) }); return out })
function setKind(k, value) {
  const r = [...route.value]
  if (value === 'coord') { const cur = resolved.value[k]; r[k] = { kind: 'coord', name: k === 0 ? 'DEP' : k === r.length - 1 ? 'DEST' : `WP${k}`, text: cur?.ok ? `${cur.lat.toFixed(4)}, ${cur.lon.toFixed(4)}` : '' } }
  else r[k] = { kind: 'airport', id: value }
  setRoute(r)
}
const setField = (k, field, value) => { const r = [...route.value]; r[k] = { ...r[k], [field]: value }; setRoute(r) }
function addWaypoint() { const r = [...route.value], a = resolved.value.at(-2), b = resolved.value.at(-1); const mid = a?.ok && b?.ok ? `${((a.lat + b.lat) / 2).toFixed(4)}, ${((a.lon + b.lon) / 2).toFixed(4)}` : ''; r.splice(Math.max(1, r.length - 1), 0, { kind: 'coord', name: `WP${r.length - 1}`, text: mid }); setRoute(r) }
const removeRow = k => { const r = [...route.value]; r.splice(k, 1); setRoute(r) }
const moveRow = (k, d) => { const r = [...route.value], j = k + d; if (j < 0 || j >= r.length) return; [r[k], r[j]] = [r[j], r[k]]; setRoute(r) }
const reverseRoute = () => setRoute([...route.value].reverse())
const deg = v => `${v.toFixed(v >= 10 ? 1 : 2)}°`
// ---- Days, times and flight rules ----------------------------------------------------------------------------------
const WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'], MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']
const dayLabel = ms => { const d = new Date(ms); return `${WEEKDAYS[d.getUTCDay()]} ${String(d.getUTCDate()).padStart(2, '0')} ${MONTHS[d.getUTCMonth()]}` }
const whenLabel = ms => `${dayLabel(ms)} ${utc(ms)}`
const dayKey = ms => new Date(ms).toISOString().slice(0, 10)
// The run's days, each with its frames (for the return day chips and the time choice).
const days = computed(() => { const out = []; props.frameTimes.forEach((ms, k) => { const key = dayKey(ms); let d = out.find(x => x.key === key); if (!d) out.push(d = { key, label: dayLabel(ms), indices: [] }); d.indices.push(k) }); return out })
const returnDay = computed(() => (props.returnIndex === null ? null : dayKey(props.frameTimes[props.returnIndex])))
const returnTimes = computed(() => days.value.find(d => d.key === returnDay.value)?.indices ?? [])
// Pick a return day: keep the time of day as close as possible.
function setReturnDay(day) {
  const cur = props.returnIndex === null ? null : new Date(props.frameTimes[props.returnIndex]).getUTCHours()
  const idx = day.indices.reduce((b, k) => (b === null || Math.abs(new Date(props.frameTimes[k]).getUTCHours() - (cur ?? 10)) < Math.abs(new Date(props.frameTimes[b]).getUTCHours() - (cur ?? 10)) ? k : b), null)
  set('returnDeparture', String(idx))
}
const coverage = computed(() => (props.frameTimes.length ? `${whenLabel(props.frameTimes[0])} – ${whenLabel(props.frameTimes.at(-1))}` : ''))
const bandText = r => !r ? '' : r.band.continuous ? `VFR band ${Math.round(r.band.floorFt / 100) * 100}–${Math.round(r.band.ceilingFt / 100) * 100} ft AMSL all the way` : r.band.ceilingFt === null ? 'no VFR band' : `no single VFR altitude (floor up to ${Math.round(r.band.floorFt / 100) * 100} ft, ceiling down to ${Math.round(r.band.ceilingFt / 100) * 100} ft)`
const windowSummary = list => { const ok = list.filter(w => w.rule.key === 'VFR'); if (!ok.length) return 'no VFR departure time on this day'; const runs = []; for (const w of ok) { const last = runs.at(-1); if (last && last.to === w.index - 1) { last.to = w.index; last.toMs = w.ms } else runs.push({ from: w.index, to: w.index, fromMs: w.ms, toMs: w.ms }) } return `VFR departures ${runs.map(r => (r.from === r.to ? utc(r.fromMs) : `${utc(r.fromMs)}–${utc(r.toMs)}`)).join(', ')}` }
const levelLabel = ft => FB_LEVELS.find(l => l.ft === ft)?.label ?? `${ft.toLocaleString('en-US')} ft`
const set = (key, value) => emit('settings', { [key]: value })
const num = (key, e, lo, hi) => { const v = Number(e.target.value); if (Number.isFinite(v) && v >= lo && v <= hi) set(key, v) }
const verdict = computed(() => {
  if (!p.value?.ok) return null
  const c = p.value.checks
  if (c.some(x => x.level === 'severe')) return { level: 'severe', text: 'Not advisable as planned' }
  if (c.some(x => x.level === 'warning')) return { level: 'warning', text: 'Possible with conditions' }
  return { level: 'ok', text: 'No hazards diagnosed on the route' }
})

// ---- The cross-section ---------------------------------------------------------------------------------------------
const W = 1000, H = 340, L = 66, R = 18, T = 30, B = 36, TOP_FT = 41000
const X = d => L + d / (p.value?.lengthNm || 1) * (W - L - R)
const Y = ft => H - B - Math.max(0, Math.min(TOP_FT, ft)) / TOP_FT * (H - T - B)
const section = computed(() => {
  const plan = p.value
  if (!plan?.ok) return null
  const st = plan.section, mid = j => (j <= 0 ? 0 : j >= st.length ? plan.lengthNm : (st[j - 1].distNm + st[j].distNm) / 2)
  const spans = st.map((s, j) => ({ x0: X(mid(j)), x1: X(mid(j + 1)), s }))
  const terrain = [`${X(0)},${Y(0)}`, ...plan.profile.map(q => `${X(q.distNm).toFixed(1)},${Y(q.terrainFt).toFixed(1)}`), `${X(plan.lengthNm)},${Y(0)}`].join(' ')
  const path = [`${X(0)},${Y(plan.from.elevationFt)}`, ...plan.profile.map(q => `${X(q.distNm).toFixed(1)},${Y(q.altFt).toFixed(1)}`), `${X(plan.lengthNm)},${Y(plan.to.elevationFt)}`].join(' ')
  const freezing = st.map(s => `${X(s.distNm).toFixed(1)},${Y(s.freezingFt).toFixed(1)}`).join(' ')
  const barbs = st.flatMap((s, j) => s.winds.filter(w => !w.below).map(w => ({ key: `${j}-${w.level.key}`, x: X(s.distNm), y: Y(w.level.ft), g: barbSvg(w.kt, w.fromDeg, { size: 30, northernHemisphere: props.northernHemisphere }), colour: barbColour(w.kt), title: `${w.level.label} at ${Math.round(s.distNm)} NM: ${dir3(w.fromDeg)}° ${Math.round(w.kt)} kt, ${Math.round(w.tempC)} °C` })))
  const step = [10, 20, 25, 50, 100, 200, 250, 500].find(v => plan.lengthNm / v <= 8) ?? 1000, ticks = []
  for (let d = 0; d <= plan.lengthNm + 1e-6; d += step) ticks.push(d)
  return { spans, terrain, path, freezing, barbs, ticks }
})
const cloudFill = c => (c.convective === 'CB' ? '#5d6b78' : c.convective === 'TCU' ? '#8797a5' : '#a9bcc8')
</script>

<template>
  <section class="comparison wx-report flight-plan" aria-label="Flight planning">
    <div class="wx-head">
      <div>
        <div class="section-label">FLIGHT PLANNING · SIMULATED WEATHER · NOT FOR NAVIGATION</div>
        <h3 v-if="p?.ok">{{ p.points.map(q => q.id).join(' → ') }} <small>{{ Math.round(p.lengthNm) }} NM great circle · initial course {{ deg3(p.tcDeg) }}° · {{ levelLabel(settings.cruiseFt) }} · {{ settings.tasKt }} kt TAS</small></h3>
        <h3 v-else>Plan a flight <small>between the aerodromes of this run</small></h3>
      </div>
      <div class="fp-head-actions">
        <button v-if="p?.ok" class="fp-fly" :disabled="flying" @click="emit('fly')">{{ flying ? '✈ Flying…' : '✈ Fly this route in 3D' }}</button>
        <div v-if="verdict" class="wx-verdict" :class="verdict.level">{{ verdict.text }}</div>
      </div>
    </div>

    <div class="fp-form">
      <label>Cruise <select :value="settings.cruiseFt" aria-label="Cruise level" @change="set('cruiseFt', Number($event.target.value))"><option v-for="l in FB_LEVELS" :key="l.key" :value="l.ft">{{ l.label }}</option></select></label>
      <label>TAS <input type="number" min="40" max="600" step="5" :value="settings.tasKt" aria-label="True airspeed in knots" @change="num('tasKt', $event, 40, 600)"> kt</label>
      <label>Fuel burn <input type="number" min="0" max="5000" step="1" :value="settings.fuelPerHour" aria-label="Fuel burn per hour" @change="num('fuelPerHour', $event, 0, 5000)"> /h</label>
      <label>Departure <select :value="settings.departure" aria-label="Departure time" @change="set('departure', $event.target.value)"><option value="current">At the frame shown (follows playback)</option><option v-for="(ms, k) in frameTimes" :key="k" :value="String(k)">{{ whenLabel(ms) }}</option></select></label>
    </div>

    <div class="fp-route">
      <div class="wx-small-label">ROUTE · AERODROMES OR COORDINATES (DECIMAL, DEGREES-MINUTES OR ICAO 4657N00727E) · GREAT-CIRCLE LEGS</div>
      <div v-for="(it, k) in route" :key="k" class="fp-row" :class="{ bad: !resolved[k]?.ok || resolved[k]?.inside === false }">
        <span class="fp-role">{{ rowRole(k) }}</span>
        <select :value="it.kind === 'airport' ? it.id : 'coord'" :aria-label="`${rowRole(k)}: aerodrome or coordinates`" @change="setKind(k, $event.target.value)">
          <option v-for="a in airports" :key="a.id" :value="a.id">{{ a.id }} · {{ a.name }}</option>
          <option value="coord">Coordinates…</option>
        </select>
        <template v-if="it.kind === 'coord'">
          <input class="fp-name" :value="it.name" maxlength="12" :aria-label="`${rowRole(k)} name`" placeholder="Name" @change="setField(k, 'name', $event.target.value)">
          <input class="fp-coord" :value="it.text" :aria-label="`${rowRole(k)} coordinates`" placeholder="46.95, 7.45" @change="setField(k, 'text', $event.target.value)">
        </template>
        <span class="fp-pos">
          <template v-if="resolved[k]?.ok">{{ formatDM(resolved[k]) }} · <code>{{ formatIcao(resolved[k]) }}</code><em v-if="resolved[k].inside === false"> · outside the simulated area</em></template>
          <em v-else>{{ resolved[k]?.error }}</em>
        </span>
        <span class="fp-row-tools">
          <button :disabled="k === 0" title="Move up" aria-label="Move up" @click="moveRow(k, -1)">↑</button>
          <button :disabled="k === route.length - 1" title="Move down" aria-label="Move down" @click="moveRow(k, 1)">↓</button>
          <button :disabled="route.length <= 2" title="Remove" aria-label="Remove" @click="removeRow(k)">✕</button>
        </span>
      </div>
      <div class="fp-route-tools">
        <button @click="addWaypoint">+ Add waypoint</button>
        <button :class="{ on: picking }" :aria-pressed="picking" @click="emit('picking', !picking)">{{ picking ? '◉ Click the map to add waypoints — done' : '⊕ Pick waypoints on the map' }}</button>
        <button @click="reverseRoute">⇄ Reverse route</button>
        <span v-if="bounds" class="fp-bounds">Simulated area {{ Math.abs(bounds.south).toFixed(1) }}°{{ bounds.south < 0 ? 'S' : 'N' }}–{{ Math.abs(bounds.north).toFixed(1) }}°{{ bounds.north < 0 ? 'S' : 'N' }}, {{ Math.abs(bounds.west).toFixed(1) }}°{{ bounds.west < 0 ? 'W' : 'E' }}–{{ Math.abs(bounds.east).toFixed(1) }}°{{ bounds.east < 0 ? 'W' : 'E' }}</span>
      </div>
      <table v-if="routeLegs.length" class="fp-legs">
        <thead><tr><th>Leg</th><th>Great-circle distance</th><th>Initial true course</th><th>Final true course</th></tr></thead>
        <tbody>
          <tr v-for="(l, k) in routeLegs" :key="k"><th>{{ l.from }} → {{ l.to }}</th><td>{{ l.nm.toFixed(1) }} NM</td><td>{{ deg3(l.initial) }}° <small>({{ deg(l.initial) }})</small></td><td>{{ deg3(l.final) }}°</td></tr>
          <tr v-if="routeLegs.length > 1" class="total"><th>Total</th><td>{{ routeLegs.reduce((a, l) => a + l.nm, 0).toFixed(1) }} NM</td><td colspan="2"></td></tr>
        </tbody>
      </table>
    </div>
    <p v-if="p && !p.ok" class="wx-note fp-error">{{ p.reason }}</p>
    <template v-if="p?.ok">
      <div class="fp-summary">
        <div><small>DISTANCE · COURSE</small><b>{{ Math.round(p.lengthNm) }} NM</b><span>true course {{ deg3(p.tcDeg) }}°</span></div>
        <div><small>TIME EN ROUTE</small><b>{{ hm(p.hours) }}</b><span>off {{ utc(p.departMs) }} · on {{ utc(p.etaMs) }}{{ day(p.etaMs) !== day(p.departMs) ? ` (${day(p.etaMs)})` : '' }}</span></div>
        <div><small>GROUND SPEED · WIND</small><b>{{ Math.round(p.avgGsKt) }} kt</b><span :class="p.avgWindKt < 0 ? 'warn' : 'good'">{{ p.avgWindKt < 0 ? `headwind ${Math.round(-p.avgWindKt)} kt on average` : `tailwind ${Math.round(p.avgWindKt)} kt on average` }}</span></div>
        <div><small>FUEL</small><b>{{ Math.round(p.fuel.trip) }}</b><span>trip · + {{ Math.round(p.fuel.reserve) }} reserve (45 min) = {{ Math.round(p.fuel.trip + p.fuel.reserve) }}</span></div>
        <div v-if="rules" class="fp-rules" :style="{ '--r': rules.rule.colour }"><small>FLIGHT RULES</small><b>{{ rules.rule.label }}</b><span>{{ bandText(rules) }}</span></div>
        <div><small>GAFOR ON THE ROUTE (WORST)</small><b><span class="vfr-badge big" :style="{ background: (GAFOR[p.gaforRoute] ?? UNKNOWN_GAFOR).colour }">{{ p.gaforRoute }}</span></b><span>{{ (GAFOR[p.gaforRoute] ?? UNKNOWN_GAFOR).name }} · at the time flown</span></div>
        <div v-if="p.best"><small>BEST LEVEL (WIND, NO ICING)</small><b>{{ p.best.level.label }}</b><span>{{ hm(p.best.hours) }} en route<template v-if="p.best.level.ft !== settings.cruiseFt"> · <button class="fp-link" @click="set('cruiseFt', p.best.level.ft)">fly it</button></template></span></div>
      </div>

      <div class="wx-grid">
        <div class="wx-card">
          <div class="wx-small-label">PLANNING CHECKS</div>
          <ul v-if="rules && rules.reasons.length" class="wx-hazards fp-rule-reasons"><li v-for="(r, k) in rules.reasons" :key="`r${k}`" :class="r.rule === 'NOGO' || r.rule === 'IFR' ? 'severe' : 'caution'"><b :style="{ color: RULES[r.rule].colour }">{{ RULES[r.rule].short }}</b> {{ r.text }}</li></ul>
          <ul v-if="p.checks.length" class="wx-hazards"><li v-for="(c, k) in p.checks" :key="k" :class="c.level">{{ c.text }}</li></ul>
          <p v-else class="wx-note">No hazards diagnosed on the route, at departure or at the destination.</p>
        </div>
        <div class="wx-card">
          <div class="wx-small-label">DEPARTURE · {{ utc(p.departMs) }}</div>
          <p class="fp-apt"><WeatherIcon :symbol="p.symbols.departure" :size="30"/><b :style="{ background: p.departure.category.colour }">{{ p.departure.category.name }}</b><button v-if="p.from.isAirport" class="fp-link" @click="emit('select-airport', p.from.id)">{{ p.from.id }}</button><b v-else class="fp-pt">{{ p.from.id }}</b><template v-if="p.from.isAirport"> runway {{ p.departure.runway.designator }}, {{ Math.round(Math.abs(p.departure.runway.cross)) }} kt crosswind</template><template v-else> {{ formatDM(p.from) }}</template></p>
          <code class="fp-metar">{{ p.departure.metar }}</code>
          <div class="wx-small-label" style="margin-top:12px">DESTINATION AT THE ETA · {{ utc(p.etaMs) }}</div>
          <p class="fp-apt"><WeatherIcon :symbol="p.symbols.destination" :size="30"/><b :style="{ background: p.destination.category.colour }">{{ p.destination.category.name }}</b><button v-if="p.to.isAirport" class="fp-link" @click="emit('select-airport', p.to.id)">{{ p.to.id }}</button><b v-else class="fp-pt">{{ p.to.id }}</b><template v-if="p.to.isAirport"> runway {{ p.destination.runway.designator }}, {{ Math.round(Math.abs(p.destination.runway.cross)) }} kt crosswind</template><template v-else> {{ formatDM(p.to) }}</template></p>
          <code class="fp-metar">{{ p.destination.metar }}</code>
          <p class="wx-note">Weather of the frame valid {{ frameLabels[p.arrivalFrameIndex] ?? '' }} (nearest to the ETA) · <button class="fp-link" @click="emit('seek', p.arrivalFrameIndex)">show that frame</button></p>
          <template v-if="p.alternate">
            <div class="wx-small-label" style="margin-top:10px">ALTERNATE</div>
            <p class="fp-apt"><WeatherIcon :symbol="p.alternate.symbol" :size="30"/><b :style="{ background: p.alternate.category.colour }">{{ p.alternate.category.name }}</b><button class="fp-link" @click="emit('select-airport', p.alternate.airport.id)">{{ p.alternate.airport.id }}</button> {{ Math.round(p.alternate.distanceNm) }} NM from {{ p.to.id }}, expected {{ utc(p.alternate.etaMs) }}</p>
          </template>
        </div>
      </div>

      <div class="fp-return">
        <label class="fp-return-toggle"><input type="checkbox" :checked="settings.returnEnabled" @change="set('returnEnabled', $event.target.checked)"> <b>Return trip on a later day</b> <small>the reversed route, flown through the forecast valid when you come back · forecast {{ coverage }}</small></label>
        <template v-if="settings.returnEnabled">
          <div class="fp-return-when">
            <span class="wx-small-label">RETURN</span>
            <button v-for="d in days" :key="d.key" :class="{ on: d.key === returnDay }" :disabled="d.key < dayKey(p.departMs)" @click="setReturnDay(d)">{{ d.label }}</button>
            <select :value="returnIndex === null ? '' : String(returnIndex)" aria-label="Return departure time" @change="set('returnDeparture', $event.target.value)"><option v-for="k in returnTimes" :key="k" :value="String(k)">{{ utc(frameTimes[k]) }}</option></select>
          </div>
          <template v-if="returnData">
            <div class="fp-verdict" :style="{ '--r': returnData.verdict.rule.colour }"><b>{{ returnData.verdict.rule.label.toUpperCase() }}</b> {{ returnData.verdict.text }}<small v-if="returnData.stayHours !== null && returnData.stayHours >= 0">{{ Math.floor(returnData.stayHours / 24) ? `${Math.floor(returnData.stayHours / 24)} d ` : '' }}{{ Math.round(returnData.stayHours % 24) }} h on the ground at {{ p.to.id }}</small></div>
            <div class="fp-legs2">
              <div v-for="(leg, k) in [returnData.outbound, returnData.back]" :key="k" class="wx-card fp-leg" :style="{ '--r': leg.rules?.rule.colour ?? '#7e98a5' }">
                <div class="wx-small-label">{{ k ? 'RETURN' : 'OUTBOUND' }}<template v-if="leg.plan.ok"> · {{ whenLabel(leg.plan.departMs) }} → {{ utc(leg.plan.etaMs) }}</template></div>
                <template v-if="leg.plan.ok">
                  <p class="fp-leg-head"><b>{{ leg.rules.rule.label }}</b><span>{{ leg.plan.points.map(q => q.id).join(' → ') }} · {{ hm(leg.plan.hours) }} · GAFOR worst <span class="vfr-badge" :style="{ background: (GAFOR[leg.plan.gaforRoute] ?? UNKNOWN_GAFOR).colour }">{{ leg.plan.gaforRoute }}</span></span></p>
                  <p class="fp-leg-ends"><template v-for="e in leg.rules.ends" :key="e.role"><span>{{ e.role === 'departure' ? 'Off' : 'On' }} {{ e.id }} <span class="vfr-badge" :style="{ background: (GAFOR[e.gafor] ?? UNKNOWN_GAFOR).colour }">{{ e.gafor }}</span></span></template><span>{{ bandText(leg.rules) }}</span></p>
                  <ul v-if="leg.rules.reasons.length" class="fp-leg-reasons"><li v-for="(r, j) in leg.rules.reasons" :key="j"><b :style="{ color: RULES[r.rule].colour }">{{ RULES[r.rule].short }}</b> {{ r.text }}</li></ul>
                  <p v-else class="wx-note">Visual all the way: a VFR band below the cloud, the ground in sight, both ends C/O/D.</p>
                </template>
                <p v-else class="wx-note">{{ leg.plan.reason }}</p>
              </div>
            </div>
          </template>
          <div v-if="windows" class="fp-windows">
            <div v-for="(w, k) in [windows.outbound, windows.back].filter(Boolean)" :key="k" class="fp-window">
              <div class="wx-small-label">{{ k ? 'RETURN' : 'OUTBOUND' }} DAY · {{ dayLabel(frameTimes[w.windows[0]?.index] ?? 0) }} · FLIGHT RULES BY DEPARTURE TIME · CLICK TO CHOOSE</div>
              <div class="fp-window-strip">
                <button v-for="x in w.windows" :key="x.index" :class="{ now: k ? x.index === returnIndex : x.index === p.departFrameIndex }" :style="{ background: x.rule.colour }" :title="`${utc(x.ms)}: ${x.rule.label} · ${x.reason}`" @click="k ? set('returnDeparture', String(x.index)) : set('departure', String(x.index))"><b>{{ utc(x.ms).slice(0, 2) }}</b><small>{{ x.rule.short }}</small></button>
              </div>
              <p class="wx-note">{{ windowSummary(w.windows) }}</p>
            </div>
          </div>
          <p class="wx-note">Flight rules from the simulated weather at the time each part is flown: VFR needs a band above terrain (500 ft) and below cloud with the ground in sight all the way, and GAFOR Charlie, Oscar or Delta at both ends and en route; Mike or a thin band makes it marginal; X-ray or no band means IFR. Thunderstorms make it not advisable either way. Not a forecast: synthetic inputs.</p>
        </template>
      </div>

      <div class="fp-section">
        <div class="wx-small-label">ROUTE CROSS-SECTION · WEATHER WHEN THE AIRCRAFT PASSES · BARBS AT EVERY LEVEL</div>
        <svg :viewBox="`0 0 ${W} ${H}`" class="fp-xsec" role="img" :aria-label="`Cross-section from ${p.from.id} to ${p.to.id}`">
          <defs>
            <pattern id="fp-ice" width="8" height="8" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="8" height="8" fill="rgba(111,184,255,0.18)"/><line x1="0" y1="0" x2="0" y2="8" stroke="#6fb8ff" stroke-width="2.5"/></pattern>
            <linearGradient id="fp-sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#0b1a26"/><stop offset="1" stop-color="#16303f"/></linearGradient>
          </defs>
          <rect :x="L" :y="T" :width="W - L - R" :height="H - T - B" fill="url(#fp-sky)"/>
          <g class="fp-grid"><template v-for="l in FB_LEVELS" :key="l.key"><line :x1="L" :x2="W - R" :y1="Y(l.ft)" :y2="Y(l.ft)"/><text :x="L - 6" :y="Y(l.ft) + 3">{{ l.label.replace(' ft', '') }}</text></template><text :x="L - 6" :y="Y(0) + 3">SFC</text></g>
          <template v-for="sp in section.spans" :key="sp.s.distNm">
            <rect v-if="sp.s.cloud" :x="sp.x0" :width="sp.x1 - sp.x0" :y="Y(sp.s.cloud.topFt)" :height="Math.max(2, Y(sp.s.cloud.baseFt) - Y(sp.s.cloud.topFt))" :fill="cloudFill(sp.s.cloud)" :stroke="sp.s.cloud.convective === 'CB' ? '#ff5252' : 'none'" opacity="0.55"><title>{{ sp.s.cloud.cover }} {{ sp.s.cloud.abbreviation }} {{ Math.round(sp.s.cloud.baseFt / 100) * 100 }}–{{ Math.round(sp.s.cloud.topFt / 100) * 100 }} ft</title></rect>
            <rect v-if="sp.s.icing" :x="sp.x0" :width="sp.x1 - sp.x0" :y="Y(sp.s.icing.toFt)" :height="Math.max(2, Y(sp.s.icing.fromFt) - Y(sp.s.icing.toFt))" fill="url(#fp-ice)"><title>Icing {{ sp.s.icing.severity }}</title></rect>
            <text v-if="sp.s.thunder" :x="(sp.x0 + sp.x1) / 2" :y="Y(sp.s.cloud?.topFt ?? 30000) - 6" class="fp-ts">⚡ CB</text>
          </template>
          <polyline :points="section.freezing" class="fp-freezing"/>
          <text :x="W - R - 4" :y="Y(p.section.at(-1).freezingFt) - 4" class="fp-freezing-label">0 °C</text>
          <polygon :points="section.terrain" class="fp-terrain"/>
          <g v-for="b in section.barbs" :key="b.key" :transform="`translate(${b.x - 15} ${b.y - 15})`"><title>{{ b.title }}</title>
            <line v-for="(l, k) in b.g.lines" :key="k" :x1="l.x1" :y1="l.y1" :x2="l.x2" :y2="l.y2" :stroke="b.colour" stroke-width="1.4" stroke-linecap="round"/>
            <polygon v-for="(q, k) in b.g.polygons" :key="`q${k}`" :points="q" :fill="b.colour"/>
            <circle v-if="b.g.calm" :cx="b.g.circle.cx" :cy="b.g.circle.cy" :r="b.g.circle.r" fill="none" :stroke="b.colour"/>
          </g>
          <polyline :points="section.path" class="fp-path"/>
          <g v-for="r in p.ranges.filter(x => x.kind !== 'cloud')" :key="`${r.kind}${r.fromNm}`"><rect :x="X(r.fromNm)" :width="Math.max(3, X(r.toNm) - X(r.fromNm))" :y="T - 18" height="9" :fill="HAZARD_INFO[r.kind].colour"><title>{{ HAZARD_INFO[r.kind].label }}: {{ Math.round(r.fromNm) }}–{{ Math.round(r.toNm) }} NM</title></rect></g>
          <g class="fp-axis"><template v-for="d in section.ticks" :key="d"><line :x1="X(d)" :x2="X(d)" :y1="H - B" :y2="H - B + 5"/><text :x="X(d)" :y="H - B + 17">{{ d }}</text></template><text :x="W - R" :y="H - 4" class="end">NM</text></g>
          <text :x="X(0)" :y="H - 4" class="fp-apt-label">{{ p.from.id }}</text><text :x="X(p.lengthNm)" :y="H - 4" class="fp-apt-label end">{{ p.to.id }}</text>
        </svg>
        <div class="fp-legend"><span><i class="path"></i>flight profile</span><span><i class="terrain"></i>terrain (corridor)</span><span><i class="cloud"></i>cloud</span><span><i class="cb"></i>CB</span><span><i class="ice"></i>icing</span><span><i class="frz"></i>freezing level</span><span v-for="k in ['thunderstorm','icing','turbulence','terrain','cyclone']" :key="k"><i :style="{ background: HAZARD_INFO[k].colour }"></i>{{ HAZARD_INFO[k].label }}</span></div>
      </div>

      <div class="fp-tables">
        <div class="wx-card fp-navlog">
          <div class="wx-small-label">NAVIGATION LOG · WINDS AT THE TIME OF EACH LEG</div>
          <table>
            <thead><tr><th>From → to</th><th>NM</th><th>Alt ft</th><th>TC</th><th colspan="2">Wind</th><th>Head</th><th>WCA</th><th>TH</th><th>GS</th><th>Time</th><th>ETA</th><th>Temp</th><th title="Worst GAFOR category along the row, at the time flown">GAFOR</th><th>Hazards</th></tr></thead>
            <tbody>
              <tr v-for="(l, k) in p.legs" :key="k">
                <th class="fp-cp">{{ l.fromName || '·' }} → {{ l.toName || '·' }}</th>
                <td>{{ Math.round(l.fromNm) }}–{{ Math.round(l.toNm) }}</td>
                <td>{{ Math.abs(l.altFromFt - l.altToFt) > 100 ? `${l.altFromFt < l.altToFt ? '↗' : '↘'} ${Math.round(l.altToFt / 100) * 100}` : Math.round(l.altFt / 100) * 100 }}</td>
                <td>{{ deg3(l.tcDeg) }}</td>
                <td class="barb-cell"><WindBarb :kt="l.wind.kt" :from-deg="l.wind.fromDeg" :size="28" :northern-hemisphere="northernHemisphere"/></td>
                <td>{{ dir3(l.wind.fromDeg) }}/{{ Math.round(l.wind.kt) }}</td>
                <td :class="l.headKt > 0 ? 'warn' : 'good'">{{ l.headKt > 0 ? '−' : '+' }}{{ Math.round(Math.abs(l.headKt)) }}</td>
                <td>{{ l.wcaDeg >= 0 ? 'R' : 'L' }}{{ Math.round(Math.abs(l.wcaDeg)) }}°</td>
                <td>{{ deg3(l.headingDeg) }}</td>
                <td>{{ Math.round(l.gsKt) }}</td>
                <td>{{ hm(l.hours) }}</td>
                <td>{{ utc(l.etaMs) }}</td>
                <td>{{ Math.round(l.tempC) }}° <small>ISA{{ l.isaDevC >= 0 ? '+' : '−' }}{{ Math.round(Math.abs(l.isaDevC)) }}</small></td>
                <td><span class="vfr-badge" :style="{ background: (GAFOR[l.gafor] ?? UNKNOWN_GAFOR).colour }" :title="(GAFOR[l.gafor] ?? UNKNOWN_GAFOR).meaning">{{ l.gafor }}</span></td>
                <td><span v-for="h in l.hazards.filter(x => x !== 'cloud')" :key="h" class="fp-chip" :style="{ borderColor: HAZARD_INFO[h].colour, color: HAZARD_INFO[h].colour }">{{ HAZARD_INFO[h].label.split(' ')[0] }}</span><span v-if="l.hazards.includes('cloud')" class="fp-chip dim">IMC</span></td>
              </tr>
            </tbody>
          </table>
          <p class="wx-note">Head: − headwind, + tailwind. WCA: wind correction angle (R right, L left). Courses and headings true (great circle; the course at the start of each row). Checkpoints: route points, TOC and TOD. Climb and descent at 300 ft per NM; constant true airspeed.</p>
        </div>
        <div class="wx-card fp-levels">
          <div class="wx-small-label">CRUISE LEVEL COMPARISON</div>
          <table>
            <thead><tr><th>Level</th><th>Time</th><th>Fuel</th><th>Wind</th><th>At cruise</th><th></th></tr></thead>
            <tbody>
              <tr v-for="l in [...p.levels].reverse()" :key="l.level.key" :class="{ best: p.best && l.level.ft === p.best.level.ft, current: l.level.ft === settings.cruiseFt }">
                <th>{{ l.level.label }}</th>
                <td>{{ l.possible ? hm(l.hours) : '—' }}</td>
                <td>{{ l.possible ? Math.round(l.fuel) : '—' }}</td>
                <td :class="l.avgWindKt < 0 ? 'warn' : 'good'">{{ l.possible ? `${l.avgWindKt < 0 ? '−' : '+'}${Math.round(Math.abs(l.avgWindKt))} kt` : '—' }}</td>
                <td><span v-if="!l.reached" class="fp-chip dim">not reached</span><span v-for="h in l.hazards.filter(x => x !== 'cloud')" :key="h" class="fp-chip" :style="{ borderColor: HAZARD_INFO[h].colour, color: HAZARD_INFO[h].colour }">{{ HAZARD_INFO[h].label.split(' ')[0] }}</span><span v-if="l.hazards.includes('cloud')" class="fp-chip dim">IMC</span></td>
                <td><button v-if="l.level.ft !== settings.cruiseFt && l.possible" class="fp-link" @click="set('cruiseFt', l.level.ft)">fly</button></td>
              </tr>
            </tbody>
          </table>
          <p class="wx-note">The same route at every winds-aloft level, through the same timeline. Best: the quickest level reached without terrain conflict or icing at cruise. {{ p.aloft ? '' : 'No winds aloft in this run: every level meets the surface wind.' }}</p>
        </div>
      </div>
    </template>
  </section>
</template>
