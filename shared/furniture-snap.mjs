// Magnetic snapping: a furniture item that comes near a wall or another item
// connects to it. Within `distance` (metres):
// - its rotation turns square to the nearest wall, or to a neighbouring item
//   (only when already within `angle` degrees, so a drag never flips it);
// - along each of its own axes it moves flush against a wall (keeping the wall
//   gap), flush against a neighbour's side, or into line with a neighbour's
//   edge, so corners meet corners. Each axis takes the nearest such contact.
// A snap is used only when the result is valid (inside the room, no overlap)
// and does not move the item into a door or window clearance zone that the
// unsnapped position kept clear; otherwise the item keeps its own position.
// Corner and side names are the stable cabinet IDs (back-left, front, …).
import {cabinetFootprint,footprintsOverlap,footprintInside,CABINET_CORNER_IDS} from './furniture.mjs';
import {roomWalls,wallLabel} from './room-topology.mjs';
import {furnitureClearance} from './furniture-clearance.mjs';

export const SNAP_DEFAULTS=Object.freeze({distance:.15,angle:12});
const EPS=1e-9,round=v=>Math.round(v*1e6)/1e6;
const dot=(a,b)=>a.x*b.x+a.z*b.z;
// The item's width axis (u, local +x) and front axis (v, local +z) in the plan.
export function itemAxes(rotation) {
  const r=rotation*Math.PI/180,c=Math.cos(r),s=Math.sin(r);
  return {u:{x:c,z:-s},v:{x:s,z:c}};
}
const normalise=deg=>((deg%360)+360)%360;
// Rotation with the item's front facing along the direction n.
export const facing=n=>normalise(Math.atan2(n.x,n.z)*180/Math.PI);
// Signed difference to the nearest quarter turn of `target`, in (−45, 45].
const quarter=(rotation,target)=>{let d=normalise(rotation-target)%90;if(d>45) d-=90;return d;};
const range=(points,axis)=>{const values=points.map(p=>dot(p,axis));return {min:Math.min(...values),max:Math.max(...values)};};
function wallDistance(footprint,wall) {
  return Math.min(...footprint.map(p=>{const t=Math.max(0,Math.min(wall.length,(p.x-wall.a.x)*wall.tx+(p.z-wall.a.z)*wall.tz));return Math.hypot(p.x-wall.a.x-t*wall.tx,p.z-wall.a.z-t*wall.tz);}));
}
const conflicts=(room,item,settings)=>new Set(furnitureClearance(room,item,settings).issues.filter(i=>i.code==='DOOR_ZONE'||i.code==='WINDOW_ZONE').map(i=>i.openingId));

