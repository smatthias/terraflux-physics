<script setup>
// Furniture designer perspective. It replaces the room view in the same window
// and edits the project's furniture designs. Designs are stored with the project;
// each design has its own undo history for this session.
import {ref,computed,watch,onMounted,onBeforeUnmount,onActivated,onDeactivated,defineAsyncComponent} from 'vue';
import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import * as D from '../shared/furniture-design-edit.mjs';
import {cabinetParts} from '../shared/cabinet-parts.mjs';
import {cncPlan,cncPlanJson} from '../shared/design-cnc.mjs';
import {camPlan,POSTS} from '../shared/design-cam.mjs';
// Program and DXF writers load on the first export.
const camExport=()=>import('../shared/cam-export.mjs');
import {createHistory,commitHistory,undoHistory,redoHistory} from '../shared/history.mjs';
import {buildDesignGroup,disposeDesignGroup,fitDesignCamera,pickDesign,DESIGN_VIEWS} from './design-scene.js';
import {buildRotateGizmo,dropPlane,dropCorner,nudgeAxis} from './design-gizmo.js';
import {createDesignInteraction} from './design-interaction.js';
import {localAxis} from '../shared/design-transform.mjs';
import {MATERIALS} from '../shared/materials.mjs';
import DesignPartEditor from './DesignPartEditor.vue';
import DesignLayers from './DesignLayers.vue';
import DesignCncPanel from './DesignCncPanel.vue';
import DesignPieceLibrary from './DesignPieceLibrary.vue';
import BenchDesk from './BenchDesk.vue';
// The drilling bench loads when first opened.
const DrillingBench=defineAsyncComponent(()=>import('./DrillingBench.vue'));
const CuttingBench=defineAsyncComponent(()=>import('./CuttingBench.vue'));
const Workbench=defineAsyncComponent(()=>import('./Workbench.vue'));

const props=defineProps({designs:{type:Array,required:true},applyDesigns:{type:Function,required:true},cabinet:{type:Object,default:null},
  placeDesign:{type:Function,default:null},placedDesignIds:{type:Array,default:()=>[]},focusDesignId:{type:String,default:null},
  workspaceKey:{type:String,default:''},projectBytes:{type:Number,default:0},maxBytes:{type:Number,default:65536},
  windowBar:{type:null,default:null}});
const emit=defineEmits(['message','active']);
const say=text=>emit('message',text);
// The designer's desk (BenchDesk.vue): the design view is the canvas; the panels
// are windows docked at its sides (or floating), with their buttons in the top
// bar while the designer is shown and no workshop bench is open.
const half=d=>Math.round((d.height-24)*.6),narrow=d=>d.width<760;
const WINDOWS=[
  {id:'designs',title:'Designs and parts',rect:d=>({x:8,y:8,w:290,h:half(d)}),minimized:narrow},
  {id:'layers',title:'Machining layers',rect:d=>({x:8,y:half(d)+16,w:290,h:d.height-24-half(d)}),minimized:narrow},
  {id:'properties',title:'Design properties',rect:d=>({x:d.width-348,y:8,w:340,h:half(d)}),minimized:narrow},
  {id:'cnc',title:'CNC and CAM',rect:d=>({x:d.width-348,y:half(d)+16,w:340,h:d.height-24-half(d)}),minimized:narrow},
];
const onScreen=ref(true);
const makeId=()=>crypto.randomUUID();

// ---- Designs and selection
// `partId` is the active piece (edited in the properties); `partIds` is the whole selection.
const activeId=ref(props.designs[0]?.id??null),selection=ref({partId:null,featureId:null,partIds:[]}),confirmDelete=ref(false),formKey=ref(0);
const design=computed(()=>props.designs.find(d=>d.id===activeId.value)??null);
const selectedPart=computed(()=>design.value?.parts.find(p=>p.id===selection.value.partId)??null);
const layerOf=id=>design.value.layers.find(l=>l.id===id);
const clearSelection=()=>{selection.value={partId:null,featureId:null,partIds:[]};};
const selectedIds=computed(()=>selection.value.partIds);
const selectedParts=computed(()=>design.value?.parts.filter(p=>selectedIds.value.includes(p.id))??[]);
watch(()=>props.designs.map(d=>d.id).join('|'),()=>{
  if(!props.designs.some(d=>d.id===activeId.value)) {activeId.value=props.designs[0]?.id??null;clearSelection();}
});
watch(activeId,()=>{clearSelection();confirmDelete.value=false;});
watch(activeId,id=>emit('active',id),{immediate:true});
// Undo or another edit can remove the selected part or feature.
watch(design,value=>{
  const present=new Set(value?.parts.map(p=>p.id)??[]),ids=selection.value.partIds.filter(id=>present.has(id));
  const partId=present.has(selection.value.partId)?selection.value.partId:ids.at(-1)??null,part=value?.parts.find(p=>p.id===partId);
  const featureId=part?.features?.some(f=>f.id===selection.value.featureId)?selection.value.featureId:null;
  if(ids.length!==selection.value.partIds.length||partId!==selection.value.partId||featureId!==selection.value.featureId) selection.value={partId,featureId,partIds:ids};
});
const selectPart=partId=>{selection.value={partId,featureId:null,partIds:[partId]};};
const selectParts=partIds=>{selection.value={partId:partIds.at(-1)??null,featureId:null,partIds:[...partIds]};};
const selectFeature=(partId,featureId)=>{selection.value={partId,featureId,partIds:[partId]};};
// Adds or removes a piece; the last piece added is active.
function togglePart(partId) {
  const ids=selectedIds.value;
  if(ids.includes(partId)) {const rest=ids.filter(id=>id!==partId);selection.value={partId:selection.value.partId===partId?rest.at(-1)??null:selection.value.partId,featureId:null,partIds:rest};}
  else selection.value={partId,featureId:null,partIds:[...ids,partId]};
}
const clickPart=(event,partId)=>event.shiftKey||event.ctrlKey||event.metaKey?togglePart(partId):selectPart(partId);

