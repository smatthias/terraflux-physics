<script setup>
// Cutting bench: an overhead saw and a router for one piece. Pick a blade or a
// bit, set the blade height or depth, position the piece by entering the
// position or by hand, and cut. A saw cut through the piece divides it in two;
// a shallower one saws a groove the width of the kerf. The router cuts grooves
// and pockets. Each cut is one design edit (undo with Ctrl/⌘+Z). Loaded on first use.
import {ref,computed,watch,onMounted,onBeforeUnmount,onActivated,onDeactivated,nextTick} from 'vue';
import * as THREE from 'three';
import {createBenchScene,removalBox,steel,paint} from './bench-scene.js';
import BenchDesk from './BenchDesk.vue';
import BenchLayers from './BenchLayers.vue';
import {PIECE_FORMS} from '../shared/furniture-design.mjs';
import {splitPart,addMachining} from '../shared/bench-edits.mjs';
import {SAW_BLADES,ROUTER_BITS,cutFrame,cutMatrix,sawPlan,routerPlan} from '../shared/cutting-bench.mjs';
import {MATERIALS} from '../shared/materials.mjs';

const props=defineProps({design:{type:Object,required:true},partId:{type:String,required:true}});
const emit=defineEmits(['commit','remove','close','message']);
const part=computed(()=>props.design.parts.find(p=>p.id===props.partId)??null);
watch(part,value=>{if(!value) emit('close');});
const panel=computed(()=>part.value?.shape==='panel');

// The bench's windows (BenchDesk.vue), back to front, with where they open.
const WINDOWS=[
  {id:'recesses',title:'Grooves and pockets',rect:d=>({x:8,y:Math.max(8,d.height-248),w:360,h:240})},
  {id:'layers',title:'Layers',rect:d=>({x:8,y:8,w:290,h:Math.min(240,d.height-16)})},
  {id:'tools',title:'Saw and router',rect:d=>({x:Math.max(8,d.width-378),y:8,w:370,h:Math.min(d.height-16,680)})},
  {id:'help',title:'Help',rect:d=>({x:Math.round(d.width/2-220),y:Math.max(8,Math.round(d.height/2-110)),w:440,h:220}),minimized:true},
];
const layerId=ref('');   // the layer new grooves and pockets go on ('' = by operation)
// ---- Settings (kept while the bench is open)
const TOOLS=[...SAW_BLADES,...ROUTER_BITS];
const toolId=ref('saw-3.2'),face=ref('A'),direction=ref('cross'),through=ref(true),depth=ref(6),step=ref(1),mode=ref('groove');
const target=ref({a:0,b:0}),end=ref({a:0,b:0}),working=ref(false);
const tool=computed(()=>TOOLS.find(t=>t.id===toolId.value));
const saw=computed(()=>tool.value.kind!=='router');
const frame=computed(()=>part.value?cutFrame(part.value,panel.value?face.value:'top'):null);
function centre() {
  const f=frame.value,a=Math.round(f.a.length/2*10)/10,b=Math.round(f.b.length/2*10)/10;
  target.value={a,b};end.value={a:Math.min(f.a.length,a+100),b};
}
if(part.value) centre();
watch(face,centre);
// After a cut the piece is shorter: start again from its middle if the position is off it.
watch(()=>part.value&&[part.value.length,part.value.width],()=>{if(part.value&&(target.value.a>=frame.value.a.length||target.value.b>=frame.value.b.length)) centre();});
function chooseTool(t) {
  toolId.value=t.id;
  if(t.kind==='router') {through.value=false;if(!panel.value) emit('message','The router works on panels');}
  else if(t.kind==='dado') through.value=false;
}
// A position kept on the piece, rounded to `stepSize` mm (0: exact).
function onTop({a,b},stepSize=0) {
  const f=frame.value,snapTo=v=>stepSize>0?Math.round(v/stepSize)*stepSize:v,clamp=(v,max)=>Math.round(Math.min(max,Math.max(0,snapTo(v)))*1e6)/1e6;
  return {a:clamp(a,f.a.length),b:clamp(b,f.b.length)};
}
const handStep=p=>onTop(p);
function moveTo(a,b) {const next=handStep({a,b}),da=next.a-target.value.a,db=next.b-target.value.b;target.value=next;end.value=handStep({a:end.value.a+da,b:end.value.b+db});}
function nudge(axis,sign) {const s=(step.value||1)*sign;moveTo(target.value.a+(axis==='a'?s:0),target.value.b+(axis==='b'?s:0));}
// Number fields apply valid values; anything else reverts the field.
function numberFrom(event,min,max,current) {const value=Number(event.target.value);if(event.target.value!==''&&Number.isFinite(value)&&value>=min&&value<=max) return value;event.target.value=current;return null;}
function setStart(axis,event) {const v=numberFrom(event,-1e4,1e4,target.value[axis]);if(v!==null) moveTo(axis==='a'?v:target.value.a,axis==='b'?v:target.value.b);}
function setEnd(axis,event) {const v=numberFrom(event,-1e4,1e4,end.value[axis]);if(v!==null) end.value=handStep({...end.value,[axis]:v});}
function setDepth(event) {const v=numberFrom(event,.01,frame.value.depthLimit,depth.value);if(v!==null) depth.value=v;}

