// Placing furniture designs in the room. A placed design is a room furniture
// item of type 'design' that refers to a project design. Its footprint (width ×
// depth, metres) and height come from the design's bounds and are re-derived on
// every validation, so editing the design updates the room (or is rejected if
// it would no longer fit). Position and rotation work like cabinets: (x, z) is
// the footprint centre, rotation is about the vertical axis, and the design's
// front (+z) faces the room at rotation 0.
import {designBounds} from './furniture-design.mjs';
const round=v=>Number(v.toFixed(6));
export function designFootprint(design) {
  const {size}=designBounds(design);
  if(!design.parts.length||size.x<10||size.y<10||size.z<10) throw Error(`Design “${design.name}” is too small to place; it needs at least 10 mm in every direction`);
  return {width:round(size.x/1000),depth:round(size.z/1000),height:round(size.y/1000)};
}
export function syncPlacedDesign(item,designsById) {
  const design=designsById.get(item?.designId);
  if(!design) throw Error(`Placed design “${item?.name??'?'}” refers to a design that does not exist; remove it from the room first`);
  return {...item,...designFootprint(design)};
}
export function placedDesignItem(design,{id,x=0,z=0}) {
  return {id,type:'design',name:design.name.slice(0,60),designId:design.id,...designFootprint(design),x,z,rotation:0};
}
export const isPlacedDesign=item=>item?.type==='design';
export const isCabinet=item=>item?.type==='cabinet';