// ---- History: one stack per design, cleared when the workspace changes project.
const histories=ref(new Map());
watch(()=>props.workspaceKey,()=>{histories.value=new Map();});
const history=computed(()=>design.value?histories.value.get(design.value.id)??null:null);
function replaceDesigns(list,text) {
  const error=props.applyDesigns(list);
  if(error) {say(error);formKey.value++;return false;}
  say(text);return true;
}
function commit(next,text) {
  const current=design.value,previous=histories.value.get(current.id)??createHistory(current);
  if(!replaceDesigns(props.designs.map(d=>d.id===current.id?next:d),text)) return false;
  histories.value=new Map(histories.value).set(current.id,commitHistory(previous,next));
  return true;
}
// Runs a model edit; validation errors are reported and leave the design unchanged.
function run(edit,text) {
  if(!design.value) return null;
  try {const result=edit(design.value);return commit(result.design??result,text)?result:null;}
  catch(error) {say(error.message);formKey.value++;return null;}
}
function travel(direction) {
  const current=design.value,stack=current&&histories.value.get(current.id);if(!stack) return;
  const moved=direction==='undo'?undoHistory(stack):redoHistory(stack);if(moved===stack) return;
  if(replaceDesigns(props.designs.map(d=>d.id===current.id?moved.present:d),direction==='undo'?'Design edit undone':'Design edit redone'))
    histories.value=new Map(histories.value).set(current.id,moved);
}

// ---- Design list (project data, not part of a design's own history)
function addDesign(next,text) {if(replaceDesigns([...props.designs,next],text)) {activeId.value=next.id;fitSoon();}}
function newDesign(template) {
  try {addDesign(D.createDesign({id:makeId(),name:template==='box'?'Box carcass':'New design',template,makeId}),template==='box'?'Box carcass design created':'Design created');}
  catch(error) {say(error.message);}
}
function fromCabinet() {
  try {addDesign(D.designFromCabinet(props.cabinet,cabinetParts(props.cabinet),{id:makeId(),makeId}),'Design created from '+props.cabinet.name);}
  catch(error) {say(error.message);}
}
function duplicateActive() {try {addDesign(D.duplicateDesign(design.value,{id:makeId(),makeId}),'Design duplicated');} catch(error) {say(error.message);}}
// "Edit design" from the room opens that design here.
watch(()=>props.focusDesignId,id=>{if(id&&props.designs.some(d=>d.id===id)) activeId.value=id;},{immediate:true});
const placedCount=computed(()=>props.placedDesignIds.filter(id=>id===activeId.value).length);
function placeActive() {const error=props.placeDesign?.(activeId.value);if(error) say(error);}
function deleteActive() {
  const name=design.value.name;confirmDelete.value=false;
  if(placedCount.value) {say(`“${name}” is placed in the room ${placedCount.value===1?'once':placedCount.value+' times'}; remove it from the room before deleting the design`);return;}
  if(replaceDesigns(props.designs.filter(d=>d.id!==activeId.value),'Design “'+name+'” deleted')) activeId.value=props.designs[0]?.id??null;
}
function rename(event) {run(d=>D.renameDesign(d,event.target.value),'Design renamed');}

// ---- Part, feature and layer commands
// Adds a piece from the library; the new piece is selected for editing.
function addPiece({shape,material,color,length}) {
  const result=run(d=>D.addPart(d,shape,{makeId,overrides:{material,color,length,...(activeLayerId.value?{layer:activeLayerId.value}:{})}}),'Added '+D.PIECE_FORMS[shape].label.toLowerCase());
  if(result) selectPart(result.partId);
}
const convertPart=shape=>run(d=>D.convertPart(d,selection.value.partId,shape),'Changed to '+D.PIECE_FORMS[shape].label.toLowerCase());
const updatePart=changes=>run(d=>D.updatePart(d,selection.value.partId,changes),'Part updated');
const plural=(n,word)=>n+' '+word+(n===1?'':'s');
// A unique, descriptive name for a feature (identical holes differ by position).
const featureName=(part,f)=>`${part.name}: ${f.type} on ${f.face.length>1?'edge':'face'} ${f.face} at u ${f.u}, v ${f.v}${f.t!==undefined?', t '+f.t:''}, ${f.depth} mm deep`;
function duplicateSelectedPart() {const ids=selectedIds.value,result=run(d=>D.duplicateParts(d,ids,makeId),ids.length>1?'Duplicated '+plural(ids.length,'piece'):'Part duplicated');if(result) selectParts(result.partIds);}
function removeSelected() {
  const {partId,featureId}=selection.value,ids=selectedIds.value;
  if(featureId) {if(run(d=>D.removeFeature(d,featureId),'Feature deleted')) selectPart(partId);}
  else if(ids.length && run(d=>D.removeParts(d,ids),ids.length>1?'Deleted '+plural(ids.length,'piece'):'Part deleted')) clearSelection();
}
function addFeature(type) {const partId=selection.value.partId,result=run(d=>D.addFeature(d,partId,type,makeId),'Added '+type);if(result) selectFeature(partId,result.featureId);}
const updateFeature=(featureId,changes)=>run(d=>D.updateFeature(d,featureId,changes),'Feature updated');
const usage=computed(()=>{
  const counts={};
  for(const part of design.value?.parts??[]) {counts[part.layer]=(counts[part.layer]??0)+1;for(const f of part.features??[]) counts[f.layer]=(counts[f.layer]??0)+1;}
  return counts;
});
const layerCommands={
  add:()=>run(d=>D.addLayer(d,{makeId}),'Layer added'),
  update:(id,changes)=>run(d=>D.updateLayer(d,id,changes),'Layer updated'),
  remove:id=>run(d=>D.removeLayer(d,id),'Layer deleted'),
  move:(id,delta)=>run(d=>D.moveLayer(d,id,delta),'Machining order changed'),
};

