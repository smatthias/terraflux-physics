<script setup>
// Properties of the selected part and its machining features. Fields apply on
// change (Enter or leaving the field); each change is one design undo step.
import {computed} from 'vue';
import {MATERIALS,WOOD_IDS,METAL_IDS} from '../shared/materials.mjs';
import {PANEL_ORIENTATIONS,PIECE_FORMS,SHAPES,SECTION_AXES,materialColor,isEdgeFace} from '../shared/furniture-design.mjs';
import {isThrough,featureDepthLimit} from '../shared/furniture-design-geometry.mjs';
const props=defineProps({part:{type:Object,required:true},layers:{type:Array,required:true},featureId:{type:String,default:null}});
const emit=defineEmits(['update','convert','remove','duplicate','add-feature','select-feature','update-feature','remove-feature']);
const feature=computed(()=>props.part.features?.find(f=>f.id===props.featureId)??null);
const num=event=>event.target.value===''?NaN:Number(event.target.value);
// Form-specific sizes; section fields name the world axis they run along.
const dimensions=computed(()=>{
  const part=props.part;
  if(part.shape==='panel') return [['length','Length'],['width','Width'],['thickness','Thickness']];
  const [a,b]=SECTION_AXES[part.axis],leg=part.shape==='angle'?'Leg':'Section';
  const section=part.shape==='cylinder'||part.shape==='tube'?[['diameter','Diameter']]:[['width',`${leg} width (${a})`],['height',`${leg} height (${b})`]];
  return [['length','Length'],...section,...(part.wall!==undefined?[['wall','Wall']]:[])];
});
const AXES={x:'x · left to right',y:'y · upright',z:'z · front to back'};
const usesMaterialColor=computed(()=>props.part.color===materialColor(props.part.material));
// Edge holes are placed along their edge and across the thickness (t from face B).
const featureFields=computed(()=>{
  const f=feature.value;
  if(f?.type!=='drill') return [['u','Start u'],['v','Start v'],['length','Length along u'],['width','Width along v']];
  if(isEdgeFace(f.face)) return [[f.face[0]==='v'?'u':'v','Along the edge'],['t','Across the thickness t'],['diameter','Diameter']];
  return [['u','Centre u'],['v','Centre v'],['diameter','Diameter']];
});
const FACES=[['A','A (positive side)'],['B','B (turned over)'],['u0','Edge u = 0 (end)'],['u1','Edge u = length (end)'],['v0','Edge v = 0 (long edge)'],['v1','Edge v = width (long edge)']];
const depthLimit=computed(()=>feature.value?featureDepthLimit(props.part,feature.value):props.part.thickness);
const layerLabel=layer=>layer.name+' · '+layer.operation+(layer.locked?' · locked':'');
const featureLabel=f=>`${f.type} · ${isEdgeFace(f.face)?'edge':'face'} ${f.face} · ${f.depth} mm${isThrough(props.part,f)?' through':''}`;
const setThrough=checked=>emit('update-feature',feature.value.id,{depth:checked?depthLimit.value:Math.max(.5,Math.round(depthLimit.value/2))});
</script>
<template>
  <section class="design-props" aria-label="Part properties">
    <p class="eyebrow">PIECE · {{ PIECE_FORMS[part.shape].label.toUpperCase() }}</p>
    <label>Part name<input :value="part.name" maxlength="60" @change="emit('update',{name:$event.target.value})"></label>
    <label>Piece form<select :value="part.shape" aria-label="Piece form" @change="emit('convert',$event.target.value)"><option v-for="id in SHAPES" :key="id" :value="id">{{ PIECE_FORMS[id].label }}</option></select></label>
    <label>Part material<select :value="part.material" aria-label="Part material" @change="emit('update',{material:$event.target.value})">
      <optgroup label="Wood"><option v-for="id in WOOD_IDS" :key="id" :value="id">{{ MATERIALS[id].label }}</option></optgroup>
      <optgroup label="Metal"><option v-for="id in METAL_IDS" :key="id" :value="id">{{ MATERIALS[id].label }}</option></optgroup>
    </select></label>
    <div class="colour-row"><label>Piece colour<input type="color" :value="part.color" aria-label="Piece colour" @change="emit('update',{color:$event.target.value})"></label>
      <button :disabled="usesMaterialColor" @click="emit('update',{color:materialColor(part.material)})">Use material colour</button></div>
    <label>Part layer<select :value="part.layer" aria-label="Part layer" @change="emit('update',{layer:$event.target.value})"><option v-for="layer in layers" :key="layer.id" :value="layer.id">{{ layerLabel(layer) }}</option></select></label>
    <label v-if="part.shape==='panel'">Panel orientation<select :value="part.orientation" aria-label="Panel orientation" @change="emit('update',{orientation:$event.target.value})"><option v-for="(info,id) in PANEL_ORIENTATIONS" :key="id" :value="id">{{ info.label }}</option></select></label>
    <label v-if="part.shape!=='panel'">Length direction<select :value="part.axis" aria-label="Length direction" @change="emit('update',{axis:$event.target.value})"><option v-for="(label,axis) in AXES" :key="axis" :value="axis">{{ label }}</option></select></label>
    <div class="mm-grid"><label v-for="[key,label] in dimensions" :key="key">{{ label }} mm<input type="number" step="0.1" :value="part[key]" @change="emit('update',{[key]:num($event)})"></label></div>
    <div class="mm-grid"><label v-for="axis in ['x','y','z']" :key="axis">Position {{ axis }} mm<input type="number" step="1" :value="part[axis]" @change="emit('update',{[axis]:num($event)})"></label></div>
    <div class="mm-grid"><label v-for="axis in ['x','y','z']" :key="axis">Rotation {{ axis }} °<input type="number" step="1" :value="part.rotation?.[axis]??0" @change="emit('update',{rotation:{...(part.rotation??{x:0,y:0,z:0}),[axis]:num($event)}})"></label></div>
    <div class="design-row"><button @click="emit('duplicate')">Duplicate part</button><button @click="emit('remove')">Delete part</button></div>
    <template v-if="part.shape==='panel'">
      <p class="eyebrow">MACHINING · DEPTH WORK</p>
      <div class="design-row"><button @click="emit('add-feature','drill')">Add drill hole</button><button @click="emit('add-feature','pocket')">Add pocket</button><button @click="emit('add-feature','groove')">Add groove</button></div>
      <ul class="feature-list"><li v-for="f in part.features" :key="f.id"><button :aria-pressed="f.id===featureId" :aria-label="'Edit '+f.type+' on face '+f.face+' at u '+f.u+', v '+f.v+', '+f.depth+' mm deep'" @click="emit('select-feature',f.id)"><span class="swatch" :style="{background:layers.find(l=>l.id===f.layer)?.color}"></span>{{ featureLabel(f) }}</button></li></ul>
      <fieldset v-if="feature" class="feature-editor">
        <legend>{{ feature.type }} · sheet frame mm</legend>
        <label>Feature face<select :value="feature.face" aria-label="Feature face" @change="emit('update-feature',feature.id,{face:$event.target.value})"><option v-for="[id,label] in FACES.slice(0,feature.type==='drill'?6:2)" :key="id" :value="id">{{ label }}</option></select></label>
        <label>Feature layer<select :value="feature.layer" aria-label="Feature layer" @change="emit('update-feature',feature.id,{layer:$event.target.value})"><option v-for="layer in layers" :key="layer.id" :value="layer.id">{{ layerLabel(layer) }}</option></select></label>
        <div class="mm-grid"><label v-for="[key,label] in featureFields" :key="key">{{ label }} mm<input type="number" step="0.1" :value="feature[key]" @change="emit('update-feature',feature.id,{[key]:num($event)})"></label>
          <label>Depth mm<input type="number" step="0.1" min="0.01" :max="depthLimit" :value="feature.depth" @change="emit('update-feature',feature.id,{depth:num($event)})"></label></div>
        <label class="snap-toggle"><input type="checkbox" :checked="isThrough(part,feature)" aria-label="Cut through" @change="setThrough($event.target.checked)"> Cut through ({{ depthLimit }} mm)</label>
        <p v-if="isEdgeFace(feature.face)" class="hint">Edge holes drill into the panel from its edge, for dowels and connectors. They need a horizontal drilling unit and are not in 3-axis G-code.</p>
        <button @click="emit('remove-feature',feature.id)">Delete feature</button>
      </fieldset>
      <p class="hint">u runs along the length and v along the width from the panel corner, as the panel lies on the CNC bed with face A up. Depth is measured into the material. Round tools leave inside corners of pockets and grooves rounded to the tool radius.</p>
    </template>
  </section>
</template>