// ---- What the tool will do, and whether it can
const plan=computed(()=>{
  if(!part.value) return {error:'No piece'};
  try {
    if(saw.value) return sawPlan(part.value,panel.value?face.value:'top',{direction:panel.value?direction.value:'cross',at:direction.value==='rip'&&panel.value?target.value.b:target.value.a,kerf:tool.value.kerf,depth:depth.value,through:through.value});
    return {kind:'feature',feature:routerPlan(part.value,face.value,{mode:mode.value,from:target.value,to:end.value,diameter:tool.value.diameter,depth:depth.value,through:through.value})};
  } catch(error) {return {error:error.message};}
});
const check=computed(()=>{
  const p=plan.value;if(p.error) return p.error;
  let n=0;const id=()=>'check-'+(++n);
  try {
    if(p.kind==='split') splitPart(props.design,props.partId,{along:p.along,at:p.at,kerf:p.kerf},id);
    else addMachining(props.design,props.partId,[p.feature],id,'machined',layerId.value||null);
    return '';
  } catch(error) {return error.message;}
});
const where=computed(()=>saw.value?(direction.value==='rip'&&panel.value?`${frame.value.b.key} ${target.value.b}`:`${frame.value.a.key} ${target.value.a}`):`${frame.value.a.key} ${target.value.a} · ${frame.value.b.key} ${target.value.b}`);
const readout=computed(()=>`${tool.value.label} · ${saw.value?(direction.value==='rip'&&panel.value?'rip':'crosscut'):mode.value} at ${where.value} · ${check.value||outcome.value}`);
const action=computed(()=>{
  const p=plan.value;
  if(p.kind==='split') return p.rest<1?'Trim':'Cut in two';
  if(p.kind==='groove') return 'Saw groove';
  return mode.value==='pocket'?'Rout pocket':'Rout groove';
});
const outcome=computed(()=>{
  const p=plan.value;if(p.error) return '';
  if(p.kind==='split') return p.rest<1?`trims to ${p.at} mm`:`${p.at} mm + ${p.rest} mm pieces, ${p.kerf} mm kerf`;
  const f=p.feature;return `${f.type} ${f.length} × ${f.width} × ${f.depth} mm at ${frame.value.a.key} ${f.u} · ${frame.value.b.key} ${f.v}`;
});
async function cut() {
  if(working.value||check.value||!part.value) {if(check.value) emit('message',check.value);return;}
  const p=plan.value,name=part.value.name;
  working.value=true;await animateTool();working.value=false;
  // The cut applies to the design as it is now (it may have changed meanwhile).
  const makeId=()=>crypto.randomUUID();
  try {
    if(p.kind==='split') emit('commit',{design:splitPart(props.design,props.partId,{along:p.along,at:p.at,kerf:p.kerf},makeId).design,message:p.rest<1?`Trimmed ${name} to ${p.at} mm`:`Cut ${name} in two: ${p.at} mm and ${p.rest} mm (${p.kerf} mm kerf)`});
    else {
      const f=p.feature,text=p.kind==='groove'?`Sawed a ${tool.value.kerf} mm groove ${f.depth} mm deep at ${where.value}`:f.type==='pocket'?`Routed a pocket ${f.length} × ${f.width} × ${f.depth} mm`:`Routed a groove ${Math.max(f.length,f.width)} mm long, Ø${tool.value.diameter} × ${f.depth} mm`;
      emit('commit',{design:addMachining(props.design,props.partId,[f],makeId,'machined',layerId.value||null).design,message:text});
    }
  } catch(error) {emit('message',error.message);}
}
const machining=computed(()=>(part.value?.features??[]).map((f,i)=>({feature:f,index:i+1})).filter(h=>h.feature.type!=='drill'&&h.feature.face===face.value));
const featureText=h=>`${h.feature.type==='pocket'?'Pocket':'Groove'} ${h.index} · ${h.feature.length} × ${h.feature.width} × ${h.feature.depth} mm at u ${h.feature.u} · v ${h.feature.v}`;