// ---- CNC plan
const plan=computed(()=>{try {return design.value?cncPlan(design.value):null;} catch(error) {return {error:error.message};}});
const camResult=computed(()=>{try {return design.value?camPlan(design.value):null;} catch(error) {return null;}});
const fileBase=text=>text.replace(/[^a-zA-Z0-9_-]+/g,'_').slice(0,60)||'design';
async function download(make,type,suffix,done) {
  const source=design.value;
  try {
    const url=URL.createObjectURL(new Blob([await make(source)],{type})),link=document.createElement('a');
    link.href=url;link.download=fileBase(source.name)+suffix;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);say(done);
  } catch(error) {say(error.message);}
}
const post=()=>POSTS[design.value.cam?.post??'grbl'];
const exportPlan=()=>download(d=>cncPlanJson(d),'application/json','_cnc-plan.json','CNC plan exported');
const exportDxf=()=>download(async d=>(await camExport()).designDxf(d),'application/dxf','.dxf','DXF exported');
const exportAllGcode=()=>download(async d=>(await camExport()).designProgram(d),'text/plain','_all-panels'+post().extension,`NC program exported for all panels · ${post().label}`);
function exportGcode(partId,face) {
  const part=design.value.parts.find(p=>p.id===partId);
  download(async d=>(await camExport()).partProgram(d,partId,face),'text/plain','_'+fileBase(part.name)+'_face-'+face+post().extension,`NC program exported for ${part.name} face ${face} · ${post().label}`);
}
const updateCam=changes=>run(d=>D.updateCam(d,changes),'CAM settings updated');
const updateTool=(layerId,changes)=>run(d=>D.updateLayerTool(d,layerId,changes),'Tool updated');
const updateStock=changes=>run(d=>D.updateStock(d,changes),'Sheet stock updated');
function selectIssue(issue) {if(issue.featureId) selectFeature(issue.partId,issue.featureId);else if(issue.partId) selectPart(issue.partId);}
const summary=computed(()=>{
  if(!design.value) return '';
  const size=D.designBounds(design.value).size,features=design.value.parts.reduce((n,p)=>n+(p.features?.length??0),0);
  return `${Math.round(size.x)} × ${Math.round(size.y)} × ${Math.round(size.z)} mm · ${design.value.parts.length} parts · ${features} machining features`;
});

// ---- In-canvas tools. While dragging, `preview` holds the edited design; it is
// committed once on release, so a whole drag is one undo step.
const tool=ref('move'),gridStep=ref(10),angleStep=ref(15),colorByLayer=ref(false),focusLayerId=ref(null),activeLayerId=ref(null),axisMode=ref('world');
const preview=ref(null),dragInfo=ref('');
const shown=computed(()=>preview.value??design.value);
const shownSelection=computed(()=>shown.value?.parts.filter(p=>selectedIds.value.includes(p.id))??[]);
const shownActive=computed(()=>shown.value?.parts.find(p=>p.id===selection.value.partId)??null);
watch(()=>design.value?.layers.map(l=>l.id).join('|'),()=>{
  const ids=new Set(design.value?.layers.map(l=>l.id)??[]);
  if(!ids.has(activeLayerId.value)) activeLayerId.value=null;
  if(!ids.has(focusLayerId.value)) focusLayerId.value=null;
});
const MIME='application/x-terratec-design';

// ---- 3D view
const view=ref('3D'),canvasHost=ref(null),webgl=ref(true);
let renderer,scene,camera,controls,group,gizmo,grid,observer,frame=0,active=true;
const EMPTY={parts:[],layers:[]};
function fit() {
  if(!camera||!canvasHost.value) return;
  const aspect=Math.max(canvasHost.value.clientWidth,1)/Math.max(canvasHost.value.clientHeight,1);
  controls.enableDamping=false;controls.update();
  controls.target.copy(fitDesignCamera(camera,design.value??EMPTY,aspect,view.value));controls.update();controls.enableDamping=true;
}
const fitSoon=()=>requestAnimationFrame(fit);
function setupCamera() {
  if(!renderer) return;
  cancelDrag();controls?.dispose();
  camera=view.value==='3D'?new THREE.PerspectiveCamera(40,1,1,1e6):new THREE.OrthographicCamera();
  controls=new OrbitControls(camera,renderer.domElement);controls.enableDamping=true;controls.enableRotate=view.value==='3D';controls.screenSpacePanning=true;
  if(grid) grid.visible=view.value==='3D'||view.value==='Top';
  fit();rebuild();
}
function rebuild() {
  if(!scene) return;
  disposeDesignGroup(group);disposeDesignGroup(gizmo);group=gizmo=null;
  if(!shown.value) return;
  group=buildDesignGroup(shown.value,{selectedPartIds:selectedIds.value,selectedFeatureId:selection.value.featureId,colorByLayer:colorByLayer.value,focusLayerId:focusLayerId.value});
  scene.add(group);
  if(tool.value==='rotate'&&shownSelection.value.length) {gizmo=buildRotateGizmo(shownSelection.value,view.value,axisMode.value==='local'?shownActive.value?.rotation:undefined);scene.add(gizmo);}
}
watch([shown,selection,colorByLayer,focusLayerId,tool,axisMode],rebuild,{deep:true});
watch(view,setupCamera);
watch(activeId,fitSoon);
function resize() {
  const host=canvasHost.value;if(!renderer||!host?.clientWidth||!host.clientHeight) return;
  renderer.setSize(host.clientWidth,host.clientHeight);fit();
}
function ndc(event) {const rect=renderer.domElement.getBoundingClientRect();return {x:(event.clientX-rect.left)/rect.width*2-1,y:-(event.clientY-rect.top)/rect.height*2+1};}
function planeHit(event,plane) {
  const p=ndc(event),ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2(p.x,p.y),camera);
  return ray.ray.intersectPlane(plane,new THREE.Vector3());
}
const pickAt=event=>{const p=ndc(event);return group?pickDesign(group,camera,p.x,p.y):null;};
// Canvas pointer handling (see design-interaction.js) runs in the capture phase,
// so a drag on a piece never orbits the camera.
let interaction=null;
function createInteraction() {
  interaction=createDesignInteraction({element:renderer.domElement,camera:()=>camera,group:()=>group,gizmo:()=>gizmo,view:()=>view.value,tool:()=>tool.value,
    design:()=>design.value,selectedIds:()=>selectedIds.value,axisMode:()=>axisMode.value,gridStep:()=>gridStep.value,angleStep:()=>angleStep.value,
    onPreview:(next,info)=>{preview.value=next;dragInfo.value=info;},onCommit:(next,text)=>commit(next,text),
    onSelect:(hit,{toggle}={})=>{if(toggle) togglePart(hit.partId);else if(hit?.featureId) selectFeature(hit.partId,hit.featureId);else if(hit) selectPart(hit.partId);else clearSelection();}});
}
// A right-button press that moves pans the view; one that does not opens the menu.
const pointerDown=event=>{if(event.button===2) {rightPress={x:event.clientX,y:event.clientY,moved:false};return;}menu.value=null;interaction?.pointerDown(event);};
const pointerMove=event=>{if(rightPress&&Math.hypot(event.clientX-rightPress.x,event.clientY-rightPress.y)>5) {rightPress.moved=true;menu.value=null;}interaction?.pointerMove(event);};
const pointerUp=event=>interaction?.pointerUp(event);
function cancelDrag() {if(interaction?.cancel()) say('Drag cancelled');}

