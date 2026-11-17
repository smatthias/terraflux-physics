// True shapes of design pieces for the overlap check: boxes for panels, bars and
// rectangular tubes, two boxes for an L-angle, and cylinders for round rods,
// round tubes, drill holes and tube bores. Each is a convex solid given by its
// bounding planes in world space (rotation included), so any two can be
// intersected exactly with plane clipping.
//
// A cylinder is a regular prism with enough sides that it differs from the
// circle by at most ROUND_TOLERANCE: material (rods, tube walls) uses the prism
// inside the circle and openings (holes, bores) the one around it, so tangent
// pieces never count as overlapping and an exact-fit dowel always fits its hole.
import {partBox,partCentre,SECTION_AXES,PANEL_ORIENTATIONS} from './furniture-design.mjs';
import {featureBox,drillSheetAxis,solidBoxes,hollowBox} from './furniture-design-geometry.mjs';
import {rotationMatrix} from './design-transform.mjs';

export const ROUND_TOLERANCE=.005;
const EPS=1e-7;
const dot=(a,b)=>a.x*b.x+a.y*b.y+a.z*b.z;
const add=(a,b,s=1)=>({x:a.x+b.x*s,y:a.y+b.y*s,z:a.z+b.z*s});
const cross=(a,b)=>({x:a.y*b.z-a.z*b.y,y:a.z*b.x-a.x*b.z,z:a.x*b.y-a.y*b.x});
const unit=v=>{const n=Math.hypot(v.x,v.y,v.z);return {x:v.x/n,y:v.y/n,z:v.z/n};};
const AXIS={x:{x:1,y:0,z:0},y:{x:0,y:1,z:0},z:{x:0,y:0,z:1}};

// ---- Local shapes (in the piece's unrotated frame)
const boxShape=box=>({kind:'box',box});
// Sides of the prism standing in for a circle of radius r.
export const prismSides=r=>Math.min(256,Math.max(12,Math.ceil(Math.PI/Math.acos(Math.max(-1,1-ROUND_TOLERANCE/r)))));
const cylinderShape=(axis,centre,radius,from,to)=>({kind:'cylinder',axis,centre,radius,from,to});
// Bounding planes and face loops of a local shape, shrunk by `erode` on every side.
function localGeometry(shape,outside,erode) {
  if(shape.kind==='box') {
    const min={},max={},planes=[];
    for(const k of ['x','y','z']) {min[k]=shape.box.min[k]+erode;max[k]=shape.box.max[k]-erode;planes.push({n:AXIS[k],d:max[k]},{n:add(AXIS[k],AXIS[k],-2),d:-min[k]});}
    return {planes,faces:min.x<max.x&&min.y<max.y&&min.z<max.z?boxFaces({min,max}):null};
  }
  const {axis,centre,radius}=shape,[a,b]=SECTION_AXES[axis],sides=prismSides(radius),from=shape.from+erode,to=shape.to-erode;
  // Around the circle the faces touch it; inside it the corners do.
  const apothem=(outside?radius:radius*Math.cos(Math.PI/sides))-erode,corner=apothem/Math.cos(Math.PI/sides);
  const planes=[{n:AXIS[axis],d:to},{n:add(AXIS[axis],AXIS[axis],-2),d:-from}],ring=[];
  for(let k=0;k<sides;k++) {
    const t=2*Math.PI*k/sides,n={x:0,y:0,z:0};n[a]=Math.cos(t);n[b]=Math.sin(t);
    planes.push({n,d:apothem+n[a]*centre[a]+n[b]*centre[b]});
    const c=t+Math.PI/sides,p={x:0,y:0,z:0};p[a]=centre[a]+corner*Math.cos(c);p[b]=centre[b]+corner*Math.sin(c);ring.push(p);
  }
  if(!(apothem>0)||!(to>from)) return {planes,faces:null};
  const at=(p,v)=>({...p,[axis]:v}),faces=[ring.map(p=>at(p,from)),ring.map(p=>at(p,to)).reverse()];
  for(let k=0;k<sides;k++) {const p=ring[k],q=ring[(k+1)%sides];faces.push([at(p,from),at(q,from),at(q,to),at(p,to)]);}
  return {planes,faces};
}
// Local → world: p_world = c + M (p_local − c); planes move with their normals.
const turn=(m,v)=>({x:m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z,y:m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z,z:m[2][0]*v.x+m[2][1]*v.y+m[2][2]*v.z});
function toWorld({planes,faces},m,c) {
  return {planes:planes.map(({n,d})=>{const w=turn(m,n);return {n:w,d:d-dot(n,c)+dot(w,c)};}),faces:faces&&faces.map(face=>face.map(p=>add(c,turn(m,add(p,c,-1)))))};
}
const SOLID_SHAPE=part=>part.shape==='cylinder'||part.shape==='tube'?
  [cylinderShape(part.axis,partCentre(part),part.diameter/2,partBox(part).min[part.axis],partBox(part).max[part.axis])]:solidBoxes(part).map(boxShape);