// Snaps one item of `room.furniture` (by id). Items in `ignore` (e.g. the rest of a
// dragged group) are neither targets nor obstacles. Returns the snapped item and
// what it connected to, or null when nothing is near.
export function snapFurniture(room,id,{distance=SNAP_DEFAULTS.distance,angle=SNAP_DEFAULTS.angle,wallGap=0,rotate=true,ignore=[],clearance}={}) {
  const item=room.furniture.find(f=>f.id===id);
  if(!item) throw Error('Furniture item not found');
  const skip=new Set([id,...ignore]),others=room.furniture.filter(f=>!skip.has(f.id)),walls=roomWalls(room);
  const near=(footprint,other)=>{const a=range(footprint,{x:1,z:0}),b=range(other,{x:1,z:0}),c=range(footprint,{x:0,z:1}),d=range(other,{x:0,z:1});
    return a.min<=b.max+distance&&b.min<=a.max+distance&&c.min<=d.max+distance&&d.min<=c.max+distance;};
  let rotation=item.rotation;const snaps=[];
  // 1. Square up to the nearest wall, or else to the nearest item, when nearly square already.
  if(rotate) {
    const footprint=cabinetFootprint(item);
    const targets=[...walls.map(w=>({d:wallDistance(footprint,w),rotation:facing({x:w.nx,z:w.nz}),wall:w.id})).filter(t=>t.d<=distance).sort((a,b)=>a.d-b.d),
      ...others.filter(o=>near(footprint,cabinetFootprint(o))).map(o=>({rotation:o.rotation,otherId:o.id}))];
    const target=targets.find(t=>Math.abs(quarter(rotation,t.rotation))<=angle);
    if(target) {const d=quarter(rotation,target.rotation);if(Math.abs(d)>1e-6) {rotation=round(normalise(rotation-d));snaps.push({kind:'rotation',degrees:-d,...(target.wall?{wall:target.wall}:{otherId:target.otherId})});}}
  }
  const turned={...item,rotation},footprint=cabinetFootprint(turned),{u,v}=itemAxes(rotation);
  // 2. Contacts along each axis: the offset that brings a face or edge into place.
  const offers={u:[],v:[]};
  const offer=(axisName,offset,contact)=>{if(Math.abs(offset)<=distance+EPS) offers[axisName].push({offset,...contact});};
  for(const w of walls) {
    const t={x:w.tx,z:w.tz},n={x:w.nx,z:w.nz};
    for(const [axisName,axis] of [['u',u],['v',v]]) {
      if(Math.abs(Math.abs(dot(n,axis))-1)>1e-6) continue;
      // The item must face this wall along the wall's length, not beyond its ends.
      const along=range(footprint.map(p=>({x:p.x-w.a.x,z:p.z-w.a.z})),t);
      if(along.max<=EPS||along.min>=w.length-EPS) continue;
      const inside=Math.min(...footprint.map(p=>(p.x-w.a.x)*n.x+(p.z-w.a.z)*n.z));
      offer(axisName,(wallGap-inside)*dot(n,axis),{kind:'wall',wall:w.id});
    }
  }
  for(const other of others) {
    if(Math.abs(quarter(rotation,other.rotation))>1e-6) continue;
    const theirs=cabinetFootprint(other);
    if(!near(footprint,theirs)) continue;
    for(const [axisName,axis,cross] of [['u',u,v],['v',v,u]]) {
      const a=range(footprint,axis),b=range(theirs,axis),ca=range(footprint,cross),cb=range(theirs,cross);
      const across=Math.max(ca.min-cb.max,cb.min-ca.max);  // gap across this axis (negative: overlapping)
      if(across>distance) continue;
      // Side by side (faces touch) when they overlap across; edges in line when they are next to each other.
      if(across<-EPS) {offer(axisName,b.max-a.min,{kind:'furniture',otherId:other.id,contact:'side'});offer(axisName,b.min-a.max,{kind:'furniture',otherId:other.id,contact:'side'});}
      offer(axisName,b.min-a.min,{kind:'furniture',otherId:other.id,contact:'edge'});offer(axisName,b.max-a.max,{kind:'furniture',otherId:other.id,contact:'edge'});
    }
  }
  const obstacles=others.map(o=>cabinetFootprint(o)),settings=clearance??{},before=conflicts(room,item,settings);
  const place=choice=>{
    let x=turned.x,z=turned.z;
    for(const [axisName,o] of Object.entries(choice)) {const axis=axisName==='u'?u:v;x+=o.offset*axis.x;z+=o.offset*axis.z;}
    const candidate={...turned,x:round(x),z:round(z)},shape=cabinetFootprint(candidate);
    if(!footprintInside(shape,room.outline)||obstacles.some(o=>footprintsOverlap(shape,o))) return null;
    const after=conflicts({...room,furniture:room.furniture.map(f=>f.id===id?candidate:f)},candidate,settings);
    return [...after].every(openingId=>before.has(openingId))?candidate:null;
  };
  // Nearest contacts first: both axes, then one, then only the rotation.
  for(const k of ['u','v']) offers[k].sort((a,b)=>Math.abs(a.offset)-Math.abs(b.offset));
  const U=offers.u.slice(0,6),V=offers.v.slice(0,6),choices=[];
  for(const a of U) for(const b of V) choices.push({u:a,v:b});
  choices.sort((a,b)=>Math.abs(a.u.offset)+Math.abs(a.v.offset)-Math.abs(b.u.offset)-Math.abs(b.v.offset));
  choices.push(...[...U.map(a=>({u:a})),...V.map(b=>({v:b}))].sort((a,b)=>Math.abs((a.u??a.v).offset)-Math.abs((b.u??b.v).offset)),{});
  for(const choice of choices) {
    const candidate=place(choice);
    if(!candidate) continue;
    const used=[...snaps,...Object.values(choice).map(({offset,...contact})=>contact)];
    return used.length?{item:candidate,snaps:used}:null;
  }
  return null;
}
// A short description, e.g. “North wall · Unit 2”.
export function describeSnaps(room,snaps) {
  const names=[];
  for(const s of snaps) {
    const name=s.wall?wallLabel(s.wall):room.furniture.find(f=>f.id===s.otherId)?.name;
    if(name&&!names.includes(name)) names.push(name);
  }
  return names.join(' · ');
}
// Corners of two items that touch (within 1 mm), by their stable corner IDs.
export function touchingCorners(a,b) {
  const pa=cabinetFootprint(a),pb=cabinetFootprint(b),out=[];
  pa.forEach((p,i)=>pb.forEach((q,j)=>{if(Math.hypot(p.x-q.x,p.z-q.z)<=.001) out.push([CABINET_CORNER_IDS[i],CABINET_CORNER_IDS[j]]);}));
  return out;
}
