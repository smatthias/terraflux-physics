<script setup>
import { ref, shallowRef, customRef, computed, watch, onMounted, onBeforeUnmount, defineAsyncComponent } from 'vue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { createRoom, updateSurfaces, fitCamera, frameBox, pickSurface, roomSurfaces, cutawayWalls, createGrid, pickOpening } from './room-scene.js';
import FurnitureToolbar from './FurnitureToolbar.vue';
import BenchDesk from './BenchDesk.vue';
// Loaded on first inspection to keep the entry bundle within budget.
const FurnitureInspector = defineAsyncComponent(() => import('./FurnitureInspector.vue'));
const DesignerWorkspace = defineAsyncComponent(() => import('./DesignerWorkspace.vue'));
const TimelinePanel = defineAsyncComponent(() => import('./TimelinePanel.vue'));
import { createTimeline, appendVersion, stateAt, headState, elementAt, withElement, deepEqual, TIMELINE_FORMAT } from '../shared/timeline.mjs';
import { createTimelineStore } from './timeline-store.js';
import { placedDesignItem } from '../shared/design-placement.mjs';
import { copyFurniture, cutFurniture, deleteFurniture, pasteFurniture, duplicateFurniture as duplicateFurnitureSelection, rotateFurniture, moveFurnitureGroup, roomSelectionBounds } from '../shared/furniture-clipboard.mjs';
import { MATERIALS, MATERIAL_GROUPS, METAL_IDS } from '../shared/materials.mjs';
import { toggleSelection, selectAll, pruneSelection } from '../shared/furniture-selection.mjs';

import {createFurnitureGhost,disposeFurnitureGhost,createClearanceOverlay} from './furniture-ghost.js';
import FurnitureClearance from './FurnitureClearance.vue';
import ClearanceReview from './ClearanceReview.vue';
const CabinetCompartments = defineAsyncComponent(() => import('./CabinetCompartments.vue'));
const ShelfJoinerySettings = defineAsyncComponent(() => import('./ShelfJoinerySettings.vue'));
// Cabinet settings panels render only for a selected cabinet, so they load on first use.
const EdgeBandingSettings = defineAsyncComponent(() => import('./EdgeBandingSettings.vue'));
const CabinetDrawingSheets = defineAsyncComponent(() => import('./CabinetDrawingSheets.vue'));
// Export-only modules load on first use to keep the entry bundle within budget.
// Inputs are captured before loading so a selection change cannot alter the export.
async function exportCabinetSheets(options){const item=selectedFurniture.value;try{const {cabinetDrawingSheets}=await import('../shared/cabinet-sheets.mjs');downloadText(cabinetDrawingSheets(item,options),'text/html;charset=utf-8','_cabinet-sheets.html');message.value='Cabinet drawing sheets exported';}catch(error){message.value=error.message;}}
async function exportCabinetElevation(){const item=selectedFurniture.value;try{const {cabinetElevationSvg}=await import('../shared/cabinet-elevation.mjs');downloadText(cabinetElevationSvg(item),'image/svg+xml','_cabinet-elevation.svg');message.value='Cabinet elevation exported';}catch(error){message.value=error.message;}}
const CabinetShelfSettings = defineAsyncComponent(() => import('./CabinetShelfSettings.vue'));
const CabinetWidthSettings = defineAsyncComponent(() => import('./CabinetWidthSettings.vue'));
const CabinetFrontSettings = defineAsyncComponent(() => import('./CabinetFrontSettings.vue'));
async function exportClearance(format){const source=JSON.parse(JSON.stringify(project.value)),options={...clearanceOptions.value};try{const csv=format==='csv',print=format==='print';const [{clearancePrint},{clearanceJson,clearanceCsv}]=await Promise.all([import('../shared/clearance-print.mjs'),import('../shared/clearance-export.mjs')]);downloadText((print?clearancePrint:csv?clearanceCsv:clearanceJson)(source,options),print?'text/html;charset=utf-8':csv?'text/csv;charset=utf-8':'application/json',print?'_clearance.html':csv?'_clearance.csv':'_clearance.json');message.value='Clearance review exported';}catch(error){message.value=error.message;}}
import {furnitureClearance,reviewFurnitureClearance} from '../shared/furniture-clearance.mjs';
const showClearanceOverlay=ref(true);
const clearanceOptions=ref({minWall:.05,doorDepth:.6,enforce:false});
function updateClearance(options){clearanceOptions.value=options;if(furnitureInsertion.value){const request=furnitureInsertion.value.request;furnitureInsertion.value={...evaluateInsertion(request),request};}}
import {evaluateFurniturePlacement,placementRotation} from '../shared/furniture-placement.mjs';
import {snapFurniture,describeSnaps} from '../shared/furniture-snap.mjs';
import {ARRANGE_DEFAULTS} from '../shared/arrange-defaults.mjs';
import FurnitureArrange from './FurnitureArrange.vue';
const furnitureInsertion=ref(null),pointerFurniture=ref(false),guideSnapping=ref(false);
function evaluateInsertion(request){return evaluateFurniturePlacement(project.value.room,request,gridSpacing.value,snapEnabled.value,guideSnapping.value,clearanceOptions.value,connectFurniture.value);}
// ---- Connecting furniture: while dragging or previewing, items snap flush to nearby
// walls and furniture (furniture-snap.mjs). Auto-arrange (loaded on first use)
// places furniture along free walls, clear of doors, windows and the walkway.
const connectFurniture=ref(true),arrangeOptions=ref({...ARRANGE_DEFAULTS});
const connectOptions=extra=>({wallGap:clearanceOptions.value.minWall??.05,clearance:clearanceOptions.value,...extra});
async function autoArrangeFurniture() {
  if(editingLocked.value) return;
  const ids=[...furnitureSelection.value];
  busy.value=true;
  try {
    const {autoArrange}=await import('../shared/furniture-autoarrange.mjs');
    const result=autoArrange(project.value.room,ids,{...arrangeOptions.value,wallGap:clearanceOptions.value.minWall??.05});
    busy.value=false;
    applyRoom(result.room);selectFurnitureIds(result.placed.map(p=>p.id));
    const kept=result.unplaced.filter(u=>!u.moved),moved=result.unplaced.filter(u=>u.moved);
    message.value='Arranged '+plural(result.placed.length,'item')+' along the walls'+(kept.length?' · no wall space for '+kept.map(u=>u.name).join(', ')+' (left in place)':'')+(moved.length?' · no wall space for '+moved.map(u=>u.name).join(', ')+' (moved clear of doors and windows)':'');
  } catch(error) {message.value=error.message;}
  finally {busy.value=false;}
}
function setPreviewRotation(value){if(!furnitureInsertion.value)return;try{const request={...furnitureInsertion.value.request,rotation:placementRotation(value)};furnitureInsertion.value={...evaluateInsertion(request),request};}catch(error){message.value=error.message;}}
function rotatePreview(delta){setPreviewRotation((furnitureInsertion.value?.candidate?.rotation??furnitureInsertion.value?.request.rotation??0)+delta);}
function setGuideSnapping(value){guideSnapping.value=value;if(furnitureInsertion.value){const request=furnitureInsertion.value.request;furnitureInsertion.value={...evaluateInsertion(request),request};}}
let insertionPointerId=null;
function cancelFurnitureInsertion(){
  furnitureInsertion.value=null;pointerFurniture.value=false;
  if(insertionPointerId!==null){const id=insertionPointerId;insertionPointerId=null;clearPointer();if(controls)controls.enabled=true;if(renderer?.domElement.hasPointerCapture(id))renderer.domElement.releasePointerCapture(id);}
}
function startPointerFurniture(payload){cancelFurnitureInsertion();previewFurniture(payload);pointerFurniture.value=true;view.value='Plan';message.value='Move pointer in Plan · click to place · Escape cancels';}
function updatePointerFurniture(event){
  if(!pointerFurniture.value||!furnitureInsertion.value||view.value!=='Plan')return;
  const hit=planPoint(event);if(!hit)return;
  const request={...furnitureInsertion.value.request,point:{x:hit.x,z:hit.z}};
  furnitureInsertion.value={...evaluateInsertion(request),request};
}
function insideCanvas(event){const r=renderer.domElement.getBoundingClientRect();return event.clientX>=r.left&&event.clientX<=r.right&&event.clientY>=r.top&&event.clientY<=r.bottom;}
function canvasLeave(){clearPreview();if(pointerFurniture.value&&insertionPointerId===null){cancelFurnitureInsertion();message.value='Furniture pointer placement cancelled';}}
function cancelPointerFurnitureOnBlur(){if(pointerFurniture.value)cancelFurnitureInsertion();}
function previewFurniture({kind,point,presetId}){
  placementType.value=null;furniturePlacing.value=false;
  const preset=project.value.furniturePresets?.find(p=>p.id===presetId);
  const previous=furnitureInsertion.value?.request,same=previous?.kind===kind&&(kind==='preset'?previous.preset?.id===presetId:previous.sourceId===selectedFurniture.value?.id);
  const request={kind,point,preset,sourceId:selectedFurniture.value?.id,id:previous?.id??crypto.randomUUID(),...(same&&previous.rotation!==undefined?{rotation:previous.rotation}:{})};
  const result=evaluateInsertion(request);
  furnitureInsertion.value={...result,request};
}
function confirmFurnitureInsertion(){
  if(!furnitureInsertion.value)return;
  const result=evaluateInsertion(furnitureInsertion.value.request);
  if(!result.valid){furnitureInsertion.value={...furnitureInsertion.value,...result};message.value=result.error;return;}
  const id=result.candidate.id;applyRoom({...project.value.room,furniture:[...project.value.room.furniture,result.candidate]});cancelFurnitureInsertion();selected.value='furniture:'+id;message.value='Furniture preview committed';
}
import FurnitureLibrary from './FurnitureLibrary.vue';
import {duplicateCabinet,saveFurniturePreset,instantiateFurniturePreset,previewFurnitureInsertion} from '../shared/furniture-library.mjs';
function duplicateFurniture(point){try{const id=crypto.randomUUID();applyRoom(duplicateCabinet(project.value.room,selectedFurniture.value?.id,id,point,gridSpacing.value,snapEnabled.value));selected.value='furniture:'+id;message.value='Cabinet duplicated';}catch(error){message.value=error.message;}}
function saveFurniture(name){try{const furniturePresets=saveFurniturePreset(project.value.furniturePresets??[],{...selectedFurniture.value,id:crypto.randomUUID(),name});project.value=validateProject({...project.value,furniturePresets});message.value='Furniture preset saved · save project to keep it';}catch(error){message.value=error.message;}}
function addFurniturePreset({id,point}){try{const preset=project.value.furniturePresets?.find(p=>p.id===id);if(!preset)throw Error('Furniture preset not found');const newId=crypto.randomUUID();applyRoom(instantiateFurniturePreset(project.value.room,preset,newId,point,gridSpacing.value,snapEnabled.value));selected.value='furniture:'+newId;message.value='Cabinet added from preset';}catch(error){message.value=error.message;}}
function deleteFurniturePreset(id){project.value=validateProject({...project.value,furniturePresets:project.value.furniturePresets.filter(p=>p.id!==id)});message.value='Furniture preset removed · cabinet settings retained';}
const CabinetArrange = defineAsyncComponent(() => import('./CabinetArrange.vue'));
import {arrangeCabinet,arrangeCabinetGroup} from '../shared/cabinet-arrange.mjs';
function arrangeGroup({ids,options}){try{applyRoom(arrangeCabinetGroup(project.value.room,ids,options));message.value=options.mode==='align-group'?'Cabinet group aligned':'Cabinet gaps distributed';}catch(error){message.value=error.message;}}
function arrangeSelected({referenceId,options}){try{applyRoom(arrangeCabinet(project.value.room,selectedFurniture.value?.id,referenceId,options));message.value=options.mode==='align'?'Cabinet aligned':'Cabinet gap applied';}catch(error){message.value=error.message;}}
import {moveFurniture} from '../shared/furniture.mjs';
const CabinetLayout = defineAsyncComponent(() => import('./CabinetLayout.vue'));
const ManufacturingPanel=defineAsyncComponent(()=>import('./ManufacturingPanel.vue'));
const WorkshopJobs=defineAsyncComponent(()=>import('./WorkshopJobs.vue'));
function exportJobRecord(text){downloadText(text,'application/json','_job-record.json');message.value='Workshop job record exported';}
function exportQuotePrint(text){downloadText(text,'text/html;charset=utf-8','_quote.html');message.value='Printable quote exported';}
function exportQuote(text){downloadText(text,'application/json','_quote.json');message.value='Quote snapshot exported';}
function exportSavedJob(text){downloadText(text,'application/json','_job-revision.json');message.value='Saved job revision exported';}
const WorkshopProfiles = defineAsyncComponent(() => import('./WorkshopProfiles.vue'));
import {saveWorkshopProfile,applyWorkshopProfile,exportWorkshopProfiles,importWorkshopProfiles,applyWorkshopProfileToCabinets,MAX_WORKSHOP_BYTES} from '../shared/workshop-profiles.mjs';
import {exportJobManifest,verifyJobManifest,MAX_MANIFEST_BYTES} from '../shared/job-manifest.mjs';
const manifestResult=ref('');
async function exportJob(){try{downloadText(await exportJobManifest(project.value),'application/json','_job.json');message.value='Workshop job manifest exported';}catch(error){message.value=error.message;}}
async function verifyJob(file){
  if(!file)return;
  try{
    if(file.size>MAX_MANIFEST_BYTES)throw Error('Job manifest exceeds 2 MiB');
    const snapshot=JSON.stringify(project.value);
    const result=await verifyJobManifest(await file.text(),JSON.parse(snapshot));
    manifestResult.value=result.matchesCurrent&&snapshot===JSON.stringify(project.value)?'Verified manifest · matches current project · '+result.validation.status:'Verified manifest · differs from current project';
  }catch(error){manifestResult.value=error.message;}
}
import {workshopHandoff} from '../shared/workshop-handoff.mjs';
function exportWorkshop(){downloadText(workshopHandoff(project.value),'text/html;charset=utf-8','_workshop.html');message.value='Workshop handoff exported';}
const showManufacturing=ref(false);
function exportManufacturing(csv){downloadText(csv,'text/csv;charset=utf-8','_manufacturing.csv');message.value='Project manufacturing CSV exported';}
import {nestCabinet,nestingCsv} from '../shared/nesting.mjs';
import {cabinetBom,cabinetBomCsv,cabinetMass} from '../shared/cabinet-parts.mjs';
import { savePreset } from '../shared/presets.mjs';
import { parseProjectJson, serializeProjectJson, MAX_PROJECT_BYTES } from '../shared/project-json.mjs';
import { RECOVERY_KEY, readDrafts, storeDraft, removeDraft } from '../shared/recovery.mjs';
import { duplicateProject } from '../shared/duplicate-project.mjs';
import { exportPlanSvg, planSheetLayout } from '../shared/plan-svg.mjs';
import { validateProject } from '../shared/project.mjs';
import { bounds, validateOutline, snapPoint, scaleOutline, moveCorner, moveCorners } from '../shared/geometry.mjs';
import { cornerIds, isWallId, wallIds, wallLabel, wallCorners, wallSegment, cornerWalls, cornerLabel, cornerPoint, MIN_WALLS } from '../shared/room-topology.mjs';
import { moveWall, removeWall, replaceWall } from '../shared/room-construction.mjs';
import { isLocked, lockRoom, unlockRoom, checkRoomEdit, LOCKED_MESSAGE } from '../shared/room-lock.mjs';
import { dimensionLayout, openingChainLayout } from './dimensions.js';
import { createPlacementPreview } from './placement-preview.js';
import { wallLength, openingAtPoint, previewOpening, openingPreset } from '../shared/openings.mjs';
import { createHistory, commitHistory, undoHistory, redoHistory } from '../shared/history.mjs';
const blankProject=()=>validateProject({schemaVersion:1,units:'m',name:'Untitled studio',room:{width:6,depth:4,height:2.8}});
const project=ref(blankProject());
// The version of the history shown read-only, if any (see Version history below).
const previewSeq=ref(null),previewing=computed(()=>previewSeq.value!==null);
const printScale=ref(0),paperSize=ref('auto'),paperOrientation=ref('landscape');
const sheetFeedback=computed(()=>{
  if(paperSize.value==='auto') return null;
  try{return planSheetLayout(project.value,{scale:printScale.value||null,paper:paperSize.value,orientation:paperOrientation.value});}
  catch(error){return {fits:false,error:error.message};}
});
const fingerprint=p=>JSON.stringify(validateProject(p));
const baseline=ref(fingerprint(project.value)),draftKey=ref('draft-'+crypto.randomUUID()),recoveryOrigin=ref(null);
const recoveryDrafts=ref([]),recoveryChoice=ref(''),recoveryNotice=ref(''),jsonInput=ref(null);
const dirty=computed(()=>{try{return fingerprint(project.value)!==baseline.value;}catch{return true;}});
function refreshDraftChoice() {
  if(!recoveryDrafts.value.some(d=>d.key===recoveryChoice.value)) recoveryChoice.value=recoveryDrafts.value[0]?.key||'';
}
let ignoredDraft=null;
function captureDraft() {
  if(dragging.value || previewing.value || !dirty.value) return;
  try {validateProject(project.value);}catch{return;}
  if(ignoredDraft?.key===draftKey.value && ignoredDraft.snapshot===fingerprint(project.value)) return;
  try {recoveryDrafts.value=storeDraft(localStorage,draftKey.value,project.value);refreshDraftChoice();}
  catch {recoveryNotice.value='Local recovery storage is unavailable. Save or export your project.';}
}
function clearCurrentDraft() {
  try {recoveryDrafts.value=removeDraft(localStorage,draftKey.value);if(recoveryOrigin.value) recoveryDrafts.value=removeDraft(localStorage,recoveryOrigin.value);recoveryOrigin.value=null;refreshDraftChoice();}
  catch {recoveryNotice.value='Could not clear local recovery data.';}
}
function resetWorkspaceState() {
  furniturePlacing.value=false;placementType.value=null;selected.value=null;selectedOpeningId.value=null;history.value=createHistory(project.value.room);resetView();
}
async function recoverDraft() {
  const draft=recoveryDrafts.value.find(d=>d.key===recoveryChoice.value);if(!draft)return;
  captureDraft();exitPreview();busy.value=true;
  const stored=await storedTimeline(timelineStore.load(draft.key));busy.value=false;
  project.value=validateProject(draft.project);activeProjectId.value=null;projectChoice.value='';
  recoveryOrigin.value=draft.key;draftKey.value='draft-'+crypto.randomUUID();baseline.value='';resetWorkspaceState();message.value='Draft recovered as an unsaved copy';
  startTimeline(stored,'Draft recovered');
}
function discardDraft() {
  if(recoveryChoice.value===draftKey.value) {
    let snapshot=null;try{snapshot=fingerprint(project.value);}catch{}
    ignoredDraft={key:draftKey.value,snapshot};
  }
  timelineStore.remove(recoveryChoice.value).catch(()=>{});
  try {recoveryDrafts.value=removeDraft(localStorage,recoveryChoice.value);refreshDraftChoice();message.value='Local draft discarded';}
  catch {recoveryNotice.value='Could not clear local recovery data.';}
}
function beforeUnload() {captureDraft();timelineStore.flush().catch(()=>{});}

