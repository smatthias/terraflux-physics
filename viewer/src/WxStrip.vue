<script setup>
// A weather strip along a route: rows of coloured bands (category, visibility, Beaufort force, sails...) and marks
// (wind barbs, weather symbols, short texts) over one axis (distance or time), with ticks and an optional cursor.
// Used by the flight route weather (RouteWeather.vue) and the sailing route (SailPlan.vue). Clicking a band emits it.
import WindBarb from './WindBarb.vue'
import WeatherIcon from './WeatherIcon.vue'

const props = defineProps({
  span: { type: Array, required: true },              // [start, end] in the axis unit
  rows: { type: Array, required: true },              // [{ label, bands: [{ from, to, colour, title, text, data }], marks: [{ at, barb?, icon?, text?, title? }] }]
  ticks: { type: Array, default: () => [] },          // [{ at, label }]
  cursor: { type: Number, default: null },            // axis position of a cursor line
  northernHemisphere: { type: Boolean, default: true },
})
const emit = defineEmits(['pick'])
const pct = v => `${Math.max(0, Math.min(100, (v - props.span[0]) / ((props.span[1] - props.span[0]) || 1) * 100))}%`
const width = (a, b) => `${Math.max(0.25, (Math.min(b, props.span[1]) - Math.max(a, props.span[0])) / ((props.span[1] - props.span[0]) || 1) * 100)}%`
</script>

<template>
  <div class="wxs">
    <div class="wxs-row wxs-axis"><span class="wxs-label"></span><div class="wxs-track"><span v-for="(t, k) in ticks" :key="k" class="wxs-tick" :style="{ left: pct(t.at) }">{{ t.label }}</span></div></div>
    <div v-for="(row, r) in rows" :key="r" class="wxs-row" :class="{ tall: row.tall }">
      <span class="wxs-label" :title="row.title ?? row.label">{{ row.label }}</span>
      <div class="wxs-track">
        <button v-for="(b, k) in row.bands" :key="k" class="wxs-band" :style="{ left: pct(b.from), width: width(b.from, b.to), background: b.colour }" :title="b.title" :aria-label="b.title" @click="emit('pick', b)"><span v-if="b.text">{{ b.text }}</span></button>
        <span v-for="(m, k) in row.marks ?? []" :key="`m${k}`" class="wxs-mark" :style="{ left: pct(m.at) }" :title="m.title">
          <WindBarb v-if="m.barb" :kt="m.barb.kt" :from-deg="m.barb.fromDeg" :size="row.tall ? 34 : 26" :northern-hemisphere="northernHemisphere"/>
          <WeatherIcon v-else-if="m.icon" :symbol="m.icon" :night="!!m.night" :size="row.tall ? 30 : 22"/>
          <b v-else>{{ m.text }}</b>
        </span>
        <i v-if="cursor !== null && cursor >= span[0] && cursor <= span[1]" class="wxs-cursor" :style="{ left: pct(cursor) }"></i>
      </div>
    </div>
  </div>
</template>
