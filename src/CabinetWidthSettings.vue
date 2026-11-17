<script setup>
import {computed} from 'vue';
const props=defineProps({draft:{type:Object,required:true}}),emit=defineEmits(['update']);
const columns=computed(()=>{const n=props.draft.columns??1;return Number.isInteger(n)&&n>=1&&n<=8?n:0;});
function toggle(enabled){const next={...props.draft};if(enabled)next.columnWeights=Array(columns.value).fill(1);else delete next.columnWeights;emit('update',next);}
function set(index,value){const weights=[...props.draft.columnWeights];weights[index]=value===''?NaN:Number(value);emit('update',{...props.draft,columnWeights:weights});}
</script>
<template><section aria-label="Column width settings"><label class="snap-toggle"><input type="checkbox" :checked="!!draft.columnWeights" :disabled="!columns" aria-label="Unequal cabinet bay widths" @change="toggle($event.target.checked)">Unequal cabinet bay widths</label><template v-if="draft.columnWeights"><label v-for="n in columns" :key="n">Column {{ n }} width weight<input :value="draft.columnWeights[n-1]" type="number" min="0.01" max="1000" step="0.1" @input="set(n-1,$event.target.value)"></label></template><p class="hint">Weights divide available clear width after panel thicknesses: 1 / 2 / 1 gives the middle bay twice the width. Minimum clear bay width is 50 mm. New columns start at weight 1. Disable to restore equal widths; apply cabinet to commit.</p></section></template>
