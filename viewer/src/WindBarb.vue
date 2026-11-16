<script setup>
// One wind barb as SVG (north up): direction and speed in a single symbol, from barbs.js.
import { computed } from 'vue'
import { barbSvg, barbColour } from './barbs.js'
const props = defineProps({
  kt: { type: Number, required: true },
  fromDeg: { type: Number, required: true },
  size: { type: Number, default: 40 },
  northernHemisphere: { type: Boolean, default: true },
  colour: { type: String, default: null },
})
const g = computed(() => barbSvg(props.kt, props.fromDeg, { size: props.size, northernHemisphere: props.northernHemisphere }))
const stroke = computed(() => props.colour ?? barbColour(props.kt))
</script>

<template>
  <svg class="wind-barb" :width="size" :height="size" :viewBox="`0 0 ${size} ${size}`" role="img" :aria-label="g.calm ? 'calm' : `wind from ${Math.round(fromDeg)}° at ${Math.round(kt)} kt`">
    <circle :cx="size / 2" :cy="size / 2" r="1.8" :fill="stroke"/>
    <circle v-if="g.calm" :cx="g.circle.cx" :cy="g.circle.cy" :r="g.circle.r" fill="none" :stroke="stroke" stroke-width="1.5"/>
    <line v-for="(l, k) in g.lines" :key="k" :x1="l.x1" :y1="l.y1" :x2="l.x2" :y2="l.y2" :stroke="stroke" stroke-width="1.6" stroke-linecap="round"/>
    <polygon v-for="(p, k) in g.polygons" :key="`p${k}`" :points="p" :fill="stroke"/>
  </svg>
</template>