const activeProjectId=ref(null),projectChoice=ref(''),projectList=ref([]),listLoading=ref(true);
const dimensionDraft=ref({width:project.value.room.width,depth:project.value.room.depth,height:project.value.room.height});
watch(()=>project.value.room,room=>{dimensionDraft.value={width:room.width,depth:room.depth,height:room.height};},{deep:true});
const history=ref(createHistory(project.value.room)), dragging=ref(false), cornerHandles=ref([]);
let dragState;
watch(project,()=>{manifestResult.value='';},{deep:true});
watch([project,dragging],captureDraft,{deep:true,flush:'post'});
const editingLocked=computed(()=>busy.value || dragging.value || previewing.value);
// Locked walls: the room shell is fixed and only the interior can be edited.
const wallsLocked=computed(()=>isLocked(project.value.room));
function toggleWallLock() {
  if(editingLocked.value) return;
  const locking=!wallsLocked.value;
  try {
    if(locking) {placementType.value=null;clearPreview();}
    applyRoom(locking?lockRoom(project.value.room):unlockRoom(project.value.room));
    message.value=locking?'Walls locked · interior editing':'Walls unlocked · wall editing';
  } catch(error) {message.value=error.message;}
}
// Rooms with stored walls need schema 7; validation writes the original four walls as schema 6.
function setRoom(room) {
  const next=validateProject({...project.value,schemaVersion:7,room});
  project.value.schemaVersion=next.schemaVersion;project.value.room=next.room;
  return next.room;
}
// Every room edit passes here, so the wall lock is enforced for all of them.
function applyRoom(room,record=true) {
  if(previewing.value) throw Error(PREVIEW_MESSAGE);
  const next=validateProject({...project.value,schemaVersion:7,room}).room;
  checkRoomEdit(project.value.room,next);
  if(record) history.value=commitHistory(history.value,next);
  setRoom(next);
}
function travel(direction) {
  if(editingLocked.value) return;
  furniturePlacing.value=false;placementType.value=null;
  const wasLocked=wallsLocked.value;
  history.value=direction==='undo'?undoHistory(history.value):redoHistory(history.value);
  setRoom(JSON.parse(JSON.stringify(history.value.present)));
  if(wasLocked!==wallsLocked.value) message.value=(direction==='undo'?'Undo':'Redo')+': walls '+(wallsLocked.value?'locked · interior editing':'unlocked · wall editing');
  else message.value=direction==='undo'?'Geometry edit undone':'Geometry edit redone';
}
function setHeight(event) {
  try { applyRoom({...project.value.room,height:Number(event.target.value)}); }
  catch(error) { message.value=error.message; }
  dimensionDraft.value.height=project.value.room.height;
  event.target.value=project.value.room.height;
}
// The status message; each assignment (even of the same text) also labels the version it causes.
let freshMessage='',freshTimer;
const message=customRef((track,trigger)=>{let value='Ready to design';return {get() {track();return value;},
  set(text) {value=text;freshMessage=text;clearTimeout(freshTimer);freshTimer=setTimeout(()=>{freshMessage='';});trigger();}};});