// ---- Drag and drop from the piece library and the layer panel.
function canvasDragOver(event) {if([...(event.dataTransfer?.types??[])].includes(MIME)) {event.preventDefault();event.dataTransfer.dropEffect='copy';}}
function canvasDrop(event) {
  let data;try {data=JSON.parse(event.dataTransfer.getData(MIME));} catch {return;}
  event.preventDefault();
  if(!design.value) {say('Create a design first');return;}
  if(data.kind==='form') {dropPiece(data,event);return;}
  const hit=camera?pickAt(event):null;
  if(!hit) {say('Drop materials and layers onto a piece');return;}
  // Dropping onto a selected piece applies to the whole selection.
  const ids=selectedIds.value.includes(hit.partId)?selectedIds.value:[hit.partId],count=ids.length>1?' · '+plural(ids.length,'piece'):'';
  if(!selectedIds.value.includes(hit.partId)) selectPart(hit.partId);
  if(data.kind==='material') run(d=>D.updateParts(d,ids,{material:data.material}),'Applied '+materialLabel(data.material)+count);
  else if(data.kind==='layer') run(d=>D.updateParts(d,ids,{layer:data.layerId}),'Moved to layer '+(layerOf(data.layerId)?.name??'')+count);
}
const materialLabel=id=>MATERIALS[id]?.label??id;
function dropPiece(data,event) {
  const result=run(d=>{
    const added=D.addPart(d,data.shape,{makeId,overrides:{material:data.material,color:data.color,length:data.length,...(activeLayerId.value?{layer:activeLayerId.value}:{})}});
    const point=camera?planeHit(event,dropPlane(view.value)):null;
    if(!point) return added;
    const part=added.design.parts.find(p=>p.id===added.partId);
    return {design:D.updatePart(added.design,added.partId,dropCorner(view.value,point,D.partSize(part),gridStep.value)),partId:added.partId};
  },'Placed '+D.PIECE_FORMS[data.shape].label.toLowerCase());
  if(result) selectPart(result.partId);
}

// ---- Selection toolbar commands
// Quick turns use world axes, or the active piece's own axes in local mode.
function rotateSelected(axis,degrees) {
  const local=axisMode.value==='local',about=local?localAxis(selectedPart.value?.rotation,axis):axis;
  run(d=>D.rotatePartsAbout(d,selectedIds.value,about,degrees),`Rotated about ${local?'local ':''}${axis} ${degrees>0?'+':''}${degrees}°${selectedIds.value.length>1?' · '+plural(selectedIds.value.length,'piece'):''}`);
}
const resetRotation=()=>run(d=>D.updateParts(d,selectedIds.value,{rotation:undefined}),'Rotation reset');
function nudge(axis,sign) {
  const step=gridStep.value||1;
  run(d=>D.moveParts(d,selectedIds.value,{[axis]:sign*step}),`Nudged ${axis} ${sign>0?'+':'−'}${step} mm`);
}
const setSelectionLayer=layerId=>run(d=>D.updateParts(d,selectedIds.value,{layer:layerId}),'Moved to layer '+layerOf(layerId).name);

// ---- Context menu (right-click a piece, or the Menu key / Shift+F10 for the active piece)
const menu=ref(null),menuEl=ref(null);
const menuPart=computed(()=>menu.value&&design.value?.parts.find(p=>p.id===menu.value.partId)||null);
let rightPress=null;
function openMenu(partId,x,y) {
  if(!selectedIds.value.includes(partId)) selectPart(partId);else selection.value={...selection.value,partId,featureId:null};
  menu.value={partId,x,y};requestAnimationFrame(()=>menuEl.value?.querySelector?.('button:not(:disabled)')?.focus());
}
function contextMenu(event) {
  event.preventDefault();
  const panned=rightPress?.moved;rightPress=null;
  if(panned) return;
  const hit=camera?pickAt(event):null;
  if(!hit) {menu.value=null;return;}
  const rect=canvasHost.value.getBoundingClientRect();openMenu(hit.partId,event.clientX-rect.left,event.clientY-rect.top);
}
// Menu commands act on the piece the menu was opened for (now the active piece).
function menuCommand(action) {const partId=menu.value?.partId;menu.value=null;action(partId);}
const drillable=part=>part?.shape==='panel';