function receiverShapes(part) {
  const out=[];
  for(const f of part.features??[]) {
    const box=featureBox(part,f);
    if(f.type!=='drill') {out.push(boxShape(box));continue;}
    const sheet=drillSheetAxis(f),axis=sheet==='n'?PANEL_ORIENTATIONS[part.orientation].n:PANEL_ORIENTATIONS[part.orientation][sheet];
    const centre={x:(box.min.x+box.max.x)/2,y:(box.min.y+box.max.y)/2,z:(box.min.z+box.max.z)/2};
    out.push(cylinderShape(axis,centre,f.diameter/2,box.min[axis],box.max[axis]));
  }
  if(part.shape==='tube') out.push(cylinderShape(part.axis,partCentre(part),part.diameter/2-part.wall,partBox(part).min[part.axis],partBox(part).max[part.axis]));
  else {const hollow=hollowBox(part);if(hollow) out.push(boxShape(hollow));}
  return out;
}
// World bounds of a local shape (from the corners of its local bounds).
function worldBounds(shape,m,c) {
  let local=shape.box;
  if(shape.kind==='cylinder') {
    const {axis,centre,radius,from,to}=shape,min={...centre},max={...centre};
    for(const k of ['x','y','z']) if(k===axis) {min[k]=from;max[k]=to;} else {min[k]-=radius;max[k]+=radius;}
    local={min,max};
  }
  return bounds(boxFaces(local)[0].concat(boxFaces(local)[1]).map(p=>add(c,turn(m,add(p,c,-1)))));
}
// Openings are kept as exact boxes or circles: a receiver holds its world
// bounds and tests whether points lie inside it (within `tolerance`).
function receiver(shape,m,c) {
  const box=worldBounds(shape,m,c);
  if(shape.kind==='box') {
    const {planes}=toWorld({planes:localGeometry(shape,true,0).planes,faces:null},m,c);
    return {box,inside:(points,tol)=>points.every(p=>planes.every(({n,d})=>dot(n,p)<=d+tol))};
  }
  const axis=turn(m,AXIS[shape.axis]),centre=add(c,turn(m,add(shape.centre,c,-1))),from=shape.from-shape.centre[shape.axis],to=shape.to-shape.centre[shape.axis],r=shape.radius;
  return {box,inside:(points,tol)=>points.every(p=>{
    const d=add(p,centre,-1),s=dot(d,axis);
    return s>=from-tol&&s<=to+tol&&Math.hypot(d.x-s*axis.x,d.y-s*axis.y,d.z-s*axis.z)<=r+tol;
  })};
}
// A piece's material (shrunk by `erode`, so touching is not overlapping) and the
// openings that may receive other pieces, in world space. The planes and faces
// of material are built on first use.
export function pieceSolids(part,erode=0) {
  const m=rotationMatrix(part.rotation),c=partCentre(part);
  const solid=shape=>{let geometry;return {box:worldBounds(shape,m,c),geometry:()=>geometry??=toWorld(localGeometry(shape,false,erode),m,c)};};
  return {solids:SOLID_SHAPE(part).map(solid),receivers:receiverShapes(part).map(shape=>receiver(shape,m,c))};
}

