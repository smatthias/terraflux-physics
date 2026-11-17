// The cutting bench: saws and a router for one piece, like the drilling bench
// (drilling-bench.mjs, whose face frame it shares).
//
// Saw: a straight cut across the whole face, across the length (crosscut, at a
// distance along the length) or along it (rip, at a distance across the width).
// Through, it cuts the piece in two (removing the kerf); with a blade height
// below the thickness it cuts a groove the width of the kerf. Profiles (bars,
// rods, tubes, angles) are crosscut only.
// Router: a straight bit cuts a groove from one point to another (along the
// length or across it; the groove is the bit wide with square ends, as the CNC
// plan models it) or clears a rectangular pocket.
import {round,partSize,SECTION_AXES,ROUND_SHAPES} from './furniture-design.mjs';
import {benchFace,benchMatrix} from './drilling-bench.mjs';

export const SAW_BLADES=Object.freeze([
  {id:'saw-3.2',kind:'saw',kerf:3.2,label:'Saw blade 3.2 mm'},
  {id:'saw-2.4',kind:'saw',kerf:2.4,label:'Thin-kerf blade 2.4 mm'},
  {id:'dado-6',kind:'dado',kerf:6,label:'Dado set 6 mm'},
  {id:'dado-12',kind:'dado',kerf:12,label:'Dado set 12 mm'},
  {id:'dado-19',kind:'dado',kerf:19,label:'Dado set 19 mm'},
].map(Object.freeze));
export const ROUTER_BITS=Object.freeze([3,6,8,10,12,16,19,25].map(d=>Object.freeze({id:'router-'+d,kind:'router',diameter:d,label:`Straight bit Ø${d}`})));

// The piece on the bench: panels with face A or B up (see benchFace); profiles
// lying along the table with a = the distance along their length.
export function cutFrame(part,face='A') {
  if(part.shape==='panel') {
    if(face!=='A'&&face!=='B') throw Error('Cutting works with face A or face B up');
    return {...benchFace(part,face),profile:false};
  }
  const [w,h]=SECTION_AXES[part.axis],size=partSize(part),round_=ROUND_SHAPES.includes(part.shape);
  return {face:'top',label:'Lying along the table',a:{key:'length',label:'Along the length',length:part.length},b:{key:'across',label:'Across',length:round_?part.diameter:size[w]},
    depthLimit:round_?part.diameter:size[h],height:round_?part.diameter:size[h],bZero:'back',profile:true,edge:false};
}
// Drawing matrix (3 × 4, design coordinates of the unrotated piece with its
// minimum corner at the origin → bench, the target point at x = z = 0 on top).
export function cutMatrix(part,face,target) {
  if(part.shape==='panel') return benchMatrix(part,face,target);
  // Length → x, section height → up, and z = x × y (so the piece is not mirrored).
  const [w,h]=SECTION_AXES[part.axis],unit=k=>({x:[1,0,0],y:[0,1,0],z:[0,0,1]})[k],size=partSize(part);
  const X=unit(part.axis),Y=unit(h),Z=[X[1]*Y[2]-X[2]*Y[1],X[2]*Y[0]-X[0]*Y[2],X[0]*Y[1]-X[1]*Y[0]];
  // b = 0 is the back side (lowest z) of the section.
  const sign=Z[{x:0,y:1,z:2}[w]];
  return [[...X,-target.a],[...Y,0],[...Z,sign>0?-target.b:size[w]-target.b]];
}

// ---- Saw
// A saw cut at `at` (mm along a for a crosscut, along b for a rip).
export function sawPlan(part,face,{direction='cross',at,kerf,depth,through=true}) {
  const f=cutFrame(part,face);
  if(f.profile&&direction!=='cross') throw Error('Profiles are cut across their length');
  if(!(kerf>0)) throw Error('Choose a saw blade');
  const along=direction==='cross'?'length':'width',total=direction==='cross'?f.a.length:f.b.length;
  if(through||f.profile) {
    if(!(at>=1)) throw Error('Cut at least 1 mm from the start');
    if(at>=total) throw Error('The cut is beyond the end of the piece');
    // From face B, a position across the width is measured the same way (v).
    return {kind:'split',along,at:round(at),kerf,rest:round(total-at-kerf)};
  }
  if(!(depth>0&&depth<f.depthLimit)) throw Error(`Blade height must be between 0 and ${f.depthLimit} mm for a groove; cut through to divide the piece`);
  const groove=direction==='cross'?{u:round(at),v:0,length:kerf,width:f.b.length}:{u:0,v:round(at),length:f.a.length,width:kerf};
  return {kind:'groove',feature:{type:'groove',face,...groove,depth:round(depth)}};
}

// ---- Router
// A groove from `from` to `to` (snapped to the nearer direction) or a pocket
// with those corners, as a feature on the face that is up.
export function routerPlan(part,face,{mode='groove',from,to,diameter,depth,through=false}) {
  const f=cutFrame(part,face);
  if(f.profile) throw Error('The router works on panels');
  const d=through?f.depthLimit:depth,r=diameter/2;
  if(!(diameter>0)) throw Error('Choose a router bit');
  let u0,u1,v0,v1;
  if(mode==='groove') {
    const alongA=Math.abs(to.a-from.a)>=Math.abs(to.b-from.b);
    if(alongA) {u0=Math.min(from.a,to.a)-r;u1=Math.max(from.a,to.a)+r;v0=from.b-r;v1=from.b+r;}
    else {u0=from.a-r;u1=from.a+r;v0=Math.min(from.b,to.b)-r;v1=Math.max(from.b,to.b)+r;}
    // Keep the groove on the face: a groove may start or end at an edge.
    u0=Math.max(0,u0);v0=Math.max(0,v0);u1=Math.min(f.a.length,u1);v1=Math.min(f.b.length,v1);
    if(d>=f.depthLimit-1e-9&&((alongA&&u0<=0&&u1>=f.a.length)||(!alongA&&v0<=0&&v1>=f.b.length))) throw Error('A through groove from edge to edge divides the panel; use the saw');
  } else {
    u0=Math.min(from.a,to.a);u1=Math.max(from.a,to.a);v0=Math.min(from.b,to.b);v1=Math.max(from.b,to.b);
    if(u1-u0<diameter-1e-9||v1-v0<diameter-1e-9) throw Error(`The Ø${diameter} bit needs a pocket at least ${diameter} × ${diameter} mm`);
  }
  return {type:mode==='groove'?'groove':'pocket',face,u:round(u0),v:round(v0),length:round(u1-u0),width:round(v1-v0),depth:round(d)};
}
