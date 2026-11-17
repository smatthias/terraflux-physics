// Desktop-style windows for the workshop benches: windows float over the 3D
// bench (the background, which always fills the desk), overlap in layers, move
// by their title bar, resize from any edge or corner, snap to the desk's halves,
// quarters or the whole desk (dragged to an edge or corner, as on a desktop),
// minimise to the taskbar, and stay reachable when the desk is resized.
// Pure functions over plain state, so they can be tested without a browser:
//   desk    {width, height}   (pixels, the area windows live in)
//   window  {id, x, y, w, h, z, minimized, snap, restore}
// `snap` is null or a zone ('left', 'right', 'max', 'top-left', …); `restore` is
// the rectangle a snapped window returns to.
export const MIN_SIZE=Object.freeze({w:220,h:120});
export const TITLE=32;          // title bar height: the part that must stay on the desk
export const EDGE=24;           // pointer distance from a desk edge that snaps
export const GAP=8;             // margin around snapped windows
export const ZONES=Object.freeze(['left','right','max','top-left','top-right','bottom-left','bottom-right']);

const clampRect=(r,desk)=>{
  const w=Math.max(MIN_SIZE.w,Math.min(r.w,desk.width)),h=Math.max(MIN_SIZE.h,Math.min(r.h,desk.height));
  // Keep the title bar on the desk: at least 60 px of it across, and all of it down.
  const x=Math.min(Math.max(r.x,60-w),desk.width-60),y=Math.min(Math.max(r.y,0),Math.max(0,desk.height-TITLE));
  return {x:Math.round(x),y:Math.round(y),w:Math.round(w),h:Math.round(h)};
};
// The rectangle of a snap zone.
export function snapRect(zone,desk) {
  const halfW=Math.round((desk.width-3*GAP)/2),halfH=Math.round((desk.height-3*GAP)/2),right=GAP*2+halfW,bottom=GAP*2+halfH;
  return {
    max:{x:GAP,y:GAP,w:desk.width-2*GAP,h:desk.height-2*GAP},
    left:{x:GAP,y:GAP,w:halfW,h:desk.height-2*GAP},right:{x:right,y:GAP,w:halfW,h:desk.height-2*GAP},
    'top-left':{x:GAP,y:GAP,w:halfW,h:halfH},'top-right':{x:right,y:GAP,w:halfW,h:halfH},
    'bottom-left':{x:GAP,y:bottom,w:halfW,h:halfH},'bottom-right':{x:right,y:bottom,w:halfW,h:halfH},
  }[zone]??null;
}
// The zone a pointer (desk coordinates) is over while dragging a window, or null.
export function snapZone(pointer,desk,edge=EDGE) {
  const left=pointer.x<=edge,right=pointer.x>=desk.width-edge,top=pointer.y<=edge,bottom=pointer.y>=desk.height-edge;
  if(top&&left) return 'top-left';if(top&&right) return 'top-right';
  if(bottom&&left) return 'bottom-left';if(bottom&&right) return 'bottom-right';
  if(top) return 'max';if(left) return 'left';if(right) return 'right';
  return null;
}