const viewport = ref(null), busy = ref(false), metrics = ref(null), view = ref('3D');
// ---- Version history (shared/timeline.mjs). Every committed change becomes a
// timestamped version, labelled with the status message its action set. An
// earlier version can be previewed read-only and restored as a new version, in
// whole or one element at a time. Kept in this browser (IndexedDB) and, on
// Save, on the server beside the project.
const PREVIEW_MESSAGE='Viewing an earlier version · restore it or go back to now to edit';
const timelineStore=createTimelineStore({onError:()=>{recoveryNotice.value='Version history could not be stored in this browser. Save the project to keep it on the server.';}});
const timeline=shallowRef(createTimeline(validateProject(project.value),{at:Date.now(),label:'New project'})),showTimeline=ref(false);
const restoreCount=ref(0),designerActiveId=ref(null);
let timelineHead=timeline.value.entries[0].keyframe;
const currentState=()=>{try {return validateProject(JSON.parse(JSON.stringify(project.value)));} catch {return null;}};
const restorable=state=>validateProject({...state,schemaVersion:7});
const timeLabel=at=>new Date(at).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});
// A stored history is used when its newest version rebuilds to a valid project
// (the server checks every version when it stores one; previews check theirs).
const storedTimeline=async pending=>{try {const value=await pending;if(value?.format!==TIMELINE_FORMAT||value.version!==1) return null;validateProject(headState(value));return value;} catch {return null;}};
// Starts the history for the project just opened: continues `existing` (adding a
// version when the project differs from its last one) or begins a new one.
function startTimeline(existing,label) {
  previewSeq.value=null;
  const state=currentState();if(!state) return;
  let next=existing;
  if(next) {const head=headState(next);if(!deepEqual(head,state)) next=appendVersion(next,head,state,{at:Date.now(),label});}
  else next=createTimeline(state,{at:Date.now(),label});
  timeline.value=next;timelineHead=state;restoreCount.value++;
  if(next.entries.length>1) timelineStore.save(draftKey.value,next);
}
watch([project,dragging],()=>{
  if(dragging.value || previewing.value) return;
  const state=currentState();if(!state || deepEqual(state,timelineHead)) return;
  timeline.value=appendVersion(timeline.value,timelineHead,state,{at:Date.now(),label:freshMessage});
  timelineHead=state;freshMessage='';timelineStore.save(draftKey.value,timeline.value);
},{deep:true,flush:'post'});
function previewVersion(seq) {
  if(busy.value || dragging.value) return;
  if(seq===timeline.value.entries.at(-1).seq) {exitPreview();return;}
  if(!previewing.value && !currentState()) {message.value='Correct the current project before viewing earlier versions';return;}
  try {
    const state=restorable(stateAt(timeline.value,seq));
    cancelDrag();stopPlacementModes();exitInspection();previewSeq.value=seq;project.value=state;
    message.value='Viewing the version from '+timeLabel(timeline.value.entries.find(e=>e.seq===seq).at)+' · read-only';
  } catch(error) {message.value='This version cannot be shown: '+error.message;}
}
function exitPreview() {
  if(!previewing.value) return;
  previewSeq.value=null;project.value=JSON.parse(JSON.stringify(timelineHead));message.value='Back to the current version';
}
function restoreVersion(seq) {
  const entry=timeline.value.entries.find(e=>e.seq===seq);if(!entry || busy.value) return;
  try {
    const state=restorable(stateAt(timeline.value,seq));
    previewSeq.value=null;project.value=state;history.value=createHistory(state.room);restoreCount.value++;
    message.value='Restored the version from '+timeLabel(entry.at);
  } catch(error) {message.value='This version cannot be restored: '+error.message;}
}
// Puts one element back as it was in a version (or removes it if it did not exist then).
function restoreElement({seq,key,name}) {
  const entry=timeline.value.entries.find(e=>e.seq===seq);if(!entry || busy.value) return;
  try {
    const next=restorable(withElement(timelineHead,key,elementAt(timeline.value,seq,key)));
    previewSeq.value=null;project.value=next;history.value=commitHistory(history.value,next.room);restoreCount.value++;
    message.value='Restored “'+name+'” from '+timeLabel(entry.at);
  } catch(error) {
    message.value='“'+name+'” cannot be restored into the current project: '+error.message;
  }
}
// The element the history panel offers to follow: the selection in the active perspective.
const timelineFocus=computed(()=>{
  if(perspective.value==='designer') {const d=project.value.furnitureDesigns?.find(d=>d.id===designerActiveId.value);return d?{key:'design:'+d.id,name:d.name}:null;}
  const id=selected.value;if(!id) return null;
  if(id.startsWith('furniture:')) {const item=project.value.room.furniture.find(f=>'furniture:'+f.id===id);return item?{key:id,name:item.name}:null;}
  const opening=project.value.room.openings.find(o=>o.id===selectedOpeningId.value);
  return opening?{key:'opening:'+opening.id,name:(opening.type==='door'?'Door':'Window')+' on '+opening.wall}:{key:'room',name:'Room shell'};
});
const selected = ref(null);
// ---- Furniture selection and editing toolkit (cut/copy/paste/duplicate/delete/rotate).
// `selected` is the active item shown in the inspector; furnitureSelection holds
// every selected item. The clipboard is session-only and survives project switches.
const furnitureSelection=ref([]),clipboard=ref(null),pastePlacing=ref(false),inspectingId=ref(null);
const inspectedItem=computed(()=>project.value.room.furniture.find(item=>item.id===inspectingId.value)??null);
const makeFurnitureId=()=>crypto.randomUUID();
const plural=(n,word)=>n+' '+word+(n===1?'':'s');
watch(selected,id=>{
  const itemId=id?.startsWith('furniture:')?id.slice(10):null;
  if(!itemId) furnitureSelection.value=[];
  else if(!furnitureSelection.value.includes(itemId)) furnitureSelection.value=[itemId];
});
// Undo/redo, cut or delete can remove selected items; keep the selection consistent.
const activeFurniture=()=>selected.value?.startsWith('furniture:')?selected.value.slice(10):null;
// Sets the selection first so the `selected` watcher sees the active item as a member.
function setFurnitureSelection({ids,active}) {furnitureSelection.value=ids;selected.value=active?'furniture:'+active:null;}
watch(()=>project.value.room.furniture.map(item=>item.id).join('|'),()=>{
  const present=project.value.room.furniture.map(item=>item.id),active=activeFurniture();
  const next=pruneSelection({ids:furnitureSelection.value,active},present);
  if(next.ids.length!==furnitureSelection.value.length || (active && next.active!==active)) setFurnitureSelection(next);
  if(inspectingId.value && !present.includes(inspectingId.value)) exitInspection();
});
function selectFurnitureIds(ids) {setFurnitureSelection({ids:[...ids],active:ids[0]??null});}
function toggleFurniture(id) {
  setFurnitureSelection(toggleSelection({ids:furnitureSelection.value,active:activeFurniture()},id));
  message.value=plural(furnitureSelection.value.length,'item')+' selected';
}
function selectAllFurniture() {
  const ids=project.value.room.furniture.map(item=>item.id);if(!ids.length) return;
  setFurnitureSelection(selectAll({active:activeFurniture()},ids));message.value=plural(ids.length,'item')+' selected';
}
function stopPlacementModes() {cancelFurnitureInsertion();placementType.value=null;furniturePlacing.value=false;pastePlacing.value=false;}
function furnitureCommand(run) {
  if(editingLocked.value) {if(previewing.value) message.value=PREVIEW_MESSAGE;return false;}
  try {run();return true;} catch(error) {message.value=error.message;return false;}
}
const copySelection=()=>furnitureCommand(()=>{clipboard.value=copyFurniture(project.value.room,furnitureSelection.value);message.value='Copied '+plural(clipboard.value.items.length,'item');});
const cutSelection=()=>furnitureCommand(()=>{
  const result=cutFurniture(project.value.room,furnitureSelection.value);stopPlacementModes();
  applyRoom(result.room);clipboard.value=result.clipboard;selectFurnitureIds([]);message.value='Cut '+plural(result.clipboard.items.length,'item');
});
const deleteSelection=()=>furnitureCommand(()=>{
  const count=furnitureSelection.value.length;stopPlacementModes();
  applyRoom(deleteFurniture(project.value.room,furnitureSelection.value));selectFurnitureIds([]);message.value='Deleted '+plural(count,'item');
});
function pasteClipboard(point=null) {
  if(!clipboard.value) {message.value='The furniture clipboard is empty';return false;}
  return furnitureCommand(()=>{
    const result=pasteFurniture(project.value.room,clipboard.value,{makeId:makeFurnitureId,point,spacing:gridSpacing.value,snap:snapEnabled.value});
    applyRoom(result.room);selectFurnitureIds(result.ids);message.value='Pasted '+plural(result.ids.length,'item');
  });
}
function startPastePlacing() {
  if(!clipboard.value||editingLocked.value) return;
  stopPlacementModes();exitInspection();view.value='Plan';pastePlacing.value=true;message.value='Click in Plan to paste · Escape cancels';
}
const duplicateSelection=()=>furnitureCommand(()=>{
  const result=duplicateFurnitureSelection(project.value.room,furnitureSelection.value,{makeId:makeFurnitureId,spacing:gridSpacing.value,snap:snapEnabled.value});
  applyRoom(result.room);selectFurnitureIds(result.ids);message.value='Duplicated '+plural(result.ids.length,'item');
});
const rotateSelection=degrees=>furnitureCommand(()=>{
  applyRoom(rotateFurniture(project.value.room,furnitureSelection.value,degrees));
  message.value='Rotated '+plural(furnitureSelection.value.length,'item')+' by '+(degrees>0?'+':'−')+Math.abs(degrees)+'°';
});
function frameCamera(box) {
  if(!camera||!controls) {message.value='3D view requires WebGL';return;}
  controls.enableDamping=false;controls.update();
  const aspect=Math.max(viewport.value.clientWidth,1)/Math.max(viewport.value.clientHeight,1);
  const target=frameBox(camera,box,aspect,view.value,project.value.room.height);
  controls.target.set(target.x,target.y,target.z);controls.update();controls.enableDamping=true;
}
function zoomToSelection() {
  try {frameCamera(roomSelectionBounds(project.value.room,furnitureSelection.value));message.value='Zoomed to '+plural(furnitureSelection.value.length,'item');}
  catch(error) {message.value=error.message;}
}
// Inspection isolates one item in 3D; orbit and scroll work as usual.
function frameInspected() {if(inspectedItem.value) frameCamera(roomSelectionBounds(project.value.room,[inspectedItem.value.id]));}
function toggleInspection() {
  if(inspectingId.value) {exitInspection();message.value='Inspection closed';return;}
  const item=selectedFurniture.value;if(!item) {message.value='Select a furniture item to inspect';return;}
  stopPlacementModes();inspectingId.value=item.id;
  if(view.value!=='3D') view.value='3D'; else frameInspected();
  message.value='Inspecting '+item.name+' · drag to orbit · scroll to zoom · Escape exits';
}
function exitInspection() {if(!inspectingId.value) return;inspectingId.value=null;resetView();}
function refreshSurfaces() {
  if(!roomGroup) return;
  const highlight=furnitureSelection.value.length?furnitureSelection.value.map(id=>'furniture:'+id):selected.value;
  updateSurfaces(roomGroup,view.value,highlight,{isolate:inspectingId.value});
}
// Office-style shortcuts; text fields keep their own clipboard and undo.
function furnitureShortcut(event) {
  const key=event.key.toLowerCase(),mod=event.ctrlKey||event.metaKey,hasSelection=furnitureSelection.value.length>0;
  const run=action=>{event.preventDefault();action();return true;};
  if(mod && !event.altKey && !event.shiftKey) {
    if(key==='c' && hasSelection) return run(copySelection);
    if(key==='x' && hasSelection) return run(cutSelection);
    if(key==='v' && clipboard.value) return run(()=>pasteClipboard());
    if(key==='d' && hasSelection) return run(duplicateSelection);
    if(key==='a' && project.value.room.furniture.length) return run(selectAllFurniture);
    return false;
  }
  if(mod || event.altKey) return false;
  if((key==='delete'||key==='backspace') && hasSelection) return run(deleteSelection);
  if(key==='r' && hasSelection) return run(()=>rotateSelection(event.shiftKey?-15:15));
  if(key==='f' && hasSelection) return run(zoomToSelection);
  if(key==='i' && (hasSelection||inspectingId.value)) return run(toggleInspection);
  return false;
}
const surfaces = computed(() => roomSurfaces(project.value.room));
const selectedLabel = computed(() => surfaces.value.find(surface => surface.id === selected.value)?.label || project.value.room.furniture.find(item=>'furniture:'+item.id===selected.value)?.name || 'Nothing selected');
// ---- Perspectives: the room editor or the furniture designer, in the same window.
const perspective=ref('room');
function setPerspective(mode) {
  // Both perspectives can be viewed while previewing an earlier version.
  if(mode===perspective.value || busy.value || dragging.value) return;
  if(mode==='designer') {cancelDrag();stopPlacementModes();exitInspection();}
  perspective.value=mode;message.value=mode==='designer'?'Furniture designer':'Room editor';
}
// Places a design at the nearest free spot to the room centre (like a paste)
// and shows it in the room. Returns an error message instead of throwing.
function placeDesign(designId) {
  const design=project.value.furnitureDesigns?.find(d=>d.id===designId);
  try {
    const b=bounds(project.value.room.outline),item=placedDesignItem(design,{id:'new',x:b.x,z:b.z});
    const result=pasteFurniture(project.value.room,{format:'terratec-furniture-clipboard',version:1,mode:'cut',items:[item]},{makeId:makeFurnitureId,spacing:gridSpacing.value,snap:snapEnabled.value});
    applyRoom(result.room);setPerspective('room');selectFurnitureIds(result.ids);message.value='Placed “'+design.name+'” in the room';return null;
  } catch(error) {return error.message;}
}
const focusDesignId=ref(null);
function openDesign(designId) {focusDesignId.value=null;setPerspective('designer');focusDesignId.value=designId;}
// Designs are project data; returns an error message instead of throwing.
function applyDesigns(furnitureDesigns) {
  if(previewing.value) return PREVIEW_MESSAGE;
  try {project.value=validateProject({...project.value,schemaVersion:7,furnitureDesigns});return null;}
  catch(error) {return error.message;}
}
const projectBytes=computed(()=>{try {return new TextEncoder().encode(serializeProjectJson(project.value)).length;} catch {return MAX_PROJECT_BYTES+1;}});
// ---- The studio as a desktop application: the canvas is the core of the view and
// the editors are windows on a desk around it (BenchDesk.vue). Windows against
// the left or right edge dock there and the canvas takes the free middle; the
// others float over it. Their buttons sit in the top bar. The whole studio can
// go full screen (the browser's Fullscreen API; it always fills the window).
const ROOM_WINDOWS=[
  {id:'furniture',title:'Furniture',rect:d=>({x:8,y:8,w:330,h:d.height-16}),minimized:d=>d.width<760},
  {id:'room',title:'Room & walls',rect:d=>({x:d.width-338,y:8,w:330,h:Math.round((d.height-24)/2)}),minimized:d=>d.width<760},
  {id:'project',title:'Project',rect:d=>({x:d.width-338,y:Math.round((d.height-24)/2)+16,w:330,h:d.height-24-Math.round((d.height-24)/2)}),minimized:d=>d.width<760},
];
// shallowRef: elements must stay unproxied (requestFullscreen, Teleport targets).
const workspaceEl=shallowRef(null),windowBar=shallowRef(null),roomDesk=ref(null),fullScreen=ref(false);
// Selecting a wall or a furniture item brings its window forward.
function showSelectionWindow(id) {const win=id?.startsWith('furniture:')?'furniture':id&&id!=='floor'?'room':null;if(win&&perspective.value==='room') roomDesk.value?.focus(win);}
watch(selected,showSelectionWindow);
// The surface list also brings the window back when the surface is already selected.
function selectSurface(id) {selected.value=id;showSelectionWindow(id);}
async function toggleFullScreen() {
  if(document.fullscreenElement) {try {await document.exitFullscreen();} catch {}return;}
  const element=workspaceEl.value;
  if(!element?.requestFullscreen) {message.value='Full screen is unavailable in this browser · the studio already fills the window';return;}
  try {await element.requestFullscreen({navigationUI:'hide'});message.value='Full screen · Esc exits';}
  catch {message.value='Full screen was refused by the browser · the studio still fills the window';}
}
function onFullscreenChange() {
  const on=!!document.fullscreenElement&&document.fullscreenElement===workspaceEl.value;
  if(fullScreen.value&&!on) message.value='Full screen closed';
  fullScreen.value=on;
}
const hiddenSelection = computed(() => view.value === '3D' && cutawayWalls(project.value.room).includes(selected.value));
const valid = computed(() => { try { validateProject(project.value); return true; } catch { return false; } });
const gridSpacing=ref(.25), snapEnabled=ref(true), endpointDraft=ref([]);
const selectedWall=computed(()=>isWallId(project.value.room,selected.value)?selected.value:null);
// Undo or wall edits can remove the selected wall; clear a selection that no longer exists.
watch(()=>project.value.room,room=>{
  const id=selected.value;
  if(id && id!=='floor' && !id.startsWith('furniture:') && !isWallId(room,id)) {selected.value=null;selectedOpeningId.value=null;}
},{deep:true});
const placementType=ref(null), placementPreview=ref(null), placementSize=ref(null);
const furniturePlacing=ref(false),furnitureDraft=ref(null);
const selectedFurniture=computed(()=>project.value.room.furniture.find(item=>'furniture:'+item.id===selected.value));
const clearanceReview=computed(()=>{try{return reviewFurnitureClearance(project.value.room,clearanceOptions.value);}catch(error){return {error:error.message};}});
function inspectClearance(id){
 if(editingLocked.value||!project.value.room.furniture.some(item=>item.id===id))return;
 cancelFurnitureInsertion();selected.value='furniture:'+id;showClearanceOverlay.value=true;
}
const selectedClearance=computed(()=>{try{return selectedFurniture.value?furnitureClearance(project.value.room,selectedFurniture.value,clearanceOptions.value):null;}catch(error){return {error:error.message};}});
// Cabinet-only tools; placed designs have their own editor and CNC output.
const selectedCabinet=computed(()=>selectedFurniture.value?.type==='cabinet'?selectedFurniture.value:null);
const furnitureBom=computed(()=>selectedCabinet.value?cabinetBom(selectedCabinet.value):null);
const furnitureLayout=computed(()=>selectedCabinet.value?nestCabinet(selectedCabinet.value):null);
const furnitureMass=computed(()=>{try{return selectedCabinet.value?cabinetMass(selectedCabinet.value):null;}catch{return null;}});
function exportNesting(){downloadText(nestingCsv(selectedFurniture.value),'text/csv;charset=utf-8','_cabinet-layout.csv');message.value='Cabinet layout CSV exported';}
function exportCabinetBom() {
  try {downloadText(cabinetBomCsv(selectedFurniture.value),'text/csv;charset=utf-8','_cabinet-panels.csv');message.value='Cabinet panel CSV exported';}
  catch(error){message.value=error.message;}
}
watch([selected,project],()=>{furnitureDraft.value=selectedFurniture.value?{...selectedFurniture.value}:null;},{deep:true});
function cabinetDefault(point) {
  const b=bounds(project.value.room.outline);
  return {id:crypto.randomUUID(),type:'cabinet',name:'Cabinet',width:Math.min(.9,b.width*.4),depth:Math.min(.5,b.depth*.4),height:Math.min(1.2,project.value.room.height),thickness:.018,shelves:1,x:point?.x??b.x,z:point?.z??b.z,rotation:0,material:'oak',lengthAllowanceMm:0,widthAllowanceMm:0,stockLengthMm:2440,stockWidthMm:1220,stockTrimMm:0,stockAllowRotation:true,stockKerfMm:3};
}
function addCabinet(point) {
  try {
    const item=cabinetDefault(point),position=snapPoint(item,gridSpacing.value,snapEnabled.value);
    item.x=position.x;item.z=position.z;
    applyRoom({...project.value.room,furniture:[...project.value.room.furniture,item]});
    selected.value='furniture:'+item.id;furniturePlacing.value=false;message.value='Cabinet added';
  } catch(error) {message.value=error.message;}
}
function placeCabinet() {cancelFurnitureInsertion();placementType.value=null;view.value='Plan';furniturePlacing.value=true;message.value='Click in the room to place a cabinet · Escape cancels';}
function setCabinetColumns(value){
 const columns=value===''?NaN:Number(value),draft=furnitureDraft.value;
 draft.columns=columns;
 if(draft.columnFronts&&Number.isInteger(columns)&&columns>=1&&columns<=8)draft.columnFronts=Array.from({length:columns},(_,i)=>draft.columnFronts[i]??'open');
 if(draft.columnWeights&&Number.isInteger(columns)&&columns>=1&&columns<=8)draft.columnWeights=Array.from({length:columns},(_,i)=>draft.columnWeights[i]??1);
 if(draft.columnShelves&&Number.isInteger(columns)&&columns>=1&&columns<=8)draft.columnShelves=Array.from({length:columns},(_,i)=>draft.columnShelves[i]??draft.shelves);
}
function applyCabinet() {
  try {
    const next={...furnitureDraft.value},position=snapPoint(next,gridSpacing.value,snapEnabled.value);next.x=position.x;next.z=position.z;
    applyRoom({...project.value.room,furniture:project.value.room.furniture.map(item=>item.id===next.id?next:item)});message.value='Cabinet updated';
  } catch(error) {message.value=error.message;}
}
function saveWorkshop(name) {
  try {
    const workshopProfiles=saveWorkshopProfile(project.value.workshopProfiles,{...selectedFurniture.value,id:crypto.randomUUID(),name});
    project.value=validateProject({...project.value,workshopProfiles});message.value='Workshop profile saved · save project to keep it';
  }catch(error){message.value=error.message;}
}
function useWorkshop(id) {
  try {
    const profile=project.value.workshopProfiles.find(p=>p.id===id);
    if(!profile)throw Error('Workshop profile not found');
    applyRoom(applyWorkshopProfile(project.value.room,selectedFurniture.value?.id,profile));message.value='Workshop profile applied';
  }catch(error){message.value=error.message;}
}
function exportWorkshops(){downloadText(exportWorkshopProfiles(project.value.workshopProfiles),'application/json','_workshops.json');message.value='Workshop profiles exported';}
async function importWorkshops(file) {
  if(!file)return;
  busy.value=true;
  try {
    if(file.size>MAX_WORKSHOP_BYTES)throw Error('Workshop profile file exceeds 32 KiB');
    const workshopProfiles=importWorkshopProfiles(project.value.workshopProfiles,await file.text());
    project.value=validateProject({...project.value,workshopProfiles});message.value='Workshop profiles imported · save project to keep them';
  }catch(error){message.value=error.message;}finally{busy.value=false;}
}
function useWorkshopBulk({profileId,cabinetIds}) {
  try {
    const profile=project.value.workshopProfiles.find(p=>p.id===profileId);
    if(!profile)throw Error('Workshop profile not found');
    applyRoom(applyWorkshopProfileToCabinets(project.value.room,cabinetIds,profile));message.value='Workshop profile applied to '+cabinetIds.length+' cabinets';
  }catch(error){message.value=error.message;}
}
function deleteWorkshop(id) {
  project.value=validateProject({...project.value,workshopProfiles:project.value.workshopProfiles.filter(p=>p.id!==id)});
  message.value='Workshop profile removed · cabinet settings retained';
}
function removeCabinet() {
  try {applyRoom({...project.value.room,furniture:project.value.room.furniture.filter(item=>item.id!==selectedFurniture.value.id)});selected.value=null;message.value='Cabinet removed';}
  catch(error) {message.value=error.message;}
}
watch(selected,()=>{furniturePlacing.value=false;});
watch(view,mode=>{if(mode!=='Plan')furniturePlacing.value=false;});