// ---- 3D bench (bench-scene.js)
const host=ref(null),webgl=ref(true),topView=ref(false);
let view=null,toolGroup=null,rest=0;
const toBench=(a,b)=>({x:a-target.value.a,z:frame.value.bZero==='back'?b-target.value.b:-(b-target.value.b)});
function buildPiece() {
  if(!view||!part.value) return;
  const f=frame.value;
  view.setPiece(props.design,part.value,cutMatrix(part.value,panel.value?face.value:'top',target.value),{height:f.height,backZ:f.bZero==='back'?-target.value.b:-(f.b.length-target.value.b),leftX:-target.value.a});
  // The material the tool will remove.
  const p=plan.value,box=(a0,a1,b0,b1,y0,y1)=>{const p0=toBench(a0,b0),p1=toBench(a1,b1);return removalBox([Math.min(p0.x,p1.x),y0,Math.min(p0.z,p1.z)],[Math.max(p0.x,p1.x),y1,Math.max(p0.z,p1.z)],check.value?0xff2020:0xff8a3c);};
  if(p.error) view.setOverlay(null);
  else if(p.kind==='split') view.setOverlay(p.along==='length'?box(p.at,p.at+p.kerf,0,f.b.length,0,f.height):box(0,f.a.length,p.at,p.at+p.kerf,0,f.height));
  else {const g=p.feature;view.setOverlay(box(g.u,g.u+g.length,g.v,g.v+g.width,f.height-g.depth,f.height));}
}
// Overhead saw (a blade in its guard) or router (bit, collet, motor) above the cut.
function placeTool() {
  if(!view||!part.value) return;
  const f=frame.value;toolGroup=new THREE.Group();
  const add=(geometry,material,position,rotate)=>{const mesh=new THREE.Mesh(geometry,material);if(rotate) rotate(mesh);mesh.position.set(...position);toolGroup.add(mesh);};
  if(saw.value) {
    const r=150,k=tool.value.kerf,rip=direction.value==='rip'&&panel.value,offset=toBench(target.value.a,target.value.b);
    const c=rip?{x:0,z:offset.z+(f.bZero==='back'?k/2:-k/2)}:{x:k/2,z:0};
    add(new THREE.CylinderGeometry(r,r,k,48),steel(),[c.x,r,c.z],m=>rip?m.rotateX(Math.PI/2):m.rotateZ(Math.PI/2));
    add(new THREE.BoxGeometry(rip?360:60,140,rip?60:360),paint(0xd9822b),[c.x,r+60,c.z]);
    rest=f.height+20;
  } else {
    const r=tool.value.diameter/2;
    add(new THREE.CylinderGeometry(r,r,40,24),steel(),[0,20,0]);
    add(new THREE.CylinderGeometry(12,14,40,16),paint(0x555b60),[0,60,0]);
    add(new THREE.CylinderGeometry(50,50,220,32),paint(0x2f5d50),[0,190,0]);
    rest=f.height+15;
  }
  toolGroup.position.y=rest;view.setTool(toolGroup);
}
function animateTool() {
  if(!toolGroup||!view) return Promise.resolve();
  const f=frame.value,p=plan.value,bottom=f.height-(p.kind==='split'?f.height+2:p.feature.depth);
  return view.play(560,t=>{const down=t<.5?t*2:(1-t)*2;toolGroup.position.y=t<1?rest+(bottom-rest)*down:rest;});
}
const frameCamera=()=>view?.frame({height:frame.value.height,size:Math.max(frame.value.a.length,frame.value.b.length,400),top:topView.value});
watch([face,target,end,toolId,direction,depth,through,mode,()=>props.design],()=>{buildPiece();placeTool();},{deep:true});
watch([face,topView,()=>part.value?.id],frameCamera);
// By hand: a click brings that point under the tool and a drag slides the piece;
// with the router, a drag draws the groove or pocket from where it started.
let press=null;
const topPoint=event=>view.topPoint(event,frame.value.height);
const onPiece=p=>{const f=frame.value,a=target.value.a+p.x,b=target.value.b+(f.bZero==='back'?p.z:-p.z);return a>=0&&a<=f.a.length&&b>=0&&b<=f.b.length?{a,b}:null;};
const snap=p=>onTop(p,step.value);
function pointerDown(event) {
  if(event.button!==0||working.value) return;
  const p=topPoint(event),hit=p&&onPiece(p);if(!hit) return;
  event.stopImmediatePropagation();view.capture(event.pointerId);
  press={x:event.clientX,y:event.clientY,point:p,start:{...target.value},hit,moved:false,id:event.pointerId};
}
function pointerMove(event) {
  if(!press||event.pointerId!==press.id) return;
  if(!press.moved&&Math.hypot(event.clientX-press.x,event.clientY-press.y)<=4) return;
  press.moved=true;
  const p=topPoint(event);if(!p) return;
  if(!saw.value) {
    // Drawing: the start (where the press was) goes under the bit; the end follows the pointer.
    if(!press.drawing) {press.drawing=true;const s=snap(press.hit);moveTo(s.a,s.b);}
    const f=frame.value;end.value=snap({a:target.value.a+p.x,b:target.value.b+(f.bZero==='back'?p.z:-p.z)});
    return;
  }
  const f=frame.value,dx=p.x-press.point.x,dz=p.z-press.point.z,next=snap({a:press.start.a-dx,b:press.start.b-(f.bZero==='back'?dz:-dz)});
  moveTo(next.a,next.b);
}
function pointerUp(event) {
  if(!press||event.pointerId!==press.id) return;
  if(!press.moved) {const p=snap(press.hit);moveTo(p.a,p.b);}
  press=null;
}
function dblclick(event) {const p=topPoint(event),hit=p&&onPiece(p);if(hit&&saw.value) {const q=snap(hit);moveTo(q.a,q.b);cut();}}
function keydown(event) {
  const tag=event.target?.tagName;
  if(event.key==='Escape') {event.preventDefault();emit('close');return;}
  if(['INPUT','TEXTAREA','SELECT'].includes(tag)||event.ctrlKey||event.metaKey||event.altKey) return;
  const f=frame.value,arrows={ArrowLeft:['a',-1],ArrowRight:['a',1],ArrowUp:['b',f.bZero==='back'?-1:1],ArrowDown:['b',f.bZero==='back'?1:-1]}[event.key];
  if(arrows) {event.preventDefault();nudge(arrows[0],arrows[1]*(event.shiftKey?10:1));return;}
  if(event.key==='Enter'||event.key===' ') {if(tag==='BUTTON') return;event.preventDefault();cut();}
}
onMounted(async()=>{
  window.addEventListener('keydown',keydown);
  await nextTick();
  try {view=createBenchScene(host.value,{down:pointerDown,move:pointerMove,up:pointerUp,double:dblclick});buildPiece();placeTool();frameCamera();}
  catch {webgl.value=false;view=null;}
});
onActivated(()=>window.addEventListener('keydown',keydown));
onDeactivated(()=>window.removeEventListener('keydown',keydown));
onBeforeUnmount(()=>{window.removeEventListener('keydown',keydown);view?.dispose();});
</script>
<template>
  <section v-if="part" class="workbench hand-bench cutting-bench" aria-label="Cutting bench">
    <BenchDesk storage-key="cutting-bench" :windows="WINDOWS" :status="readout">
      <template #background><div ref="host" class="bench-canvas"></div><p v-if="!webgl" class="bench-note">The bench view needs WebGL; positions, tools and cutting all work from the windows.</p></template>
      <template #start>
        <button @click="emit('close')">Back to designer</button>
        <button :aria-pressed="!topView" @click="topView=false">Bench view</button>
        <button :aria-pressed="topView" @click="topView=true">Look down at the cut</button>
      </template>
      <template #tools><div class="bench-panel">
        <h2>{{ part.name }}</h2>
        <p class="hint">{{ panel ? `${part.length} × ${part.width} × ${part.thickness} mm` : `${PIECE_FORMS[part.shape].label} · ${part.length} mm long` }} · {{ MATERIALS[part.material].label }}</p>
        <fieldset v-if="panel"><legend>Face up</legend>
        <div class="bench-choices"><button v-for="k in ['A','B']" :key="k" :aria-pressed="face===k" @click="face=k">{{ k==='A'?'Face A up':'Face B up (turned over)' }}</button></div>
      </fieldset>
      <fieldset><legend>Tools</legend>
        <div class="bench-choices rack" role="radiogroup" aria-label="Cutting tools">
          <button v-for="t in TOOLS" :key="t.id" role="radio" :aria-checked="toolId===t.id" :disabled="t.kind==='router'&&!panel" @click="chooseTool(t)">{{ t.label }}</button>
        </div>
      </fieldset>
      <fieldset v-if="saw"><legend>Saw cut</legend>
        <div v-if="panel" class="bench-choices"><button :aria-pressed="direction==='cross'" @click="direction='cross'">Crosscut (across the length)</button><button :aria-pressed="direction==='rip'" @click="direction='rip'">Rip (along the length)</button></div>
        <label v-if="direction==='rip'&&panel">Cut at {{ frame.b.key }} mm (across the width)<input type="number" step="0.5" :value="target.b" aria-label="Cut position" @change="setStart('b',$event)"></label>
        <label v-else>Cut at {{ frame.a.key }} mm (along the length)<input type="number" step="0.5" :value="target.a" aria-label="Cut position" @change="setStart('a',$event)"></label>
        <p class="hint">The kerf runs from the cut position onward; the piece from 0 keeps its name.</p>
        <label class="snap-toggle"><input v-model="through" type="checkbox" aria-label="Cut through" :disabled="!panel">Through (divides the piece)</label>
        <label v-if="!through">Blade height mm<input type="number" min="0.01" :max="frame.depthLimit" step="0.5" :value="depth" aria-label="Blade height" @change="setDepth"></label>
      </fieldset>
      <fieldset v-else><legend>Router</legend>
        <div class="bench-choices"><button :aria-pressed="mode==='groove'" @click="mode='groove'">Groove</button><button :aria-pressed="mode==='pocket'" @click="mode='pocket'">Pocket</button></div>
        <div class="bench-row two">
          <label>From {{ frame.a.key }} mm<input type="number" step="0.5" :value="target.a" aria-label="Start along" @change="setStart('a',$event)"></label>
          <label>From {{ frame.b.key }} mm<input type="number" step="0.5" :value="target.b" aria-label="Start across" @change="setStart('b',$event)"></label>
          <label>To {{ frame.a.key }} mm<input type="number" step="0.5" :value="end.a" aria-label="End along" @change="setEnd('a',$event)"></label>
          <label>To {{ frame.b.key }} mm<input type="number" step="0.5" :value="end.b" aria-label="End across" @change="setEnd('b',$event)"></label>
        </div>
        <p class="hint">{{ mode==='groove' ? 'A groove runs along the length or across, whichever is nearer to the line from start to end; it is the bit wide.' : 'A pocket has the start and end as opposite corners.' }}</p>
        <label>Router depth mm<input type="number" min="0.01" :max="frame.depthLimit" step="0.5" :value="depth" aria-label="Router depth" :disabled="through" @change="setDepth"></label>
        <label class="snap-toggle"><input v-model="through" type="checkbox" aria-label="Rout through">Through ({{ frame.depthLimit }} mm)</label>
      </fieldset>
      <label>Hand step<select v-model.number="step" aria-label="Hand step"><option :value="0">exact</option><option v-for="s in [.5,1,5,10,32]" :key="s" :value="s">{{ s }} mm</option></select></label>
      <button class="primary bench-drill" :disabled="working||!!check" @click="cut">{{ action }}</button>
      <p v-if="check" class="bench-problem" data-testid="cut-problem" role="alert">{{ check }}</p>
      </div></template>
      <template #recesses><div class="bench-panel"><template v-if="panel">
        <p class="eyebrow">GROOVES AND POCKETS ON THIS FACE · {{ machining.length }}</p>
        <ol class="bench-holes">
          <li v-for="h in machining" :key="h.feature.id"><span>{{ featureText(h) }}</span><button :aria-label="'Remove '+featureText(h)" @click="emit('remove',h.feature.id)">Remove</button></li>
        </ol>
      </template><p v-if="!panel" class="hint">Profiles have no grooves or pockets.</p></div></template>
      <template #layers><BenchLayers :design="design" v-model:layer-id="layerId" @commit="emit('commit',$event)" @message="emit('message',$event)"/></template>
      <template #help><div class="bench-panel"><p class="hint">By hand: click the piece to bring that point under the tool, drag the piece to slide it, or with the router drag across the piece to draw the groove or pocket. Arrow keys nudge by the hand step (Shift: ×10); Enter cuts. Escape returns to the designer; Ctrl/⌘+Z undoes the last cut.</p></div></template>
    </BenchDesk>
  </section>
</template>
