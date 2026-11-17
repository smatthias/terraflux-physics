import { isWallId, wallSegment } from './room-topology.mjs';
// `room` is {outline, walls?} (or a bare four-corner outline).
export const wallLength=(room,wall)=>wallSegment(room,wall).length;
export function validateOpenings(openings,room,height) {
  if(!Array.isArray(openings) || openings.length>64) throw Error('Expected at most 64 openings');
  const ids=new Set();
  const result=openings.map(o=>{
    if(!o || typeof o.id!=='string' || !/^[a-zA-Z0-9_-]{1,64}$/.test(o.id) || ids.has(o.id)) throw Error('Opening IDs must be unique');
    ids.add(o.id);
    if(!['door','window'].includes(o.type) || !isWallId(room,o.wall)) throw Error('Opening needs a door/window type and valid wall');
    if(![o.offset,o.width,o.height,o.sill].every(v=>typeof v==='number' && Number.isFinite(v))) throw Error('Opening dimensions must be finite numbers');
    const length=wallLength(room,o.wall);
    if(o.offset<.05-1e-9 || o.width<.1 || o.height<.1 || o.sill<0 ||
      o.offset+o.width>length-.05+1e-9 || o.sill+o.height>height-.05+1e-9 ||
      (o.type==='door' && o.sill!==0) || (o.type==='window' && o.sill<.1))
      throw Error('Opening must fit its wall with 0.05 m end/header clearance; doors start at floor and windows need a sill of at least 0.1 m');
    if(o.type==='door' && (!['A','B'].includes(o.hinge??'A') || !['in','out'].includes(o.swing??'in')))
      throw Error('Door hinge must be A or B and swing must be in or out');
    return {...(o.type==='door'?{hinge:o.hinge??'A',swing:o.swing??'in'}:{}),id:o.id,type:o.type,wall:o.wall,offset:o.offset,width:o.width,height:o.height,sill:o.sill};
  });
  for(let i=0;i<result.length;i++) for(let j=i+1;j<result.length;j++) {
    const a=result[i],b=result[j];
    if(a.wall===b.wall && Math.max(a.offset,b.offset)<Math.min(a.offset+a.width,b.offset+b.width)+.05-1e-9)
      throw Error('Openings on the same wall need at least 0.05 m horizontal separation');
  }
  return result;
}
// Rectangular wall pieces leave actual voids for doors and windows.
export function wallPanels(length,height,openings) {
  const cuts=[-.08,...openings.flatMap(o=>[o.offset,o.offset+o.width]),length+.08].sort((a,b)=>a-b);
  const panels=[];
  for(let i=0;i<cuts.length-1;i++) {
    const left=cuts[i],right=cuts[i+1];
    if(right-left<1e-9) continue;
    const opening=openings.find(o=>(left+right)/2>o.offset && (left+right)/2<o.offset+o.width);
    const spans=opening?[[0,opening.sill],[opening.sill+opening.height,height]]:[[0,height]];
    for(const [bottom,top] of spans) if(top-bottom>1e-9) panels.push({left,right,bottom,top});
  }
  return panels;
}
export function openingPreset(room,wall,type,id,offset) {
  const length=wallLength(room,wall),height=room.height;
  return {...(type==='door'?{hinge:'A',swing:'in'}:{}),id,wall,type,offset,width:Math.min(type==='door'?.9:1.2,length-.2),
    height:type==='door'?Math.min(2.1,height-.05):Math.min(1.2,height*.4),sill:type==='door'?0:Math.max(.1,height*.3)};
}
export function previewOpening(room,wall,type,id,point,spacing,snap=true,tolerance=.25,size) {
  const {a,length,tx,tz}=wallSegment(room,wall);
  const along=(point.x-a.x)*tx+(point.z-a.z)*tz;
  const distance=Math.abs((point.x-a.x)*(-tz)+(point.z-a.z)*tx);
  const opening=openingPreset(room,wall,type,id,0);
  if(size) {
    opening.width=size.width;opening.height=size.height;opening.sill=type==='door'?0:size.sill;
    if(type==='door') {opening.hinge=size.hinge??'A';opening.swing=size.swing??'in';}
  }
  const offset=along-opening.width/2;
  opening.offset=snap?Number((Math.round(offset/spacing)*spacing).toFixed(8)):offset;
  try {
    if(!Number.isFinite(along) || distance>tolerance) throw Error('Click near the selected wall to place an opening');
    validateOpenings([...room.openings,opening],room,room.height);
    return {opening,valid:true,error:''};
  } catch(error) { return {opening,valid:false,error:error.message}; }
}
export function openingAtPoint(...args) {
  const result=previewOpening(...args);
  if(!result.valid) throw Error(result.error);
  return result.opening;
}
export function doorPose(opening) {
  const hingeB=opening.hinge==='B';
  return {hingeAlong:opening.offset+(hingeB?opening.width:0),hingeSign:hingeB?-1:1,swingSign:opening.swing==='out'?-1:1};
}
