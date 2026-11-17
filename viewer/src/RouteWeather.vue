<script setup>
// The weather along a planned flight route (routeWeather.js): a strip along the distance with the weather at every
// route sample when the aircraft passes it, the route forecast by stretch in the order flown, and a summary under the
// GAMET headings. Numbers from routeWeather.js; this component lays them out.
import { computed } from 'vue'
import WxStrip from './WxStrip.vue'
import WeatherIcon from './WeatherIcon.vue'
import { stretchLine, categoryColour, windText } from './routeWeather.js'
import { GAFOR, UNKNOWN_GAFOR } from './gafor.js'
import { HAZARD_INFO } from './flightPlan.js'
import { SYMBOLS } from './weatherLayers.js'

const props = defineProps({
  weather: { type: Object, required: true },        // routeWeather(...)
  plan: { type: Object, required: true },           // planFlight(...)
  northernHemisphere: { type: Boolean, default: true },
})
const emit = defineEmits(['seek'])
const w = computed(() => props.weather)
const utc = ms => { const d = new Date(ms); return `${String(d.getUTCHours()).padStart(2, '0')}:${String(d.getUTCMinutes()).padStart(2, '0')}Z` }
const r100 = v => Math.round(v / 100) * 100
const ft = v => `${r100(v).toLocaleString('en-US')} ft`
const visColour = m => (m === null ? '#31434e' : m >= 10000 ? '#2e9e6a' : m >= 8000 ? '#3ddc84' : m >= 5000 ? '#ffd166' : m >= 1500 ? '#ff9f43' : '#ff5252')
const visText = m => (m === null ? '—' : m >= 10000 ? '10+ km' : m >= 1000 ? `${(m / 1000).toFixed(m < 5000 ? 1 : 0)} km` : `${Math.round(m / 50) * 50} m`)
const ceilColour = f => (f === null ? '#31434e' : !Number.isFinite(f) ? '#2a4b62' : f >= 5000 ? '#3c7fb0' : f >= 2000 ? '#3ddc84' : f >= 1000 ? '#ffd166' : f >= 500 ? '#ff9f43' : '#ff5252')
const precipColour = wx => !wx?.kind ? 'transparent' : ({ snow: '#dfe9ff', sleet: '#b9c8ff', drizzle: '#8fd0ff' }[wx.kind] ?? (wx.showers ? '#43a1ff' : '#2f7de0'))
const HAZ = [['thunderstorm', 'CB / TS'], ['icing', 'Icing'], ['turbulence', 'Turbulence'], ['cloud', 'In cloud'], ['mtobsc', 'Mt obscured'], ['terrain', 'Terrain']]
const hazardInfo = k => (k === 'mtobsc' ? { label: 'Mountains obscured', colour: '#c9a6ff' } : HAZARD_INFO[k])
const rows = computed(() => {
  const s = w.value.samples, band = (x, colour, title, extra = {}) => ({ from: x.fromNm, to: x.toNm, colour, title, data: x, ...extra })
  const at = x => `${Math.round(x.fromNm)}–${Math.round(x.toNm)} NM, ${utc(x.startMs)}`
  const marks = (every, make) => { const out = [], step = Math.max(1, Math.ceil(s.length / every)); for (let k = Math.floor(step / 2); k < s.length; k += step) out.push(make(s[k])); return out }
  const list = [
    { label: 'Weather', tall: true, bands: s.map(x => band(x, 'rgba(127,224,255,0.04)', `${at(x)}: ${x.surface.words ?? (SYMBOLS[x.surface.symbol]?.label ?? 'no data')}`)), marks: w.value.stretches.map(st => ({ at: (st.fromNm + st.toNm) / 2, icon: st.symbol, night: st.night, title: `${Math.round(st.fromNm)}–${Math.round(st.toNm)} NM: ${stretchLine(st)}` })) },
    { label: 'Flight category', bands: s.map(x => band(x, categoryColour(x.surface.category), `${at(x)}: ${x.surface.category}`)) },
    { label: 'GAFOR', bands: s.map(x => band(x, (GAFOR[x.surface.gafor] ?? UNKNOWN_GAFOR).colour, `${at(x)}: GAFOR ${x.surface.gafor}`)) },
    { label: 'Visibility', bands: s.map(x => band(x, visColour(x.surface.visibilityM), `${at(x)}: visibility ${visText(x.surface.visibilityM)}${x.surface.visCause ? ` (${x.surface.visCause})` : ''}`)) },
    { label: 'Ceiling AGL', bands: s.map(x => band(x, ceilColour(x.surface.ceilingAglFt), `${at(x)}: ${x.surface.ceilingAglFt === null ? 'not known' : Number.isFinite(x.surface.ceilingAglFt) ? `ceiling ${ft(x.surface.ceilingAglFt)} AGL` : 'no ceiling'}${x.surface.cloud ? ` · ${x.surface.cloud.cover} ${x.surface.cloud.abbreviation} ${ft(x.surface.cloud.baseFt)}–${x.surface.cloud.topFt ? ft(x.surface.cloud.topFt) : '?'} AMSL` : ''}`)) },
    { label: 'Precipitation', bands: s.map(x => band(x, precipColour(x.surface.wx), `${at(x)}: ${x.surface.wx?.kind ? `${x.surface.wx.intensity} ${x.surface.wx.kind}${x.surface.wx.showers ? ' showers' : ''}` : 'none'}`)) },
    { label: 'Wind at altitude', tall: true, bands: s.map(x => band(x, 'rgba(127,224,255,0.04)', `${at(x)}: ${Math.round(x.aloft.wind.fromDeg / 10) * 10 || 360}°/${Math.round(x.aloft.wind.kt)} kt at ${ft(x.altFt)}, ${Math.round(x.aloft.tempC)} °C`)), marks: marks(14, x => ({ at: x.distNm, barb: x.aloft.wind, title: `${Math.round(x.distNm)} NM at ${ft(x.altFt)}: ${Math.round(x.aloft.wind.kt)} kt` })) },
    { label: 'Temperature', bands: s.map(x => band(x, x.aloft.tempC <= 0 ? 'rgba(111,184,255,0.35)' : 'rgba(255,177,92,0.25)', `${at(x)}: ${Math.round(x.aloft.tempC)} °C at ${ft(x.altFt)} (ISA ${x.aloft.isaDevC >= 0 ? '+' : '−'}${Math.round(Math.abs(x.aloft.isaDevC))}); freezing level ${x.freezingFt === null ? '—' : ft(x.freezingFt)}`)), marks: marks(10, x => ({ at: x.distNm, text: `${Math.round(x.aloft.tempC)}°` })) },
  ]
  for (const [key, label] of HAZ) {
    const on = s.filter(x => x.hazards.includes(key)); if (!on.length) continue
    list.push({ label, bands: on.map(x => band(x, hazardInfo(key).colour, `${at(x)}: ${hazardInfo(key).label}`)) })
  }
  return list
})
const ticks = computed(() => {
  const L = props.plan.lengthNm, step = [10, 20, 25, 50, 100, 200, 250, 500].find(v => L / v <= 8) ?? 1000, out = []
  for (let d = 0; d <= L + 1e-6; d += step) { const seg = w.value.samples.find(x => d <= x.toNm + 1e-9) ?? w.value.samples.at(-1); out.push({ at: d, label: `${d} NM · ${utc(seg.startMs + (d - seg.fromNm) / Math.max(1e-9, seg.toNm - seg.fromNm) * (seg.endMs - seg.startMs))}` }) }
  return out
})
const LEVEL_CLASS = { nil: 'nil', info: 'info', caution: 'caution', warning: 'warning', severe: 'severe' }
</script>

