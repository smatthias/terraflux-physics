<script setup>
// Drilling bench: a drill press view for one panel. Put a face or edge up, pick a
// bit from the rack, set the depth stop, then position the panel under the bit by
// entering the position or by hand (click the panel to bring that point under the
// bit, drag to slide it, arrow keys to nudge) and drill. Each drilling is one
// design edit (undo with Ctrl/⌘+Z). Loaded on first use.
import {ref,computed,watch,onMounted,onBeforeUnmount,onActivated,onDeactivated,nextTick} from 'vue';
import * as THREE from 'three';
import {createBenchScene,steel,paint} from './bench-scene.js';
import BenchDesk from './BenchDesk.vue';
import BenchLayers from './BenchLayers.vue';
import {drillHoles} from '../shared/bench-edits.mjs';
import {DRILL_BITS,BIT_KINDS,BENCH_FACES,benchFace,holeAt,holePosition,rowOfHoles,handPosition,benchMatrix} from '../shared/drilling-bench.mjs';
import {MATERIALS} from '../shared/materials.mjs';

const props=defineProps({design:{type:Object,required:true},partId:{type:String,required:true}});
const emit=defineEmits(['commit','remove','close','message']);
const part=computed(()=>props.design.parts.find(p=>p.id===props.partId)??null);
watch(part,value=>{if(!value) emit('close');});

// The bench's windows (BenchDesk.vue), back to front, with where they open.
const WINDOWS=[
  {id:'holes',title:'Holes on this face',rect:d=>({x:8,y:Math.max(8,d.height-268),w:330,h:260})},
  {id:'layers',title:'Layers',rect:d=>({x:8,y:8,w:290,h:Math.min(240,d.height-16)})},
  {id:'press',title:'Drill press',rect:d=>({x:Math.max(8,d.width-378),y:8,w:370,h:Math.min(d.height-16,680)})},
  {id:'help',title:'Help',rect:d=>({x:Math.round(d.width/2-220),y:Math.max(8,Math.round(d.height/2-110)),w:440,h:220}),minimized:true},
];
const layerId=ref('');   // the layer new holes go on ('' = the drilling layer)
// ---- Bench settings (kept while the bench is open)
const face=ref('A'),bitId=ref('brad-8'),diameter=ref(8),depth=ref(12),through=ref(false),step=ref(1);
const target=ref({a:0,b:0}),row=ref({count:1,spacing:32,along:'a'}),drilling=ref(false);
const faceInfo=computed(()=>part.value?benchFace(part.value,face.value):null);
const centre=()=>({a:Math.round(faceInfo.value.a.length/2*10)/10,b:Math.round(faceInfo.value.b.length/2*10)/10});
watch(face,()=>{target.value=centre();if(depth.value>faceInfo.value.depthLimit) depth.value=faceInfo.value.depthLimit;},{flush:'sync'});
target.value=part.value?centre():target.value;
function chooseBit(bit) {bitId.value=bit.id;diameter.value=bit.diameter;if(bit.diameter===35) depth.value=Math.min(13,faceInfo.value.depthLimit);}
function setDiameter(event) {const value=Number(event.target.value);if(event.target.value!==''&&value>=.5&&value<=200) {diameter.value=value;bitId.value=DRILL_BITS.find(b=>b.diameter===value)?.id??'custom';} else event.target.value=diameter.value;}
const bit=computed(()=>DRILL_BITS.find(b=>b.id===bitId.value)??{kind:'twist',diameter:diameter.value});
const bitLabel=b=>`Ø${b.diameter} ${BIT_KINDS[b.kind].toLowerCase()}`;
// Number fields apply valid values; anything else reverts the field.
function numberFrom(event,min,max,current) {const value=Number(event.target.value);if(event.target.value!==''&&Number.isFinite(value)&&value>=min&&value<=max) return value;event.target.value=current;return null;}
function setDepth(event) {const v=numberFrom(event,.01,faceInfo.value.depthLimit,depth.value);if(v!==null) depth.value=v;}
function setPosition(axis,event) {const v=numberFrom(event,-1e4,1e4,target.value[axis]);if(v!==null) moveTo(axis==='a'?v:target.value.a,axis==='b'?v:target.value.b);}
function setRow(key,event) {
  if(key==='along') {row.value={...row.value,along:event.target.value};return;}
  const v=key==='count'?numberFrom(event,1,50,row.value.count):numberFrom(event,.5,5000,row.value.spacing);
  if(v!==null) row.value={...row.value,[key]:key==='count'?Math.round(v):v};
}
const moveTo=(a,b)=>{target.value=handPosition(part.value,face.value,{a,b},0);};
function nudge(axis,sign) {const s=(step.value||1)*sign;moveTo(target.value.a+(axis==='a'?s:0),target.value.b+(axis==='b'?s:0));}

