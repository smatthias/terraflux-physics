<script setup>
import {ref,computed,watch} from 'vue';
const props=defineProps({profiles:{type:Array,required:true},furniture:{type:Array,required:true}});
const emit=defineEmits(['save','apply','remove','export','import','bulk']);
const name=ref(''),choice=ref(''),targets=ref([]);
function importFile(event){emit('import',event.target.files?.[0]);event.target.value='';}
watch(()=>props.furniture,()=>{targets.value=targets.value.filter(id=>props.furniture.some(c=>c.id===id));},{deep:true});
const current=computed(()=>props.profiles.find(p=>p.id===choice.value));
watch(()=>props.profiles,()=>{if(!current.value)choice.value='';},{deep:true});
</script>
<template>
  <section class="workshop-profiles" aria-label="Workshop profiles">
    <h3>Workshop profiles</h3>
    <button :disabled="!profiles.length" @click="emit('export')">Export workshop profiles</button>
    <label>Import workshop profiles<input type="file" accept=".json,application/json" aria-label="Import workshop profiles file" @change="importFile"></label>
    <p class="hint">Imports keep existing profiles, rename matching names, and assign new IDs. Maximum 32 profiles and 32 KiB per file.</p>
    <p class="hint">Save the applied cabinet’s stock, trim, kerf and rotation settings. Profiles stay with this project. Saving an existing name updates that profile; cabinet settings remain independent.</p>
    <label>Workshop profile name<input v-model="name" maxlength="60"></label>
    <button :disabled="!name.trim()" @click="emit('save',name)">Save workshop profile</button>
    <label v-if="profiles.length">Workshop profile<select v-model="choice" aria-label="Workshop profile"><option value="">Choose workshop profile</option><option v-for="profile in profiles" :key="profile.id" :value="profile.id">{{ profile.name }}</option></select></label>
    <p v-if="current" class="hint">{{ current.stockLengthMm }} × {{ current.stockWidthMm }} mm · trim {{ current.stockTrimMm }} mm · kerf {{ current.stockKerfMm }} mm · rotation {{ current.stockAllowRotation ? 'allowed' : 'disabled' }}</p>
    <button :disabled="!current" @click="emit('apply',choice)">Apply workshop profile</button>
    <button :disabled="!current" @click="emit('remove',choice)">Delete workshop profile</button>
    <fieldset v-if="current"><legend>Apply profile to cabinets</legend>
      <button @click="targets=furniture.map(c=>c.id)">Select all cabinets</button><button @click="targets=[]">Clear cabinet selection</button>
      <label v-for="cabinet in furniture" :key="cabinet.id" class="snap-toggle"><input v-model="targets" type="checkbox" :value="cabinet.id" :aria-label="'Bulk cabinet '+cabinet.name"> {{ cabinet.name }}</label>
      <button :disabled="!targets.length" @click="emit('bulk',{profileId:choice,cabinetIds:[...targets]})">Apply profile to selected cabinets</button>
      <p class="hint">{{ targets.length }} cabinets selected. Applied together as one undoable edit.</p>
    </fieldset>
  </section>
</template>