const sizePresets={};
const presetName=ref(''),presetId=ref(null),chainLabels=ref([]);
const savedPresets=computed(()=>project.value.presets.filter(p=>p.type===placementType.value));
let previewGroup,previewId;
const selectedOpeningId=ref(null), openingDraft=ref(null), wallLabels=ref([]);
const wallOpenings=computed(()=>project.value.room.openings.filter(o=>o.wall===selected.value));
watch([selected,selectedOpeningId,project],()=>{
  const current=wallOpenings.value.find(o=>o.id===selectedOpeningId.value);
  openingDraft.value=current?{...current}:null;
},{deep:true});
function addOpening(type) {
  try {
    const length=wallLength(project.value.room,selected.value), height=project.value.room.height;
    const width=Math.min(type==='door'?.9:1.2,length-.2);
    const occupied=[...wallOpenings.value].sort((a,b)=>a.offset-b.offset);
    let offset=.1;
    for(const o of occupied) {
      if(offset+width<=o.offset-.05) break;
      offset=Math.max(offset,o.offset+o.width+.1);
    }
    const opening={id:crypto.randomUUID(),wall:selected.value,type,offset,width,
      height:type==='door'?Math.min(2.1,height-.05):Math.min(1.2,height*.4),sill:type==='door'?0:Math.max(.1,height*.3)};
    applyRoom({...project.value.room,openings:[...project.value.room.openings,opening]});
    selectedOpeningId.value=opening.id; message.value=type==='door'?'Door added':'Window added';
  } catch(error) { message.value=error.message; }
}
function beginPlacement(type) {
  cancelFurnitureInsertion();
  furniturePlacing.value=false;clearPreview(); previewId=crypto.randomUUID(); presetId.value=null;presetName.value='';
  const preset=openingPreset(project.value.room,selected.value,type,'size',0);
  placementSize.value={...(sizePresets[type]||{width:preset.width,height:preset.height,sill:preset.sill,hinge:'A',swing:'in'})};
  view.value='Plan'; placementType.value=type;
  message.value='Click near the selected wall · Escape cancels placement';
}
function saveOpeningPreset() {
  try {
    const presets=savePreset(project.value.presets,{id:crypto.randomUUID(),name:presetName.value,type:placementType.value,...placementSize.value});
    project.value=validateProject({...project.value,presets});
    presetId.value=presets.find(p=>p.type===placementType.value && p.name.toLowerCase()===presetName.value.trim().toLowerCase()).id;
    message.value='Preset saved · save project to keep it';
  } catch(error) {message.value=error.message;}
}
function useOpeningPreset() {
  const preset=savedPresets.value.find(p=>p.id===presetId.value);
  if(!preset) return;
  placementSize.value={width:preset.width,height:preset.height,sill:preset.sill,hinge:preset.hinge??'A',swing:preset.swing??'in'};
  presetName.value=preset.name;message.value='Preset applied';
}
function removeOpeningPreset() {
  try {
    project.value=validateProject({...project.value,presets:project.value.presets.filter(p=>p.id!==presetId.value)});
    presetId.value=null;message.value='Preset removed · save project to keep it';
  } catch(error) {message.value=error.message;}
}
function clearPreview() {
  if(previewGroup) {
    previewGroup.traverse(item=>{item.geometry?.dispose();item.material?.dispose();});
    scene?.remove(previewGroup);previewGroup=null;
  }
  placementPreview.value=null;
}
function updatePreview(event) {
  if(!placementType.value || view.value!=='Plan' || busy.value) return;
  const rect=renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster();
  ray.setFromCamera(new THREE.Vector2((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1),camera);
  const point=ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0,1,0),0),new THREE.Vector3());
  if(!point) {clearPreview();return;}
  const result=previewOpening(project.value.room,selected.value,placementType.value,previewId,point,gridSpacing.value,snapEnabled.value,
    Math.max(.15,(camera.top-camera.bottom)/camera.zoom/rect.height*18),placementSize.value);
  clearPreview();placementPreview.value=result;
  previewGroup=createPlacementPreview(project.value.room,result);scene.add(previewGroup);
}
watch(placementType,clearPreview);
watch(placementSize,size=>{
  if(size && placementType.value) sizePresets[placementType.value]={...size};
  clearPreview();
},{deep:true});
watch([gridSpacing,snapEnabled],clearPreview);
function placeOpening(point,tolerance) {
  try {
    const opening=openingAtPoint(project.value.room,selected.value,placementType.value,crypto.randomUUID(),point,gridSpacing.value,snapEnabled.value,tolerance,placementSize.value);
    applyRoom({...project.value.room,openings:[...project.value.room.openings,opening]});
    selectedOpeningId.value=opening.id; placementType.value=null;
    message.value='Opening placed';
  } catch(error) { message.value=error.message; }
}
watch(selected,()=>{placementType.value=null;});
watch(view,mode=>{if(mode!=='Plan') placementType.value=null;});
function saveOpening() {
  try {
    applyRoom({...project.value.room,openings:project.value.room.openings.map(o=>o.id===openingDraft.value.id?{...openingDraft.value}:o)});
    message.value='Opening updated';
  } catch(error) { message.value=error.message; }
}
function deleteOpening() {
  try {
    applyRoom({...project.value.room,openings:project.value.room.openings.filter(o=>o.id!==selectedOpeningId.value)});
    selectedOpeningId.value=null; message.value='Opening removed';
  } catch(error) { message.value=error.message; }
}

