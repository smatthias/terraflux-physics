<script setup>
import {computed} from 'vue';
import {shelfJoineryNote} from '../shared/shelf-joinery.mjs';
const props=defineProps({draft:{type:Object,required:true},applied:{type:Object,required:true}}),emit=defineEmits(['update']);
const note=computed(()=>shelfJoineryNote(props.applied));
function type(value){emit('update',{...props.draft,shelfJoinery:{type:value,engagementMm:value==='butt'?0:Math.min(3,props.draft.thickness*1000/3)}});}
function depth(value){emit('update',{...props.draft,shelfJoinery:{type:'dado',engagementMm:value===''?NaN:Number(value)}});}
</script>
<template><section aria-label="Shelf joinery planning"><h3>Shelf joinery planning</h3><label>Shelf joint<select :value="draft.shelfJoinery?.type??'butt'" aria-label="Shelf joint" @change="type($event.target.value)"><option value="butt">Butt</option><option value="dado">Dado allowance</option></select></label><label v-if="draft.shelfJoinery?.type==='dado'">Shelf end engagement mm<input :value="draft.shelfJoinery.engagementMm" type="number" min="0.1" max="10" step="0.1" @input="depth($event.target.value)"></label><p data-testid="shelf-joinery-summary">{{ note }}</p><p class="hint">Engagement adds blank length at both shelf ends, before the existing total length allowance. Visible clear-span geometry stays unchanged. This does not generate grooves, fittings or CNC instructions. Apply cabinet to commit.</p></section></template>
