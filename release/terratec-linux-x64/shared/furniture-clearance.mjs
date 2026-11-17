import {cabinetFootprint} from './furniture.mjs';
import {roomWalls,wallSegment} from './room-topology.mjs';
import {doorPose} from './openings.mjs';
export function clearanceSettings(input={}){
  const minWall=input.minWall??.05,doorDepth=input.doorDepth??.6,enforce=input.enforce??false,serviceDepth=input.serviceDepth??0;
  if(![minWall,doorDepth,serviceDepth].every(v=>typeof v==='number'&&Number.isFinite(v))||minWall<0||minWall>2||doorDepth<0||doorDepth>3||serviceDepth<0||serviceDepth>3||typeof enforce!=='boolean')throw Error('Clearance settings need wall gap 0–2 m and door/service depth 0–3 m');
  return {minWall,doorDepth,enforce,serviceDepth};
}
const cross=(a,b)=>a.x*b.z-a.z*b.x;
const sub=(a,b)=>({x:a.x-b.x,z:a.z-b.z});
function closest(p,a,b){const v=sub(b,a),t=Math.max(0,Math.min(1,((p.x-a.x)*v.x+(p.z-a.z)*v.z)/(v.x*v.x+v.z*v.z)));return {x:a.x+t*v.x,z:a.z+t*v.z};}
function segmentDistance(a,b,c,d){
 const r=sub(b,a),s=sub(d,c),den=cross(r,s);
 if(Math.abs(den)>1e-12){const t=cross(sub(c,a),s)/den,u=cross(sub(c,a),r)/den;if(t>=0&&t<=1&&u>=0&&u<=1){const p={x:a.x+t*r.x,z:a.z+t*r.z};return {distance:0,from:p,to:p};}}
 return [{from:a,to:closest(a,c,d)},{from:b,to:closest(b,c,d)},{from:closest(c,a,b),to:c},{from:closest(d,a,b),to:d}].map(p=>({...p,distance:Math.hypot(p.from.x-p.to.x,p.from.z-p.to.z)})).sort((a,b)=>a.distance-b.distance)[0];
}
function intersects(a,b){
 for(const polygon of [a,b])for(let i=0;i<polygon.length;i++){
  const p=polygon[i],q=polygon[(i+1)%polygon.length],axis={x:-(q.z-p.z),z:q.x-p.x};
  const project=points=>points.map(v=>v.x*axis.x+v.z*axis.z),pa=project(a),pb=project(b);
  if(Math.max(...pa)<=Math.min(...pb)+1e-9||Math.max(...pb)<=Math.min(...pa)+1e-9)return false;
 }return true;
}
// Clip to the sweep quadrant, then test distance to its circular boundary.
// The sampled polygon is only for drawing; collision uses the analytic radius.
function sweepIntersects(body,local,radius){
 let polygon=body.map(local);
 for(const axis of ['x','z']){
  const clipped=[];
  for(let i=0;i<polygon.length;i++){
   const a=polygon[i],b=polygon[(i+1)%polygon.length],insideA=a[axis]>=0,insideB=b[axis]>=0;
   if(insideA)clipped.push(a);
   if(insideA!==insideB){const t=a[axis]/(a[axis]-b[axis]);clipped.push({x:a.x+t*(b.x-a.x),z:a.z+t*(b.z-a.z)});}
  }polygon=clipped;
 }
 if(polygon.length<3||Math.abs(polygon.reduce((sum,p,i)=>sum+cross(p,polygon[(i+1)%polygon.length]),0))<1e-12)return false;
 const origin={x:0,z:0};
 const signs=polygon.map((p,i)=>cross(sub(polygon[(i+1)%polygon.length],p),sub(origin,p)));
 if(signs.every(v=>v>=-1e-12)||signs.every(v=>v<=1e-12))return true;
 return polygon.some((p,i)=>{const q=closest(origin,p,polygon[(i+1)%polygon.length]);return q.x*q.x+q.z*q.z<radius*radius-1e-10;});
}
export function cabinetServiceZone(item,depth){
 const angle=item.rotation*Math.PI/180;
 return cabinetFootprint({...item,depth,x:item.x+Math.sin(angle)*(item.depth+depth)/2,z:item.z+Math.cos(angle)*(item.depth+depth)/2});
}
export function furnitureClearance(room,item,options){
 const settings=clearanceSettings(options),body=cabinetFootprint(item),walls=[];
 for(const {id,a,b} of roomWalls(room)){
  let nearest;
  for(let n=0;n<body.length;n++){const result=segmentDistance(body[n],body[(n+1)%body.length],a,b);if(!nearest||result.distance<nearest.distance)nearest=result;}
  walls.push({wall:id,...nearest});
 }
 const nearestWall=[...walls].sort((a,b)=>a.distance-b.distance)[0],issues=[],zones=[];
 for(const wall of walls)if(wall.distance<settings.minWall-1e-8)issues.push({code:'WALL_GAP',wall:wall.wall,message:`${wall.wall} wall gap ${wall.distance.toFixed(3)} m is below ${settings.minWall.toFixed(3)} m`});
 for(const opening of room.openings??[]){
  if(item.height<=opening.sill+1e-9)continue;
  const {a,tx,tz,nx,nz}=wallSegment(room,opening.wall);
  const depth=opening.type==='door'?settings.doorDepth:.15;
  const point=(along,inward)=>({x:a.x+along*tx+inward*nx,z:a.z+along*tz+inward*nz});
  const polygon=[point(opening.offset,0),point(opening.offset+opening.width,0),point(opening.offset+opening.width,depth),point(opening.offset,depth)];
  let conflict=depth>0&&intersects(body,polygon);
  if(conflict)zones.push({openingId:opening.id,kind:opening.type==='door'?'access':'window',polygon});
  if(opening.type==='door'){
   const pose=doorPose(opening),hinge=point(pose.hingeAlong,0);
   const local=p=>({x:((p.x-hinge.x)*tx+(p.z-hinge.z)*tz)*pose.hingeSign,z:((p.x-hinge.x)*nx+(p.z-hinge.z)*nz)*pose.swingSign});
   if(sweepIntersects(body,local,opening.width)){
    const arc=Array.from({length:65},(_,n)=>{const angle=n*Math.PI/128;return point(pose.hingeAlong+pose.hingeSign*opening.width*Math.cos(angle),pose.swingSign*opening.width*Math.sin(angle));});
    zones.push({openingId:opening.id,kind:'sweep',polygon:[hinge,...arc]});conflict=true;
   }
  }
  if(conflict)issues.push({code:opening.type==='door'?'DOOR_ZONE':'WINDOW_ZONE',openingId:opening.id,wall:opening.wall,message:opening.type==='door'?`Cabinet enters ${opening.wall} door access/swing planning zone`:`Cabinet enters ${opening.wall} window service planning zone`});
 }
 if(settings.serviceDepth>0){
  const service=cabinetServiceZone(item,settings.serviceDepth);
  for(const {id,a,b,length} of roomWalls(room)){
   const edge=sub(b,a),beyond=Math.max(0,...service.map(p=>-cross(edge,sub(p,a))/length));
   if(beyond>1e-8){
    zones.push({kind:'service-wall',wall:id,polygon:service});
    issues.push({code:'SERVICE_WALL',wall:id,message:`Cabinet front service zone crosses ${id} room boundary by ${beyond.toFixed(3)} m`});
   }
  }

  for(const other of room.furniture??[]){
   if(other.id===item.id)continue;
   if(intersects(service,cabinetFootprint(other))){
    zones.push({kind:'service',otherCabinetId:other.id,polygon:service});
    issues.push({code:'SERVICE_ZONE',otherCabinetId:other.id,message:`Cabinet front service zone is blocked by ${other.name}`});
   }
   const otherService=cabinetServiceZone(other,settings.serviceDepth);
   if(intersects(body,otherService)){
    zones.push({kind:'service',otherCabinetId:other.id,polygon:otherService});
    issues.push({code:'BLOCKS_SERVICE',otherCabinetId:other.id,message:`Cabinet blocks the front service zone of ${other.name}`});
   }
  }
 }
 return {nearestWall,walls,issues,zones,settings};
}

export function reviewFurnitureClearance(room,options){
 const settings=clearanceSettings(options);
 const cabinets=(room.furniture??[]).map(item=>({id:item.id,name:item.name,report:furnitureClearance(room,item,settings)}));
 const issues=cabinets.flatMap(item=>item.report.issues.map(issue=>({...issue,cabinetId:item.id,cabinetName:item.name})));
 return {cabinetCount:cabinets.length,affectedCount:cabinets.filter(item=>item.report.issues.length).length,issues};
}
