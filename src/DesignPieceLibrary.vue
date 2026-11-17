<script setup>
// Piece library: pick a base form and a material, set length and colour, then
// add the piece. Choosing a form suggests its usual material and length;
// choosing a material resets the colour to the material colour.
import {ref} from 'vue';
import {MATERIALS,WOOD_IDS,METAL_IDS} from '../shared/materials.mjs';
import {PIECE_FORMS,SHAPES,materialColor} from '../shared/furniture-design.mjs';
const emit=defineEmits(['add']);
const form=ref('panel'),material=ref(PIECE_FORMS.panel.material),color=ref(materialColor(PIECE_FORMS.panel.material)),length=ref(PIECE_FORMS.panel.defaults.length);
function pickForm(id) {form.value=id;pickMaterial(PIECE_FORMS[id].material);length.value=PIECE_FORMS[id].defaults.length;}
function pickMaterial(id) {material.value=id;color.value=materialColor(id);}
const add=()=>emit('add',{shape:form.value,material:material.value,color:color.value,length:length.value});
// Drag a form into the view to place a piece there, or a material onto a piece.
function drag(event,payload) {
  event.dataTransfer.setData('application/x-terratec-design',JSON.stringify(payload));
  event.dataTransfer.effectAllowed='copy';
}
const dragForm=(event,id)=>{if(id!==form.value) pickForm(id);drag(event,{kind:'form',shape:id,material:material.value,color:color.value,length:length.value});};
const MATERIAL_GROUPS=[['Wood',WOOD_IDS],['Metal',METAL_IDS]];
</script>
<template>
  <section class="piece-library" aria-label="Piece library">
    <p class="eyebrow">PIECE LIBRARY</p>
    <p class="hint library-hint">Click to choose, or drag a form into the view and a material onto a piece.</p>
    <p class="library-label">Base form</p>
    <div class="form-grid" role="radiogroup" aria-label="Base forms">
      <button v-for="id in SHAPES" :key="id" role="radio" :aria-checked="form===id" :title="PIECE_FORMS[id].label+' · drag into the view to place'" draggable="true" @click="pickForm(id)" @dragstart="dragForm($event,id)">
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <rect v-if="id==='panel'" x="2" y="9" width="20" height="6"/>
          <rect v-else-if="id==='bar'" x="6" y="5" width="12" height="14"/>
          <circle v-else-if="id==='cylinder'" cx="12" cy="12" r="7"/>
          <path v-else-if="id==='tube'" fill-rule="evenodd" d="M12 5a7 7 0 1 1 0 14a7 7 0 1 1 0-14zM12 8.5a3.5 3.5 0 1 0 0 7a3.5 3.5 0 1 0 0-7z"/>
          <path v-else-if="id==='box-tube'" fill-rule="evenodd" d="M5 5h14v14H5zM8 8v8h8V8z"/>
          <path v-else d="M5 5h4v10h10v4H5z"/>
        </svg>
        <span>{{ PIECE_FORMS[id].label }}</span>
      </button>
    </div>
    <template v-for="[group,ids] in MATERIAL_GROUPS" :key="group">
      <p class="library-label">{{ group }}</p>
      <div class="material-grid" role="radiogroup" :aria-label="group+' materials'">
        <button v-for="id in ids" :key="id" role="radio" :aria-checked="material===id" :title="MATERIALS[id].label+' · '+MATERIALS[id].densityKgM3+' kg/m³ · drag onto a piece to apply'" draggable="true" @click="pickMaterial(id)" @dragstart="drag($event,{kind:'material',material:id})">
          <span class="chip" :style="{background:materialColor(id)}"></span>{{ MATERIALS[id].label }}
        </button>
      </div>
    </template>
    <div class="mm-grid">
      <label>New piece length mm<input v-model.number="length" type="number" min="1" max="5000" step="1"></label>
      <label>New piece colour<input v-model="color" type="color" aria-label="New piece colour"></label>
    </div>
    <button class="add-piece" @click="add">Add {{ PIECE_FORMS[form].label.toLowerCase() }}</button>
  </section>
</template>
<style scoped>
.library-label{margin:10px 0 4px;font-size:10px;letter-spacing:1px;text-transform:uppercase;color:#60746a}
.form-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:4px}
.form-grid button{display:flex;flex-direction:column;align-items:center;gap:3px;padding:6px 2px;font-size:10px;line-height:1.2;border:1px solid #cdd5ca;background:#f6f7f2}
.form-grid svg{width:26px;height:26px;fill:#6f7d74}
.form-grid button[aria-checked=true]{border-color:#263e3c;background:#263e3c;color:#f2f3eb}.form-grid button[aria-checked=true] svg{fill:#e7c98f}
.material-grid{display:grid;grid-template-columns:1fr 1fr;gap:3px}
.material-grid button{display:flex;align-items:center;gap:5px;padding:4px 5px;font-size:10px;text-align:left;border:1px solid #cdd5ca;background:#f6f7f2}
.material-grid button[aria-checked=true]{border-color:#263e3c;box-shadow:inset 0 0 0 1px #263e3c;font-weight:700}
.chip{width:14px;height:14px;border-radius:3px;border:1px solid #0003;flex-shrink:0}
.mm-grid{display:grid;grid-template-columns:1fr 1fr;gap:0 8px;margin-top:6px}.mm-grid input{width:100%;min-width:0}
.mm-grid input[type=color]{height:34px;padding:2px}
[draggable=true]{cursor:grab}.library-hint{margin:0 0 4px}
.add-piece{width:100%;margin-top:6px;background:#263e3c;color:#f2f3eb}
</style>