<template>
  <div class="wx-card rw">
    <div class="rw-head">
      <div class="wx-small-label">WEATHER ALONG THE ROUTE · AT THE TIME FLOWN · {{ w.samples.length }} POINTS, EVERY {{ (plan.lengthNm / w.samples.length).toFixed(1) }} NM</div>
      <div class="rw-worst">
        <WeatherIcon :symbol="w.worst.symbol" :size="26"/>
        <span class="vfr-badge" :style="{ background: categoryColour(w.worst.category) }">{{ w.worst.category }}</span>
        <span class="vfr-badge" :style="{ background: (GAFOR[w.worst.gafor] ?? UNKNOWN_GAFOR).colour }">{{ w.worst.gafor }}</span>
        <span>worst visibility {{ visText(w.worst.visibilityM) }} · lowest ceiling {{ w.worst.ceilingAglFt === null ? '—' : Number.isFinite(w.worst.ceilingAglFt) ? `${ft(w.worst.ceilingAglFt)} AGL` : 'none' }}</span>
      </div>
    </div>
    <WxStrip :span="[0, plan.lengthNm]" :rows="rows" :ticks="ticks" :northern-hemisphere="northernHemisphere" @pick="b => emit('seek', b.data.frameIndex)"/>
    <div class="rw-cols">
      <div>
        <div class="wx-small-label">ROUTE FORECAST · IN THE ORDER FLOWN · CLICK A ROW FOR ITS FRAME</div>
        <table class="rw-table">
          <thead><tr><th>NM · time</th><th></th><th>Weather · cloud · visibility</th><th>Wind / temp at altitude</th><th>Freezing level</th><th>Cat · GAFOR</th><th>Hazards</th></tr></thead>
          <tbody>
            <tr v-for="(s, k) in w.stretches" :key="k" @click="emit('seek', w.samples.find(x => x.fromNm >= s.fromNm - 1e-9)?.frameIndex ?? 0)">
              <th>{{ Math.round(s.fromNm) }}–{{ Math.round(s.toNm) }}<small>{{ utc(s.startMs) }}–{{ utc(s.endMs) }} · {{ s.phases.join('/') }}</small></th>
              <td><WeatherIcon :symbol="s.symbol" :night="!!s.night" :size="26"/></td>
              <td class="rw-words">{{ stretchLine(s) }}<small v-if="s.ceilingMinAglFt !== null && Number.isFinite(s.ceilingMinAglFt)">ceiling {{ ft(s.ceilingMinAglFt) }} AGL</small><small v-if="s.vfrTopMinFt !== null">VFR up to {{ ft(s.vfrTopMinFt) }}</small></td>
              <td>{{ windText(s.windAloft) }}<small>{{ ft(s.altFt.min) }}{{ s.altFt.max - s.altFt.min > 100 ? `–${ft(s.altFt.max)}` : '' }} · {{ Math.round(s.tempC.min) }}{{ Math.round(s.tempC.max) !== Math.round(s.tempC.min) ? ` to ${Math.round(s.tempC.max)}` : '' }} °C</small></td>
              <td>{{ s.freezingMinFt === null ? '—' : s.freezingMinFt <= 0 ? 'surface' : ft(s.freezingMinFt) }}</td>
              <td><span class="vfr-badge" :style="{ background: categoryColour(s.category) }">{{ s.category }}</span> <span class="vfr-badge" :style="{ background: (GAFOR[s.gafor] ?? UNKNOWN_GAFOR).colour }">{{ s.gafor }}</span></td>
              <td><span v-for="h in s.hazards.filter(x => x !== 'outside')" :key="h" class="fp-chip" :style="{ borderColor: hazardInfo(h).colour, color: hazardInfo(h).colour }">{{ h === 'cloud' ? 'IMC' : h === 'mtobsc' ? 'MT OBSC' : hazardInfo(h).label.split(' ')[0] }}</span></td>
            </tr>
          </tbody>
        </table>
      </div>
      <div>
        <div class="wx-small-label">SUMMARY (GAMET HEADINGS)</div>
        <dl class="rw-gamet">
          <template v-for="g in w.gamet" :key="g.key"><dt :class="LEVEL_CLASS[g.level]" :title="g.label">{{ g.key }}</dt><dd :class="LEVEL_CLASS[g.level]">{{ g.text }}</dd></template>
        </dl>
      </div>
    </div>
    <p class="wx-note">Every point of the route is read from the frame valid when the aircraft passes it: the surface (wind, weather, estimated visibility, the engine's single cloud layer and ceiling, QNH, flight category and GAFOR) and the air at the altitude flown (wind, temperature, cloud, icing, turbulence). The summary follows the headings of the ICAO low-level area forecast (GAMET). Simulated: not a forecast.</p>
  </div>
</template>