// ---- Holes to drill and whether they can be drilled
const planned=computed(()=>{
  if(!part.value) return [];
  try {return rowOfHoles(target.value,{...row.value,spacing:row.value.spacing}).map(p=>holeAt(part.value,face.value,{...p,diameter:diameter.value,depth:depth.value,through:through.value}));}
  catch(error) {return {error:error.message};}
});
const existing=computed(()=>(part.value?.features??[]).map((f,i)=>({feature:f,index:i+1,at:holePosition(part.value,face.value,f)})).filter(h=>h.at));
const check=computed(()=>{
  if(planned.value.error) return planned.value.error;
  for(const hole of planned.value) {
    const f=faceInfo.value,a=hole[f.a.key],b=hole[f.b.key];
    if(existing.value.some(h=>Math.abs(h.at.a-a)<.01&&Math.abs(h.at.b-b)<.01&&h.feature.diameter===hole.diameter)) return `There is already a Ø${hole.diameter} hole at ${a}, ${b}`;
  }
  try {let n=0;drillHoles(props.design,props.partId,planned.value,()=>'check-'+(++n),layerId.value||null);return '';} catch(error) {return error.message;}
});
const readout=computed(()=>`${bitLabel(bit.value)} · depth ${depthText.value} · at ${where(target.value.a,target.value.b)} · ${check.value||'ready to drill'}`);
const depthText=computed(()=>through.value?`through (${faceInfo.value.depthLimit} mm)`:`${depth.value} mm`);
const where=(a,b)=>`${faceInfo.value.a.key} ${a} · ${faceInfo.value.b.key} ${b}`;
async function drill() {
  if(drilling.value||check.value||!part.value) {if(check.value) emit('message',check.value);return;}
  const holes=planned.value,text=holes.length>1?`Drilled ${holes.length} holes Ø${diameter.value} × ${depthText.value} from ${where(target.value.a,target.value.b)}`:`Drilled Ø${diameter.value} × ${depthText.value} at ${where(target.value.a,target.value.b)}`;
  drilling.value=true;
  await plunge();
  drilling.value=false;
  // The holes go into the design as it is now (it may have changed meanwhile).
  try {emit('commit',{design:drillHoles(props.design,props.partId,holes,()=>crypto.randomUUID(),layerId.value||null).design,message:text});} catch(error) {emit('message',error.message);}
}

