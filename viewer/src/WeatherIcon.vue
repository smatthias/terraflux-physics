<script setup>
// One weather symbol (weatherIcons.js) as a small canvas, for the legends and the reports.
import { ref, watch, onMounted } from 'vue'
import { drawWeatherIcon } from './weatherIcons.js'
import { SYMBOLS } from './weatherLayers.js'
const props = defineProps({ symbol: { type: String, default: null }, size: { type: Number, default: 28 }, night: { type: Boolean, default: false } })
const canvas = ref(null)
function paint() {
  const c = canvas.value; if (!c) return
  const dpr = typeof devicePixelRatio === 'number' ? Math.min(3, devicePixelRatio) : 1
  c.width = c.height = Math.round(props.size * dpr)
  const ctx = c.getContext('2d'); ctx.clearRect(0, 0, c.width, c.height)
  if (props.symbol) drawWeatherIcon(ctx, props.symbol, c.width, 0, 0, { night: props.night })
}
onMounted(paint)
watch(() => [props.symbol, props.size, props.night], paint)
</script>

<template>
  <canvas ref="canvas" class="weather-icon" :style="{ width: `${size}px`, height: `${size}px` }" role="img" :aria-label="symbol ? SYMBOLS[symbol]?.label ?? symbol : 'no weather data'" :title="symbol ? SYMBOLS[symbol]?.label ?? symbol : 'no weather data'"></canvas>
</template>
