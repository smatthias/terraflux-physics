<script setup>
import {ref} from 'vue';
const paper=ref('A4'),orientation=ref('landscape'),emit=defineEmits(['export']);
</script>
<template><section aria-label="Cabinet drawing sheets"><h3>Cabinet drawing sheets</h3><label>Cabinet sheet paper<select v-model="paper" aria-label="Cabinet sheet paper"><option>A4</option><option>A3</option></select></label><label>Cabinet sheet orientation<select v-model="orientation" aria-label="Cabinet sheet orientation"><option value="landscape">Landscape</option><option value="portrait">Portrait</option></select></label><button @click="emit('export',{paper,orientation})">Export printable cabinet sheets</button><p class="hint">Exports a self-contained HTML drawing set: interior, closed front and one side cutaway per bay. Open in a browser to print or save as PDF. Pages use the selected paper size; drawings scale to fit. Use written dimensions.</p></section></template>
