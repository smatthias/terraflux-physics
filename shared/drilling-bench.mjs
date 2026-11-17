// The drilling bench: a panel lies on a drill press table with one face or
// edge up, and holes go in vertically at the point under the bit. This module
// is the bench's geometry and hole model; DrillingBench.vue draws and drives it.
//
// Positions on the face that is up are (a, b) in millimetres from the face's
// own corner, in the panel's sheet frame (the frame of the CNC plan):
//   face A / face B   a = u (along the length),  b = v (across the width)
//   edge v0 / v1      a = u (along the length),  b = t (across the thickness, from face B)
//   end u0 / u1       a = v (along the width),   b = t (across the thickness, from face B)
// On the bench `a` always runs to the right. `b` runs away from the operator
// or toward them, whichever keeps the panel the right way round (bZero tells
// which side b = 0 is on).
import {round,PANEL_ORIENTATIONS} from './furniture-design.mjs';
const SHEET={u:[1,0,0],v:[0,1,0],n:[0,0,1]};
const cross=(p,q)=>[p[1]*q[2]-p[2]*q[1],p[2]*q[0]-p[0]*q[2],p[0]*q[1]-p[1]*q[0]];
const dot=(p,q)=>p[0]*q[0]+p[1]*q[1]+p[2]*q[2];

// A drill rack: common bits in a furniture workshop.
export const DRILL_BITS=Object.freeze([
  {id:'twist-3',kind:'twist',diameter:3,use:'pilot holes'},
  {id:'twist-4',kind:'twist',diameter:4,use:'pilot holes'},
  {id:'brad-5',kind:'brad',diameter:5,use:'shelf pins, 32 mm system'},
  {id:'brad-6',kind:'brad',diameter:6,use:'dowels'},
  {id:'brad-8',kind:'brad',diameter:8,use:'dowels'},
  {id:'brad-10',kind:'brad',diameter:10,use:'dowels, connectors'},
  {id:'twist-12',kind:'twist',diameter:12,use:'cable, bolts'},
  {id:'forstner-15',kind:'forstner',diameter:15,use:'cam connectors'},
  {id:'forstner-20',kind:'forstner',diameter:20,use:'grommets'},
  {id:'forstner-26',kind:'forstner',diameter:26,use:'small hinge cups'},
  {id:'forstner-35',kind:'forstner',diameter:35,use:'hinge cups'},
].map(Object.freeze));
export const BIT_KINDS=Object.freeze({twist:'Twist drill',brad:'Brad point',forstner:'Forstner'});

