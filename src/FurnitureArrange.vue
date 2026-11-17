<script setup>
// Connecting furniture (snap while moving) and the distances auto-arrange keeps free.
const props=defineProps({connect:Boolean,options:{type:Object,required:true}});
const emit=defineEmits(['update:connect','update:options']);
const FIELDS=[['passage','Walkway width m'],['doorDepth','Clear in front of doors m'],['windowDepth','Clear in front of windows m'],['serviceDepth','Clear in front of furniture m']];
const set=(key,event)=>{const value=Number(event.target.value);if(event.target.value!==''&&Number.isFinite(value)&&value>=0&&value<=3) emit('update:options',{...props.options,[key]:value});else event.target.value=props.options[key];};
</script>
<template>
  <section class="furniture-arrange" aria-label="Connect and arrange furniture">
    <h3>Connect and arrange</h3>
    <label class="snap-toggle"><input :checked="connect" type="checkbox" aria-label="Snap furniture to walls and furniture" @change="emit('update:connect',$event.target.checked)">Snap to walls and furniture</label>
    <p class="hint">Moved and new items within 0.15 m of a wall or another item connect to it: flush against the wall (keeping the minimum wall gap), side to side, corners in line. Snaps that would enter a door or window zone are skipped.</p>
    <label v-for="[key,label] in FIELDS" :key="key">{{ label }}<input :value="options[key]" type="number" min="0" max="3" step="0.05" @change="set(key,$event)"></label>
    <p class="hint">Auto-arrange (furniture toolbar) moves the selected items, or all items, back to the wall and facing into the room, largest first, each to the nearest free corner or neighbour. Doors and their swing, windows (for items taller than the sill), the space in front of each item and a walkway from every door stay clear. Items with no wall space stay put, or move to the nearest free floor space.</p>
  </section>
</template>
