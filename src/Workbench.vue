<script setup>
// The carpenter's workbench: a tool chest of hand tools (tool-chest.mjs) to
// measure, mark and adjust one piece, with the drilling and cutting benches a
// click away. Each tool drives a workbench action with its own settings (a
// dovetail saw saws with a 0.6 mm kerf, a jointer plane planes edges); tools the
// design model cannot show are listed with the reason. Positions are typed or set
// by hand (click, drag, arrow keys); pencil and gauge marks snap the tools to
// them. Each change is one design edit (undo with Ctrl/⌘+Z). Loaded on first use.
import {ref,computed,watch,onMounted,onBeforeUnmount,onActivated,onDeactivated,nextTick} from 'vue';
import * as THREE from 'three';
import {createBenchScene,removalBox} from './bench-scene.js';
import BenchDesk from './BenchDesk.vue';
import BenchLayers from './BenchLayers.vue';
import {PIECE_FORMS} from '../shared/furniture-design.mjs';
import {planePiece,finishPiece,cutToSize,addMachining,drillHoles,updateMachining,planeSides} from '../shared/bench-edits.mjs';
import {cutFrame,cutMatrix} from '../shared/cutting-bench.mjs';
import {FINISHES,finishColor,measure,edgeDistances,markLine,snapToMarks,mortise,rebate,planeSlab} from '../shared/workbench.mjs';
import {TOOL_CHEST,CHEST_GROUPS,chestTool,planeSide,gaugeMarks,ploughGroove,housing,widenFeature} from '../shared/tool-chest.mjs';
import {holeAt} from '../shared/drilling-bench.mjs';
import {MATERIALS} from '../shared/materials.mjs';

const props=defineProps({design:{type:Object,required:true},partId:{type:String,required:true}});
const emit=defineEmits(['commit','open','close','message']);
const part=computed(()=>props.design.parts.find(p=>p.id===props.partId)??null);
watch(part,value=>{if(!value) emit('close');});
const panel=computed(()=>part.value?.shape==='panel');

// The bench's windows (BenchDesk.vue), back to front, with where they open.
const WINDOWS=[
  {id:'chest',title:'Tool chest',rect:d=>({x:8,y:8,w:260,h:Math.min(d.height-16,620)})},
  {id:'layers',title:'Layers',rect:d=>({x:276,y:Math.max(8,d.height-236),w:290,h:228})},
  {id:'tool',title:'Tool in hand',rect:d=>({x:Math.max(8,d.width-368),y:8,w:360,h:Math.min(d.height-16,640)})},
  {id:'help',title:'Help',rect:d=>({x:Math.round(d.width/2-220),y:Math.max(8,Math.round(d.height/2-120)),w:440,h:240}),minimized:true},
];
const layerId=ref('');   // the layer new cuts go on ('' = by operation)
// ---- The tool in hand and its settings (kept while the bench is open)
const chestId=ref('rule'),face=ref('A'),step=ref(1),target=ref({a:0,b:0}),other=ref({a:0,b:0}),marks=ref([]),working=ref(false);
const current=computed(()=>chestTool(chestId.value)),tool=computed(()=>current.value.action);
const frame=computed(()=>part.value?cutFrame(part.value,panel.value?face.value:'top'):null);
const faceKey=computed(()=>panel.value?face.value:'top');
const saw=ref({along:'length',size:0,kerf:1.6,keep:false}),plane=ref({side:'A',amount:1});
const chisel=ref({width:10,length:40,along:'a',depth:6}),rebateSet=ref({edge:'v1',width:12,depth:9}),finish=ref({id:'natural',custom:'#8a5a3a'});
const mark=ref({mode:'square',direction:'along',from:'near',distance:10,width:8,parts:3});
const plough=ref({width:6,distance:10,from:'near',depth:8}),router=ref({mode:'deepen',featureId:'',depth:6,width:18});
const shoulder=ref({featureId:'',toward:'high',amount:.5}),bore=ref({diameter:8,depth:12,through:false});
// Tool settings by name (templates see refs unwrapped, so they pass the name).
const models={saw,plane,chisel,rebate:rebateSet,finish,mark,plough,router,shoulder,bore};
function reset() {
  const f=frame.value;
  target.value={a:Math.round(f.a.length/4),b:Math.round(f.b.length/2)};other.value={a:Math.round(f.a.length*3/4),b:Math.round(f.b.length/2)};
  saw.value={...saw.value,along:panel.value?saw.value.along:'length',size:Math.max(1,Math.round((part.value[panel.value?saw.value.along:'length'])-10))};
  if(!planeSides(part.value).includes(plane.value.side)) plane.value={...plane.value,side:planeSides(part.value)[0]};
  for(const [model,key] of [[chisel,'depth'],[rebateSet,'depth'],[plough,'depth'],[router,'depth'],[bore,'depth']]) model.value={...model.value,[key]:Math.max(.5,Math.min(model.value[key],f.depthLimit-1))};
}
if(part.value) reset();
// Picking a tool from the chest applies its settings.
function pick(t) {
  chestId.value=t.id;const p=t.preset;
  if(t.action==='saw') saw.value={...saw.value,along:panel.value?p.along:'length',kerf:p.kerf,size:Math.max(1,Math.round(part.value[panel.value?p.along:'length']-10))};
  if(t.action==='plane') plane.value={side:planeSide(part.value,faceKey.value,p.side),amount:p.amount};
  if(t.action==='chisel') chisel.value={...chisel.value,width:p.widths.includes(chisel.value.width)?chisel.value.width:p.widths[0],depth:Math.min(p.depth,frame.value.depthLimit)};
  if(t.action==='bore') bore.value={...bore.value,diameter:p.bits.includes(bore.value.diameter)?bore.value.diameter:p.bits[Math.floor(p.bits.length/2)],depth:Math.min(p.depth,frame.value.depthLimit)};
  if(t.action==='mark') mark.value={...mark.value,mode:p.mode,...(p.direction?{direction:p.direction}:{})};
  if(t.action==='plough'&&!p.widths.includes(plough.value.width)) plough.value={...plough.value,width:p.widths[0]};
  if(!t.action) emit('message',`${t.name}: ${t.note}`);
}
watch(face,()=>{marks.value=[];reset();if(tool.value==='plane') pick(current.value);});
watch(()=>part.value&&[part.value.length,part.value.width,part.value.thickness,part.value.height],(now,before)=>{if(part.value&&before&&String(now)!==String(before)) {marks.value=[];reset();}});
// Hand positions stay on the piece, round to the hand step and snap to marks.
function onTop({a,b},stepSize=step.value) {
  const f=frame.value,snapTo=v=>stepSize>0?Math.round(v/stepSize)*stepSize:v,clamp=(v,max)=>Math.round(Math.min(max,Math.max(0,snapTo(v)))*1e6)/1e6;
  return snapToMarks({a:clamp(a,f.a.length),b:clamp(b,f.b.length)},marks.value);
}
const moveTo=(a,b)=>{target.value=onTop({a,b},0);};
function nudge(axis,sign) {const s=(step.value||1)*sign;moveTo(target.value.a+(axis==='a'?s:0),target.value.b+(axis==='b'?s:0));}
function numberFrom(event,min,max,current) {const value=Number(event.target.value);if(event.target.value!==''&&Number.isFinite(value)&&value>=min&&value<=max) return value;event.target.value=current;return null;}
function setPoint(which,axis,event) {const ref_=which==='target'?target:other,v=numberFrom(event,-1e4,1e4,ref_.value[axis]);if(v!==null) ref_.value=onTop({...ref_.value,[axis]:v},0);}
function setIn(name,key,event,min,max) {const model=models[name],v=numberFrom(event,min,max,model.value[key]);if(v!==null) model.value={...model.value,[key]:v};}
const choose=(name,key,value)=>{const model=models[name];model.value={...model.value,[key]:value};};