// +1 when the panel's sheet frame (u, v, n) is right-handed in design space, else −1.
function handedness(orientation) {
  const axes=PANEL_ORIENTATIONS[orientation],unit=k=>({x:[1,0,0],y:[0,1,0],z:[0,0,1]})[axes[k]];
  return dot(cross(unit('u'),unit('v')),unit('n'))>0?1:-1;
}
export const BENCH_FACES=Object.freeze(['A','B','v0','v1','u0','u1']);
export function benchFace(part,face) {
  if(part?.shape!=='panel') throw Error('Only panels can be drilled');
  const {length:L,width:W,thickness:T}=part;
  const faces={
    A:{label:'Face A up',a:['u','Along the length',L],b:['v','Across the width',W],depth:T,normal:[0,0,1]},
    B:{label:'Face B up (turned over)',a:['u','Along the length',L],b:['v','Across the width',W],depth:T,normal:[0,0,-1]},
    v0:{label:'Edge v = 0 up',a:['u','Along the edge',L],b:['t','Across the thickness',T],depth:W,normal:[0,-1,0]},
    v1:{label:'Edge v = width up',a:['u','Along the edge',L],b:['t','Across the thickness',T],depth:W,normal:[0,1,0]},
    u0:{label:'End u = 0 up',a:['v','Along the end',W],b:['t','Across the thickness',T],depth:L,normal:[-1,0,0]},
    u1:{label:'End u = length up',a:['v','Along the end',W],b:['t','Across the thickness',T],depth:L,normal:[1,0,0]},
  };
  const f=faces[face];
  if(!f) throw Error('Choose a face or edge to put up');
  // On the bench the face normal points up and a to the right; b then runs toward
  // the operator (b = 0 at the back, by the fence) or away (b = 0 at the front).
  // The sheet frame of horizontal and side panels is left-handed in space, which
  // turns the cross product around (the panel must not be drawn mirrored).
  const aAxis=SHEET[f.a[0]],bAxis=SHEET[f.b[0]==='t'?'n':f.b[0]],hand=handedness(part.orientation),toward=cross(aAxis,f.normal).map(x=>x*hand||0);
  return {face,label:f.label,a:{key:f.a[0],label:f.a[1],length:f.a[2]},b:{key:f.b[0],label:f.b[1],length:f.b[2]},depthLimit:f.depth,height:f.depth,
    normal:f.normal,aAxis,toward,bZero:dot(bAxis,toward)>0?'back':'front',edge:face!=='A'&&face!=='B'};
}
// Feature fields of a hole at (a, b) on the face that is up.
export function holeAt(part,face,{a,b,diameter,depth,through=false}) {
  const f=benchFace(part,face),hole={face,diameter,depth:through?f.depthLimit:depth};
  hole[f.a.key]=round(a);hole[f.b.key]=round(b);
  return hole;
}
// Where an existing hole is on the face that is up, or null if it is on another face.
export function holePosition(part,face,feature) {
  if(feature.type!=='drill'||feature.face!==face) return null;
  const f=benchFace(part,face);
  return {a:feature[f.a.key],b:feature[f.b.key]};
}
// Holes in a row from the first, `spacing` apart along a or b.
export function rowOfHoles(start,{count=1,spacing=32,along='a'}={}) {
  if(!Number.isInteger(count)||count<1||count>50) throw Error('A row has 1–50 holes');
  if(count>1&&(typeof spacing!=='number'||!Number.isFinite(spacing)||spacing<=0||spacing>5000)) throw Error('Row spacing must be between 0 and 5000 mm');
  return Array.from({length:count},(_,i)=>({...start,[along]:round(start[along]+i*spacing)}));
}
// Rounds a hand position to the step (0 keeps it exact) and keeps it on the face.
export function handPosition(part,face,{a,b},step=1) {
  const f=benchFace(part,face),snap=v=>step>0?Math.round(v/step)*step:v,clamp=(v,max)=>round(Math.min(max,Math.max(0,v)));
  return {a:clamp(snap(a),f.a.length),b:clamp(snap(b),f.b.length)};
}

// ---- Bench frame: the panel lying on the table with the chosen face up.
// Returns, for a point given in the sheet frame (u, v, n from face B), its place
// on the bench (x right, y up from the table, z toward the operator) when the
// hole position `target` is under the bit at x = z = 0.
export function benchTransform(part,face,target) {
  const f=benchFace(part,face),{length:L,width:W,thickness:T}=part;
  // Sheet directions that end up right (x), up (y) and toward the operator (z).
  const rotate=p=>[dot(p,f.aAxis),dot(p,f.normal),dot(p,f.toward)];
  // The target point on the face, in the sheet frame.
  const hole=holeAt(part,face,{...target,diameter:1,depth:1});
  const surface={A:[hole.u,hole.v,T],B:[hole.u,hole.v,0],v0:[hole.u,0,hole.t],v1:[hole.u,W,hole.t],u0:[0,hole.v,hole.t],u1:[L,hole.v,hole.t]}[face];
  const at=rotate(surface),offset=[-at[0],f.height-at[1],-at[2]];
  return {basis:{x:f.aAxis,y:f.normal,z:f.toward},apply:p=>{const r=rotate(p);return {x:round(r[0]+offset[0]),y:round(r[1]+offset[1]),z:round(r[2]+offset[2])};},height:f.height};
}
// The same placement as a 3 × 4 matrix acting on the panel's own design
// coordinates (unrotated, minimum corner at the origin), for drawing it.
export function benchMatrix(part,face,target) {
  const axes=PANEL_ORIENTATIONS[part.orientation],t=benchTransform(part,face,target),index={x:0,y:1,z:2};
  const S=['u','v','n'].map(k=>{const e=[0,0,0];e[index[axes[k]]]=1;return e;});   // design → sheet
  const R=[t.basis.x,t.basis.y,t.basis.z],o=t.apply([0,0,0]);
  return R.map((r,i)=>[...[0,1,2].map(j=>r[0]*S[0][j]+r[1]*S[1][j]+r[2]*S[2][j]),[o.x,o.y,o.z][i]]);
}
