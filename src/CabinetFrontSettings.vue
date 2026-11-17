<script setup>
import {computed} from 'vue';
const props=defineProps({draft:{type:Object,required:true}}),emit=defineEmits(['update']);
const columns=computed(()=>{const n=props.draft.columns??1;return Number.isInteger(n)&&n>=1&&n<=8?n:0;});
function toggle(enabled){const next={...props.draft};if(enabled)next.columnFronts=Array(columns.value).fill('single');else delete next.columnFronts;emit('update',next);}
function set(index,value){const fronts=[...props.draft.columnFronts];fronts[index]=value;emit('update',{...props.draft,columnFronts:fronts});}
</script>
<template><section aria-label="Column front settings"><label class="snap-toggle"><input type="checkbox" :checked="!!draft.columnFronts" :disabled="!columns" aria-label="Independent fronts per column" @change="toggle($event.target.checked)">Independent fronts per column</label><template v-if="draft.columnFronts"><label v-for="n in columns" :key="n">Column {{ n }} front<select :aria-label="'Column '+n+' front'" :value="draft.columnFronts[n-1]" @change="set(n-1,$event.target.value)"><option value="open">Open</option><option value="single">Single door</option><option value="double">Double doors</option></select></label></template><p class="hint">Fronts fit inside each bay. Open bays have no front panel or handle. New columns start open. Disable to restore the original two full-cabinet doors. Apply cabinet to commit. Closed fronts only; hinges, opening motion and joinery are not modelled.</p></section></template>