// ---- What the tool does
const bZeroLabel=computed(()=>frame.value.bZero==='back'?'back':'front');
const sideLabel=side=>({A:'Face A',B:'Face B',u0:'End u = 0 (left)',u1:'End u = length (right)',v0:`Edge v = 0 (${bZeroLabel.value})`,v1:`Edge v = width (${bZeroLabel.value==='back'?'front':'back'})`,
  width0:'Side (width, first)',width1:'Side (width, second)',height0:'Underside',height1:'Top side',length0:'Start end',length1:'Far end'})[side];
// Grooves and pockets on the face that is up, for the router and shoulder planes.
const recesses=computed(()=>(part.value?.features??[]).map((f,i)=>({f,index:i+1})).filter(h=>h.f.type!=='drill'&&h.f.face===face.value));
const recessLabel=h=>`${h.f.type==='pocket'?'Pocket':'Groove'} ${h.index} · ${h.f.length} × ${h.f.width} × ${h.f.depth} mm`;
watch(recesses,list=>{for(const model of [router,shoulder]) if(!list.some(h=>h.f.id===model.value.featureId)) model.value={...model.value,featureId:list[0]?.f.id??''};},{immediate:true});
const plan=computed(()=>{
  const p=part.value;if(!p) return {error:'No piece'};
  try {
    switch(tool.value) {
      case 'saw':return {kind:'saw',rest:Math.round((p[saw.value.along]-saw.value.size-saw.value.kerf)*1000)/1000};
      case 'plane':return {kind:'plane',slab:planeSlab(p,faceKey.value,plane.value.side,plane.value.amount)};
      case 'chisel':return {kind:'feature',feature:mortise(p,faceKey.value,{at:target.value,...chisel.value})};
      case 'rebate':return {kind:'feature',feature:rebate(p,faceKey.value,rebateSet.value)};
      case 'plough':return {kind:'feature',feature:ploughGroove(p,faceKey.value,plough.value)};
      case 'router':
        if(router.value.mode==='housing') return {kind:'feature',feature:housing(p,faceKey.value,{at:target.value.a,width:router.value.width,depth:router.value.depth})};
        if(!router.value.featureId) throw Error('There is no groove or pocket on this face to deepen');
        return {kind:'update',featureId:router.value.featureId,changes:{depth:router.value.depth}};
      case 'shoulder': {
        const f=recesses.value.find(h=>h.f.id===shoulder.value.featureId)?.f;
        if(!f) throw Error('There is no groove or pocket on this face to trim');
        return {kind:'update',featureId:f.id,changes:widenFeature(f,shoulder.value)};
      }
      case 'bore':if(!panel.value) throw Error('Bore holes in panels (the drilling bench has edges too)');
        return {kind:'hole',hole:holeAt(p,face.value,{...target.value,diameter:bore.value.diameter,depth:bore.value.depth,through:bore.value.through})};
      case 'brush':return {kind:'finish',color:finishColor(p,finish.value.id,finish.value.custom)};
      default:return {kind:tool.value??'reference'};
    }
  } catch(error) {return {error:error.message};}
});
// The edit, on the design as it is now.
function edit(design) {
  const p=plan.value,makeId=()=>crypto.randomUUID();
  if(p.kind==='saw') return cutToSize(design,props.partId,{along:saw.value.along,size:saw.value.size,kerf:saw.value.kerf,keepOffcut:saw.value.keep},makeId).design;
  if(p.kind==='plane') return planePiece(design,props.partId,plane.value.side,plane.value.amount).design;
  if(p.kind==='feature') return addMachining(design,props.partId,[p.feature],makeId,'machined',layerId.value||null).design;
  if(p.kind==='update') return updateMachining(design,props.partId,p.featureId,p.changes);
  if(p.kind==='hole') return drillHoles(design,props.partId,[p.hole],makeId,layerId.value||null).design;
  if(p.kind==='finish') return finishPiece(design,props.partId,p.color);
  throw Error('This tool measures and marks; it does not change the piece');
}
const measuring=computed(()=>['tape','mark','reference'].includes(plan.value.kind));
const check=computed(()=>{if(plan.value.error) return plan.value.error;if(measuring.value) return '';try {edit(props.design);return '';} catch(error) {return error.message;}});
const actionLabel=computed(()=>({saw:'Cut to size',plane:`Plane off ${plane.value.amount} mm`,chisel:'Chisel mortise',rebate:'Cut rebate',plough:'Plough groove',
  router:router.value.mode==='housing'?'Cut housing':'Rout to depth',shoulder:'Trim wall',bore:'Bore hole',brush:'Apply finish'})[tool.value]);