// ---- Convex polyhedra (faces as vertex loops) and clipping
function bounds(points) {
  const min={x:Infinity,y:Infinity,z:Infinity},max={x:-Infinity,y:-Infinity,z:-Infinity};
  for(const p of points) for(const k of ['x','y','z']) {if(p[k]<min[k]) min[k]=p[k];if(p[k]>max[k]) max[k]=p[k];}
  return {min,max};
}
function boxFaces({min,max}) {
  const v=(i,j,k)=>({x:i?max.x:min.x,y:j?max.y:min.y,z:k?max.z:min.z});
  return [[v(0,0,0),v(0,1,0),v(0,1,1),v(0,0,1)],[v(1,0,0),v(1,0,1),v(1,1,1),v(1,1,0)],[v(0,0,0),v(0,0,1),v(1,0,1),v(1,0,0)],
    [v(0,1,0),v(1,1,0),v(1,1,1),v(0,1,1)],[v(0,0,0),v(1,0,0),v(1,1,0),v(0,1,0)],[v(0,0,1),v(0,1,1),v(1,1,1),v(1,0,1)]];
}
// Orders points lying on a plane with normal n around their centroid.
function loop(points,n) {
  const unique=[];
  for(const p of points) if(!unique.some(q=>Math.abs(q.x-p.x)+Math.abs(q.y-p.y)+Math.abs(q.z-p.z)<1e-6)) unique.push(p);
  if(unique.length<3) return null;
  const c=unique.reduce((s,p)=>add(s,p,1/unique.length),{x:0,y:0,z:0});
  const u=unit(Math.abs(n.x)<.9?cross(n,AXIS.x):cross(n,AXIS.y)),v=cross(n,u);
  return unique.map(p=>({p,a:Math.atan2(dot(add(p,c,-1),v),dot(add(p,c,-1),u))})).sort((x,y)=>x.a-y.a).map(e=>e.p);
}
// Keeps the part of a polyhedron with n·p ≤ d; null when nothing with volume is left.
export function clip(faces,{n,d}) {
  let inside=true,outside=true;
  for(const face of faces) for(const p of face) {const s=dot(n,p)-d;if(s>EPS) inside=false;if(s<-EPS) outside=false;}
  if(inside) return faces;
  if(outside) return null;
  const out=[],cut=[];
  for(const face of faces) {
    const kept=[];
    for(let i=0;i<face.length;i++) {
      const a=face[i],b=face[(i+1)%face.length],sa=dot(n,a)-d,sb=dot(n,b)-d;
      if(sa<=EPS) kept.push(a);
      if(Math.abs(sa)<=EPS) cut.push(a);
      if((sa<-EPS&&sb>EPS)||(sa>EPS&&sb<-EPS)) {const p=add(a,add(b,a,-1),sa/(sa-sb));kept.push(p);cut.push(p);}
    }
    if(kept.length>=3) out.push(kept);
  }
  const cap=loop(cut,n);
  if(cap) out.push(cap);
  return out.length>=4?out:null;
}
const overlaps=(a,b)=>['x','y','z'].every(k=>a.min[k]<b.max[k]&&b.min[k]<a.max[k]);

// ---- Pieces against pieces
// Pass pieces from pieceSolids(part, erode): overlap deeper than `erode` on every
// side counts, touching does not. The overlap is received when all of it lies
// within one opening of either piece.
export function piecesCollide(a,b,tolerance=1e-3) {
  for(const solidA of a.solids) for(const solidB of b.solids) {
    if(!overlaps(solidA.box,solidB.box)) continue;
    const ga=solidA.geometry(),gb=solidB.geometry();
    if(!ga.faces||!gb.faces) continue;
    // Clip by the solid with fewer planes: planes that miss cost one pass and no copy.
    let [faces,other]=gb.planes.length<=ga.planes.length?[ga.faces,gb]:[gb.faces,ga];
    for(const plane of other.planes) {faces=clip(faces,plane);if(!faces) break;}
    if(!faces) continue;
    const points=faces.flat(),box=bounds(points);
    const received=[...a.receivers,...b.receivers].some(r=>['x','y','z'].every(k=>box.min[k]>=r.box.min[k]-tolerance&&box.max[k]<=r.box.max[k]+tolerance)&&r.inside(points,tolerance));
    if(!received) return true;
  }
  return false;
}