// ---- 3D bench (bench-scene.js)
const host=ref(null),webgl=ref(true),topView=ref(false);
let view=null,bitGroup=null,tipRest=0;
function buildPiece() {
  if(!view||!part.value) return;
  const f=faceInfo.value;
  view.setPiece(props.design,part.value,benchMatrix(part.value,face.value,target.value),{height:f.height,backZ:f.bZero==='back'?-target.value.b:-(f.b.length-target.value.b),leftX:-target.value.a});
}
// The drill press: bit, chuck, head and column, raised above the piece.
function placeBit() {
  if(!view) return;
  const f=faceInfo.value,r=diameter.value/2,length=Math.max(60,Math.min(f.depthLimit+30,160)),forstner=bit.value.kind==='forstner',cut=forstner?Math.min(length,20):length;
  bitGroup=new THREE.Group();
  const add=(geometry,material,y,z=0)=>{const mesh=new THREE.Mesh(geometry,material);mesh.position.set(0,y,z);bitGroup.add(mesh);};
  add(new THREE.CylinderGeometry(r,r*(forstner?1:.4),cut,24),steel(),cut/2);
  if(forstner) add(new THREE.CylinderGeometry(5,5,length-20,16),steel(),20+(length-20)/2);
  add(new THREE.CylinderGeometry(22,26,60,24),paint(0x555b60),length+30);
  add(new THREE.BoxGeometry(180,160,300),paint(0x2f5d50),length+140,-90);
  tipRest=f.height+15;
  const columnHeight=tipRest+length+200;add(new THREE.CylinderGeometry(45,45,columnHeight,24),paint(0x6b7378),columnHeight/2-tipRest,-520);
  bitGroup.position.y=tipRest;view.setTool(bitGroup);
}
// Lowers the bit to the depth stop and back (about half a second).
function plunge() {
  if(!bitGroup) return Promise.resolve();
  const bottom=faceInfo.value.height-(through.value?faceInfo.value.depthLimit+1:depth.value);
  return view.play(520,t=>{const down=t<.5?t*2:(1-t)*2;bitGroup.position.y=t<1?tipRest+(bottom-tipRest)*down:tipRest;});
}
const frameCamera=()=>view?.frame({height:faceInfo.value.height,size:Math.max(faceInfo.value.a.length,faceInfo.value.b.length,400),top:topView.value});
watch([face,target,()=>props.design],buildPiece,{deep:true});
watch([diameter,bitId,face,depth,through],placeBit);
watch([face,topView],frameCamera);
// Hand positioning: a click on the panel brings that point under the bit; a drag slides the panel.
let press=null;
const topPoint=event=>view.topPoint(event,faceInfo.value.height);
const onFace=p=>{const f=faceInfo.value,a=target.value.a+p.x,b=target.value.b+(f.bZero==='back'?p.z:-p.z);return a>=0&&a<=f.a.length&&b>=0&&b<=f.b.length?{a,b}:null;};
function pointerDown(event) {
  if(event.button!==0||drilling.value) return;
  const p=topPoint(event),hit=p&&onFace(p);if(!hit) return;
  event.stopImmediatePropagation();view.capture(event.pointerId);
  press={x:event.clientX,y:event.clientY,point:p,start:{...target.value},hit,moved:false,id:event.pointerId};
}
function pointerMove(event) {
  if(!press||event.pointerId!==press.id) return;
  if(!press.moved&&Math.hypot(event.clientX-press.x,event.clientY-press.y)<=4) return;
  press.moved=true;
  const p=topPoint(event);if(!p) return;
  const f=faceInfo.value,dx=p.x-press.point.x,dz=p.z-press.point.z;
  target.value=handPosition(part.value,face.value,{a:press.start.a-dx,b:press.start.b-(f.bZero==='back'?dz:-dz)},step.value);
}
function pointerUp(event) {
  if(!press||event.pointerId!==press.id) return;
  if(!press.moved) target.value=handPosition(part.value,face.value,press.hit,step.value);
  press=null;
}
function dblclick(event) {const p=topPoint(event),hit=p&&onFace(p);if(hit) {target.value=handPosition(part.value,face.value,hit,step.value);drill();}}
function keydown(event) {
  const tag=event.target?.tagName;
  if(event.key==='Escape') {event.preventDefault();emit('close');return;}
  if(['INPUT','TEXTAREA','SELECT'].includes(tag)||event.ctrlKey||event.metaKey||event.altKey) return;
  const arrows={ArrowLeft:['a',-1],ArrowRight:['a',1],ArrowUp:['b',faceInfo.value.bZero==='back'?-1:1],ArrowDown:['b',faceInfo.value.bZero==='back'?1:-1]}[event.key];
  if(arrows) {event.preventDefault();nudge(arrows[0],arrows[1]*(event.shiftKey?10:1));return;}
  if(event.key==='Enter'||event.key===' ') {if(tag==='BUTTON') return;event.preventDefault();drill();}
}
onMounted(async()=>{
  window.addEventListener('keydown',keydown);
  await nextTick();
  try {view=createBenchScene(host.value,{down:pointerDown,move:pointerMove,up:pointerUp,double:dblclick});buildPiece();placeBit();frameCamera();}
  catch {webgl.value=false;view=null;}
});
// Inside the kept-alive designer: keys only while the designer is shown.
onActivated(()=>window.addEventListener('keydown',keydown));
onDeactivated(()=>window.removeEventListener('keydown',keydown));
onBeforeUnmount(()=>{window.removeEventListener('keydown',keydown);view?.dispose();});
const faceLabel=k=>benchFace(part.value,k).label;
const holeText=h=>`Hole ${h.index} · Ø${h.feature.diameter} · ${where(h.at.a,h.at.b)} · ${h.feature.depth} mm`;
</script>
<template>
  <section v-if="part" class="workbench hand-bench drilling-bench" aria-label="Drilling bench">
    <BenchDesk storage-key="drilling-bench" :windows="WINDOWS" :status="readout">
      <template #background><div ref="host" class="bench-canvas"></div><p v-if="!webgl" class="bench-note">The bench view needs WebGL; positions, bits and drilling all work from the windows.</p></template>
      <template #start>
        <button @click="emit('close')">Back to designer</button>
        <button :aria-pressed="!topView" @click="topView=false">Bench view</button>
        <button :aria-pressed="topView" @click="topView=true">Look down the bit</button>
      </template>
      <template #press><div class="bench-panel">
        <h2>{{ part.name }}</h2>
        <p class="hint">{{ part.length }} × {{ part.width }} × {{ part.thickness }} mm · {{ MATERIALS[part.material].label }}</p>
        <fieldset><legend>Face up</legend>
        <div class="bench-choices"><button v-for="k in BENCH_FACES" :key="k" :aria-pressed="face===k" @click="face=k">{{ faceLabel(k) }}</button></div>
      </fieldset>
      <fieldset><legend>Drill rack</legend>
        <div class="bench-choices rack" role="radiogroup" aria-label="Drill bits">
          <button v-for="b in DRILL_BITS" :key="b.id" role="radio" :aria-checked="bitId===b.id" :title="BIT_KINDS[b.kind]+' · '+b.use" @click="chooseBit(b)">{{ bitLabel(b) }}</button>
        </div>
        <label>Bit diameter mm<input type="number" min="0.5" max="200" step="0.5" :value="diameter" @change="setDiameter"></label>
      </fieldset>
      <fieldset><legend>Depth stop</legend>
        <label>Depth mm<input type="number" min="0.01" :max="faceInfo.depthLimit" step="0.5" :value="depth" :disabled="through" @change="setDepth"></label>
        <label class="snap-toggle"><input v-model="through" type="checkbox" aria-label="Drill through">Through ({{ faceInfo.depthLimit }} mm)</label>
      </fieldset>
      <fieldset><legend>Position under the bit</legend>
        <label>{{ faceInfo.a.label }} ({{ faceInfo.a.key }}) mm<input type="number" step="0.5" :value="target.a" aria-label="Position along" @change="setPosition('a',$event)"></label>
        <label>{{ faceInfo.b.label }} ({{ faceInfo.b.key }}) mm<input type="number" step="0.5" :value="target.b" aria-label="Position across" @change="setPosition('b',$event)"></label>
        <p class="hint">{{ faceInfo.b.key }} = 0 is the {{ faceInfo.bZero==='back'?'back edge, at the fence':'front edge' }}; {{ faceInfo.a.key }} = 0 is the left end, at the stop.</p>
        <label>Hand step<select v-model.number="step" aria-label="Hand step"><option :value="0">exact</option><option v-for="s in [.5,1,5,10,32]" :key="s" :value="s">{{ s }} mm</option></select></label>
      </fieldset>
      <fieldset><legend>Row of holes</legend>
        <div class="bench-row">
          <label>Holes<input type="number" min="1" max="50" step="1" :value="row.count" aria-label="Holes in the row" @change="setRow('count',$event)"></label>
          <label>Spacing mm<input type="number" min="1" max="5000" step="1" :value="row.spacing" aria-label="Row spacing" @change="setRow('spacing',$event)"></label>
          <label>Along<select :value="row.along" aria-label="Row direction" @change="setRow('along',$event)"><option value="a">{{ faceInfo.a.key }}</option><option value="b">{{ faceInfo.b.key }}</option></select></label>
        </div>
      </fieldset>
      <button class="primary bench-drill" :disabled="drilling||!!check" @click="drill">{{ row.count>1?`Drill ${row.count} holes`:'Drill' }}</button>
      <p v-if="check" class="bench-problem" data-testid="bench-problem" role="alert">{{ check }}</p>
      </div></template>
      <template #holes><div class="bench-panel"><p class="eyebrow">HOLES ON THIS FACE · {{ existing.length }}</p>
      <ol class="bench-holes">
        <li v-for="h in existing" :key="h.feature.id">
          <button :aria-label="'Go to '+holeText(h)" @click="moveTo(h.at.a,h.at.b)">{{ holeText(h) }}</button>
          <button :aria-label="'Remove '+holeText(h)" @click="emit('remove',h.feature.id)">Remove</button>
        </li>
      </ol></div></template>
      <template #layers><BenchLayers :design="design" v-model:layer-id="layerId" @commit="emit('commit',$event)" @message="emit('message',$event)"/></template>
      <template #help><div class="bench-panel"><p class="hint">By hand: click the panel to bring that point under the bit, drag the panel to slide it, use the arrow keys to nudge it by the hand step (Shift: ×10), and press Enter or double-click to drill. Escape returns to the designer; Ctrl/⌘+Z undoes the last drilling.</p></div></template>
    </BenchDesk>
  </section>
</template>