const readout=computed(()=>{
  const t=current.value.name,p=plan.value,f=frame.value;
  if(!tool.value) return `${t} · ${current.value.note}`;
  if(check.value) return `${t} · ${check.value}`;
  if(tool.value==='tape') {const m=measure(target.value,other.value),e=edgeDistances(part.value,faceKey.value,target.value);return `${t} · ${m.distance} mm (${f.a.key} ${m.da}, ${f.b.key} ${m.db}) · cross from the start ${e.start}, the end ${e.end}, the ${bZeroLabel.value} ${e.near}, the other side ${e.far}`;}
  if(tool.value==='mark') return `${t} · ${marks.value.length} marks · cross at ${f.a.key} ${target.value.a} · ${f.b.key} ${target.value.b}`;
  if(p.kind==='saw') return `${t} · ${saw.value.along} ${saw.value.size} mm, ${saw.value.kerf} mm kerf${p.rest>=1?` · offcut ${p.rest} mm ${saw.value.keep?'kept':'discarded'}`:''}`;
  if(p.kind==='plane') return `${t} · ${plane.value.amount} mm off ${sideLabel(plane.value.side)}`;
  if(p.kind==='feature') {const g=p.feature;return `${t} · ${g.length} × ${g.width} × ${g.depth} mm at u ${g.u} · v ${g.v}`;}
  if(p.kind==='update') return `${t} · ${Object.entries(p.changes).map(([k,v])=>`${k} ${v} mm`).join(' · ')}`;
  if(p.kind==='hole') return `${t} · Ø${bore.value.diameter} × ${p.hole.depth} mm at ${f.a.key} ${target.value.a} · ${f.b.key} ${target.value.b}`;
  return `${t} · ${FINISHES.find(x=>x.id===finish.value.id)?.label??'Custom colour'}`;
});
async function apply() {
  if(working.value||check.value||!actionLabel.value) {if(check.value) emit('message',check.value);return;}
  const p=part.value,t=current.value.name,r=recesses.value.find(h=>h.f.id===(tool.value==='router'?router.value.featureId:shoulder.value.featureId));
  const text={saw:`${t}: cut ${p.name} to ${saw.value.size} mm${saw.value.keep&&plan.value.rest>=1?', offcut kept':''}`,plane:`${t}: ${plane.value.amount} mm off ${sideLabel(plane.value.side).replace(/^./,c=>c.toLowerCase())} of ${p.name}`,
    chisel:`${t}: a ${chisel.value.width} mm mortise ${chisel.value.length} mm long, ${chisel.value.depth} mm deep`,rebate:`${t}: a rebate ${rebateSet.value.width} × ${rebateSet.value.depth} mm along ${rebateSet.value.edge}`,
    plough:`${t}: a ${plough.value.width} mm groove ${plough.value.depth} mm deep, ${plough.value.distance} mm from the ${plough.value.from==='near'?bZeroLabel.value:'other'} edge`,
    router:router.value.mode==='housing'?`${t}: a housing ${router.value.width} mm wide, ${router.value.depth} mm deep at ${frame.value.a.key} ${target.value.a}`:`${t}: ${r?recessLabel(r).split(' · ')[0].toLowerCase():'recess'} to ${router.value.depth} mm deep`,
    shoulder:`${t}: trimmed ${shoulder.value.amount} mm off the wall of ${r?recessLabel(r).split(' · ')[0].toLowerCase():'the recess'}`,
    bore:`${t}: Ø${bore.value.diameter} × ${plan.value.hole?.depth} mm hole at ${frame.value.a.key} ${target.value.a} · ${frame.value.b.key} ${target.value.b}`,
    brush:`${t}: ${p.name} finished with ${(FINISHES.find(f=>f.id===finish.value.id)?.label??'a custom colour').toLowerCase()}`}[tool.value];
  working.value=true;await stroke();working.value=false;
  try {emit('commit',{design:edit(props.design),message:text});} catch(error) {emit('message',error.message);}
}
// Marking: through the cross, with a gauge, dividers or an awl.
function addMarks(list,text) {
  marks.value=[...marks.value.filter(m=>!list.some(n=>n.direction===m.direction&&n.at===m.at)),...list];
  emit('message',`${current.value.name}: ${text}`);
}
function markThrough(direction) {
  try {addMarks([markLine(part.value,faceKey.value,{direction,at:direction==='across'?target.value.a:target.value.b})],direction==='across'?`marked across at ${frame.value.a.key} ${target.value.a}`:`marked along at ${frame.value.b.key} ${target.value.b}`);}
  catch(error) {emit('message',error.message);}
}
function markWith() {
  try {
    const m=mark.value,list=gaugeMarks(part.value,faceKey.value,m),f=frame.value;
    addMarks(list,list.length===1?`marked ${m.direction} at ${m.direction==='across'?f.a.key:f.b.key} ${list[0].at}`:`${list.length} marks ${m.direction} at ${list.map(x=>x.at).join(', ')}`);
  } catch(error) {emit('message',error.message);}
}

