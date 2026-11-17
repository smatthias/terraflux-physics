// Designer-only geometry for panels and machining (the sheet frame, feature
// boxes, drill axes and solid material). Kept out of furniture-design.mjs so
// loading a project does not carry it; the designer, CNC and CAM modules use it.
import {PANEL_ORIENTATIONS,SECTION_AXES,partBox} from './furniture-design.mjs';

// Sheet frame: u along the length, v along the width and n through the thickness
// from face B (n = 0) to face A (n = thickness), mapped to the piece's own frame.
export function sheetPoint(part,u,v,n) {
  const axes=PANEL_ORIENTATIONS[part.orientation],point={x:part.x,y:part.y,z:part.z};
  point[axes.u]+=u;point[axes.v]+=v;point[axes.n]+=n;
  return point;
}
// The material a feature removes, as ranges in the sheet frame (drills use their bounding square).
export function featureSheetBox(part,feature) {
  const r=feature.diameter/2;
  if(feature.face==='A'||feature.face==='B') {
    const [u,v]=feature.type==='drill'?[[feature.u-r,feature.u+r],[feature.v-r,feature.v+r]]:[[feature.u,feature.u+feature.length],[feature.v,feature.v+feature.width]];
    return {u,v,n:feature.face==='A'?[part.thickness-feature.depth,part.thickness]:[0,feature.depth]};
  }
  const n=[feature.t-r,feature.t+r];
  if(feature.face==='u0') return {u:[0,feature.depth],v:[feature.v-r,feature.v+r],n};
  if(feature.face==='u1') return {u:[part.length-feature.depth,part.length],v:[feature.v-r,feature.v+r],n};
  if(feature.face==='v0') return {u:[feature.u-r,feature.u+r],v:[0,feature.depth],n};
  return {u:[feature.u-r,feature.u+r],v:[part.width-feature.depth,part.width],n};
}
// Sheet axis a drill runs along: through the thickness on faces, along u or v on edges.
export const drillSheetAxis=feature=>feature.face==='A'||feature.face==='B'?'n':feature.face[0];
export function drillAxis(part,feature) {
  const sheet=drillSheetAxis(feature),axes=PANEL_ORIENTATIONS[part.orientation];
  return sheet==='n'?axes.n:axes[sheet];
}
// Maps sheet coordinates on a panel face to world points. `depth` goes into the material.
export function faceFrame(part,face) {
  const axes=PANEL_ORIENTATIONS[part.orientation],origin={x:part.x,y:part.y,z:part.z};
  const sign=face==='A'?1:-1,surface=face==='A'?part.thickness:0;
  return (u,v,depth=0)=>{
    const point={...origin};
    point[axes.u]+=u;point[axes.v]+=v;point[axes.n]+=surface-sign*depth;
    return point;
  };
}
// Box (in the piece's own frame) of the material a feature removes.
export function featureBox(part,feature) {
  const box=featureSheetBox(part,feature),a=sheetPoint(part,box.u[0],box.v[0],box.n[0]),b=sheetPoint(part,box.u[1],box.v[1],box.n[1]),min={},max={};
  for(const axis of ['x','y','z']) {min[axis]=Math.min(a[axis],b[axis]);max[axis]=Math.max(a[axis],b[axis]);}
  return {min,max};
}
// How far a feature can go: the thickness on faces, the length or width from an edge.
export const featureDepthLimit=(part,feature)=>feature.face==='A'||feature.face==='B'?part.thickness:feature.face[0]==='u'?part.length:part.width;
export const isThrough=(part,feature)=>feature.depth>=featureDepthLimit(part,feature)-1e-9;
// The piece's solid material as boxes in its own frame. An L-angle is its two
// legs (corner at the section minimum, as drawn), so its inside corner is free.
export function solidBoxes(part) {
  const box=partBox(part);
  if(part.shape!=='angle') return [{min:box.min,max:box.max}];
  const [a,b]=SECTION_AXES[part.axis],flat={min:{...box.min},max:{...box.max}},upright={min:{...box.min},max:{...box.max}};
  flat.max[b]=box.min[b]+part.wall;upright.max[a]=box.min[a]+part.wall;
  return [flat,upright];
}
// Open space inside a tube, which can receive another piece (a rod in a tube).
export function hollowBox(part) {
  if(part.shape!=='tube'&&part.shape!=='box-tube') return null;
  const box=partBox(part),[a,b]=SECTION_AXES[part.axis],min={...box.min},max={...box.max};
  for(const axis of [a,b]) {min[axis]+=part.wall;max[axis]-=part.wall;}
  return {min,max};
}