// ---- Drilling bench (one panel at a time; the design view pauses meanwhile)
// bench: {kind: 'work' | 'drill' | 'cut', partId}. Drilling takes panels; the others any piece.
const bench=ref(null),benchPartId=computed(()=>bench.value?.partId??null);
function openBench(partId=selection.value.partId,kind='drill') {
  const part=design.value?.parts.find(p=>p.id===partId);menu.value=null;
  if(!part) return;
  if(kind==='drill'&&!drillable(part)) {say(`${part.name} is a ${D.PIECE_FORMS[part.shape].label.toLowerCase()}; the drilling bench takes panels`);return;}
  cancelDrag();selectPart(partId);bench.value={kind,partId};active=false;say({drill:'Drilling bench · ',cut:'Cutting bench · ',work:'Workbench · '}[kind]+part.name);
}
const openCutting=(partId=selection.value.partId)=>openBench(partId,'cut');
const openWorkbench=(partId=selection.value.partId)=>openBench(partId,'work');
function closeBench() {if(!bench.value) return;bench.value=null;active=true;say('Furniture designer');requestAnimationFrame(resize);}
// The benches hand over the edited design; it is committed as one undo step.
const benchCommit=({design:next,message})=>{if(run(()=>next,message)) selectPart(benchPartId.value);};
const benchRemove=featureId=>run(d=>D.removeFeature(d,featureId),bench.value?.kind==='drill'?'Hole removed':'Cut removed');
watch(activeId,closeBench);

onMounted(()=>{
  try {
    renderer=new THREE.WebGLRenderer({antialias:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));
    canvasHost.value.appendChild(renderer.domElement);
    scene=new THREE.Scene();scene.background=new THREE.Color('#1b2f31');
    scene.add(new THREE.HemisphereLight(0xffffff,0x3a4a44,2.6));
    const sun=new THREE.DirectionalLight(0xfff3dd,2.2);sun.position.set(1500,2500,2000);scene.add(sun);
    grid=new THREE.GridHelper(4000,40,0x4d6a60,0x2f4a44);scene.add(grid);
    setupCamera();createInteraction();
    renderer.domElement.addEventListener('pointerdown',pointerDown,true);
    renderer.domElement.addEventListener('pointermove',pointerMove);
    renderer.domElement.addEventListener('pointerup',pointerUp);
    renderer.domElement.addEventListener('pointercancel',cancelDrag);
    renderer.domElement.addEventListener('contextmenu',contextMenu);
    observer=new ResizeObserver(resize);observer.observe(canvasHost.value);
    const animate=()=>{frame=requestAnimationFrame(animate);if(!active) return;controls.update();renderer.render(scene,camera);};animate();
  } catch {webgl.value=false;say('The designer preview needs WebGL · designing still works');}
});