// ---- 3D bench (bench-scene.js): the piece, marks, the tape and what a tool removes.
const host=ref(null),webgl=ref(true),topView=ref(false);
let view=null,toolGroup=null;
const toBench=(a,b)=>({x:a-target.value.a,z:frame.value.bZero==='back'?b-target.value.b:-(b-target.value.b)});
function line(p,q,y,color,width=1.2) {
  const s=toBench(p.a,p.b),e=toBench(q.a,q.b),length=Math.hypot(e.x-s.x,e.z-s.z)||.1;
  const mesh=new THREE.Mesh(new THREE.BoxGeometry(length,.3,width),new THREE.MeshBasicMaterial({color}));
  mesh.position.set((s.x+e.x)/2,y,(s.z+e.z)/2);mesh.rotation.y=-Math.atan2(e.z-s.z,e.x-s.x);return mesh;
}
function buildScene() {
  if(!view||!part.value) return;
  const f=frame.value,p=plan.value;
  // The brush previews its finish on the piece.
  const shown=p.kind==='finish'&&!check.value?{...part.value,color:p.color}:part.value;
  view.setPiece(props.design,shown,cutMatrix(part.value,faceKey.value,target.value),{height:f.height,backZ:f.bZero==='back'?-target.value.b:-(f.b.length-target.value.b),leftX:-target.value.a});
  const group=new THREE.Group(),y=f.height+.4;
  for(const m of marks.value) group.add(m.direction==='across'?line({a:m.at,b:0},{a:m.at,b:f.b.length},y,0x222222,.8):line({a:0,b:m.at},{a:f.a.length,b:m.at},y,0x222222,.8));
  if(tool.value==='tape') group.add(line(target.value,other.value,y+.2,0xf2c230,2));
  const box=(a0,a1,b0,b1,y0,y1)=>{const p0=toBench(a0,b0),p1=toBench(a1,b1);return removalBox([Math.min(p0.x,p1.x),y0,Math.min(p0.z,p1.z)],[Math.max(p0.x,p1.x),y1,Math.max(p0.z,p1.z)],check.value?0xff2020:0xff8a3c);};
  if(!p.error) {
    if(p.kind==='saw') group.add(saw.value.along==='length'?box(saw.value.size,saw.value.size+saw.value.kerf,0,f.b.length,0,f.height):box(0,f.a.length,saw.value.size,saw.value.size+saw.value.kerf,0,f.height));
    if(p.kind==='plane'&&p.slab) group.add(box(p.slab.a[0],p.slab.a[1],p.slab.b[0],p.slab.b[1],p.slab.y[0],p.slab.y[1]));
    if(p.kind==='feature') {const g=p.feature;group.add(box(g.u,g.u+g.length,g.v,g.v+g.width,f.height-g.depth,f.height));}
    if(p.kind==='hole') {const r=bore.value.diameter/2,g=p.hole;group.add(box(target.value.a-r,target.value.a+r,target.value.b-r,target.value.b+r,f.height-g.depth,f.height));}
    if(p.kind==='update') {const g={...recesses.value.find(h=>h.f.id===p.featureId).f,...p.changes};group.add(box(g.u,g.u+g.length,g.v,g.v+g.width,f.height-g.depth,f.height));}
  }
  view.setOverlay(group);
}
// A simple hand tool hovering over the cross (moves down and up when used).
const LOOKS={tape:[0xf2c230,[60,40,60]],mark:[0x9aa1a6,[200,4,30]],saw:[0xb8bec4,[260,90,2]],plane:[0x8a5a2b,[220,60,60]],chisel:[0xb8bec4,[12,150,25]],rebate:[0x8a5a2b,[200,60,40]],
  plough:[0x8a5a2b,[220,70,60]],router:[0x8a5a2b,[200,50,80]],shoulder:[0x6b7378,[160,40,20]],bore:[0x6b4a32,[30,260,30]],brush:[0xc0392b,[40,120,15]]};
