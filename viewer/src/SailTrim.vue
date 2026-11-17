<script setup>
// The sail settings for the conditions at the boat, drawn: a top view (hull on its heading, boom and headsail sheeted
// to leeward, true and apparent wind), the sail plan from the side (reefs, headsail size, gennaker, storm sails), the
// trim, the boat's polar with the best VMG angles, and a tack or gybe simulated and played in real time (the 3D boat
// follows the same clock). Numbers from sailTrim.js, manoeuvre.js and polars.js; this component draws them.
import { computed } from 'vue'
import { polarCurves, polarSpeed, optimalVmg, BOATS, hullSpeedKt } from './polars.js'
import { compass16 } from './station.js'
import { STRONG_GYBE_KT } from './manoeuvre.js'

const props = defineProps({
  report: { type: Object, required: true },          // sailingReport(...)
  plan: { type: Object, required: true },            // sailPlan(...) for the report's conditions
  boat: { type: Object, required: true },            // polars.js boat
  settings: { type: Object, required: true },        // the sailing settings (boat, usePolar, reefKt, gennaker)
  turn: { type: Object, required: true },            // { sim, t, playing, rate }
  turnState: { type: Object, default: null },        // manoeuvreAt(sim, t)
})
const emit = defineEmits(['turn', 'replay', 'rate', 'settings'])
const r = computed(() => props.report)
const rad = d => d * Math.PI / 180
const deg3 = d => String(Math.round(((d % 360) + 360) % 360) || 360).padStart(3, '0')
// What is drawn: the manoeuvre while it plays (or has just played), else the steady sailing of the report.
const animating = computed(() => !!props.turn.sim && props.turnState && (props.turn.playing || props.turn.t > 0))
const live = computed(() => {
  if (animating.value && props.turn.playing) { const s = props.turnState; return { headingDeg: s.headingDeg, boomDeg: s.boomDeg, jibDeg: s.jibDeg, heelDeg: s.heelDeg, awa: s.awa, aws: s.aws, twa: s.twa, speedKt: s.speedKt, luffing: s.luffing, phase: s.phase } }
  const p = props.plan, b = r.value.boat
  return { headingDeg: b.headingDeg, boomDeg: p.boomDeg * p.lee, jibDeg: (p.gooseWing ? -1 : 1) * p.jibDeg * p.lee, heelDeg: p.heelDeg * p.lee, awa: b.awa, aws: b.aws, twa: b.twa, speedKt: b.speedKt, luffing: p.luffing, phase: null }
})
// ---- Top view (north up, 260 x 260, the boat at the centre) ----------------------------------------------------------
const C = 130
const top = computed(() => {
  const s = live.value, h = rad(s.headingDeg), cs = Math.cos(h), sn = Math.sin(h)
  // Boat frame: x to starboard, y aft (SVG down when heading north); rotate by the heading.
  const P = (x, y) => [C + x * cs - y * sn, C + x * sn + y * cs]
  const pts = list => list.map(([x, y]) => P(x, y).map(v => v.toFixed(1)).join(',')).join(' ')
  const hull = pts([[0, -46], [15, -10], [12, 34], [-12, 34], [-15, -10]])
  const mast = [0, -14], boomLen = 40, bd = rad(s.boomDeg), boomEnd = [Math.sin(bd) * boomLen, mast[1] + Math.cos(bd) * boomLen]
  // A sail as a curve from luff to clew, bulging to leeward (the side the sheet angle points to).
  const curve = (a, b, bulge) => { const mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2, dx = b[0] - a[0], dy = b[1] - a[1], L = Math.hypot(dx, dy) || 1, nx = -dy / L, ny = dx / L, side = Math.sign(b[0] - a[0]) || 1, k = bulge * L * side * (nx >= 0 ? 1 : -1); const c = [mx + nx * k, my + ny * k]; const [A, Cc, B] = [P(...a), P(...c), P(...b)]; return `M${A[0].toFixed(1)} ${A[1].toFixed(1)} Q${Cc[0].toFixed(1)} ${Cc[1].toFixed(1)} ${B[0].toFixed(1)} ${B[1].toFixed(1)}` }
  const kind = props.plan.headsail.kind, jd = rad(s.jibDeg)
  const tack = kind === 'gennaker' ? [0, -58] : [0, -46], jibLen = { genoa: 50 * (1 - (props.plan.headsail.furl ?? 0) * 0.8), jib: 32, storm: 18, gennaker: 66, none: 0 }[kind] ?? 0
  const clew = [Math.sin(jd) * jibLen, tack[1] + Math.cos(jd) * jibLen]
  const main = props.plan.main.kind === 'none' ? null : curve(mast, props.plan.main.kind === 'trysail' ? [boomEnd[0] * 0.55, mast[1] + (boomEnd[1] - mast[1]) * 0.55] : boomEnd, s.luffing ? 0.02 : 0.16)
  const head = kind === 'none' ? null : curve(tack, clew, s.luffing ? 0.02 : kind === 'gennaker' ? 0.32 : 0.14)
  const arrow = (fromDeg, len, r0 = 108) => { const a = rad(fromDeg), ux = Math.sin(a), uy = -Math.cos(a); return { x1: C + ux * r0, y1: C + uy * r0, x2: C + ux * (r0 - len), y2: C + uy * (r0 - len) } }
  const twd = r.value.wind.fromDeg, awd = s.headingDeg + s.awa
  return { hull, mast: P(...mast), boom: [P(...mast), P(...boomEnd)], main, head, sprit: kind === 'gennaker' ? [P(0, -46), P(0, -58)] : null, tw: arrow(twd, 40), aw: arrow(awd, 30, 96), twd, awd }
})
// ---- Sail plan from the side (220 x 250): mast, boom, main with its reefs, headsail by kind ---------------------------
const SIDE = { W: 230, H: 250, deckY: 222, mastX: 118, bowX: 40, sternX: 200, mastH: 190 }
const side = computed(() => {
  const p = props.plan, S = SIDE, top = S.deckY - S.mastH, boomY = S.deckY - 14, boomEnd = S.mastX + 70
  const hoist = p.main.kind === 'main' ? (p.main.hoist ?? 1) : p.main.kind === 'trysail' ? 0.45 : 0
  const headY = boomY - (boomY - top) * hoist
  const main = p.main.kind === 'main' ? `${S.mastX},${boomY} ${S.mastX},${headY.toFixed(1)} ${boomEnd},${boomY}` : p.main.kind === 'trysail' ? `${S.mastX},${boomY - 10} ${S.mastX},${(boomY - (boomY - top) * 0.45).toFixed(1)} ${S.mastX + 40},${boomY - 6}` : null
  // The reef points still to take, on the sail as set: the next reef brings the cringle at (hoist - next hoist) of the
  // luff down to the boom.
  const H = boomY - top, hoists = [1, 0.84, 0.7, 0.57]
  const reefLines = p.main.kind !== 'main' ? [] : hoists.slice((p.main.reef ?? 0) + 1).map((f, j) => { const k = (p.main.reef ?? 0) + 1 + j, up = (hoist - f) * H; return { k, y: boomY - up, x2: S.mastX + 70 * (1 - up / (hoist * H)) } })
  const kind = p.headsail.kind, furl = p.headsail.furl ?? 0, forestayTop = top + 18
  const head = kind === 'genoa' ? `${S.bowX},${S.deckY - 6} ${S.bowX + (S.mastX - S.bowX) * 0.1},${forestayTop} ${S.mastX + 40 - furl * 90},${S.deckY - 10}`
    : kind === 'jib' ? `${S.bowX},${S.deckY - 6} ${S.bowX + 6},${forestayTop + (1 - (1 - furl * 0.35)) * 150 + 20} ${S.mastX - 10 - furl * 30},${S.deckY - 14}`
    : kind === 'storm' ? `${S.bowX},${S.deckY - 8} ${S.bowX + 12},${S.deckY - 80} ${S.bowX + 45},${S.deckY - 12}` : null
  const gennaker = kind === 'gennaker' ? `M${S.bowX - 24} ${S.deckY - 6} Q${S.bowX - 70} ${top + 70} ${S.mastX - 6} ${top + 8} Q${S.mastX + 10} ${S.deckY - 90} ${S.mastX + 30} ${S.deckY - 22} Z` : null
  return { top, boomY, boomEnd, headY, main, reefLines, head, gennaker, hull: `${S.bowX - 8},${S.deckY - 4} ${S.sternX + 10},${S.deckY - 4} ${S.sternX},${S.deckY + 12} ${S.bowX + 10},${S.deckY + 12}` }
})
// ---- Polar (half disc, 250 x 250, wind from the top) -----------------------------------------------------------------
const PC = { x: 20, y: 125, R: 108 }
const polar = computed(() => {
  const b = props.boat, tws = r.value.wind.kt, curves = polarCurves(b, [6, 10, 14, 20, 25]), max = Math.max(9, ...curves.flatMap(c => c.points.map(p => p.speed)))
  const xy = (twa, v) => [PC.x + Math.sin(rad(twa)) * v / max * PC.R, PC.y - Math.cos(rad(twa)) * v / max * PC.R]
  const path = pts => pts.map((p, k) => `${k ? 'L' : 'M'}${xy(p.twa, p.speed).map(v => v.toFixed(1)).join(' ')}`).join('')
  const nowCurve = Array.from({ length: 31 }, (_, k) => { const a = 30 + k * 5; return { twa: a, speed: polarSpeed(b, tws, a) } })
  const vmg = optimalVmg(b, tws), twaNow = Math.abs(r.value.boat.twa), vNow = polarSpeed(b, tws, twaNow)
  const rings = [2, 4, 6, 8, 10].filter(v => v <= max).map(v => ({ v, r: v / max * PC.R }))
  return { curves: curves.map(c => ({ tws: c.tws, d: path(c.points) })), now: path(nowCurve), rings, vmg, up: xy(vmg.upTwa, vmg.upSpeed), down: xy(vmg.downTwa, vmg.downSpeed), point: xy(twaNow, vNow), vNow, max }
})
// ---- The manoeuvre: charts and track ---------------------------------------------------------------------------------
const sim = computed(() => props.turn.sim)
const charts = computed(() => {
  const s = sim.value; if (!s) return null
  const W = 260, H = 70, T = s.summary.durationS, vmax = Math.max(...s.samples.map(x => x.speedKt)) * 1.1 || 1
  const X = t => 4 + t / T * (W - 8)
  const speed = s.samples.map((x, k) => `${k ? 'L' : 'M'}${X(x.t).toFixed(1)} ${(H - 4 - x.speedKt / vmax * (H - 10)).toFixed(1)}`).join('')
  const h0 = s.fromHeadingDeg, span = Math.abs(s.deltaDeg) || 1
  const heading = s.samples.map((x, k) => { const d = Math.abs(((x.headingDeg - h0 + 540) % 360) - 180); return `${k ? 'L' : 'M'}${X(x.t).toFixed(1)} ${(H - 4 - Math.min(1, d / span) * (H - 10)).toFixed(1)}` }).join('')
  // Track: the path in metres, fitted to the box, with a boat outline every ~4 s.
  const xs = s.samples.map(x => x.x), ys = s.samples.map(x => x.y), minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys)
  const span2 = Math.max(maxX - minX, maxY - minY, props.boat.loaM * 4), sc = 180 / span2, cx = (minX + maxX) / 2, cy = (minY + maxY) / 2
  const TX = x => 110 + (x - cx) * sc, TY = y => 100 - (y - cy) * sc
  const track = s.samples.map((x, k) => `${k ? 'L' : 'M'}${TX(x.x).toFixed(1)} ${TY(x.y).toFixed(1)}`).join('')
  const every = Math.max(1, Math.round(4 / (s.samples[1]?.t - s.samples[0].t || 0.25)))
  const L = props.boat.loaM * sc
  const ghosts = s.samples.filter((x, k) => k % every === 0).map(x => ({ x: TX(x.x), y: TY(x.y), h: x.headingDeg, L: Math.max(6, L), luff: x.luffing }))
  return { W, H, T, X, speed, heading, track, ghosts, vmax, start: { x: TX(0), y: TY(0) } }
})
const cursorX = computed(() => (charts.value ? charts.value.X(Math.min(props.turn.t, charts.value.T)) : 0))
// The wind of the simulated turn (the heading and TWA at its start give it back).
const windFrom = computed(() => (sim.value ? sim.value.fromHeadingDeg + sim.value.fromTwa : r.value.wind.fromDeg))
const kindNow = computed(() => (Math.abs(r.value.boat.twa) < 90 ? 'tack' : 'gybe'))
const reefAt = computed(() => { const R = props.settings.reefKt; return [R, R + 7, R + 14] })
const setNum = (key, e, lo, hi) => { const v = Number(e.target.value); if (Number.isFinite(v) && v >= lo && v <= hi) emit('settings', { [key]: v }) }
const PHASES = { approach: 'steady before the turn', turning: 'turning', 'head to wind': 'head to wind: sails luffing', 'stern to wind': 'stern to wind: the boom crosses', accelerating: 'accelerating on the new side', steady: 'back to speed' }
</script>

