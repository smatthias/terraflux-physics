<script setup>
// A desk for a workshop bench (desk-layout.js): the background (the 3D bench)
// fills the whole desk and resizes with it; the bench's panels are windows on
// top that overlap in layers. Drag a title bar to move a window, or to an edge
// or corner to snap it to a half, a quarter or the whole desk; drag an edge or
// corner to resize; double-click the title to maximise. The taskbar lists the
// windows (click to bring one to the front, again to minimise), holds the
// bench's own buttons and its status, and resets the layout. With a title bar
// focused, arrow keys move the window and Shift+arrows resize it. The layout is
// kept in this browser per bench.
// The same desk hosts the studio's room editor and furniture designer:
//   dock           windows against the left or right edge keep that strip, and the
//                  background (the canvas) is given the free middle as the slot
//                  prop `insets` {left, right} (desk-layout.js dockInsets);
//   taskbarTarget  an element (the app's top bar) to put the window buttons in,
//                  instead of the desk's own taskbar; passing it at all (even
//                  while still null on the first render) removes that taskbar;
//   taskbarActive  whether those buttons are shown (the desk is on screen).
import {ref,watch,onMounted,onBeforeUnmount} from 'vue';
import * as L from './desk-layout.js';
import './desk.css';

const props=defineProps({storageKey:{type:String,required:true},windows:{type:Array,required:true},status:{type:String,default:''},
  dock:{type:Boolean,default:false},taskbarTarget:{type:null,default:undefined},taskbarActive:{type:Boolean,default:true}});