function placeTool() {
  if(!view) return;
  const [color,size]=LOOKS[tool.value]??[0x9aa1a6,[40,40,40]];
  toolGroup=new THREE.Group();const mesh=new THREE.Mesh(new THREE.BoxGeometry(...size),new THREE.MeshStandardMaterial({color,roughness:.5,metalness:color===0xb8bec4?.7:.1}));
  mesh.position.y=size[1]/2;toolGroup.add(mesh);toolGroup.visible=!!tool.value;toolGroup.position.y=frame.value.height+40;view.setTool(toolGroup);
}
function stroke() {
  if(!toolGroup||!view) return Promise.resolve();
  const top=frame.value.height+40,low=frame.value.height+2;
  return view.play(420,t=>{const down=t<.5?t*2:(1-t)*2;toolGroup.position.y=t<1?top+(low-top)*down:top;});
}
const frameCamera=()=>view?.frame({height:frame.value.height,size:Math.max(frame.value.a.length,frame.value.b.length,400),top:topView.value});
watch([chestId,face,target,other,marks,saw,plane,chisel,rebateSet,finish,plough,router,shoulder,bore,()=>props.design],()=>{buildScene();placeTool();},{deep:true});
watch([face,topView,()=>part.value?.id],frameCamera);
// By hand: a click brings that point under the cross (with the tape, it sets the
// tape's end); a drag slides the piece.
let press=null;
const topPoint=event=>view.topPoint(event,frame.value.height);
const onPiece=p=>{const f=frame.value,a=target.value.a+p.x,b=target.value.b+(f.bZero==='back'?p.z:-p.z);return a>=0&&a<=f.a.length&&b>=0&&b<=f.b.length?{a,b}:null;};
function pointerDown(event) {
  if(event.button!==0||working.value) return;
  const p=topPoint(event),hit=p&&onPiece(p);if(!hit) return;
  event.stopImmediatePropagation();view.capture(event.pointerId);
  press={x:event.clientX,y:event.clientY,point:p,start:{...target.value},hit,moved:false,id:event.pointerId};
}
function pointerMove(event) {
  if(!press||event.pointerId!==press.id) return;
  if(!press.moved&&Math.hypot(event.clientX-press.x,event.clientY-press.y)<=4) return;
  press.moved=true;const p=topPoint(event);if(!p) return;
  const f=frame.value,dx=p.x-press.point.x,dz=p.z-press.point.z;
  target.value=onTop({a:press.start.a-dx,b:press.start.b-(f.bZero==='back'?dz:-dz)});
}
function pointerUp(event) {
  if(!press||event.pointerId!==press.id) return;
  if(!press.moved) {if(tool.value==='tape') other.value=onTop(press.hit);else target.value=onTop(press.hit);}
  press=null;
}
function keydown(event) {
  const tag=event.target?.tagName;
  if(event.key==='Escape') {event.preventDefault();emit('close');return;}
  if(['INPUT','TEXTAREA','SELECT'].includes(tag)||event.ctrlKey||event.metaKey||event.altKey) return;
  const f=frame.value,arrows={ArrowLeft:['a',-1],ArrowRight:['a',1],ArrowUp:['b',f.bZero==='back'?-1:1],ArrowDown:['b',f.bZero==='back'?1:-1]}[event.key];
  if(arrows) {event.preventDefault();nudge(arrows[0],arrows[1]*(event.shiftKey?10:1));return;}
  if(event.key==='Enter'||event.key===' ') {if(tag==='BUTTON') return;event.preventDefault();apply();}
}
onMounted(async()=>{
  window.addEventListener('keydown',keydown);
  await nextTick();
  try {view=createBenchScene(host.value,{down:pointerDown,move:pointerMove,up:pointerUp});buildScene();placeTool();frameCamera();}
  catch {webgl.value=false;view=null;}
});
onActivated(()=>window.addEventListener('keydown',keydown));
onDeactivated(()=>window.removeEventListener('keydown',keydown));
onBeforeUnmount(()=>{window.removeEventListener('keydown',keydown);view?.dispose();});
const usable=t=>!!t.action&&(panel.value||!['chisel','rebate','plough','router','shoulder','bore'].includes(t.action))&&(t.preset.along!=='width'||panel.value);
</script>
<template>
  <section v-if="part" class="workbench hand-bench" aria-label="Workbench">
    <BenchDesk storage-key="workbench" :windows="WINDOWS" :status="readout">
      <template #background><div ref="host" class="bench-canvas"></div><p v-if="!webgl" class="bench-note">The bench view needs WebGL; every tool works from the windows.</p></template>
      <template #start>
        <button @click="emit('close')">Back to designer</button>
        <button :aria-pressed="!topView" @click="topView=false">Bench view</button>
        <button :aria-pressed="topView" @click="topView=true">Look down at the piece</button>
      </template>
      <template #chest>
        <nav class="tool-chest" aria-label="Hand tools">
          <details v-for="group in CHEST_GROUPS" :key="group" :open="current.group===group||group==='Measuring and marking'">
        <summary>{{ group }}</summary>
        <ul><li v-for="t in TOOL_CHEST.filter(x=>x.group===group)" :key="t.id">
          <button :aria-pressed="chestId===t.id" :class="{reference:!t.action,unusable:t.action&&!usable(t)}" :disabled="!!t.action&&!usable(t)" :title="t.use+(t.note?' · '+t.note:'')" @click="pick(t)">{{ t.name }}<small>{{ t.use }}</small></button>
        </li></ul>
      </details>
          <p class="hint">Greyed tools are for reference: the design cannot show what they do yet.</p>
        </nav>
      </template>
      <template #tool>
        <div class="bench-panel">
      <h2>{{ part.name }}</h2>
      <p class="hint">{{ panel ? `${part.length} × ${part.width} × ${part.thickness} mm` : `${PIECE_FORMS[part.shape].label} · ${part.length} mm long` }} · {{ MATERIALS[part.material].label }}</p>
      <div class="bench-choices"><button @click="emit('open','drill')" :disabled="!panel">Drilling bench…</button><button @click="emit('open','cut')">Cutting bench…</button></div>
      <fieldset v-if="panel"><legend>Face up</legend>
        <div class="bench-choices"><button v-for="k in ['A','B']" :key="k" :aria-pressed="face===k" @click="face=k">{{ k==='A'?'Face A up':'Face B up (turned over)' }}</button></div>
      </fieldset>
      <p class="tool-in-hand" data-testid="tool-in-hand"><strong>{{ current.name }}</strong> · {{ current.use }}<small v-if="current.note">{{ current.note }}</small></p>
      <fieldset><legend>Position (the cross)</legend>
        <div class="bench-row two">
          <label>{{ frame.a.label }} ({{ frame.a.key }}) mm<input type="number" step="0.5" :value="target.a" aria-label="Cross along" @change="setPoint('target','a',$event)"></label>
          <label>{{ frame.b.label }} ({{ frame.b.key }}) mm<input type="number" step="0.5" :value="target.b" aria-label="Cross across" @change="setPoint('target','b',$event)"></label>
        </div>
        <label>Hand step<select v-model.number="step" aria-label="Hand step"><option :value="0">exact</option><option v-for="s in [.5,1,5,10,32]" :key="s" :value="s">{{ s }} mm</option></select></label>
      </fieldset>
      <fieldset v-if="tool==='tape'"><legend>Measure</legend>
        <div class="bench-row two">
          <label>To {{ frame.a.key }} mm<input type="number" step="0.5" :value="other.a" aria-label="Tape end along" @change="setPoint('other','a',$event)"></label>
          <label>To {{ frame.b.key }} mm<input type="number" step="0.5" :value="other.b" aria-label="Tape end across" @change="setPoint('other','b',$event)"></label>
        </div>
        <p class="hint">From the cross to the tape's end; click the piece to move the end. The readout also gives the cross's distance from each side.</p>
      </fieldset>
      <fieldset v-if="tool==='mark'"><legend>Mark</legend>
        <div v-if="mark.mode==='square'" class="bench-choices"><button @click="markThrough('across')">Mark across</button><button @click="markThrough('along')">Mark along</button></div>
        <div v-else-if="mark.mode==='point'" class="bench-choices"><button @click="markThrough('across');markThrough('along')">Mark the point</button></div>
        <template v-else>
          <div class="bench-row two">
            <label>Direction<select :value="mark.direction" aria-label="Mark direction" @change="choose('mark','direction',$event.target.value)"><option value="along">Along the length</option><option value="across">Across the length</option></select></label>
            <label v-if="mark.mode!=='divide'">From<select :value="mark.from" aria-label="Measure from" @change="choose('mark','from',$event.target.value)"><option value="near">{{ mark.direction==='across'?'The start (u = 0)':`The ${bZeroLabel} edge` }}</option><option value="far">{{ mark.direction==='across'?'The end':'The other edge' }}</option></select></label>
            <label v-if="mark.mode!=='divide'">Distance mm<input type="number" min="0" step="0.5" :value="mark.distance" aria-label="Gauge distance" @change="setIn('mark','distance',$event,0,5000)"></label>
            <label v-if="mark.mode==='mortise'">Mortise width mm<input type="number" min="0.5" step="0.5" :value="mark.width" aria-label="Gauge width" @change="setIn('mark','width',$event,.5,500)"></label>
            <label v-if="mark.mode==='divide'">Equal parts<input type="number" min="2" max="50" step="1" :value="mark.parts" aria-label="Equal parts" @change="setIn('mark','parts',$event,2,50)"></label>
          </div>
          <button @click="markWith">{{ mark.mode==='divide'?'Step off':mark.mode==='mortise'?'Scribe both lines':'Scribe the line' }}</button>
        </template>
        <button :disabled="!marks.length" @click="marks=[]">Clear marks</button>
        <p class="hint">Marks stay while the bench is open; positions snap to marks within 5 mm.</p>
        <ul class="bench-marks"><li v-for="(m,i) in marks" :key="i">{{ m.direction==='across'?`Across at ${frame.a.key} ${m.at}`:`Along at ${frame.b.key} ${m.at}` }}</li></ul>
      </fieldset>
      <fieldset v-if="tool==='saw'"><legend>Saw</legend>
        <div v-if="panel" class="bench-choices"><button :aria-pressed="saw.along==='length'" @click="choose('saw','along','length')">To length</button><button :aria-pressed="saw.along==='width'" @click="choose('saw','along','width')">To width</button></div>
        <div class="bench-row two">
          <label>Size mm<input type="number" min="1" step="0.5" :value="saw.size" aria-label="Cut to size" @change="setIn('saw','size',$event,1,5000)"></label>
          <label>Kerf mm<input type="number" min="0" max="5" step="0.1" :value="saw.kerf" aria-label="Saw kerf" @change="setIn('saw','kerf',$event,0,5)"></label>
        </div>
        <label class="snap-toggle"><input :checked="saw.keep" type="checkbox" aria-label="Keep the offcut" @change="choose('saw','keep',$event.target.checked)">Keep the offcut as a piece</label>
      </fieldset>
      <fieldset v-if="tool==='plane'"><legend>Plane</legend>
        <label>Side<select :value="plane.side" aria-label="Side to plane" @change="choose('plane','side',$event.target.value)"><option v-for="s in planeSides(part)" :key="s" :value="s">{{ sideLabel(s) }}</option></select></label>
        <label>Take off mm<input type="number" min="0.05" max="100" step="0.05" :value="plane.amount" aria-label="Plane off" @change="setIn('plane','amount',$event,.01,1000)"></label>
      </fieldset>
      <fieldset v-if="tool==='chisel'"><legend>Chisel</legend>
        <label>Chisel width<select :value="chisel.width" aria-label="Chisel width" @change="choose('chisel','width',Number($event.target.value))"><option v-for="w in current.preset.widths" :key="w" :value="w">{{ w }} mm</option></select></label>
        <div class="bench-row two">
          <label>Length mm<input type="number" min="1" step="1" :value="chisel.length" aria-label="Mortise length" @change="setIn('chisel','length',$event,1,5000)"></label>
          <label>Depth mm<input type="number" min="0.5" step="0.5" :value="chisel.depth" aria-label="Mortise depth" @change="setIn('chisel','depth',$event,.01,frame.depthLimit)"></label>
        </div>
        <label>Along<select :value="chisel.along" aria-label="Mortise direction" @change="choose('chisel','along',$event.target.value)"><option value="a">{{ frame.a.key }}</option><option value="b">{{ frame.b.key }}</option></select></label>
        <p class="hint">The mortise starts at the cross (its corner); at an edge it is a notch.</p>
      </fieldset>
      <fieldset v-if="tool==='rebate'"><legend>Rebate</legend>
        <label>Edge<select :value="rebateSet.edge" aria-label="Rebate edge" @change="choose('rebate','edge',$event.target.value)"><option v-for="e in ['v0','v1','u0','u1']" :key="e" :value="e">{{ sideLabel(e) }}</option></select></label>
        <div class="bench-row two">
          <label>Width mm<input type="number" min="1" step="1" :value="rebateSet.width" aria-label="Rebate width" @change="setIn('rebate','width',$event,.5,5000)"></label>
          <label>Depth mm<input type="number" min="0.5" step="0.5" :value="rebateSet.depth" aria-label="Rebate depth" @change="setIn('rebate','depth',$event,.01,frame.depthLimit)"></label>
        </div>
      </fieldset>
      <fieldset v-if="tool==='plough'"><legend>Plough</legend>
        <label>Iron<select :value="plough.width" aria-label="Plough iron" @change="choose('plough','width',Number($event.target.value))"><option v-for="w in current.preset.widths" :key="w" :value="w">{{ w }} mm</option></select></label>
        <div class="bench-row two">
          <label>Fence mm<input type="number" min="0" step="0.5" :value="plough.distance" aria-label="Fence distance" @change="setIn('plough','distance',$event,0,5000)"></label>
          <label>Depth mm<input type="number" min="0.5" step="0.5" :value="plough.depth" aria-label="Groove depth" @change="setIn('plough','depth',$event,.01,frame.depthLimit)"></label>
        </div>
        <label>Fence against<select :value="plough.from" aria-label="Fence against" @change="choose('plough','from',$event.target.value)"><option value="near">The {{ bZeroLabel }} edge (v = 0)</option><option value="far">The other edge</option></select></label>
      </fieldset>
      <fieldset v-if="tool==='router'"><legend>Router plane</legend>
        <div class="bench-choices"><button :aria-pressed="router.mode==='deepen'" @click="choose('router','mode','deepen')">Deepen a groove or pocket</button><button :aria-pressed="router.mode==='housing'" @click="choose('router','mode','housing')">Housing across</button></div>
        <label v-if="router.mode==='deepen'">Recess<select :value="router.featureId" aria-label="Recess to deepen" @change="choose('router','featureId',$event.target.value)"><option v-for="h in recesses" :key="h.f.id" :value="h.f.id">{{ recessLabel(h) }}</option></select></label>
        <div class="bench-row two">
          <label v-if="router.mode==='housing'">Width mm<input type="number" min="1" step="0.5" :value="router.width" aria-label="Housing width" @change="setIn('router','width',$event,.5,5000)"></label>
          <label>Depth mm<input type="number" min="0.5" step="0.5" :value="router.depth" aria-label="Router depth" @change="setIn('router','depth',$event,.01,frame.depthLimit)"></label>
        </div>
        <p class="hint">{{ router.mode==='housing'?'A housing (dado) across the whole face, starting at the cross, sawn, chopped and routed to depth.':'Sets the depth of the chosen groove or pocket.' }}</p>
      </fieldset>
      <fieldset v-if="tool==='shoulder'"><legend>Shoulder plane</legend>
        <label>Recess<select :value="shoulder.featureId" aria-label="Recess to trim" @change="choose('shoulder','featureId',$event.target.value)"><option v-for="h in recesses" :key="h.f.id" :value="h.f.id">{{ recessLabel(h) }}</option></select></label>
        <div class="bench-row two">
          <label>Wall<select :value="shoulder.toward" aria-label="Wall to trim" @change="choose('shoulder','toward',$event.target.value)"><option value="low">Toward 0</option><option value="high">Away from 0</option></select></label>
          <label>Trim mm<input type="number" min="0.05" step="0.05" :value="shoulder.amount" aria-label="Trim amount" @change="setIn('shoulder','amount',$event,.01,100)"></label>
        </div>
      </fieldset>
      <fieldset v-if="tool==='bore'"><legend>Bore</legend>
        <label>Bit<select :value="bore.diameter" aria-label="Bit" @change="choose('bore','diameter',Number($event.target.value))"><option v-for="d in current.preset.bits" :key="d" :value="d">Ø{{ d }} mm</option></select></label>
        <label>Depth mm<input type="number" min="0.5" step="0.5" :value="bore.depth" aria-label="Hole depth" :disabled="bore.through" @change="setIn('bore','depth',$event,.01,frame.depthLimit)"></label>
        <label class="snap-toggle"><input :checked="bore.through" type="checkbox" aria-label="Bore through" @change="choose('bore','through',$event.target.checked)">Through ({{ frame.depthLimit }} mm)</label>
        <p class="hint">One hole at the cross. Rows of holes and edge holes are on the drilling bench.</p>
      </fieldset>
      <fieldset v-if="tool==='brush'"><legend>Finish</legend>
        <div class="bench-choices" role="radiogroup" aria-label="Finishes"><button v-for="f in [...FINISHES,{id:'custom',label:'Custom colour'}]" :key="f.id" role="radio" :aria-checked="finish.id===f.id" @click="choose('finish','id',f.id)">{{ f.label }}</button></div>
        <label v-if="finish.id==='custom'">Colour<input type="color" :value="finish.custom" aria-label="Custom finish colour" @change="choose('finish','custom',$event.target.value)"></label>
        <p class="hint">The finish is the piece's colour in the designer and the room; the material (and its weight and price) is unchanged.</p>
      </fieldset>
      <button v-if="actionLabel" class="primary bench-drill" :disabled="working||!!check" @click="apply">{{ actionLabel }}</button>
      <p v-if="check" class="bench-problem" data-testid="work-problem" role="alert">{{ check }}</p>
        </div>
      </template>
      <template #layers><BenchLayers :design="design" v-model:layer-id="layerId" @commit="emit('commit',$event)" @message="emit('message',$event)"/></template>
      <template #help><div class="bench-panel"><p class="hint">By hand: click the piece to bring that point under the cross (with the tape, to set its end), drag the piece to slide it, arrow keys nudge by the hand step (Shift: ×10); Enter uses the tool. Escape returns to the designer; Ctrl/⌘+Z undoes the last change.</p>
        <p class="hint">Windows: drag a title bar to move a window, or to an edge or corner of the desk to snap it; drag its edges to resize; the taskbar brings windows forward or minimises them.</p></div></template>
    </BenchDesk>
  </section>
</template>
