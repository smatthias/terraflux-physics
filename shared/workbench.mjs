// The carpenter's workbench: what its hand tools (tool-chest.mjs) do to a piece,
// next to the drilling and cutting benches. Positions are on the face or
// side that is up, as on the other benches (cutFrame: a along, b across).
import {round,materialColor} from './furniture-design.mjs';
import {cutFrame} from './cutting-bench.mjs';

// Stains and paints (colours for the view); the natural finish is the material colour.
export const FINISHES=Object.freeze([
  {id:'natural',label:'Natural (material colour)',color:null},
  {id:'oil',label:'Clear oil',color:'#c08a4e'},
  {id:'oak',label:'Oak stain',color:'#a0703c'},
  {id:'walnut',label:'Walnut stain',color:'#5a3a22'},
  {id:'white',label:'White paint',color:'#f2f2ee'},
  {id:'grey',label:'Grey paint',color:'#8a8f91'},
  {id:'black',label:'Black paint',color:'#262626'},
].map(Object.freeze));
export const finishColor=(part,id,custom)=>id==='custom'?custom:FINISHES.find(f=>f.id===id)?.color??materialColor(part.material);

// ---- Tape measure: distance between two points on the face that is up.
export function measure(p,q) {
  const da=round(q.a-p.a),db=round(q.b-p.b);
  return {da,db,distance:Math.round(Math.hypot(da,db)*100)/100};
}
// Distances from a point to the four sides of the face (what a tape reads from each edge).
export function edgeDistances(part,face,p) {
  const f=cutFrame(part,face);
  return {start:round(p.a),end:round(f.a.length-p.a),near:round(p.b),far:round(f.b.length-p.b)};
}
// ---- Square and pencil: marks (kept while the bench is open) across the piece
// at a distance along it (across) or along it at a distance across (along).
export function markLine(part,face,{direction,at}) {
  const f=cutFrame(part,face),max=direction==='across'?f.a.length:f.b.length;
  if(!(at>=0&&at<=max)) throw Error(`A mark must be on the piece (0–${max} mm)`);
  return {direction,at:round(at)};
}
// The nearest mark within `reach` mm of a position, for snapping tools to it.
export function snapToMarks(p,marks,reach=5) {
  const nearest=(direction,value)=>marks.filter(m=>m.direction===direction&&Math.abs(m.at-value)<=reach).sort((x,y)=>Math.abs(x.at-value)-Math.abs(y.at-value))[0]?.at??value;
  return {a:nearest('across',p.a),b:nearest('along',p.b)};
}
// ---- Chisel: a mortise (or a notch at an edge) the chisel wide, `length` long
// along a or b, from the corner at `at`; a pocket in the CNC plan.
export function mortise(part,face,{at,width,length,along='a',depth}) {
  const f=cutFrame(part,face);
  if(f.profile) throw Error('The chisel works on panels');
  if(!(length>0)) throw Error('Enter the mortise length');
  if(!(depth>0&&depth<=f.depthLimit)) throw Error(`Depth must be between 0 and ${f.depthLimit} mm`);
  const size=along==='a'?{length:round(length),width}:{length:width,width:round(length)};
  return {type:'pocket',face,u:round(at.a),v:round(at.b),...size,depth:round(depth)};
}
// ---- Rebate plane: a rebate along an edge of the face that is up, `width` in
// from that edge and `depth` deep, the full length of the edge; a groove.
export function rebate(part,face,{edge,width,depth}) {
  const f=cutFrame(part,face),L=f.a.length,W=f.b.length;
  if(f.profile) throw Error('The rebate plane works on panels');
  if(!(width>0)) throw Error('Enter the rebate width');
  if(!(depth>0&&depth<f.depthLimit)) throw Error(`Rebate depth must be between 0 and ${f.depthLimit} mm; a full-depth rebate cuts the edge off`);
  const rect={v0:{u:0,v:0,length:L,width},v1:{u:0,v:W-width,length:L,width},u0:{u:0,v:0,length:width,width:W},u1:{u:L-width,v:0,length:width,width:W}}[edge];
  if(!rect) throw Error('Choose an edge for the rebate');
  return {type:'groove',face,...Object.fromEntries(Object.entries(rect).map(([k,v])=>[k,round(v)])),depth:round(depth)};
}
// ---- Plane: the slab a planing takes off, in the frame of the face that is up
// (a, b ranges; `fromTop` when it comes off the face that is up, `fromBottom` the one below).
export function planeSlab(part,face,side,amount) {
  const f=cutFrame(part,face),L=f.a.length,W=f.b.length;
  if(part.shape!=='panel') {
    if(side==='length0') return {a:[0,amount],b:[0,W],y:[0,f.height]};
    if(side==='length1') return {a:[L-amount,L],b:[0,W],y:[0,f.height]};
    return null;
  }
  const up=face,down=face==='A'?'B':'A';
  if(side===up) return {a:[0,L],b:[0,W],y:[f.height-amount,f.height]};
  if(side===down) return {a:[0,L],b:[0,W],y:[0,amount]};
  if(side==='u0') return {a:[0,amount],b:[0,W],y:[0,f.height]};
  if(side==='u1') return {a:[L-amount,L],b:[0,W],y:[0,f.height]};
  if(side==='v0') return {a:[0,L],b:[0,amount],y:[0,f.height]};
  return {a:[0,L],b:[W-amount,W],y:[0,f.height]};
}
