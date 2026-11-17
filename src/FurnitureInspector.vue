<script setup>
// Read-only details for the inspected furniture item, shown over the isolated 3D view.
import {computed} from 'vue';
import {MATERIALS,DEFAULT_WOOD} from '../shared/materials.mjs';
import {cabinetBom,cabinetMass} from '../shared/cabinet-parts.mjs';
const props=defineProps({item:{type:Object,required:true},clearance:Object,designs:{type:Array,default:()=>[]}});
const emit=defineEmits(['rotate','zoom','exit']);
const design=computed(()=>props.item.type==='design'?props.designs.find(d=>d.id===props.item.designId)??null:null);
const details=computed(()=>{if(props.item.type==='design') return null;try{return {bom:cabinetBom(props.item),mass:cabinetMass(props.item)};}catch(error){return {error:error.message};}});
const designSummary=computed(()=>{const d=design.value;if(!d) return null;
  return {pieces:d.parts.length,panels:d.parts.filter(p=>p.shape==='panel').length,features:d.parts.reduce((n,p)=>n+(p.features?.length??0),0),materials:[...new Set(d.parts.map(p=>MATERIALS[p.material].label))].join(', ')};});
const wood=computed(()=>MATERIALS[props.item.material??DEFAULT_WOOD]);
const metal=computed(()=>MATERIALS[details.value?.bom?.hardware.material??'steel']);
const mm=v=>(v*1000).toFixed(0);
const swatch=color=>'#'+color.toString(16).padStart(6,'0');
</script>
<template>
  <section class="furniture-inspector" aria-label="Furniture inspector">
    <p class="eyebrow">INSPECTING</p>
    <h3>{{ item.name }}</h3>
    <dl v-if="item.type==='design'" data-testid="inspector-design">
      <div><dt>Design</dt><dd>{{ design?.name ?? 'Missing design' }}</dd></div>
      <div><dt>Size W × D × H</dt><dd>{{ mm(item.width) }} × {{ mm(item.depth) }} × {{ mm(item.height) }} mm</dd></div>
      <div><dt>Position</dt><dd>X {{ item.x.toFixed(3) }} · Z {{ item.z.toFixed(3) }} m · {{ item.rotation }}°</dd></div>
      <template v-if="designSummary"><div><dt>Pieces</dt><dd>{{ designSummary.pieces }} · {{ designSummary.panels }} panels</dd></div>
        <div><dt>Machining</dt><dd>{{ designSummary.features }} features</dd></div><div><dt>Materials</dt><dd>{{ designSummary.materials }}</dd></div></template>
      <div v-if="clearance && !clearance.error"><dt>Nearest wall</dt><dd>{{ clearance.nearestWall.wall }} · {{ clearance.nearestWall.distance.toFixed(3) }} m</dd></div>
    </dl>
    <p v-else-if="details.error" class="hint">{{ details.error }}</p>
    <dl v-else>
      <div><dt>Size W × D × H</dt><dd>{{ mm(item.width) }} × {{ mm(item.depth) }} × {{ mm(item.height) }} mm</dd></div>
      <div><dt>Position</dt><dd>X {{ item.x.toFixed(3) }} · Z {{ item.z.toFixed(3) }} m · {{ item.rotation }}°</dd></div>
      <div><dt>Wood</dt><dd><span class="swatch" :style="{background:swatch(wood.color)}"></span>{{ wood.label }} · {{ (item.thickness*1000).toFixed(0) }} mm</dd></div>
      <div><dt>Hardware</dt><dd><span class="swatch" :style="{background:swatch(metal.color)}"></span>{{ metal.label }}</dd></div>
      <div><dt>Panels</dt><dd>{{ details.bom.panelCount }} · {{ details.bom.area.toFixed(2) }} m²</dd></div>
      <div><dt>Screws</dt><dd data-testid="inspector-screws">{{ details.bom.hardware.screws }} × {{ details.bom.hardware.screwSpec }} ({{ details.bom.hardware.screwsPerJoint }} per joint × {{ details.bom.hardware.joints }} joints, estimate)</dd></div>
      <div><dt>Handles</dt><dd>{{ details.bom.handles }}</dd></div>
      <div><dt>Estimated weight</dt><dd data-testid="inspector-weight">{{ details.mass.totalKg.toFixed(1) }} kg</dd></div>
      <div v-if="clearance && !clearance.error"><dt>Nearest wall</dt><dd>{{ clearance.nearestWall.wall }} · {{ clearance.nearestWall.distance.toFixed(3) }} m</dd></div>
    </dl>
    <p v-if="details" class="hint">Weight uses design panel volumes and typical densities: wood {{ details.mass?.panelsKg.toFixed(1) }} kg, screws {{ details.mass?.screwsKg.toFixed(2) }} kg, handles {{ details.mass?.handlesKg.toFixed(2) }} kg. Planning estimate only.</p>
    <div class="opening-actions"><button @click="emit('rotate',-15)">Rotate item −15°</button><button @click="emit('rotate',15)">Rotate item +15°</button></div>
    <div class="opening-actions"><button @click="emit('zoom')">Reframe item</button><button @click="emit('exit')">Exit inspection</button></div>
  </section>
</template>
