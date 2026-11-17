// Editing toolkit for furniture selections: copy, cut, paste, duplicate, delete,
// rotate and group move. Every function takes a room and returns a new validated
// room (or clipboard); the input is never modified, so each call is one clean
// undo step for the caller.
import {validateFurniture,cabinetFootprint,footprintInside,footprintsOverlap} from './furniture.mjs';
import {snapPoint,rectangle} from './geometry.mjs';

export const CLIPBOARD_FORMAT='terratec-furniture-clipboard';
// The auto-paste search covers up to 40 grid steps in every direction (81² offsets).
export const MAX_PASTE_RINGS=40;
const round=value=>{const result=Number(value.toFixed(9));return result===0?0:result;};
const copyOf=value=>JSON.parse(JSON.stringify(value));

// Selected items in room order; unknown or missing IDs are an error.
function pick(room,ids) {
  if(!Array.isArray(ids) || !ids.length) throw Error('Select furniture first');
  const wanted=new Set(ids),items=room.furniture.filter(item=>wanted.has(item.id));
  if(items.length!==wanted.size) throw Error('Selected furniture no longer exists');
  return items;
}
// Bounds of the rotated body footprints; x/z is the centre used as the group pivot.
export function selectionBounds(items) {
  if(!items.length) throw Error('Select furniture first');
  const points=items.flatMap(cabinetFootprint),xs=points.map(p=>p.x),zs=points.map(p=>p.z);
  const minX=Math.min(...xs),maxX=Math.max(...xs),minZ=Math.min(...zs),maxZ=Math.max(...zs);
  return {minX,maxX,minZ,maxZ,x:(minX+maxX)/2,z:(minZ+maxZ)/2,width:maxX-minX,depth:maxZ-minZ,height:Math.max(...items.map(i=>i.height))};
}
export const roomSelectionBounds=(room,ids)=>selectionBounds(pick(room,ids));

export function copyFurniture(room,ids,mode='copy') {
  return {format:CLIPBOARD_FORMAT,version:1,mode,items:copyOf(pick(room,ids))};
}
export function deleteFurniture(room,ids) {
  const gone=new Set(pick(room,ids).map(item=>item.id));
  return {...room,furniture:room.furniture.filter(item=>!gone.has(item.id))};
}
export function cutFurniture(room,ids) {
  return {room:deleteFurniture(room,ids),clipboard:copyFurniture(room,ids,'cut')};
}

function checkClipboard(clipboard) {
  if(clipboard?.format!==CLIPBOARD_FORMAT || clipboard.version!==1 || !['copy','cut'].includes(clipboard.mode) ||
    !Array.isArray(clipboard.items) || !clipboard.items.length || clipboard.items.length>64)
    throw Error('The clipboard does not contain furniture');
}
const copyName=name=>name.endsWith(' (copy)')?name:name.slice(0,53)+' (copy)';

// Offsets ordered by distance, preferring down-right (+x, +z) like offset paste
// in office tools; (0, 0) comes first so cut → paste returns items in place.
function offsets(step,rings) {
  const list=[];
  for(let i=-rings;i<=rings;i++) for(let j=-rings;j<=rings;j++) list.push([i,j]);
  return list.sort((a,b)=>Math.hypot(...a)-Math.hypot(...b) || (b[0]+b[1])-(a[0]+a[1]) || b[0]-a[0] || b[1]-a[1])
    .map(([i,j])=>({x:round(i*step),z:round(j*step)}));
}

// Pastes clipboard items with fresh IDs. With `point`, the group centre is placed
// at that (snapped) point. Otherwise the original position is tried first, then
// the nearest free grid offset. Copies are named "… (copy)"; cut items keep names.
export function pasteFurniture(room,clipboard,{makeId,point=null,spacing=.25,snap=true}={}) {
  checkClipboard(clipboard);
  if(typeof makeId!=='function') throw Error('Paste needs an ID generator');
  const items=clipboard.items,ids=items.map(()=>makeId());
  const centre=selectionBounds(items),names=items.map(item=>clipboard.mode==='copy'?copyName(item.name):item.name);
  // Design parameters and mutual overlap are checked once, independent of position.
  validateFurniture(items.map((item,n)=>({...item,id:ids[n],x:item.x-centre.x,z:item.z-centre.z})),rectangle(1000,1000),Infinity);
  const place=delta=>items.map((item,n)=>({...item,id:ids[n],name:names[n],x:round(item.x+delta.x),z:round(item.z+delta.z)}));
  const commit=delta=>{
    const furniture=validateFurniture([...room.furniture,...place(delta)],room.outline,room.height);
    return {room:{...room,furniture},ids};
  };
  if(point) {
    const target=snapPoint(point,spacing,snap);
    try {return commit({x:target.x-centre.x,z:target.z-centre.z});}
    catch(error) {throw Error('Cannot paste here: '+error.message);}
  }
  const existing=room.furniture.map(cabinetFootprint),local=items.map(cabinetFootprint);
  const xs=room.outline.map(p=>p.x),zs=room.outline.map(p=>p.z),extent=Math.max(Math.max(...xs)-Math.min(...xs),Math.max(...zs)-Math.min(...zs));
  const step=snap?spacing:.1,rings=Math.min(MAX_PASTE_RINGS,Math.ceil(extent/step));
  for(const delta of offsets(step,rings)) {
    const moved=local.map(f=>f.map(p=>({x:p.x+delta.x,z:p.z+delta.z})));
    if(moved.every(f=>footprintInside(f,room.outline)) && !moved.some(f=>existing.some(o=>footprintsOverlap(f,o)))) return commit(delta);
  }
  throw Error(`No free space in the room for ${items.length===1?'the pasted item':items.length+' pasted items'}`);
}

export function duplicateFurniture(room,ids,options) {
  return pasteFurniture(room,copyFurniture(room,ids),options);
}

// Rotates the selection rigidly about the centre of its footprint bounds.
export function rotateFurniture(room,ids,degrees) {
  if(typeof degrees!=='number' || !Number.isFinite(degrees)) throw Error('Enter a finite rotation');
  const items=pick(room,ids),centre=selectionBounds(items),angle=degrees*Math.PI/180,c=Math.cos(angle),s=Math.sin(angle);
  const turned=new Map(items.map(item=>{
    const dx=item.x-centre.x,dz=item.z-centre.z,rotation=round((((item.rotation+degrees)%360)+360)%360);
    return [item.id,{...item,x:round(centre.x+c*dx+s*dz),z:round(centre.z-s*dx+c*dz),rotation:rotation>=360?0:rotation}];
  }));
  return {...room,furniture:validateFurniture(room.furniture.map(item=>turned.get(item.id)??item),room.outline,room.height)};
}

// Moves the selection so the anchor item's centre lands on `point` (snapped).
export function moveFurnitureGroup(room,ids,anchorId,point,spacing,snap=true) {
  const items=pick(room,ids),anchor=items.find(item=>item.id===anchorId);
  if(!anchor) throw Error('Drag a selected cabinet to move the selection');
  const target=snapPoint(point,spacing,snap),dx=target.x-anchor.x,dz=target.z-anchor.z,moving=new Set(items.map(i=>i.id));
  const furniture=room.furniture.map(item=>moving.has(item.id)?{...item,x:round(item.x+dx),z:round(item.z+dz)}:item);
  return {...room,furniture:validateFurniture(furniture,room.outline,room.height)};
}
