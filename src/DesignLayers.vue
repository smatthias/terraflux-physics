<script setup>
// Layer panel, shown floating in the design view. The order is the machining
// order. Drag a row's handle onto a piece to move the piece to that layer.
import {ref} from 'vue';
import {LAYER_OPERATIONS} from '../shared/furniture-design.mjs';
defineProps({layers:{type:Array,required:true},usage:{type:Object,required:true},activeLayerId:{type:String,default:null},focusLayerId:{type:String,default:null}});
const emit=defineEmits(['add','update','remove','move','activate','focus']);
const open=ref(true);
function dragLayer(event,layer) {
  event.dataTransfer.setData('application/x-terratec-design',JSON.stringify({kind:'layer',layerId:layer.id}));
  event.dataTransfer.effectAllowed='link';
}
</script>
<template>
  <section class="design-layers" aria-label="Layers">
    <header class="layers-head"><p class="eyebrow">LAYERS · MACHINING ORDER</p><button :aria-expanded="open" @click="open=!open">{{ open ? 'Hide layers' : 'Show layers' }}</button></header>
    <template v-if="open">
      <ol>
        <li v-for="(layer,i) in layers" :key="layer.id" class="layer-row" :class="{hidden:!layer.visible,locked:layer.locked,focused:focusLayerId===layer.id}">
          <span class="layer-order" draggable="true" :title="'Drag onto a piece to move it to '+layer.name" @dragstart="dragLayer($event,layer)">⠿{{ i+1 }}</span>
          <input type="color" :value="layer.color" :aria-label="'Layer '+(i+1)+' colour'" @change="emit('update',layer.id,{color:$event.target.value})">
          <input class="layer-name" :value="layer.name" maxlength="40" :aria-label="'Layer '+(i+1)+' name'" @change="emit('update',layer.id,{name:$event.target.value})">
          <select :value="layer.operation" :aria-label="'Layer '+(i+1)+' operation'" @change="emit('update',layer.id,{operation:$event.target.value})"><option v-for="op in LAYER_OPERATIONS" :key="op" :value="op">{{ op }}</option></select>
          <span class="layer-flags">
            <label class="layer-flag"><input type="radio" name="active-layer" :checked="activeLayerId===layer.id" :aria-label="'Layer '+(i+1)+' active'" @change="emit('activate',layer.id)">Active</label>
            <label class="layer-flag"><input type="checkbox" :checked="layer.visible" :aria-label="'Layer '+(i+1)+' visible'" @change="emit('update',layer.id,{visible:$event.target.checked})">Show</label>
            <label class="layer-flag"><input type="checkbox" :checked="layer.locked" :aria-label="'Layer '+(i+1)+' locked'" @change="emit('update',layer.id,{locked:$event.target.checked})">Lock</label>
            <button :aria-pressed="focusLayerId===layer.id" :aria-label="'Focus layer '+(i+1)" title="Dim everything else in the view" @click="emit('focus',focusLayerId===layer.id?null:layer.id)">Focus</button>
          </span>
          <span class="layer-actions">
            <button :disabled="i===0" :aria-label="'Move layer '+(i+1)+' earlier'" @click="emit('move',layer.id,-1)">↑</button>
            <button :disabled="i===layers.length-1" :aria-label="'Move layer '+(i+1)+' later'" @click="emit('move',layer.id,1)">↓</button>
            <button :disabled="usage[layer.id]>0||layers.length===1" :aria-label="'Delete layer '+(i+1)" :title="usage[layer.id]?'In use':''" @click="emit('remove',layer.id)">✕</button>
            <small class="layer-usage">{{ usage[layer.id]||0 }}</small>
          </span>
        </li>
      </ol>
      <div class="design-row"><button @click="emit('add')">Add layer</button><button v-if="activeLayerId" @click="emit('activate',null)">Automatic layers</button></div>
      <p class="hint">Layers run top to bottom when machining. New pieces go to the active layer, or by type when none is active. Focus dims other layers; lock protects a layer's pieces and features.</p>
    </template>
  </section>
</template>