// Endpoint A/B drafts carry the stable ID of the corner they edit.
function syncEndpoints() {
  const wall=selectedWall.value, room=project.value.room;
  endpointDraft.value=wall?Object.values(wallCorners(room,wall)).map(corner=>({corner,...cornerPoint(room,corner)})):[];
}
watch([selected,project],syncEndpoints,{deep:true,immediate:true});
function updateOutline(outline) {
  const next=validateOutline(outline), b=bounds(next);
  applyRoom({...project.value.room,outline:next,width:b.width,depth:b.depth});
}
function applyEndpoints() {
  try {
    const moves=Object.fromEntries(endpointDraft.value.map(({corner,x,z})=>[corner,{x,z}]));
    updateOutline(moveCorners(project.value.room,moves,gridSpacing.value,snapEnabled.value)); message.value='Wall endpoints updated';
  } catch(error) { message.value=error.message; }
}
// Wall construction: the other walls keep their lines and the shared corners are recomputed.
const wallMoveDistance=ref(.5), replacementDraft=ref([]);
const wallCount=computed(()=>wallIds(project.value.room).length);
function syncReplacement() {
  const wall=selectedWall.value;
  if(!wall) {replacementDraft.value=[];return;}
  const {a,b}=wallSegment(project.value.room,wall);
  replacementDraft.value=[{a:{...a},b:{...b}}];
}
watch([selected,project],syncReplacement,{deep:true,immediate:true});
// Splits the last replacement wall into a shallow outward bay, which keeps the room convex.
function addReplacementSegment() {
  const last=replacementDraft.value.at(-1),{a,b}=last,length=Math.hypot(b.x-a.x,b.z-a.z);
  if(!(length>0)) return;
  const depth=Math.min(.5,length*.15),tip=snapPoint({x:(a.x+b.x)/2+(b.z-a.z)/length*depth,z:(a.z+b.z)/2-(b.x-a.x)/length*depth},gridSpacing.value,snapEnabled.value);
  replacementDraft.value.splice(-1,1,{a:{...a},b:{...tip}},{a:{...tip},b:{...b}});
}
function runWallEdit(edit,describe) {
  const before=project.value.room.openings.length;
  try {
    const room=edit(project.value.room);applyRoom(room);
    const dropped=before-project.value.room.openings.length;
    message.value=describe(room)+(dropped>0?' · '+dropped+' opening'+(dropped===1?'':'s')+' removed with it':'');
    return true;
  } catch(error) {message.value=error.message;return false;}
}
function moveSelectedWall() {
  const wall=selectedWall.value,distance=wallMoveDistance.value;
  runWallEdit(room=>moveWall(room,wall,distance),()=>wallLabel(wall)+' moved '+Math.abs(distance).toFixed(2)+' m '+(distance<0?'inward':'outward'));
}
function removeSelectedWall() {
  const wall=selectedWall.value;
  if(runWallEdit(room=>removeWall(room,wall),()=>wallLabel(wall)+' removed · neighbouring walls now meet')) {selected.value=null;selectedOpeningId.value=null;}
}
function replaceSelectedWall() {
  const wall=selectedWall.value,index=wallIds(project.value.room).indexOf(wall);
  let segments;
  try {segments=replacementDraft.value.map(({a,b})=>({a:snapPoint(a,gridSpacing.value,snapEnabled.value),b:snapPoint(b,gridSpacing.value,snapEnabled.value)}));}
  catch(error) {message.value=error.message;return;}
  let first=null;
  if(runWallEdit(room=>{const next=replaceWall(room,wall,segments);first=wallIds(next)[index];return next;},()=>wallLabel(wall)+' replaced by '+segments.length+' wall'+(segments.length===1?'':'s')))
    {selected.value=first;selectedOpeningId.value=null;}
}
function setDimension(key,event) {
  try {
    updateOutline(scaleOutline(project.value.room.outline,key==='width'?'x':'z',Number(event.target.value)));
  } catch(error) { message.value=error.message; }
  dimensionDraft.value[key]=project.value.room[key];
  event.target.value=project.value.room[key];
}
let scene, camera, renderer, controls, roomGroup, grid, observer, frame, designScene=null;
let furnitureGhost,selectedClearanceOverlay;
function syncSelectedClearance(){
 disposeFurnitureGhost(selectedClearanceOverlay);selectedClearanceOverlay=null;
 if(scene&&showClearanceOverlay.value&&!furnitureInsertion.value&&selectedClearance.value&&!selectedClearance.value.error){selectedClearanceOverlay=createClearanceOverlay(selectedClearance.value);scene.add(selectedClearanceOverlay);}
}
watch([selectedClearance,showClearanceOverlay,furnitureInsertion,view],syncSelectedClearance,{deep:true});
function syncFurnitureGhost(){disposeFurnitureGhost(furnitureGhost);furnitureGhost=null;if(scene&&furnitureInsertion.value){furnitureGhost=createFurnitureGhost(furnitureInsertion.value.candidate,furnitureInsertion.value.valid,view.value,furnitureInsertion.value.guides,furnitureInsertion.value.clearance);scene.add(furnitureGhost);}}
watch([furnitureInsertion,view],syncFurnitureGhost,{deep:true});
watch([project,selected,gridSpacing,snapEnabled],cancelFurnitureInsertion,{deep:true});
let pointerStart;
let revision = 0;
watch(project, () => { revision++; metrics.value = null; rebuild(); }, { deep: true });
watch([selected,furnitureSelection,inspectingId], refreshSurfaces, { deep: true });
async function request(path, method, body) {
  const response = await fetch(path, { method, headers: { 'Content-Type': 'application/json' }, ...(body ? { body: JSON.stringify(body) } : {}) });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || 'Request failed');
  return data;
}
async function refreshProjects() {
  listLoading.value=true;
  try {
    projectList.value=await request('/api/projects','GET');
    if(!projectChoice.value && projectList.value.length) projectChoice.value=projectList.value[0].id;
  } catch(error) {message.value=error.message;}
  finally {listLoading.value=false;}
}
function newProject() {
  exitPreview();captureDraft();
  furniturePlacing.value=false;placementType.value=null;project.value=blankProject();activeProjectId.value=null;projectChoice.value='';
  selected.value=null;selectedOpeningId.value=null;history.value=createHistory(project.value.room);
  baseline.value=fingerprint(project.value);draftKey.value='draft-'+crypto.randomUUID();recoveryOrigin.value=null;
  resetView();message.value='New project';startTimeline(null,'New project');
}
function duplicateWorkspace() {
  try {
    exitPreview();const copy=duplicateProject(project.value);captureDraft();project.value=copy;activeProjectId.value=null;projectChoice.value='';
    draftKey.value='draft-'+crypto.randomUUID();recoveryOrigin.value=null;baseline.value='';
    resetWorkspaceState();message.value='Project duplicated as an unsaved copy';startTimeline(null,'Duplicated project');
  } catch(error) {message.value=error.message;}
}
function downloadText(text,type,extension) {
  const url=URL.createObjectURL(new Blob([text],{type})),link=document.createElement('a');
  link.href=url;link.download=(project.value.name.replace(/[^a-zA-Z0-9_-]+/g,'_').slice(0,80)||'terratec')+extension;
  link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}
function exportJson() {
  try {downloadText(serializeProjectJson(project.value),'application/json','.json');message.value='Project JSON exported';}
  catch(error) {message.value=error.message;}
}
async function importJson(event) {
  const file=event.target.files?.[0];if(!file)return;
  busy.value=true;
  try {
    if(file.size>65536) throw Error('Project JSON exceeds 64 KiB');
    const imported=parseProjectJson(await file.text());
    exitPreview();captureDraft();project.value=imported;activeProjectId.value=null;projectChoice.value='';draftKey.value='draft-'+crypto.randomUUID();
    baseline.value='';recoveryOrigin.value=null;resetWorkspaceState();message.value='Project JSON imported as an unsaved copy';startTimeline(null,'Imported from JSON');
  } catch(error) {message.value='Import failed: '+error.message;}
  finally {event.target.value='';busy.value=false;}
}
function exportPlan() {
  try {
    const blob=new Blob([exportPlanSvg(project.value,{scale:printScale.value||null,paper:paperSize.value,orientation:paperOrientation.value})],{type:'image/svg+xml'}),url=URL.createObjectURL(blob);
    const link=document.createElement('a');link.href=url;link.download=(project.value.name.replace(/[^a-zA-Z0-9_-]+/g,'_').slice(0,80)||'terratec')+(paperSize.value!=='auto'?'_'+paperSize.value+'-'+paperOrientation.value:'')+(printScale.value?'_1-'+printScale.value:'')+'.svg';
    link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);message.value='SVG plan exported';
  } catch(error) {message.value=error.message;}
}
async function action(kind) {
  if(kind==='load') {exitPreview();captureDraft();}
  furniturePlacing.value=false;placementType.value=null;
  busy.value = true;
  const atRevision = revision;
  try {
    if(kind==='load') {
      let stored=null;
      if(projectChoice.value) {
        const record=await request('/api/projects/'+projectChoice.value,'GET');
        // The newer of this browser's and the server's history of the project.
        const copies=(await Promise.all([storedTimeline(timelineStore.load(record.id)),storedTimeline(request('/api/projects/'+record.id+'/timeline','GET'))])).filter(Boolean);
        stored=copies.sort((a,b)=>Date.parse(b.entries.at(-1).at)-Date.parse(a.entries.at(-1).at))[0]??null;
        project.value=record.project;activeProjectId.value=record.id;
      }
      else {project.value=await request('/api/project','GET');activeProjectId.value=null;}
      baseline.value=fingerprint(project.value);recoveryOrigin.value=null;draftKey.value=activeProjectId.value||'draft-'+crypto.randomUUID();
      history.value=createHistory(project.value.room);selected.value=null;selectedOpeningId.value=null;resetView();message.value='Saved project opened';
      startTimeline(stored,'Saved project opened');
    } else if(kind==='save') {
      const record=await request(activeProjectId.value?'/api/projects/'+activeProjectId.value:'/api/projects',activeProjectId.value?'PUT':'POST',project.value);
      const previousKey=draftKey.value,origin=recoveryOrigin.value;
      baseline.value=fingerprint(record.project);clearCurrentDraft();draftKey.value=record.id;
      activeProjectId.value=record.id;projectChoice.value=record.id;
      // The history moves to the saved project's key and is stored with it on the server.
      for(const key of [previousKey,origin]) if(key && key!==record.id) timelineStore.remove(key).catch(()=>{});
      timelineStore.save(record.id,timeline.value);
      let historyNote='';
      try {await request('/api/projects/'+record.id+'/timeline','PUT',timeline.value);} catch(error) {historyNote=' · version history kept in this browser only ('+error.message+')';}
      await refreshProjects();message.value='Project saved'+historyNote;
    }
    else {
      const result = await request('/api/geometry', 'POST', project.value);
      if (atRevision === revision) { metrics.value = result; message.value = 'Geometry calculated'; }
    }
  } catch (error) { message.value = error.message; }
  finally { busy.value = false; }
}
function disposeRoom() {
  if (!roomGroup) return;
  roomGroup.traverse(item => { item.geometry?.dispose(); item.material?.dispose(); });
  scene.remove(roomGroup);
  if(grid) { grid.geometry.dispose(); grid.material.dispose(); scene.remove(grid); }
}
function rebuild() {
  if (!scene || !valid.value) return;
  disposeRoom();
  roomGroup = createRoom(project.value.room,{designs:project.value.furnitureDesigns??[],designScene});
  if(!designScene&&project.value.room.furniture.some(item=>item.type==='design')) import('./design-scene.js').then(module=>{designScene=module;rebuild();});
  refreshSurfaces();
  scene.add(roomGroup);
  grid=createGrid(project.value.room,gridSpacing.value); grid.visible=view.value==='Plan'; scene.add(grid);
}
function resetView() {
  if (!camera || !valid.value) return;
  // Consume residual damping before applying a deterministic fitted camera.
  controls.enableDamping = false; controls.update();
  const aspect = Math.max(viewport.value.clientWidth,1) / Math.max(viewport.value.clientHeight,1);
  fitCamera(camera, project.value.room, aspect, view.value);
  const b=bounds(project.value.room.outline);
  controls.target.set(b.x,0,b.z); controls.update();
  controls.enableDamping = true;
}
function setupCamera() {
  if (!renderer) return;
  cancelDrag();
  controls?.dispose();
  camera = view.value === 'Plan' ? new THREE.OrthographicCamera() : new THREE.PerspectiveCamera(45,1,.01,10000);
  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.enableRotate = view.value !== 'Plan';
  controls.screenSpacePanning = true;
  controls.minZoom = .05; controls.maxZoom = 100;
  if (view.value === 'Plan') controls.mouseButtons.LEFT = THREE.MOUSE.PAN;
  resetView();
  refreshSurfaces();
  if(grid) grid.visible=view.value==='Plan';
  if(inspectingId.value) frameInspected();
}
function planPoint(event) {
  const rect=renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster();camera.updateMatrixWorld();
  ray.setFromCamera(new THREE.Vector2((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1),camera);
  return ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0,1,0),0),new THREE.Vector3());
}
function pointerDown(event) {
  if(pointerFurniture.value&&view.value==='Plan'){
    if(!editingLocked.value&&event.button===0&&event.isPrimary){
      event.preventDefault();event.stopImmediatePropagation();
      controls.enableDamping=false;controls.update();controls.enableDamping=true;controls.enabled=false;
      pointerStart={x:event.clientX,y:event.clientY,id:event.pointerId,moved:false};insertionPointerId=event.pointerId;
      renderer.domElement.setPointerCapture(event.pointerId);updatePointerFurniture(event);
    }return;
  }
  if(view.value==='Plan' && !editingLocked.value && !placementType.value && !furniturePlacing.value && !pastePlacing.value && event.button===0 && event.isPrimary && roomGroup) {
    const rect=renderer.domElement.getBoundingClientRect();
    const id=pickSurface(roomGroup,camera,(event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1);
    if(id?.startsWith('furniture:') && (event.shiftKey||event.ctrlKey||event.metaKey)) {
      event.preventDefault();event.stopImmediatePropagation();clearPointer();toggleFurniture(id.slice(10));return;
    }
    if(id?.startsWith('furniture:')) {
      event.preventDefault();event.stopImmediatePropagation();clearPointer();
      controls.enableDamping=false;controls.update();controls.enableDamping=true;controls.enabled=false;
      const hit=planPoint(event),item=project.value.room.furniture.find(item=>'furniture:'+item.id===id);
      dragState={kind:'furniture',itemId:item.id,id:event.pointerId,original:JSON.parse(JSON.stringify(project.value.room)),valid:true,target:renderer.domElement,start:hit,screen:{x:event.clientX,y:event.clientY},moved:false};
      dragging.value=true;selected.value=id;renderer.domElement.setPointerCapture(event.pointerId);message.value='Dragging cabinet · Escape cancels';return;
    }
  }
  if(editingLocked.value) { pointerStart=null; return; }
  pointerStart = event.button === 0 && event.isPrimary ? { x: event.clientX, y: event.clientY, id: event.pointerId, moved:false } : null;
}
function pointerUp(event) {
  if(pointerFurniture.value){
    if(event.pointerId!==insertionPointerId)return;
    const start=pointerStart;clearPointer();insertionPointerId=null;controls.enabled=true;
    if(renderer.domElement.hasPointerCapture(event.pointerId))renderer.domElement.releasePointerCapture(event.pointerId);
    if(start&&!start.moved&&Math.hypot(event.clientX-start.x,event.clientY-start.y)<=5&&insideCanvas(event)){updatePointerFurniture(event);confirmFurnitureInsertion();}
    return;
  }
  if(dragState?.kind==='furniture') {finishDrag(event);return;}
  const start = pointerStart; pointerStart = null;
  if (!start || event.pointerId !== start.id || !roomGroup || start.moved || Math.hypot(event.clientX-start.x,event.clientY-start.y) > 5) return;
  const rect = renderer.domElement.getBoundingClientRect();
  const x=(event.clientX-rect.left)/rect.width*2-1,y=-(event.clientY-rect.top)/rect.height*2+1;
  if(pastePlacing.value && view.value==='Plan') {
    const ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2(x,y),camera);
    const point=ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0,1,0),0),new THREE.Vector3());
    if(point && pasteClipboard({x:point.x,z:point.z})) pastePlacing.value=false;
    return;
  }
  if(furniturePlacing.value && view.value==='Plan') {
    const ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2(x,y),camera);
    const point=ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0,1,0),0),new THREE.Vector3());
    if(point)addCabinet(point);return;
  }
  if(placementType.value && view.value==='Plan') {
    const ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2(x,y),camera);
    const point=ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0,1,0),0),new THREE.Vector3());
    if(point) placeOpening(point,Math.max(.15,(camera.top-camera.bottom)/camera.zoom/rect.height*18));
    return;
  }
  const openingId=pickOpening(roomGroup,camera,x,y);
  const picked=openingId?project.value.room.openings.find(o=>o.id===openingId).wall:pickSurface(roomGroup,camera,x,y);
  if(picked?.startsWith('furniture:') && (event.shiftKey||event.ctrlKey||event.metaKey)) {toggleFurniture(picked.slice(10));return;}
  selected.value=picked;
  selectedOpeningId.value=openingId;
}
function pointerMove(event) {
  if(pointerFurniture.value){if(pointerStart&&event.pointerId===pointerStart.id&&Math.hypot(event.clientX-pointerStart.x,event.clientY-pointerStart.y)>5)pointerStart.moved=true;updatePointerFurniture(event);return;}
  if(dragState?.kind==='furniture') {dragCabinet(event);return;}
  if(pointerStart && Math.hypot(event.clientX-pointerStart.x,event.clientY-pointerStart.y)>5) pointerStart.moved=true;
  if(pointerStart?.moved) clearPreview(); else updatePreview(event);
}
function clearPointer() { pointerStart = null; }
function cancelCanvasDrag() {if(insertionPointerId!==null){cancelFurnitureInsertion();return;}clearPointer();if(dragState?.kind==='furniture')cancelDrag();}
function dragCabinet(event) {
  if(!dragState || event.pointerId!==dragState.id)return;
  const state=dragState;
  if(!state.moved && Math.hypot(event.clientX-state.screen.x,event.clientY-state.screen.y)<=5)return;
  state.moved=true;
  const hit=planPoint(event),item=state.original.furniture.find(item=>item.id===state.itemId);if(!hit)return;
  try {
    const point={x:item.x+hit.x-state.start.x,z:item.z+hit.z-state.start.z},group=furnitureSelection.value.length>1&&furnitureSelection.value.includes(item.id);
    let room=group?moveFurnitureGroup(state.original,furnitureSelection.value,item.id,point,gridSpacing.value,snapEnabled.value):moveFurniture(state.original,item.id,point,gridSpacing.value,snapEnabled.value);
    state.snaps=null;
    if(connectFurniture.value) {
      // Groups move rigidly: they snap by the dragged item, without turning.
      const snapped=snapFurniture(room,item.id,connectOptions({rotate:!group,ignore:group?furnitureSelection.value:[]}));
      if(snapped) try {
        room=group?moveFurnitureGroup(state.original,furnitureSelection.value,item.id,{x:snapped.item.x,z:snapped.item.z},gridSpacing.value,false):{...room,furniture:room.furniture.map(f=>f.id===item.id?snapped.item:f)};
        state.snaps=describeSnaps(room,snapped.snaps);
      } catch {}
    }
    applyRoom(room,false);
    state.valid=true;message.value='Dragging cabinet'+(state.snaps?' · connects to '+state.snaps:'')+' · Escape cancels';
  } catch(error){state.valid=false;message.value=error.message;}
}