// ---- State
// `defs`: [{id, rect(desk) → {x,y,w,h}, minimized?}] in back-to-front order;
// `minimized` may be a function of the desk (e.g. start minimised on small screens).
export function createDesk(defs,desk,saved=null) {
  const windows=defs.map((d,i)=>{
    const stored=saved?.find(s=>s.id===d.id);
    const minimized=typeof d.minimized==='function'?d.minimized(desk):!!d.minimized;
    const base=stored?{...stored}:{id:d.id,...d.rect(desk),z:i+1,minimized,snap:null,restore:null};
    return place(base,desk);
  });
  return normalise(windows);
}
const place=(win,desk)=>win.snap?{...win,...snapRect(win.snap,desk)}:{...win,...clampRect(win,desk)};
// z from 1 upward, in the current order.
function normalise(windows) {
  const order=[...windows].sort((a,b)=>a.z-b.z).map(w=>w.id);
  return windows.map(w=>({...w,z:order.indexOf(w.id)+1}));
}
const update=(windows,id,change)=>windows.map(w=>w.id===id?{...w,...change(w)}:w);
const top=windows=>Math.max(0,...windows.map(w=>w.z));
export const topWindow=windows=>[...windows].filter(w=>!w.minimized).sort((a,b)=>b.z-a.z)[0]?.id??null;
// Layers: bring to the front (also un-minimises) or send to the back.
export const focusWindow=(windows,id)=>normalise(update(windows,id,()=>({z:top(windows)+1,minimized:false})));
export const sendToBack=(windows,id)=>normalise(update(windows,id,()=>({z:0})));
export const minimizeWindow=(windows,id)=>update(windows,id,()=>({minimized:true}));
// Moving a snapped window first gives it back its own size.
export function moveWindow(windows,id,to,desk) {
  return update(windows,id,w=>({...clampRect({...(w.snap&&w.restore?w.restore:w),x:to.x,y:to.y},desk),snap:null,restore:null}));
}
export function resizeWindow(windows,id,rect,desk) {
  return update(windows,id,()=>({...clampRect(rect,desk),snap:null,restore:null}));
}
// Resizing from an edge or corner: `edges` is a string of n, s, e, w.
export function resizeBy(rect,edges,dx,dy) {
  let {x,y,w,h}=rect;
  if(edges.includes('e')) w+=dx;
  if(edges.includes('s')) h+=dy;
  if(edges.includes('w')) {const nw=Math.max(MIN_SIZE.w,w-dx);x+=w-nw;w=nw;}
  if(edges.includes('n')) {const nh=Math.max(MIN_SIZE.h,h-dy);y+=h-nh;h=nh;}
  return {x,y,w:Math.max(MIN_SIZE.w,w),h:Math.max(MIN_SIZE.h,h)};
}
export function snapWindow(windows,id,zone,desk) {
  if(!ZONES.includes(zone)) return windows;
  return focusWindow(update(windows,id,w=>({...snapRect(zone,desk),snap:zone,restore:w.snap?w.restore:{x:w.x,y:w.y,w:w.w,h:w.h}})),id);
}
export function toggleMaximize(windows,id,desk) {
  const win=windows.find(w=>w.id===id);
  if(win?.snap==='max') return update(windows,id,w=>({...clampRect(w.restore??w,desk),snap:null,restore:null}));
  return snapWindow(windows,id,'max',desk);
}
// Docked windows: a shown window standing against the left or right edge of the
// desk, at least 40 % of its height and at most 40 % of its width, keeps that
// strip for itself, so the canvas behind (the desk's background) shrinks to the
// free middle instead of being covered. Returns the strips' widths in pixels
// (0 when nothing is docked, and both 0 when they would leave the canvas less
// than MIN_CANVAS wide). Other windows float over the canvas.
export const DOCK_EDGE=16,MIN_CANVAS=240;
export function dockInsets(windows,desk) {
  let left=0,right=0;
  for(const w of windows) {
    if(w.minimized||w.h<desk.height*.4||w.w>desk.width*.4) continue;
    if(w.x<=DOCK_EDGE) left=Math.max(left,w.x+w.w+GAP);
    else if(w.x+w.w>=desk.width-DOCK_EDGE) right=Math.max(right,desk.width-w.x+GAP);
  }
  return left+right>desk.width-MIN_CANVAS?{left:0,right:0}:{left,right};
}
// When the desk changes size: snapped windows take their zone again, the others stay reachable.
export const fitDesk=(windows,desk)=>windows.map(w=>place(w,desk));

// ---- Keeping a layout (per bench, in this browser)
export function readLayout(text,ids) {
  try {
    const value=JSON.parse(text);
    if(value?.version!==1||!Array.isArray(value.windows)) return null;
    const ok=w=>ids.includes(w?.id)&&['x','y','w','h','z'].every(k=>Number.isFinite(w[k]))&&typeof w.minimized==='boolean'&&(w.snap===null||ZONES.includes(w.snap));
    const windows=value.windows.filter(ok).map(w=>({id:w.id,x:w.x,y:w.y,w:w.w,h:w.h,z:w.z,minimized:w.minimized,snap:w.snap,restore:w.restore&&['x','y','w','h'].every(k=>Number.isFinite(w.restore[k]))?{x:w.restore.x,y:w.restore.y,w:w.restore.w,h:w.restore.h}:null}));
    return windows.length?windows:null;
  } catch {return null;}
}
export const writeLayout=windows=>JSON.stringify({version:1,windows});
