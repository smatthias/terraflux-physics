<script setup>
// The design's layers on a bench: show or hide each (hidden machining is not cut
// or drawn), and choose the layer new cuts go on (by default, the first unlocked
// layer for their operation, as in the designer).
import {showLayer} from '../shared/bench-edits.mjs';
const props=defineProps({design:{type:Object,required:true},layerId:{type:String,default:''}});
const emit=defineEmits(['commit','update:layerId','message']);
function toggle(layer,visible) {
  try {emit('commit',{design:showLayer(props.design,layer.id,visible),message:`Layer “${layer.name}” ${visible?'shown':'hidden'}`});} catch(error) {emit('message',error.message);}
}
</script>
<template>
  <div class="bench-layers">
    <label>New cuts go on<select :value="layerId" aria-label="Layer for new cuts" @change="emit('update:layerId',$event.target.value)">
      <option value="">The layer for their operation</option>
      <option v-for="l in design.layers" :key="l.id" :value="l.id" :disabled="l.locked">{{ l.name }} · {{ l.operation }}{{ l.locked?' · locked':'' }}</option>
    </select></label>
    <ol>
      <li v-for="l in design.layers" :key="l.id" :class="{hidden:!l.visible}">
        <span class="swatch" :style="{background:l.color}"></span><span class="layer-name">{{ l.name }}</span><small>{{ l.operation }}{{ l.locked?' · locked':'' }}</small>
        <label class="layer-show"><input type="checkbox" :checked="l.visible" :aria-label="'Show layer '+l.name" @change="toggle(l,$event.target.checked)">Show</label>
      </li>
    </ol>
    <p class="hint">A cut on a layer whose CNC operation does not match (a hole on a groove layer) is flagged in the CNC plan.</p>
  </div>
</template>