// ---- Keyboard (only while this perspective is shown)
function keydown(event) {
  const tag=event.target?.tagName;
  if(['INPUT','TEXTAREA','SELECT'].includes(tag)||event.target?.isContentEditable) return;
  const key=event.key.toLowerCase(),mod=event.ctrlKey||event.metaKey;
  const handled=()=>{event.preventDefault();return true;};
  if(mod&&key==='z') return handled()&&travel(event.shiftKey?'redo':'undo');
  if(mod&&key==='y') return handled()&&travel('redo');
  // The bench handles its own keys.
  if(benchPartId.value) return;
  if(menu.value&&key==='escape') {menu.value=null;return handled();}
  if((key==='contextmenu'||event.shiftKey&&key==='f10')&&selection.value.partId) {const host=canvasHost.value;return handled()&&openMenu(selection.value.partId,(host?.clientWidth??400)/2,Math.max(20,(host?.clientHeight??400)-200));}
  if(mod&&key==='a'&&design.value?.parts.length) return handled()&&selectParts(design.value.parts.map(p=>p.id));
  if(mod&&key==='d'&&selection.value.partId) return handled()&&duplicateSelectedPart();
  if(!mod&&(key==='delete'||key==='backspace')&&selection.value.partId) return handled()&&removeSelected();
  if(key==='escape') {if(interaction?.dragging()) cancelDrag();else clearSelection();return;}
  if(mod||event.altKey) return;
  if(key==='m') return handled()&&(tool.value='move');
  if(key==='r') return handled()&&(tool.value='rotate');
  if(key==='l') return handled()&&(axisMode.value=axisMode.value==='local'?'world':'local');
  const step=selection.value.partId&&nudgeAxis(view.value,event.key,event.shiftKey);
  if(step) return handled()&&nudge(step.axis,step.sign);
}
onActivated(()=>{onScreen.value=true;active=!benchPartId.value;window.addEventListener('keydown',keydown);resize();});
onDeactivated(()=>{onScreen.value=false;active=false;cancelDrag();window.removeEventListener('keydown',keydown);});
onBeforeUnmount(()=>{
  window.removeEventListener('keydown',keydown);cancelAnimationFrame(frame);observer?.disconnect();controls?.dispose();
  disposeDesignGroup(group);grid?.geometry.dispose();grid?.material.dispose();
  renderer?.domElement.removeEventListener('pointerdown',pointerDown,true);renderer?.domElement.removeEventListener('pointermove',pointerMove);renderer?.domElement.removeEventListener('pointerup',pointerUp);renderer?.domElement.removeEventListener('pointercancel',cancelDrag);renderer?.domElement.removeEventListener('contextmenu',contextMenu);disposeDesignGroup(gizmo);renderer?.dispose();
});
</script>
<template>
  <div class="designer">
    <BenchDesk storage-key="designer" :windows="WINDOWS" dock :taskbar-target="windowBar" :taskbar-active="onScreen&&!bench">
      <template #background="{insets}">
        <section class="designer-stage" aria-label="Design view" :style="{left:insets.left+'px',right:insets.right+'px'}">
        <nav class="designer-views" aria-label="Designer view">
          <button v-for="mode in DESIGN_VIEWS" :key="mode" :aria-pressed="view===mode" @click="view=mode">{{ mode }}</button>
          <button @click="fit">Fit design</button>
          <button :disabled="!history?.past.length" @click="travel('undo')">Undo design</button>
          <button :disabled="!history?.future.length" @click="travel('redo')">Redo design</button>
          <span class="tool-sep"></span>
          <button :aria-pressed="tool==='move'" title="Drag pieces to move them (M). Shift drags vertically in 3D; arrow keys nudge." @click="tool='move'">Move</button>
          <button :aria-pressed="tool==='rotate'" title="Drag a ring to rotate the selected piece (R)" @click="tool='rotate'">Rotate</button>
          <label class="tool-select">Grid<select v-model.number="gridStep" aria-label="Move snap"><option :value="0">off</option><option v-for="step in [1,5,10,25,50]" :key="step" :value="step">{{ step }} mm</option></select></label>
          <label class="tool-select">Angle<select v-model.number="angleStep" aria-label="Angle snap"><option :value="0">off</option><option v-for="step in [5,15,45,90]" :key="step" :value="step">{{ step }}°</option></select></label>
          <button :aria-pressed="axisMode==='local'" title="Rings and quick turns use the active piece's own axes (L)" @click="axisMode=axisMode==='local'?'world':'local'">Local axes</button>
          <button :aria-pressed="colorByLayer" title="Shade pieces in their layer colour" @click="colorByLayer=!colorByLayer">Colour by layer</button>
        </nav>
        <div ref="canvasHost" class="designer-canvas" @dragover="canvasDragOver" @drop="canvasDrop"></div>
        <p v-if="dragInfo" class="drag-info" role="status">{{ dragInfo }}</p>
        <div v-if="selectedPart" class="selection-hud" role="toolbar" aria-label="Selected piece">
          <span v-if="selectedIds.length>1" class="hud-name" data-testid="selection-count">{{ selectedIds.length }} pieces <small>active: {{ selectedPart.name }}</small></span>
          <span v-else class="hud-name"><span class="swatch" :style="{background:selectedPart.color}"></span>{{ selectedPart.name }} <small>{{ D.PIECE_FORMS[selectedPart.shape].short }}</small></span>
          <select :value="selectedParts.every(p=>p.layer===selectedPart.layer)?selectedPart.layer:''" aria-label="Piece layer" @change="setSelectionLayer($event.target.value)">
            <option v-if="!selectedParts.every(p=>p.layer===selectedPart.layer)" value="" disabled>Mixed layers</option>
            <option v-for="layer in design.layers" :key="layer.id" :value="layer.id">{{ layer.name }}{{ layer.locked?' · locked':'' }}</option>
          </select>
          <button v-for="[axis,degrees] in [['x',90],['y',-15],['y',15],['y',90],['z',90]]" :key="axis+degrees" :aria-label="'Rotate about '+axis+' '+(degrees>0?'+':'')+degrees+'°'" @click="rotateSelected(axis,degrees)">{{ axis.toUpperCase() }} {{ degrees>0?'+':'' }}{{ degrees }}°</button>
          <button :disabled="!selectedParts.some(p=>p.rotation)" @click="resetRotation">Reset rotation</button>
          <button :disabled="!drillable(selectedPart)" :title="drillable(selectedPart)?'Open the drilling bench for '+selectedPart.name:'The drilling bench takes panels'" @click="openBench()">Drill…</button>
          <button :title="'Open the cutting bench (saw and router) for '+selectedPart.name" @click="openCutting()">Cut…</button>
          <button :title="'Open the workbench (measure, mark, saw, plane, chisel, finish) for '+selectedPart.name" @click="openWorkbench()">Workbench…</button>
          <button title="Ctrl/⌘+D" @click="duplicateSelectedPart">Duplicate selection</button><button title="Delete" @click="removeSelected">Delete selection</button>
        </div>
        <div v-if="menu" class="menu-backdrop" @pointerdown="menu=null" @contextmenu.prevent="menu=null"></div>
        <div v-if="menu&&menuPart" ref="menuEl" class="piece-menu" role="menu" :aria-label="'Actions for '+menuPart.name" :style="{left:menu.x+'px',top:menu.y+'px'}">
          <p class="menu-title">{{ selectedIds.length>1?selectedIds.length+' pieces':menuPart.name }}</p>
          <button role="menuitem" :disabled="!drillable(menuPart)" :title="drillable(menuPart)?'':'The drilling bench takes panels'" @click="menuCommand(openBench)">Drill on the bench…</button>
          <button role="menuitem" @click="menuCommand(openWorkbench)">Open the workbench…</button>
          <button role="menuitem" @click="menuCommand(openCutting)">Cut on the bench…</button>
          <button role="menuitem" :disabled="!drillable(menuPart)" @click="menuCommand(()=>addFeature('drill'))">Add a drill hole</button>
          <button role="menuitem" @click="menuCommand(duplicateSelectedPart)">{{ selectedIds.length>1?'Duplicate '+selectedIds.length+' pieces':'Duplicate piece' }}</button>
          <button role="menuitem" :disabled="!selectedParts.some(p=>p.rotation)" @click="menuCommand(resetRotation)">Straighten (reset rotation)</button>
          <button role="menuitem" class="danger-text" @click="menuCommand(removeSelected)">{{ selectedIds.length>1?'Delete '+selectedIds.length+' pieces':'Delete piece' }}</button>
        </div>
        <p v-if="!design" class="designer-empty">Start a design: an empty design, a box carcass, or one converted from the cabinet selected in the room.</p>
        <p v-else class="designer-summary" data-testid="design-summary">{{ summary }}</p>
        <p v-if="!webgl" class="designer-empty">3D preview needs WebGL; all editing panels still work.</p>
        </section>
      </template>
      <template #designs>
        <p class="eyebrow">FURNITURE DESIGNER</p>
        <label v-if="designs.length">Furniture design<select :value="activeId" aria-label="Furniture design" @change="activeId=$event.target.value"><option v-for="d in designs" :key="d.id" :value="d.id">{{ d.name }}</option></select></label>
        <div class="design-row"><button @click="newDesign('empty')">New empty design</button><button @click="newDesign('box')">New box carcass</button></div>
        <button :disabled="!cabinet" :title="cabinet?'Convert '+cabinet.name:'Select a cabinet in the room first'" @click="fromCabinet">Design from selected cabinet</button>
        <template v-if="design">
          <label>Design name<input :key="formKey+design.id" :value="design.name" maxlength="60" @change="rename"></label>
          <button v-if="placeDesign" class="place-design" :disabled="!design.parts.length" title="Add this design to the room as furniture at the nearest free spot" @click="placeActive">Place in room{{ placedCount?' ('+placedCount+' placed)':'' }}</button>
          <div class="design-row"><button @click="duplicateActive">Duplicate design</button>
            <button v-if="!confirmDelete" @click="confirmDelete=true">Delete design</button><button v-else class="danger" @click="deleteActive">Confirm delete design</button></div>
          <DesignPieceLibrary @add="addPiece"/>
          <p class="eyebrow">PARTS · {{ design.parts.length }}</p>
          <ul class="part-tree" aria-label="Parts">
            <li v-for="part in design.parts" :key="part.id" :class="{hidden:!layerOf(part.layer).visible}">
              <button :aria-pressed="selectedIds.includes(part.id)&&!selection.featureId" :class="{active:selection.partId===part.id}" :title="'Layer '+layerOf(part.layer).name+' · Shift/Ctrl-click to add to the selection'" @click="clickPart($event,part.id)"><span class="swatch" :style="{background:part.color}"></span>{{ part.name }}<small>{{ D.PIECE_FORMS[part.shape].short }}</small></button>
              <ul v-if="part.features?.length"><li v-for="f in part.features" :key="f.id"><button :aria-pressed="selection.featureId===f.id" :aria-label="featureName(part,f)" :title="featureName(part,f)" @click="selectFeature(part.id,f.id)"><span class="swatch" :style="{background:layerOf(f.layer).color}"></span>{{ f.type }} · {{ f.face }} · {{ f.depth }} mm</button></li></ul>
            </li>
          </ul>
        </template>
        <p class="hint" data-testid="project-size">Project size {{ (projectBytes/1024).toFixed(1) }} of {{ maxBytes/1024 }} KiB. Designs are saved with the project.</p>
      </template>
      <template #layers>
        <DesignLayers v-if="design" :key="'layers'+formKey" :layers="design.layers" :usage="usage" :active-layer-id="activeLayerId" :focus-layer-id="focusLayerId"
          @add="layerCommands.add" @update="layerCommands.update" @remove="layerCommands.remove" @move="layerCommands.move" @activate="activeLayerId=$event" @focus="focusLayerId=$event"/>
        <p v-if="!design" class="hint">Layers belong to a design: they set the machining order and what is shown.</p>
      </template>
      <template #properties>
        <p v-if="selectedIds.length>1" class="hint">{{ selectedIds.length }} pieces selected. These properties edit the active piece, {{ selectedPart?.name }}; moving, turning, layers, materials, duplicating and deleting in the view apply to all of them.</p>
        <DesignPartEditor v-if="selectedPart" :key="formKey" :part="selectedPart" :layers="design.layers" :feature-id="selection.featureId"
          @update="updatePart" @convert="convertPart" @remove="removeSelected" @duplicate="duplicateSelectedPart" @add-feature="addFeature"
          @select-feature="id=>selectFeature(selectedPart.id,id)" @update-feature="updateFeature" @remove-feature="removeSelected"/>
        <p v-else-if="design" class="hint">Select a part in the view or the parts list to edit its shape, material, layer and machining.</p>
      </template>
      <template #cnc>
        <DesignCncPanel v-if="plan" :key="'cnc'+formKey" :plan="plan" :cam="camResult" :design="design" @export="exportPlan" @select="selectIssue" @dxf="exportDxf" @gcode="exportGcode" @gcode-all="exportAllGcode" @update-cam="updateCam" @update-tool="updateTool" @update-stock="updateStock"/>
        <p v-if="!plan" class="hint">The CNC plan and CAM programs of the design appear here.</p>
      </template>
    </BenchDesk>
    <DrillingBench v-if="bench?.kind==='drill'&&design" :design="design" :part-id="bench.partId" @commit="benchCommit" @remove="benchRemove" @close="closeBench" @message="say"/>
    <Workbench v-if="bench?.kind==='work'&&design" :design="design" :part-id="bench.partId" @commit="benchCommit" @open="kind=>openBench(bench.partId,kind)" @close="closeBench" @message="say"/>
    <CuttingBench v-if="bench?.kind==='cut'&&design" :design="design" :part-id="bench.partId" @commit="benchCommit" @remove="benchRemove" @close="closeBench" @message="say"/>
  </div>
