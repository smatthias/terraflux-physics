<script setup>
import {computed} from 'vue';
const props=defineProps({draft:{type:Object,required:true}}),emit=defineEmits(['update']);
const columns=computed(()=>{const n=props.draft.columns??1;return Number.isInteger(n)&&n>=1&&n<=8?n:0;});
function toggle(enabled){const next={...props.draft};if(enabled)next.columnShelves=Array(columns.value).fill(next.shelves);else delete next.columnShelves;emit('update',next);}
function set(index,value){const counts=[...props.draft.columnShelves];counts[index]=value===''?NaN:Number(value);emit('update',{...props.draft,columnShelves:counts});}
</script>
<template><section aria-label="Column shelf settings"><label class="snap-toggle"><input type="checkbox" :checked="!!draft.columnShelves" :disabled="!columns" aria-label="Independent shelf counts per column" @change="toggle($event.target.checked)">Independent shelf counts per column</label><template v-if="draft.columnShelves"><label v-for="n in columns" :key="n">Column {{ n }} shelves<input :value="draft.columnShelves[n-1]" type="number" min="0" max="10" step="1" @input="set(n-1,$event.target.value)"></label></template><p class="hint">Columns run left to right in the front elevation. New columns inherit Cabinet shelves; removing columns drops their overrides. Disable independent counts to use Cabinet shelves in every column. Apply cabinet to commit.</p></section></template>