function projectHandles() {
  if(!camera || !viewport.value || view.value!=='Plan') { if(cornerHandles.value.length) cornerHandles.value=[]; if(wallLabels.value.length) wallLabels.value=[]; if(chainLabels.value.length) chainLabels.value=[]; return; }
  const w=viewport.value.clientWidth,h=viewport.value.clientHeight;
  const room=project.value.room;
  const next=cornerIds(room).map(id=>{
    const p=cornerPoint(room,id),point=new THREE.Vector3(p.x,0,p.z).project(camera),walls=cornerWalls(room,id);
    return {id,label:cornerLabel(id),title:cornerLabel(id)+' · joins '+wallLabel(walls.incoming)+' and '+wallLabel(walls.outgoing),
      x:(point.x+1)*w/2,y:(1-point.y)*h/2,visible:Math.abs(point.x)<1 && Math.abs(point.y)<1};
  });
  if(JSON.stringify(next)!==JSON.stringify(cornerHandles.value)) cornerHandles.value=next;
  const labels=dimensionLayout(project.value.room,camera,w,h,id=>id===selected.value && wallOpenings.value.length?60:28);
  if(JSON.stringify(labels)!==JSON.stringify(wallLabels.value)) wallLabels.value=labels;
  const chains=openingChainLayout(project.value.room,selected.value,camera,w,h);
  if(JSON.stringify(chains)!==JSON.stringify(chainLabels.value)) chainLabels.value=chains;
}
function startDrag(corner,event) {
  if(event.button!==0 || !event.isPrimary || editingLocked.value || wallsLocked.value || !valid.value) return;
  event.preventDefault(); event.stopPropagation(); furniturePlacing.value=false;placementType.value=null; clearPointer();
  // Stop camera inertia before establishing the drag plane.
  controls.enableDamping=false; controls.update(); controls.enableDamping=true; controls.enabled=false;
  dragState={corner,id:event.pointerId,original:JSON.parse(JSON.stringify(project.value.room)),valid:true,target:event.currentTarget};
  // Select the wall that starts at this corner, so the corner is endpoint A.
  dragging.value=true; selected.value=cornerWalls(project.value.room,corner).outgoing;
  event.currentTarget.setPointerCapture(event.pointerId);
  message.value='Dragging corner · Escape cancels';
}
function dragCorner(event) {
  if(!dragState || event.pointerId!==dragState.id) return;
  const rect=renderer.domElement.getBoundingClientRect();
  const ray=new THREE.Raycaster();
  ray.setFromCamera(new THREE.Vector2((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1),camera);
  const hit=ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0,1,0),0),new THREE.Vector3());
  if(!hit) return;
  try {
    const outline=moveCorner(dragState.original,dragState.corner,{x:hit.x,z:hit.z},gridSpacing.value,snapEnabled.value);
    applyRoom({...dragState.original,outline},false); dragState.valid=true;
    message.value='Dragging corner · Escape cancels';
  } catch(error) { dragState.valid=false; message.value=error.message; }
}
function finishDrag(event) {
  if(!dragState || event.pointerId!==dragState.id) return;
  const state=dragState;
  if(!state.valid) { cancelDrag(); message.value=state.kind==='furniture'?'Invalid cabinet move cancelled':'Invalid corner move cancelled'; return; }
  dragState=null; dragging.value=false; controls.enabled=true;
  if(state.target.hasPointerCapture(state.id)) state.target.releasePointerCapture(state.id);
  history.value=commitHistory(history.value,project.value.room);
  const count=furnitureSelection.value.length;
  message.value=state.kind==='furniture'?(state.moved?(count>1?count+' cabinets moved':'Cabinet moved')+(state.snaps?' · connected to '+state.snaps:''):'Cabinet selected'):'Corner moved';
}
function cancelDrag() {
  if(!dragState) return;
  const state=dragState; dragState=null; dragging.value=false;
  applyRoom(state.original,false); controls.enabled=true;
  if(state.target.hasPointerCapture(state.id)) state.target.releasePointerCapture(state.id);
  message.value=state.kind==='furniture'?'Cabinet move cancelled':'Corner move cancelled';
}
function keyboard(event) {
  if(previewing.value && event.key==='Escape') {exitPreview();return;}
  if(perspective.value!=='room') return;
  if(event.key==='Escape') { if(inspectingId.value){exitInspection();message.value='Inspection closed';return;} if(pastePlacing.value){pastePlacing.value=false;message.value='Paste placement cancelled';return;} if(furnitureInsertion.value){cancelFurnitureInsertion();message.value='Furniture preview cancelled';return;} if(furniturePlacing.value) {furniturePlacing.value=false;message.value='Cabinet placement cancelled';return;} if(placementType.value) { placementType.value=null; message.value='Opening placement cancelled'; return; } cancelDrag(); selected.value=null; return; }
  const tag=event.target?.tagName;
  if(['INPUT','TEXTAREA','SELECT'].includes(tag) || event.target?.isContentEditable) return;
  if(furnitureInsertion.value&&event.key.toLowerCase()==='r'&&!event.ctrlKey&&!event.metaKey&&!event.altKey){event.preventDefault();rotatePreview(event.shiftKey?-15:15);return;}
  if(furnitureShortcut(event)) return;
  if((event.ctrlKey || event.metaKey) && ['z','y'].includes(event.key.toLowerCase())) {
    event.preventDefault();
    travel(event.key.toLowerCase()==='y' || event.shiftKey ? 'redo':'undo');
  }
}
watch(view,mode=>{if(pointerFurniture.value&&mode!=='Plan')cancelFurnitureInsertion();if(mode!=='Plan')pastePlacing.value=false;if(mode==='Plan'&&inspectingId.value)exitInspection();});
watch(view, setupCamera);
watch(gridSpacing,rebuild);
onMounted(() => {
  try {recoveryDrafts.value=readDrafts(localStorage.getItem(RECOVERY_KEY));refreshDraftChoice();}
  catch {recoveryNotice.value='Local recovery data could not be read.';}
  window.addEventListener('beforeunload',beforeUnload);
  refreshProjects();
  window.addEventListener('keydown',keyboard);document.addEventListener('fullscreenchange',onFullscreenChange);
  window.addEventListener('blur',cancelDrag);window.addEventListener('blur',cancelPointerFurnitureOnBlur);
  try {
    scene = new THREE.Scene(); scene.background = new THREE.Color('#152c2d');
    renderer = new THREE.WebGLRenderer({ antialias: true }); renderer.setPixelRatio(Math.min(devicePixelRatio,2));
    viewport.value.appendChild(renderer.domElement);
    setupCamera();
    renderer.domElement.addEventListener('pointerdown',pointerDown,true);
    renderer.domElement.addEventListener('pointerup',pointerUp);
    renderer.domElement.addEventListener('pointermove',pointerMove);
    renderer.domElement.addEventListener('pointercancel',cancelCanvasDrag);
    renderer.domElement.addEventListener('lostpointercapture',cancelCanvasDrag);
    renderer.domElement.addEventListener('pointerleave',canvasLeave);
    scene.add(new THREE.HemisphereLight(0xffffff,0x365448,3));
    const sun = new THREE.DirectionalLight(0xffefcf,3); sun.position.set(8,12,6); scene.add(sun);
    observer = new ResizeObserver(() => {
      const { clientWidth: w, clientHeight: h } = viewport.value;
      if (!w || !h) return;
      cancelDrag(); renderer.setSize(w,h); resetView(); if(inspectingId.value) frameInspected();
    }); observer.observe(viewport.value);
    rebuild();syncSelectedClearance();
    const animate = () => { frame = requestAnimationFrame(animate); if (perspective.value !== 'room') return; controls.update(); projectHandles(); renderer.render(scene,camera); }; animate();
  } catch { message.value = '3D view requires WebGL. Project save/load remains available.'; }
});
onBeforeUnmount(() => {
  captureDraft();window.removeEventListener('beforeunload',beforeUnload);
  clearPreview(); disposeFurnitureGhost(furnitureGhost);disposeFurnitureGhost(selectedClearanceOverlay); cancelDrag(); window.removeEventListener('keydown',keyboard);document.removeEventListener('fullscreenchange',onFullscreenChange); window.removeEventListener('blur',cancelDrag);window.removeEventListener('blur',cancelPointerFurnitureOnBlur);
  cancelAnimationFrame(frame); observer?.disconnect(); controls?.dispose(); disposeRoom();
  renderer?.domElement.removeEventListener('pointerdown',pointerDown,true);
  renderer?.domElement.removeEventListener('pointerup',pointerUp);
  renderer?.domElement.removeEventListener('pointermove',pointerMove);
  renderer?.domElement.removeEventListener('pointercancel',cancelCanvasDrag);
  renderer?.domElement.removeEventListener('lostpointercapture',cancelCanvasDrag);
  renderer?.domElement.removeEventListener('pointerleave',canvasLeave);
  renderer?.dispose();
});
</script>