const external=props.taskbarTarget!==undefined,TASKBAR=external?0:40,key='terratec.desk.'+props.storageKey+'.v1';
const deskEl=ref(null),wins=ref([]),preview=ref(null),insets=ref({left:0,right:0});
let size={width:800,height:560},observer=null;
const ids=props.windows.map(w=>w.id),defs=props.windows;
const title=id=>defs.find(d=>d.id===id).title;
function measure() {
  const el=deskEl.value;if(!el) return size;
  return {width:Math.max(320,el.clientWidth||800),height:Math.max(240,(el.clientHeight||600)-TASKBAR)};
}
function load() {let text=null;try {text=localStorage.getItem(key);} catch {}return text?L.readLayout(text,ids):null;}
function save() {try {localStorage.setItem(key,L.writeLayout(wins.value));} catch {}}
// The canvas follows docked windows when they settle, not on every step of a drag.
function dockNow() {
  const next=props.dock?L.dockInsets(wins.value,size):{left:0,right:0};
  if(next.left!==insets.value.left||next.right!==insets.value.right) insets.value=next;
}
watch(wins,()=>{save();if(!drag) dockNow();},{deep:true});
onMounted(()=>{
  size=measure();wins.value=L.createDesk(defs,size,load());
  // A hidden desk (the other perspective) measures 0: keep its layout as it is.
  observer=new ResizeObserver(()=>{if(!deskEl.value?.clientWidth) return;size=measure();wins.value=L.fitDesk(wins.value,size);dockNow();});observer.observe(deskEl.value);
});
onBeforeUnmount(()=>observer?.disconnect());
const topId=()=>L.topWindow(wins.value);
const focus=id=>{if(topId()!==id||wins.value.find(w=>w.id===id).minimized) wins.value=L.focusWindow(wins.value,id);};
function taskbar(id) {
  const win=wins.value.find(w=>w.id===id);
  wins.value=!win.minimized&&topId()===id?L.minimizeWindow(wins.value,id):L.focusWindow(wins.value,id);
}
const reset=()=>{wins.value=L.createDesk(defs,size);};
const command=(id,action)=>{wins.value=action==='min'?L.minimizeWindow(wins.value,id):action==='max'?L.toggleMaximize(wins.value,id,size):action==='back'?L.sendToBack(wins.value,id):L.snapWindow(wins.value,id,action,size);};
// ---- Dragging: moving by the title bar (with snapping) and resizing by the edges.
let drag=null;
const local=event=>{const r=deskEl.value.getBoundingClientRect();return {x:event.clientX-r.left,y:event.clientY-r.top};};
function startMove(event,win) {
  if(event.button!==0||event.target?.tagName==='BUTTON') return;
  event.preventDefault();focus(win.id);event.currentTarget.setPointerCapture?.(event.pointerId);
  const p=local(event);drag={kind:'move',id:win.id,pointer:p,start:{x:win.x,y:win.y,w:win.w},snapped:!!win.snap,restoreW:win.restore?.w??win.w,moved:false};
}
function startResize(event,win,edges) {
  if(event.button!==0) return;
  event.preventDefault();event.stopPropagation();focus(win.id);event.currentTarget.setPointerCapture?.(event.pointerId);
  drag={kind:'resize',id:win.id,edges,pointer:local(event),start:{x:win.x,y:win.y,w:win.w,h:win.h}};
}
function dragMove(event) {
  if(!drag) return;
  const p=local(event),dx=p.x-drag.pointer.x,dy=p.y-drag.pointer.y;
  if(drag.kind==='resize') {wins.value=L.resizeWindow(wins.value,drag.id,L.resizeBy(drag.start,drag.edges,dx,dy),size);return;}
  if(!drag.moved&&Math.hypot(dx,dy)<4) return;
  // A snapped window leaves its snap at its own size, still under the pointer.
  if(!drag.moved&&drag.snapped) drag.start={...drag.start,x:drag.pointer.x-(drag.pointer.x-drag.start.x)*drag.restoreW/drag.start.w};
  drag.moved=true;
  wins.value=L.moveWindow(wins.value,drag.id,{x:drag.start.x+dx,y:drag.start.y+dy},size);
  const zone=L.snapZone(p,size);preview.value=zone?L.snapRect(zone,size):null;drag.zone=zone;
}
function dragEnd() {
  if(!drag) return;
  if(drag.kind==='move'&&drag.moved&&drag.zone) wins.value=L.snapWindow(wins.value,drag.id,drag.zone,size);
  drag=null;preview.value=null;dockNow();
}
// Keyboard on a focused title bar: arrows move by 10 px, Shift+arrows resize.
function titleKey(event,win) {
  const d={ArrowLeft:[-10,0],ArrowRight:[10,0],ArrowUp:[0,-10],ArrowDown:[0,10]}[event.key];
  if(event.key==='Enter') {event.preventDefault();event.stopPropagation();command(win.id,'max');return;}
  if(!d) return;
  event.preventDefault();event.stopPropagation();
  wins.value=event.shiftKey?L.resizeWindow(wins.value,win.id,{x:win.x,y:win.y,w:win.w+d[0],h:win.h+d[1]},size):L.moveWindow(wins.value,win.id,{x:win.x+d[0],y:win.y+d[1]},size);
}
const EDGES=['n','s','e','w','ne','nw','se','sw'];
defineExpose({focus});
</script>
<template>
  <div ref="deskEl" class="bench-desk" :class="{external}" @pointermove="dragMove" @pointerup="dragEnd" @pointercancel="dragEnd">
    <div class="desk-background"><slot name="background" :insets="insets"/></div>
    <div v-if="preview" class="snap-preview" :style="{left:preview.x+'px',top:preview.y+'px',width:preview.w+'px',height:preview.h+'px'}"></div>
    <section v-for="win in wins" v-show="!win.minimized" :key="win.id" class="desk-window" :class="{focused:win.id===topId(),snapped:!!win.snap}" :aria-label="title(win.id)"
      :style="{left:win.x+'px',top:win.y+'px',width:win.w+'px',height:win.h+'px',zIndex:win.z}" @pointerdown.capture="focus(win.id)">
      <header class="desk-title" tabindex="0" :aria-label="title(win.id)+' window · arrows move, Shift+arrows resize, Enter maximises'" @pointerdown="startMove($event,win)" @dblclick="command(win.id,'max')" @keydown="titleKey($event,win)">
        <span>{{ title(win.id) }}</span>
        <button :aria-label="'Snap '+title(win.id)+' left'" title="Snap to the left half" @click="command(win.id,'left')">◧</button>
        <button :aria-label="'Snap '+title(win.id)+' right'" title="Snap to the right half" @click="command(win.id,'right')">◨</button>
        <button :aria-label="'Send '+title(win.id)+' to the back'" title="Send behind the other windows" @click="command(win.id,'back')">⧉</button>
        <button :aria-label="'Minimise '+title(win.id)" title="Minimise to the taskbar" @click="command(win.id,'min')">–</button>
        <button :aria-label="(win.snap==='max'?'Restore ':'Maximise ')+title(win.id)" :title="win.snap==='max'?'Restore':'Maximise'" @click="command(win.id,'max')">{{ win.snap==='max'?'❐':'□' }}</button>
      </header>
      <div class="desk-body"><slot :name="win.id"/></div>
      <div v-for="edge in EDGES" :key="edge" :class="'desk-resize '+edge" @pointerdown="startResize($event,win,edge)"></div>
    </section>
    <Teleport v-if="external&&taskbarTarget&&taskbarActive" :to="taskbarTarget">
      <button v-for="win in wins" :key="win.id" :aria-pressed="!win.minimized" :class="{top:win.id===topId()}" :aria-label="'Window '+title(win.id)" :title="win.minimized?'Show '+title(win.id):win.id===topId()?'Minimise '+title(win.id):'Bring '+title(win.id)+' to the front'" @click="taskbar(win.id)">{{ title(win.id) }}</button>
      <button title="Put every window back where it started" @click="reset">Reset layout</button>
    </Teleport>
    <nav v-if="!external" class="desk-taskbar" aria-label="Taskbar">
      <slot name="start"/>
      <span class="taskbar-sep"></span>
      <button v-for="win in wins" :key="win.id" :aria-pressed="!win.minimized" :class="{top:win.id===topId()}" :aria-label="'Window '+title(win.id)" @click="taskbar(win.id)">{{ title(win.id) }}</button>
      <button title="Put every window back where it started" @click="reset">Reset layout</button>
      <p class="taskbar-status" role="status" data-testid="desk-status">{{ status }}</p>
    </nav>
  </div>
</template>