<template>
  <section class="comparison wx-report sail-trim" aria-label="Sails and trim">
    <div class="wx-head">
      <div>
        <div class="section-label">SAILS AND TRIM · {{ boat.label.toUpperCase() }} · FOR THE WIND AT THE BOAT</div>
        <h3>{{ plan.text }} <small>{{ Math.round(r.wind.kt) }} kt true from {{ compass16(r.wind.fromDeg) }} · apparent {{ Math.round(r.boat.aws) }} kt at {{ Math.round(Math.abs(r.boat.awa)) }}° {{ r.boat.awa >= 0 ? 'starboard' : 'port' }} · heel {{ Math.round(plan.heelDeg) }}°</small></h3>
      </div>
      <div class="st-boat">
        <label>Boat <select :value="settings.boat" aria-label="Boat" @change="emit('settings', { boat: $event.target.value })"><option v-for="b in BOATS" :key="b.key" :value="b.key">{{ b.label }}</option></select></label>
        <label><input type="checkbox" :checked="settings.usePolar" @change="emit('settings', { usePolar: $event.target.checked })"> speed from the polar</label>
      </div>
    </div>

    <div class="st-grid">
      <div class="wx-card st-top">
        <div class="wx-small-label">FROM ABOVE · NORTH UP<template v-if="turn.playing"> · {{ sim.kind.toUpperCase() }} {{ turn.t.toFixed(1) }} s</template></div>
        <svg viewBox="0 0 260 260" class="st-svg" role="img" :aria-label="`Boat heading ${Math.round(live.headingDeg)} degrees, boom ${Math.round(Math.abs(live.boomDeg))} degrees to ${live.boomDeg >= 0 ? 'starboard' : 'port'}`">
          <defs>
            <marker id="st-tw" viewBox="0 0 6 6" refX="5" refY="3" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0L6 3L0 6z" fill="#6fa8ff"/></marker>
            <marker id="st-aw" viewBox="0 0 6 6" refX="5" refY="3" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0L6 3L0 6z" fill="#7fe0ff"/></marker>
            <radialGradient id="st-sea"><stop offset="0" stop-color="#123247"/><stop offset="1" stop-color="#0a1b27"/></radialGradient>
          </defs>
          <circle cx="130" cy="130" r="124" fill="url(#st-sea)" stroke="#264252"/>
          <text x="130" y="16" class="st-n">N</text>
          <line :x1="top.tw.x1" :y1="top.tw.y1" :x2="top.tw.x2" :y2="top.tw.y2" stroke="#6fa8ff" stroke-width="3" marker-end="url(#st-tw)"/>
          <line :x1="top.aw.x1" :y1="top.aw.y1" :x2="top.aw.x2" :y2="top.aw.y2" stroke="#7fe0ff" stroke-width="2" stroke-dasharray="4 3" marker-end="url(#st-aw)"/>
          <polygon :points="top.hull" class="st-hull"/>
          <line v-if="top.sprit" :x1="top.sprit[0][0]" :y1="top.sprit[0][1]" :x2="top.sprit[1][0]" :y2="top.sprit[1][1]" stroke="#9aa7ae" stroke-width="2"/>
          <path v-if="top.head" :d="top.head" class="st-sail" :class="{ luff: live.luffing, gennaker: plan.headsail.kind === 'gennaker', storm: plan.headsail.kind === 'storm' }"/>
          <line :x1="top.boom[0][0]" :y1="top.boom[0][1]" :x2="top.boom[1][0]" :y2="top.boom[1][1]" stroke="#c9d1d6" stroke-width="3" stroke-linecap="round"/>
          <path v-if="top.main" :d="top.main" class="st-sail" :class="{ luff: live.luffing, storm: plan.main.kind === 'trysail' }"/>
          <circle :cx="top.mast[0]" :cy="top.mast[1]" r="3.2" fill="#e8eef1"/>
        </svg>
        <p class="st-legend"><span class="tw">true wind</span> <span class="aw">apparent</span> · boom {{ Math.round(Math.abs(live.boomDeg)) }}° {{ live.boomDeg >= 0 ? 'to starboard' : 'to port' }} · headsail {{ Math.round(Math.abs(live.jibDeg)) }}°<template v-if="plan.gooseWing"> (poled to windward)</template> · {{ live.speedKt.toFixed(1) }} kt<template v-if="turn.playing && live.phase"> · {{ PHASES[live.phase] ?? live.phase }}</template></p>
      </div>

      <div class="wx-card st-side">
        <div class="wx-small-label">SAIL PLAN · {{ Math.round(plan.areaM2) }} M² SET</div>
        <svg :viewBox="`0 0 ${SIDE.W} ${SIDE.H}`" class="st-svg" role="img" :aria-label="plan.text">
          <line :x1="SIDE.mastX" :y1="SIDE.deckY" :x2="SIDE.mastX" :y2="side.top" stroke="#c9d1d6" stroke-width="3"/>
          <line :x1="SIDE.bowX" :y1="SIDE.deckY - 4" :x2="SIDE.mastX" :y2="side.top + 18" stroke="#7f8c93" stroke-width="1"/>
          <line :x1="SIDE.mastX" :y1="side.top" :x2="SIDE.sternX" :y2="SIDE.deckY - 4" stroke="#7f8c93" stroke-width="1"/>
          <polygon v-if="side.head" :points="side.head" class="st-sail2" :class="{ storm: plan.headsail.kind === 'storm' }"/>
          <path v-if="side.gennaker" :d="side.gennaker" class="st-sail2 gennaker"/>
          <polygon v-if="side.main" :points="side.main" class="st-sail2" :class="{ storm: plan.main.kind === 'trysail' }"/>
          <template v-if="plan.main.kind === 'main'"><line v-for="l in side.reefLines" :key="l.k" :x1="SIDE.mastX" :x2="l.x2" :y1="l.y" :y2="l.y" class="st-reef"/></template>
          <rect v-if="plan.main.reef" :x="SIDE.mastX" :y="side.boomY - 6" :width="70" height="6" rx="3" fill="#d9d9cf"/>
          <line :x1="SIDE.mastX" :y1="side.boomY" :x2="side.boomEnd" :y2="side.boomY" stroke="#c9d1d6" stroke-width="3" stroke-linecap="round"/>
          <polygon :points="side.hull" class="st-hull"/>
          <text :x="SIDE.mastX + 8" :y="side.top + 12" class="st-label">{{ plan.main.label }}</text>
          <text :x="6" :y="SIDE.H - 4" class="st-label">{{ plan.headsail.label }}</text>
        </svg>
        <p class="st-legend">Dashed: the reef points still to take. 1st reef from {{ reefAt[0] }} kt, 2nd from {{ reefAt[1] }} kt, 3rd from {{ reefAt[2] }} kt of {{ Math.abs(r.boat.awa) < 90 ? 'apparent' : 'true' }} wind; storm sails from gale force.</p>
      </div>

      <div class="wx-card st-trimcard">
        <div class="wx-small-label">TRIM</div>
        <ul class="st-trim"><li v-for="(t, k) in plan.trim" :key="k">{{ t }}</li></ul>
        <dl class="wx-dl">
          <dt>Sail for</dt><dd>{{ Math.round(plan.effectiveKt) }} kt {{ Math.abs(r.boat.awa) < 90 ? 'apparent' : 'true' }}</dd>
          <dt>Heel</dt><dd>{{ Math.round(plan.heelDeg) }}° to {{ plan.leeward }}</dd>
          <dt>Speed</dt><dd>{{ r.boat.speedKt.toFixed(1) }} kt<small> {{ settings.usePolar ? 'from the polar' : 'as set' }} · hull speed {{ hullSpeedKt(boat).toFixed(1) }} kt</small></dd>
        </dl>
        <div class="wx-limits">
          <label>Reef at <input type="number" min="6" max="40" step="1" :value="settings.reefKt" aria-label="First reef at (kt)" @change="setNum('reefKt', $event, 6, 40)"> kt</label>
          <label><input type="checkbox" :checked="settings.gennaker" @change="emit('settings', { gennaker: $event.target.checked })"> carry a gennaker</label>
        </div>
      </div>

      <div class="wx-card st-polar">
        <div class="wx-small-label">POLAR · {{ boat.label.toUpperCase() }} (GENERIC)</div>
        <svg viewBox="0 0 250 250" class="st-svg" role="img" aria-label="Polar diagram">
          <circle v-for="g in polar.rings" :key="g.v" :cx="PC.x" :cy="PC.y" :r="g.r" class="st-ring"/>
          <text v-for="g in polar.rings" :key="`t${g.v}`" :x="PC.x + 3" :y="PC.y - g.r - 2" class="st-ringlabel">{{ g.v }} kt</text>
          <line :x1="PC.x" :y1="PC.y - PC.R - 6" :x2="PC.x" :y2="PC.y + PC.R + 6" stroke="#3a5260"/>
          <text :x="PC.x + 4" :y="12" class="st-label">wind ↓</text>
          <path v-for="c in polar.curves" :key="c.tws" :d="c.d" class="st-curve"/>
          <path :d="polar.now" class="st-curve now"/>
          <line :x1="PC.x" :y1="PC.y" :x2="polar.up[0]" :y2="polar.up[1]" class="st-vmg"/>
          <line :x1="PC.x" :y1="PC.y" :x2="polar.down[0]" :y2="polar.down[1]" class="st-vmg"/>
          <circle :cx="polar.point[0]" :cy="polar.point[1]" r="5" class="st-now"/>
        </svg>
        <p class="st-legend">At {{ Math.round(r.wind.kt) }} kt (bold): best VMG upwind {{ polar.vmg.upVmg.toFixed(1) }} kt at {{ Math.round(polar.vmg.upTwa) }}°, downwind {{ polar.vmg.downVmg.toFixed(1) }} kt at {{ Math.round(polar.vmg.downTwa) }}°; now {{ polar.vNow.toFixed(1) }} kt at {{ Math.round(Math.abs(r.boat.twa)) }}°. Thin: 6, 10, 14, 20, 25 kt.</p>
      </div>

      <div class="wx-card st-turn">
        <div class="wx-small-label">SIMULATE A TURN · REAL TIME · THE 3D BOAT TURNS TOO</div>
        <div class="st-actions">
          <button :class="{ primary: kindNow === 'tack' }" :disabled="turn.playing" :title="`Tack: turn the bow through the wind onto ${deg3(r.wind.fromDeg + r.boat.twa)}°`" @click="emit('turn', 'tack')">⤺ Tack</button>
          <button :class="{ primary: kindNow === 'gybe' }" :disabled="turn.playing" :title="`Gybe: turn the stern through the wind onto ${deg3(r.wind.fromDeg + r.boat.twa)}°`" @click="emit('turn', 'gybe')">⤻ Gybe</button>
          <button v-if="sim" :disabled="turn.playing" @click="emit('replay')">▶ Replay</button>
          <label>speed <select :value="turn.rate" aria-label="Playback speed" @change="emit('rate', Number($event.target.value))"><option :value="0.5">×½</option><option :value="1">×1</option><option :value="2">×2</option><option :value="4">×4</option></select></label>
        </div>
        <p v-if="!sim" class="wx-note">{{ kindNow === 'tack' ? 'The wind is forward of the beam: a tack brings the bow through the wind onto the other tack.' : 'The wind is aft of the beam: a gybe brings the stern through the wind and the boom across.' }}<template v-if="kindNow === 'gybe' && r.wind.kt >= STRONG_GYBE_KT"> In {{ Math.round(r.wind.kt) }} kt consider tacking round instead.</template></p>
        <template v-else>
          <p class="st-turn-head"><b>{{ sim.kind === 'tack' ? 'Tack' : 'Gybe' }}</b> {{ deg3(sim.fromHeadingDeg) }}° → {{ deg3(sim.toHeadingDeg) }}° · true wind angle {{ Math.round(Math.abs(sim.fromTwa)) }}° {{ sim.fromTwa >= 0 ? 'starboard' : 'port' }} → {{ Math.round(Math.abs(sim.toTwa)) }}° {{ sim.toTwa >= 0 ? 'starboard' : 'port' }}</p>
          <div class="st-turn-grid">
            <svg :viewBox="`0 0 220 200`" class="st-svg st-track" role="img" aria-label="Track of the manoeuvre">
              <path :d="charts.track" class="st-trackline"/>
              <g v-for="(gh, k) in charts.ghosts" :key="k" :transform="`translate(${gh.x} ${gh.y}) rotate(${gh.h})`"><polygon :points="`0,${-gh.L / 2} ${gh.L * 0.17},${gh.L * 0.2} 0,${gh.L / 2} ${-gh.L * 0.17},${gh.L * 0.2}`" :class="{ luff: gh.luff }" class="st-ghost"/></g>
              <text :x="charts.start.x + 6" :y="charts.start.y - 6" class="st-label">start</text>
              <line :x1="200 + 9 * Math.sin(windFrom * Math.PI / 180)" :y1="20 - 9 * Math.cos(windFrom * Math.PI / 180)" :x2="200 - 9 * Math.sin(windFrom * Math.PI / 180)" :y2="20 + 9 * Math.cos(windFrom * Math.PI / 180)" stroke="#6fa8ff" stroke-width="2" marker-end="url(#st-tw)"><title>true wind</title></line>
              <text x="4" y="196" class="st-label">{{ Math.round(Math.max(...sim.samples.map(x => Math.hypot(x.x, x.y)))) }} m sailed · boat {{ boat.loaM }} m</text>
            </svg>
            <div>
              <svg :viewBox="`0 0 ${charts.W} ${charts.H}`" class="st-svg st-chart" role="img" aria-label="Speed through the manoeuvre"><path :d="charts.speed" class="st-speed"/><line :x1="cursorX" :x2="cursorX" y1="0" :y2="charts.H" class="st-cursor"/><text x="4" y="10" class="st-label">speed (max {{ charts.vmax.toFixed(1) }} kt)</text></svg>
              <svg :viewBox="`0 0 ${charts.W} ${charts.H}`" class="st-svg st-chart" role="img" aria-label="Heading through the manoeuvre"><path :d="charts.heading" class="st-headingline"/><line :x1="cursorX" :x2="cursorX" y1="0" :y2="charts.H" class="st-cursor"/><text x="4" y="10" class="st-label">heading change ({{ Math.round(Math.abs(sim.deltaDeg)) }}°)</text></svg>
              <dl class="wx-dl">
                <dt>Duration</dt><dd>{{ sim.summary.durationS.toFixed(0) }} s <small>turn {{ sim.summary.turnS.toFixed(0) }} s</small></dd>
                <dt>Speed</dt><dd>{{ sim.summary.startSpeedKt.toFixed(1) }} → {{ sim.summary.minSpeedKt.toFixed(1) }} kt <small>−{{ Math.round(sim.summary.speedLossPct) }} %</small></dd>
                <dt>Lost</dt><dd>{{ Math.round(sim.summary.distanceLostM) }} m <small>{{ sim.summary.boatLengthsLost.toFixed(1) }} boat lengths, {{ Math.round(sim.summary.timeLostS) }} s {{ sim.kind === 'tack' ? 'upwind' : 'downwind' }}</small></dd>
              </dl>
            </div>
          </div>
          <ul v-if="sim.warnings.length" class="wx-hazards"><li v-for="(w, k) in sim.warnings" :key="k" class="warning">{{ w }}</li></ul>
        </template>
      </div>
    </div>
    <p class="wx-note">Sail plan and trim by cruising rules of thumb (reef by the apparent wind upwind and the true wind downwind); heel and the manoeuvre are illustrative models of a generic boat: the heading swings at {{ boat.turnRateDegS }}°/s on an eased curve, the speed follows the polar with a lag of {{ boat.accelTauS }} s (quicker to slow), the sails luff head to wind and the boom crosses when the apparent wind does.</p>
  </section>
</template>