<template>
  <div ref="workspaceEl" class="workspace" :class="{ 'full-screen': fullScreen }">
    <header class="app-bar"><a class="brand" href="/" title="TerraTec · design / build / connect">terra<span>tec</span></a><div class="perspective-switch" role="group" aria-label="Perspective"><button :aria-pressed="perspective === 'room'" :disabled="busy || dragging" @click="setPerspective('room')">Room</button><button :aria-pressed="perspective === 'designer'" :disabled="busy || dragging" @click="setPerspective('designer')">Furniture designer</button></div><div class="actions" role="toolbar" aria-label="Project"><div class="bar-group" role="group" aria-label="Saved project"><select v-model="projectChoice" aria-label="Saved projects" :disabled="editingLocked || listLoading"><option value="">Choose project</option><option v-for="entry in projectList" :key="entry.id" :value="entry.id">{{ entry.name }}</option></select><button :disabled="editingLocked || listLoading" @click="action('load')">Open saved</button><button class="primary" :disabled="editingLocked || !valid" @click="action('save')">Save project</button></div><span class="bar-sep"></span><div class="bar-group" role="group" aria-label="Workspace"><button :disabled="editingLocked" @click="newProject">New</button><button :disabled="editingLocked || !valid" @click="duplicateWorkspace">Duplicate</button></div><span class="bar-sep"></span><div class="bar-group" role="group" aria-label="Files"><button :disabled="editingLocked" @click="jsonInput.click()">Import JSON</button><input ref="jsonInput" type="file" accept=".json,application/json" aria-label="Import project JSON" hidden @change="importJson"><button :disabled="editingLocked || !valid" @click="exportJson">Export JSON</button><button :disabled="editingLocked || !valid || (sheetFeedback && !sheetFeedback.fits)" @click="exportPlan">Export SVG</button></div></div><span class="bar-sep"></span><div ref="windowBar" class="bar-group bar-windows" role="group" aria-label="Windows"></div><div class="bar-tray"><span v-if="dirty" class="tray-dirty" title="The project has changes that are not saved">● Unsaved</span><button :aria-pressed="showTimeline" :disabled="dragging" @click="showTimeline = !showTimeline">History</button><button class="fullscreen-toggle" :aria-pressed="fullScreen" :title="fullScreen ? 'Leave full screen (Esc)' : 'Show the studio full screen'" @click="toggleFullScreen">{{ fullScreen ? 'Exit full screen' : 'Full screen' }}</button></div></header>
    <div v-show="perspective === 'room'" class="desk-host" data-testid="room-desk"><BenchDesk ref="roomDesk" storage-key="room" :windows="ROOM_WINDOWS" dock :taskbar-target="windowBar" :taskbar-active="perspective === 'room'">
      <template #background="{insets}"><main class="room-stage" :class="{placing:placementType || furniturePlacing || pointerFurniture || pastePlacing}" :style="{left:insets.left+'px',right:insets.right+'px'}"><nav aria-label="View"><button v-for="mode in ['3D','Plan']" :aria-pressed="view === mode" :class="{ selected: view === mode }" @click="view = mode">{{ mode }}</button><button :disabled="dragging" @click="resetView">Fit room</button><button :disabled="editingLocked || !history.past.length" @click="travel('undo')">Undo</button><button :disabled="editingLocked || !history.future.length" @click="travel('redo')">Redo</button><button class="lock-toggle" :class="{ locked: wallsLocked }" :aria-pressed="wallsLocked" :disabled="editingLocked || !valid" :title="wallsLocked ? 'Unlock walls to edit the room shell again' : 'Lock walls to edit only the interior'" @click="toggleWallLock">{{ wallsLocked ? 'Unlock walls' : 'Lock walls' }}</button><span class="room-mode" data-testid="room-mode" role="status">{{ wallsLocked ? 'Interior editing · walls locked' : 'Wall editing' }}</span></nav><FurnitureToolbar :selection-count="furnitureSelection.length" :clipboard-count="clipboard?.items.length ?? 0" :furniture-count="project.room.furniture.length" :paste-placing="pastePlacing" :inspecting="!!inspectingId" :disabled="editingLocked" @cut="cutSelection" @copy="copySelection" @paste="pasteClipboard()" @paste-place="startPastePlacing" @duplicate="duplicateSelection" @remove="deleteSelection" @select-all="selectAllFurniture" @rotate="rotateSelection" @zoom="zoomToSelection" @inspect="toggleInspection" @arrange="autoArrangeFurniture"/><div ref="viewport" class="viewport" aria-label="Interactive room preview"></div><FurnitureInspector v-if="inspectedItem" :item="inspectedItem" :designs="project.furnitureDesigns ?? []" :clearance="selectedClearance" @rotate="rotateSelection" @zoom="frameInspected" @exit="exitInspection"/><div v-if="furnitureInsertion" data-testid="furniture-insertion-preview" class="preview-status" :class="{invalid:!furnitureInsertion.valid}" role="status">{{ furnitureInsertion.valid ? (pointerFurniture ? 'Furniture placement valid · click in Plan to place' : 'Furniture placement valid · confirm in Furniture library') : furnitureInsertion.error }}</div><div v-if="placementPreview" data-testid="placement-preview" class="preview-status" :class="{invalid:!placementPreview.valid}" role="status">{{ placementPreview.valid ? 'Placement valid · offset '+placementPreview.opening.offset.toFixed(2)+' m · '+placementPreview.opening.width.toFixed(2)+' × '+placementPreview.opening.height.toFixed(2)+' m' : placementPreview.error }}</div><div v-if="view === 'Plan'" class="dimension-layer"><svg class="dimension-lines" aria-label="Wall dimension lines"><g v-for="label in wallLabels" :key="label.id" v-show="label.visible" :data-wall-dimension="label.id"><line v-for="(segment,n) in label.segments" :key="n" v-bind="segment" /></g><g v-for="label in chainLabels" :key="label.id" v-show="label.visible" :data-chain-dimension="label.id"><line v-for="(segment,n) in label.segments" :key="n" v-bind="segment" /></g></svg><span v-for="label in chainLabels" :key="label.id" v-show="label.visible" class="wall-dimension chain-dimension" :class="{aperture:label.kind === 'opening'}" :data-chain-kind="label.kind" :title="label.kind === 'opening' ? 'Opening width' : 'Wall gap'" :style="{left:label.x+'px',top:label.y+'px'}">{{ label.length }} m</span><span v-for="label in wallLabels" :key="label.id" v-show="label.visible" class="wall-dimension" :data-wall="label.id" :style="{left:label.x+'px',top:label.y+'px'}">{{ label.length }} m</span></div><div v-if="view === 'Plan' && !wallsLocked" class="corner-layer"><button v-for="handle in cornerHandles" :key="handle.id" v-show="handle.visible" class="corner-handle" :class="{ dragging }" :style="{left:handle.x+'px',top:handle.y+'px'}" :data-corner="handle.id" :aria-label="handle.label+' drag handle'" :title="handle.title" :disabled="busy || !valid" @pointerdown="startDrag(handle.id,$event)" @pointermove="dragCorner" @pointerup="finishDrag" @pointercancel="cancelDrag" @lostpointercapture="cancelDrag"></button></div><section class="surface-panel" aria-label="Room surfaces"><p class="eyebrow">ROOM SURFACES</p><button v-for="surface in surfaces" :key="surface.id" :aria-pressed="selected === surface.id" :class="{ selected: selected === surface.id }" @click="selectSurface(surface.id)">{{ surface.label }}</button><p role="status">{{ selectedLabel }}</p><small v-if="hiddenSelection">Hidden in cutaway · switch to Plan</small><button :disabled="!selected" @click="selected = null">Clear selection</button></section><div class="canvas-note">{{ view === '3D' ? 'ORBIT TO EXPLORE' : 'ORTHOGRAPHIC PLAN · NORTH UP' }}<span>{{ view === '3D' ? 'Click to select · Drag to orbit · Scroll to zoom' : (wallsLocked ? 'Walls locked · Drag cabinets to arrange · Scroll to zoom' : 'Drag a corner to edit · Drag elsewhere to pan · Scroll to zoom') }}</span></div></main></template>
      <template #furniture><fieldset class="furniture-editor" :disabled="editingLocked"><legend>Furniture</legend><div class="opening-actions"><button @click="addCabinet()">Add cabinet</button><button :aria-pressed="furniturePlacing" @click="placeCabinet">Place cabinet</button></div><p v-if="furniturePlacing" class="placement-note">Click inside the room to place a cabinet.<button @click="furniturePlacing=false">Cancel cabinet placement</button></p><FurnitureClearance v-model:overlay="showClearanceOverlay" :preview-active="!!furnitureInsertion" :report="selectedClearance" :settings="clearanceOptions" @change="updateClearance"/><FurnitureArrange v-model:connect="connectFurniture" v-model:options="arrangeOptions"/><ClearanceReview :review="clearanceReview" :selected-id="selectedFurniture?.id" @select="inspectClearance" @export="exportClearance"/><FurnitureLibrary :item="selectedCabinet" :presets="project.furniturePresets??[]" :preview="furnitureInsertion" :guide-snapping="guideSnapping" @rotation="setPreviewRotation" @rotate="rotatePreview" @guide-snap="setGuideSnapping" @preview="previewFurniture" @pointer="startPointerFurniture" @confirm="confirmFurnitureInsertion" @cancel="cancelFurnitureInsertion" @duplicate="duplicateFurniture" @save="saveFurniture" @add="addFurniturePreset" @remove="deleteFurniturePreset"/><label v-if="project.room.furniture.length">Furniture item<select :value="selectedFurniture?.id || ''" aria-label="Furniture item" @change="selected=$event.target.value ? 'furniture:'+$event.target.value : null"><option value="">Choose furniture</option><option v-for="item in project.room.furniture" :key="item.id" :value="item.id">{{ item.name }}</option></select></label><template v-if="furnitureDraft?.type==='design'"><label>Placed design name<input v-model="furnitureDraft.name" maxlength="60"></label><label v-for="field in ['x','z','rotation']" :key="field">Placed design {{ field }}<input v-model.number="furnitureDraft[field]" type="number" :step="field==='rotation'?15:0.01"></label><p class="hint">{{ (furnitureDraft.width*1000).toFixed(0) }} × {{ (furnitureDraft.depth*1000).toFixed(0) }} × {{ (furnitureDraft.height*1000).toFixed(0) }} mm from the design. Edit the design to change its pieces.</p><button @click="applyCabinet">Apply placed design</button><button @click="openDesign(furnitureDraft.designId)">Edit design</button><button @click="removeCabinet">Remove from room</button></template><template v-if="furnitureDraft?.type==='cabinet'"><label>Cabinet name<input v-model="furnitureDraft.name" maxlength="60"></label><label v-for="field in ['width','depth','height','x','z','rotation','thickness','shelves']" :key="field">Cabinet {{ field }}<input v-model.number="furnitureDraft[field]" type="number" :step="field === 'shelves' ? 1 : field === 'thickness' ? 0.001 : 0.01"></label><label>Cabinet columns<input :value="furnitureDraft.columns??1" type="number" min="1" max="8" step="1" @input="setCabinetColumns($event.target.value)"></label><EdgeBandingSettings :draft="furnitureDraft" :applied="selectedFurniture" @update="furnitureDraft=$event"/><ShelfJoinerySettings :draft="furnitureDraft" :applied="selectedFurniture" @update="furnitureDraft=$event"/><CabinetFrontSettings :draft="furnitureDraft" @update="furnitureDraft=$event"/><CabinetWidthSettings :draft="furnitureDraft" @update="furnitureDraft=$event"/><CabinetShelfSettings :draft="furnitureDraft" @update="furnitureDraft=$event"/><CabinetCompartments :item="selectedFurniture"/><button @click="exportCabinetElevation">Export cabinet elevation SVG</button><CabinetDrawingSheets @export="exportCabinetSheets"/><label>Cabinet material<select v-model="furnitureDraft.material" aria-label="Cabinet material"><optgroup v-for="group in MATERIAL_GROUPS" :key="group.label" :label="group.label"><option v-for="id in group.ids" :key="id" :value="id">{{ MATERIALS[id].label }}</option></optgroup></select></label><label>Hardware metal<select :value="furnitureDraft.hardwareMaterial ?? 'steel'" aria-label="Hardware metal" @change="furnitureDraft.hardwareMaterial=$event.target.value"><option v-for="id in METAL_IDS" :key="id" :value="id">{{ MATERIALS[id].label }}</option></select></label><label>Screws per joint<input :value="furnitureDraft.screwsPerJoint ?? 4" type="number" min="0" max="20" step="1" @input="furnitureDraft.screwsPerJoint=$event.target.value===''?NaN:Number($event.target.value)"></label><label>Length allowance mm<input v-model.number="furnitureDraft.lengthAllowanceMm" type="number" min="0" max="50" step="0.1"></label><label>Width allowance mm<input v-model.number="furnitureDraft.widthAllowanceMm" type="number" min="0" max="50" step="0.1"></label><label>Stock length mm<input v-model.number="furnitureDraft.stockLengthMm" type="number" min="100" max="10000"></label><label>Stock width mm<input v-model.number="furnitureDraft.stockWidthMm" type="number" min="100" max="10000"></label><label>Stock edge trim mm<input v-model.number="furnitureDraft.stockTrimMm" type="number" min="0" max="100" step="0.1"></label><label>Stock kerf mm<input v-model.number="furnitureDraft.stockKerfMm" type="number" min="0" max="20" step="0.1"></label><label class="snap-toggle"><input v-model="furnitureDraft.stockAllowRotation" type="checkbox" aria-label="Allow panel rotation"> Allow panel rotation</label><button @click="applyCabinet">Apply cabinet</button><button @click="removeCabinet">Remove cabinet</button><CabinetArrange :item="selectedFurniture" :furniture="project.room.furniture" @arrange="arrangeSelected" @group="arrangeGroup"/><WorkshopProfiles :profiles="project.workshopProfiles" :furniture="project.room.furniture" @save="saveWorkshop" @apply="useWorkshop" @remove="deleteWorkshop" @export="exportWorkshops" @import="importWorkshops" @bulk="useWorkshopBulk"/><section v-if="furnitureBom" class="cabinet-bom" aria-label="Cabinet panel bill of materials"><p class="eyebrow">DESIGN PANEL LIST · MM</p><p class="bom-row" data-testid="cabinet-hardware"><strong>Hardware · {{ furnitureBom.hardware.materialLabel }}</strong><span>{{ furnitureBom.handles }} handles · {{ furnitureBom.hardware.screws }} × {{ furnitureBom.hardware.screwSpec }} (estimate: {{ furnitureBom.hardware.screwsPerJoint }} per joint × {{ furnitureBom.hardware.joints }} joints)</span><small v-if="furnitureMass">Estimated weight {{ furnitureMass.totalKg.toFixed(1) }} kg · wood {{ furnitureMass.panelsKg.toFixed(1) }} kg · metal {{ (furnitureMass.screwsKg+furnitureMass.handlesKg).toFixed(2) }} kg (typical densities)</small></p><div v-for="row in furnitureBom.rows" :key="row.part" class="bom-row"><strong>{{ row.label }} × {{ row.quantity }}</strong><span>Design span {{ (row.length*1000).toFixed(1) }} × {{ (row.width*1000).toFixed(1) }} × {{ (row.thickness*1000).toFixed(1) }}</span><small>{{ row.material }} · finished {{ (row.finishedLength*1000).toFixed(1) }} × {{ (row.finishedWidth*1000).toFixed(1) }} mm · core {{ (row.coreLength*1000).toFixed(1) }} × {{ (row.coreWidth*1000).toFixed(1) }} mm · band {{ row.bandLengthMetres.toFixed(3) }} m · joinery +{{ row.joineryLengthMm }} mm · blank {{ (row.blankLength*1000).toFixed(1) }} × {{ (row.blankWidth*1000).toFixed(1) }} mm · {{ row.stockFit }}</small></div><p data-testid="stock-fit" role="status">{{ furnitureBom.allFit ? 'Each panel fits the usable stock sheet' : furnitureBom.oversizeCount+' panels exceed the usable stock sheet' }}</p><p>{{ furnitureBom.panelCount }} panels · {{ furnitureBom.handles }} handles<br>{{ furnitureBom.area.toFixed(3) }} m² · {{ furnitureBom.volume.toFixed(4) }} m³</p><button @click="exportCabinetBom">Export panel CSV</button><CabinetLayout v-if="furnitureLayout" :layout="furnitureLayout"/><button @click="exportNesting">Export layout CSV</button><p class="hint">Blank dimensions add the entered total allowance once per axis. Blank area {{ furnitureBom.blankArea.toFixed(3) }} m². Fit checks include edge trim on all four sides and optional 90° rotation. Layout uses a guillotine heuristic and is not guaranteed optimal. Kerf, edging, grain, joinery and machine instructions still need workshop review.</p></section></template><p class="hint">Dimensions and position are metres; rotation is degrees. Drag a cabinet body in Plan to move it. Body footprints must fit inside the room and cannot overlap.</p></fieldset></template>
      <template #room><p class="eyebrow">ROOM DIMENSIONS · METRES{{ wallsLocked ? ' · LOCKED' : '' }}</p><label v-for="key in ['width','depth']" :key="key">{{ key }}<input v-model.number="dimensionDraft[key]" @change="setDimension(key,$event)" type="number" min="0.1" max="1000" step="0.1" :disabled="editingLocked || wallsLocked"></label><label>Height<input v-model.number="dimensionDraft.height" @change="setHeight" type="number" min="0.1" max="1000" step="0.1" :disabled="editingLocked || wallsLocked"></label><div class="divider"></div><label class="snap-toggle"><input v-model="snapEnabled" type="checkbox" :disabled="editingLocked"> Snap endpoints to grid</label><label>Grid spacing<select v-model.number="gridSpacing" :disabled="editingLocked"><option :value=".1">0.10 m</option><option :value=".25">0.25 m</option><option :value=".5">0.50 m</option><option :value="1">1.00 m</option></select></label><p class="hint">Large rooms show a coarser grid; snapping uses the selected spacing.</p><p v-if="selectedWall && wallsLocked" class="lock-notice" role="note">{{ LOCKED_MESSAGE }}. Endpoints, construction and openings are fixed while walls are locked.</p><fieldset v-if="selectedWall && !wallsLocked" class="endpoint-editor" :disabled="editingLocked"><legend>{{ selectedLabel }} endpoints</legend><template v-for="(point,n) in endpointDraft" :key="point.corner"><p class="endpoint-corner">{{ n === 0 ? 'A' : 'B' }} · {{ cornerLabel(point.corner) }}</p><div class="endpoint-row" :data-corner="point.corner"><label v-for="axis in ['x','z']" :key="axis">{{ n === 0 ? 'A' : 'B' }} {{ axis.toUpperCase() }}<input v-model.number="point[axis]" type="number" min="-1000" max="1000" :step="gridSpacing"></label></div></template><button @click="applyEndpoints">Apply endpoints</button><p class="hint">Adjoining walls share each corner. Rooms stay convex.</p></fieldset><fieldset v-if="selectedWall && !wallsLocked" class="endpoint-editor wall-construction" :disabled="editingLocked"><legend>{{ selectedLabel }} construction</legend><div class="endpoint-row"><label>Move distance (m, + outward)<input v-model.number="wallMoveDistance" type="number" min="-1000" max="1000" :step="gridSpacing"></label></div><div class="opening-actions"><button @click="moveSelectedWall">Move wall</button><button :disabled="wallCount <= MIN_WALLS" @click="removeSelectedWall">Remove wall</button></div><template v-for="(segment,n) in replacementDraft" :key="n"><p class="endpoint-corner">Replacement {{ n+1 }} · A → B</p><div v-for="end in ['a','b']" :key="end" class="endpoint-row"><label v-for="axis in ['x','z']" :key="axis">R{{ n+1 }} {{ end.toUpperCase() }} {{ axis.toUpperCase() }}<input v-model.number="segment[end][axis]" type="number" min="-1000" max="1000" :step="gridSpacing"></label></div></template><div class="opening-actions"><button :disabled="replacementDraft.length >= 8" @click="addReplacementSegment">Add segment</button><button :disabled="replacementDraft.length <= 1" @click="replacementDraft.pop()">Remove segment</button></div><button @click="replaceSelectedWall">Replace wall</button><p class="hint">Each replacement is a wall line through A and B, listed clockwise. Moving, replacing or removing a wall keeps the other walls' lines; the corners they share are recomputed. Openings keep their position; openings on a removed wall are removed. Parallel neighbours cannot meet, so walls of a rectangle cannot be removed.</p></fieldset><fieldset v-if="selectedWall && !wallsLocked" class="opening-editor" :disabled="editingLocked"><legend>Wall openings</legend><div class="opening-actions"><button @click="addOpening('door')">Add door</button><button @click="addOpening('window')">Add window</button></div><div class="opening-actions"><button :aria-pressed="placementType === 'door'" @click="beginPlacement('door')">Place door</button><button :aria-pressed="placementType === 'window'" @click="beginPlacement('window')">Place window</button></div><div v-if="placementType && placementSize" class="placement-size"><p class="eyebrow">PLACEMENT SIZE · METRES</p><label v-for="field in ['width','height','sill']" :key="field">Placement {{ field }}<input v-model.number="placementSize[field]" type="number" min="0" step="0.01" :disabled="field === 'sill' && placementType === 'door'"></label><template v-if="placementType === 'door'"><label>Placement hinge<select v-model="placementSize.hinge" aria-label="Placement hinge"><option value="A">A end</option><option value="B">B end</option></select></label><label>Placement swing<select v-model="placementSize.swing" aria-label="Placement swing"><option value="in">Inward</option><option value="out">Outward</option></select></label></template><label>Preset name<input v-model="presetName" maxlength="60"></label><button @click="saveOpeningPreset">Save opening preset</button><label v-if="savedPresets.length">Saved opening preset<select v-model="presetId" aria-label="Saved opening preset"><option :value="null">Choose preset</option><option v-for="preset in savedPresets" :key="preset.id" :value="preset.id">{{ preset.name }}</option></select></label><div v-if="savedPresets.length" class="opening-actions"><button :disabled="!presetId" @click="useOpeningPreset">Use preset</button><button :disabled="!presetId" @click="removeOpeningPreset">Delete preset</button></div></div><p v-if="placementType" class="placement-note">Click the selected wall to place a {{ placementType }}.<button @click="placementType=null">Cancel placement</button></p><label v-if="wallOpenings.length">Opening<select v-model="selectedOpeningId" aria-label="Opening"><option :value="null">Choose opening</option><option v-for="(opening,index) in wallOpenings" :key="opening.id" :value="opening.id">{{ opening.type }} {{ index+1 }}</option></select></label><template v-if="openingDraft"><label v-for="field in ['offset','width','height','sill']" :key="field">Opening {{ field }}<input v-model.number="openingDraft[field]" type="number" min="0" step="0.05" :disabled="field === 'sill' && openingDraft.type === 'door'"></label><template v-if="openingDraft.type === 'door'"><label>Door hinge<select v-model="openingDraft.hinge" aria-label="Door hinge"><option value="A">A end</option><option value="B">B end</option></select></label><label>Door swing<select v-model="openingDraft.swing" aria-label="Door swing"><option value="in">Inward</option><option value="out">Outward</option></select></label></template><button @click="saveOpening">Apply opening</button><button @click="deleteOpening">Remove opening</button></template><p class="hint">Place mode uses the entered size and snaps only the offset from A. Door hinge and swing settings apply in both views. Offset follows A → B. Keep 0.05 m at wall ends, above openings and between openings. Plan cuts walls at up to 1.2 m.</p></fieldset><p v-if="!valid" class="hint">Enter a name and dimensions from 0.1 to 1000 m.</p><button class="calculate" :disabled="editingLocked || !valid" @click="action('calculate')">Calculate geometry ↗</button><dl v-if="metrics"><div><dt>Floor area</dt><dd>{{ metrics.area.toFixed(2) }} m²</dd></div><div><dt>Volume</dt><dd>{{ metrics.volume.toFixed(2) }} m³</dd></div><div><dt>Perimeter</dt><dd>{{ metrics.perimeter.toFixed(2) }} m</dd></div></dl><p class="hint">Width and depth scale the outline. Select a wall to edit its endpoints or construction. Walls facing the 3D camera are cut away.</p></template>
      <template #project><section v-if="recoveryDrafts.length" class="recovery-panel" aria-label="Local recovery"><label>Recovery draft<select v-model="recoveryChoice" aria-label="Recovery draft"><option v-for="draft in recoveryDrafts" :key="draft.key" :value="draft.key">{{ draft.project.name }} · {{ draft.updatedAt }}</option></select></label><button :disabled="editingLocked" @click="recoverDraft">Recover draft</button><button :disabled="editingLocked" @click="discardDraft">Discard draft</button></section><p v-if="recoveryNotice" class="hint" role="status">{{ recoveryNotice }}</p><label>SVG print scale<select v-model.number="printScale" aria-label="SVG print scale"><option :value="0">Unscaled vector</option><option v-for="scale in [20,50,100,200]" :key="scale" :value="scale">1:{{ scale }}</option></select></label><p v-if="printScale" class="hint">SVG exports in physical millimetres. Print at 100% without fit-to-page.</p><label>SVG paper size<select v-model="paperSize" aria-label="SVG paper size"><option value="auto">Drawing bounds</option><option value="A4">A4</option><option value="A3">A3</option></select></label><label v-if="paperSize !== 'auto'">Sheet orientation<select v-model="paperOrientation" aria-label="Sheet orientation"><option value="landscape">Landscape</option><option value="portrait">Portrait</option></select></label><div v-if="sheetFeedback" class="sheet-feedback" :class="{invalid:!sheetFeedback.fits}" data-testid="sheet-fit" role="status"><template v-if="sheetFeedback.error">{{ sheetFeedback.error }}</template><template v-else>{{ sheetFeedback.fits ? 'Plan fits' : 'Plan does not fit' }} {{ paperSize }} {{ paperOrientation }} at 1:{{ printScale }}.<small>Drawing {{ sheetFeedback.requiredWidth.toFixed(1) }} × {{ sheetFeedback.requiredHeight.toFixed(1) }} mm; usable {{ sheetFeedback.availableWidth }} × {{ sheetFeedback.availableHeight }} mm.</small><button v-if="!sheetFeedback.fits && sheetFeedback.suggestedScale" @click="printScale=sheetFeedback.suggestedScale">Use suggested scale 1:{{ sheetFeedback.suggestedScale }}</button></template></div><label>Project name<input v-model="project.name" maxlength="120" :disabled="editingLocked"></label><div class="divider"></div><button :aria-expanded="showManufacturing" @click="showManufacturing=!showManufacturing">Project manufacturing</button><ManufacturingPanel v-if="showManufacturing" :furniture="project.room.furniture" :designs="project.furnitureDesigns ?? []" @export="exportManufacturing" @handoff="exportWorkshop" @job="exportJob" @verify="verifyJob" :manifest-result="manifestResult"/><WorkshopJobs v-if="showManufacturing" :project="project" @export="exportSavedJob" @record="exportJobRecord" @quote="exportQuote" @quote-print="exportQuotePrint"/></template>
    </BenchDesk></div>
    <KeepAlive><DesignerWorkspace v-if="perspective === 'designer'" :window-bar="windowBar" :designs="project.furnitureDesigns ?? []" :apply-designs="applyDesigns" :place-design="placeDesign" :placed-design-ids="project.room.furniture.filter(item=>item.type==='design').map(item=>item.designId)" :focus-design-id="focusDesignId" :cabinet="selectedFurniture ?? null" :workspace-key="draftKey+':'+restoreCount" :project-bytes="projectBytes" :max-bytes="MAX_PROJECT_BYTES" @message="message = $event" @active="designerActiveId = $event"/></KeepAlive>
    <TimelinePanel v-if="showTimeline" :timeline="timeline" :preview-seq="previewSeq" :focus="timelineFocus" :busy="busy || dragging" @preview="previewVersion" @exit="exitPreview" @restore="restoreVersion" @restore-element="restoreElement" @close="exitPreview(); showTimeline = false"/>
    <footer role="status" aria-live="polite"><span class="dot"></span><span>{{ busy ? 'Working…' : message }}</span><span class="version">FOUNDATION 01 · METRES</span></footer>
  </div>
</template>
