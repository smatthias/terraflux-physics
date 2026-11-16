<script setup>
// The taskbar at the bottom of the workspace: one button per inspector window (its colour, what it shows, where), the
// focused one lit, minimised ones dimmed. Click: restore a minimised window, minimise the focused one, focus another.
// Hovering a button draws an arrow from it to its data cell (the parent draws it; this reports where the button is).
import { ref } from 'vue'
const props = defineProps({
  items: { type: Array, required: true },   // [{ id, colour, title, detail, minimised, focused }]
  hint: { type: String, default: 'Click a data point to open a window' },
})
const emit = defineEmits(['activate', 'close', 'hover', 'minimise-all', 'close-all'])
const bar = ref(null)
function hover(e, id) {
  if (id === null) { emit('hover', null, null); return }
  const r = e.currentTarget.getBoundingClientRect()
  emit('hover', id, { x: r.left + r.width / 2, y: r.top })
}
</script>

<template>
  <nav ref="bar" class="taskbar" aria-label="Open windows">
    <span class="taskbar-label"><i></i>WORKSPACE<small>{{ items.length ? `${items.length} window${items.length === 1 ? '' : 's'}` : 'empty' }}</small></span>
    <div class="taskbar-items">
      <p v-if="!items.length" class="taskbar-hint">{{ hint }}</p>
      <div v-for="w in items" :key="w.id" class="taskbar-item" :data-window="w.id" :class="{ focused: w.focused, minimised: w.minimised }" :style="{ '--c': w.colour }">
        <button class="taskbar-button" :title="`${w.title} · ${w.detail} — ${w.minimised ? 'restore' : w.focused ? 'minimise' : 'bring to front'}`"
                @click="emit('activate', w.id)" @mouseenter="hover($event, w.id)" @mouseleave="hover($event, null)" @focus="hover($event, w.id)" @blur="hover($event, null)">
          <i class="dot"></i><span class="t">{{ w.title }}</span><small>{{ w.detail }}</small>
        </button>
        <button class="taskbar-close" :aria-label="`Close ${w.title}`" title="Close" @click="emit('close', w.id)">✕</button>
      </div>
    </div>
    <span class="taskbar-actions">
      <button :disabled="!items.some(w => !w.minimised)" title="Minimise all windows" @click="emit('minimise-all')">▁ All</button>
      <button :disabled="!items.length" title="Close all windows" @click="emit('close-all')">✕ All</button>
    </span>
  </nav>
</template>