</template>
<style>
/* Not scoped: these rules style the child panels too, namespaced under .designer. */
.designer{position:relative;grid-column:1/-1;grid-row:2;min-width:0;min-height:0}
.designer .designer-stage{position:absolute;top:0;bottom:0;min-width:0;background:#1b2f31}
.designer .designer-canvas{position:absolute;inset:0}
.designer .designer-views{position:absolute;top:14px;left:14px;z-index:2;display:flex;flex-wrap:wrap;gap:2px;padding:4px;background:#f2f3eb;border-radius:8px}
.designer .designer-views button{padding:6px 11px;border:0}.designer .designer-views button[aria-pressed=true]{background:#263e3c;color:#f2f3eb}
.designer .tool-sep{width:1px;margin:2px 4px;background:#c9d3c8}
.designer .tool-select{display:flex;align-items:center;gap:4px;margin:0 4px;font-size:10px;text-transform:none}.designer .tool-select select{margin:0;padding:3px;font-size:11px;width:auto}
.designer .layers-head{display:flex;justify-content:space-between;align-items:center;gap:6px}.designer .layers-head .eyebrow{margin:0}.designer .layers-head button{font-size:10px;padding:3px 6px}
.designer .layer-row{grid-template-columns:26px 26px 1fr auto}.designer .layer-flags{grid-column:1/5;display:flex;gap:8px;align-items:center}.designer .layer-flags button{font-size:10px;padding:2px 6px;margin-left:auto}
.designer .layer-flags button[aria-pressed=true]{background:#263e3c;color:#f2f3eb}.designer .layer-row.focused{background:#e9eddd}
.designer .layer-order{cursor:grab;white-space:nowrap}
.designer .selection-hud{position:absolute;left:50%;bottom:44px;transform:translateX(-50%);z-index:3;display:flex;flex-wrap:wrap;gap:4px;align-items:center;max-width:calc(100% - 40px);padding:6px 8px;background:#f2f3ebf2;border:1px solid #b9c6b9;border-radius:8px;box-shadow:0 6px 24px #001c2433}
.designer .selection-hud button{font-size:11px;padding:4px 7px}.designer .selection-hud select{margin:0;padding:3px;width:auto;font-size:11px}
.designer .hud-name{display:flex;align-items:center;gap:5px;font-size:12px;font-weight:700;margin-right:4px}.designer .hud-name small{font-weight:400;color:#60746a}
.designer .drag-info{position:absolute;top:64px;left:50%;transform:translateX(-50%);z-index:3;margin:0;padding:5px 10px;border-radius:6px;background:#263e3ce6;color:#f2f3eb;font-size:12px}
.designer .designer-summary,.designer .designer-empty{position:absolute;left:16px;bottom:10px;z-index:2;margin:0;padding:6px 10px;border-radius:6px;background:#f2f3ebe6;font-size:11px}
.designer .designer-empty{bottom:auto;top:70px;max-width:420px}
.designer .design-row{display:flex;flex-wrap:wrap;gap:4px;margin:6px 0}.designer .design-row button{font-size:11px;padding:6px 8px}
.designer .part-tree,.designer .feature-list,.designer .design-layers ol,.designer .cnc-issues{list-style:none;margin:4px 0;padding:0}
.designer .part-tree ul{list-style:none;padding-left:16px;margin:0}
.designer .part-tree button,.designer .feature-list button{width:100%;text-align:left;border:0;padding:4px 6px;font-size:12px;display:flex;gap:6px;align-items:center}
.designer .part-tree button small{margin-left:auto;color:#60746a}.designer .part-tree li.hidden>button{opacity:.45}
.designer .part-tree button.active{box-shadow:inset 3px 0 0 #263e3c}.designer .part-tree button[aria-pressed=true],.designer .feature-list button[aria-pressed=true]{background:#d3dfd1}
.designer .swatch{display:inline-block;width:10px;height:10px;border:1px solid #0003;border-radius:2px;flex-shrink:0}
.designer .mm-grid{display:grid;grid-template-columns:1fr 1fr;gap:0 8px}.designer .mm-grid input{width:100%;min-width:0}.designer .mm-grid .cam-check{display:flex;align-items:center;gap:6px}.designer .mm-grid .cam-check input{width:auto}
.designer .feature-editor{margin:8px 0;padding:8px;border:1px solid #becabe;border-radius:6px}
.designer .layer-row{display:grid;grid-template-columns:18px 28px 1fr auto;gap:4px;align-items:center;padding:5px 0;border-bottom:1px solid #dde3d9;font-size:11px}
.designer .layer-row select{grid-column:3/5;margin:0;padding:4px}.designer .layer-row .layer-name{min-width:0;padding:4px}
.designer .layer-row input[type=color]{width:26px;height:22px;padding:0;border:0}
.designer .layer-flag{display:flex;gap:3px;align-items:center;text-transform:none;margin:0}.designer .layer-flag input{width:auto;margin:0}
.designer .layer-actions{display:flex;gap:2px}.designer .layer-actions button{padding:2px 6px}
.designer .layer-row.hidden .layer-name{opacity:.5}.designer .layer-row.locked .layer-name{font-style:italic}
.designer .layer-order{font-weight:700;color:#60746a}.designer .layer-usage{color:#60746a}
.designer .cnc-layers{width:100%;border-collapse:collapse;font-size:11px;margin:6px 0}.designer .cnc-layers th,.designer .cnc-layers td{text-align:left;padding:3px;border-bottom:1px solid #dde3d9}
.designer .cnc-issues button{width:100%;text-align:left;border:0;font-size:11px;padding:3px}
.designer .cnc-issues .error button{color:#8d2b19}.designer .cnc-issues .warning button{color:#7a5a10}
.designer .cam-settings{margin:8px 0}.designer .cam-settings summary{cursor:pointer;font-size:12px;font-weight:700}
.designer .cam-tools{width:100%;border-collapse:collapse;font-size:10px;margin:6px 0}.designer .cam-tools td,.designer .cam-tools th{padding:2px}.designer .cam-tools input{width:100%;min-width:0;padding:3px;font-size:10px}
.designer .cam-programs{list-style:none;margin:4px 0;padding:0;font-size:11px}.designer .cam-programs li{display:flex;justify-content:space-between;align-items:center;padding:2px 0}.designer .cam-programs button{font-size:10px;padding:2px 8px}
.designer .cnc-ready{color:#2e7d5b;font-weight:700}.designer .cnc-blocked{color:#8d2b19;font-weight:700}
.designer .danger{background:#8d2b19;color:#fff}
.designer .place-design{width:100%;margin:6px 0;background:#263e3c;color:#f2f3eb}
.designer .colour-row{display:flex;gap:6px;align-items:flex-end}.designer .colour-row label{flex:1}.designer .colour-row input{height:34px;padding:2px}.designer .colour-row button{font-size:11px;margin-bottom:8px}
.designer .menu-backdrop{position:absolute;inset:0;z-index:9}
.designer .piece-menu{position:absolute;z-index:10;display:flex;flex-direction:column;min-width:200px;padding:4px;background:#f2f3eb;border:1px solid #9fb09f;border-radius:8px;box-shadow:0 8px 28px #001c2455}
.designer .piece-menu button{border:0;text-align:left;font-size:12px;padding:7px 10px;border-radius:4px}.designer .piece-menu button:focus-visible{outline:2px solid #263e3c}
.designer .piece-menu .menu-title{margin:2px 8px 4px;font-size:10px;letter-spacing:1px;text-transform:uppercase;color:#60746a}.designer .piece-menu .danger-text{color:#8d2b19}
</style>
