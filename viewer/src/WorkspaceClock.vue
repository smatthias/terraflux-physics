<script setup>
// The clock on the right of the top bar: local time (seconds small), the date below, UTC in the tooltip.
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { clockParts } from './workspace.js'
const now = ref(new Date())
let timer = 0
const parts = computed(() => clockParts(now.value))
onMounted(() => {
  // Tick on the second boundary, so the seconds change together with the system clock.
  const tick = () => { now.value = new Date(); timer = setTimeout(tick, 1000 - (Date.now() % 1000) + 5) }
  tick()
})
onBeforeUnmount(() => clearTimeout(timer))
</script>

<template>
  <time class="ws-clock" :datetime="now.toISOString()" :title="`Local time · ${parts.utc}`" aria-live="off">
    <b>{{ parts.time }}<small>:{{ parts.seconds }}</small></b><span>{{ parts.date }}</span>
  </time>
</template>
