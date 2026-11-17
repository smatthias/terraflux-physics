import * as THREE from 'three';
import {roomWalls,isWallId,wallSegment} from '../shared/room-topology.mjs';
export function dimensionSegment(a,b,camera,width,height,id,offset=28) {
  const screen=p=>{const v=new THREE.Vector3(p.x,0,p.z).project(camera);return {x:(v.x+1)*width/2,y:(1-v.y)*height/2};};
  const length=Math.hypot(b.x-a.x,b.z-a.z),p=screen(a),q=screen(b),middle={x:(a.x+b.x)/2,z:(a.z+b.z)/2};
  const inner=screen({x:middle.x-(b.z-a.z)/length,z:middle.z+(b.x-a.x)/length}),centre=screen(middle);
  const nx=centre.x-inner.x,ny=centre.y-inner.y,n=Math.hypot(nx,ny),ux=nx/n,uy=ny/n,dx=q.x-p.x,dy=q.y-p.y,d=Math.hypot(dx,dy),tx=dx/d,ty=dy/d;
  const start={x:p.x+ux*offset,y:p.y+uy*offset},end={x:q.x+ux*offset,y:q.y+uy*offset};
  const tick=point=>({x1:point.x-(tx+ux)*4,y1:point.y-(ty+uy)*4,x2:point.x+(tx+ux)*4,y2:point.y+(ty+uy)*4});
  const extension=point=>({x1:point.x+ux*4,y1:point.y+uy*4,x2:point.x+ux*(offset+8),y2:point.y+uy*(offset+8)});
  return {id,length:length.toFixed(2),value:length,x:(start.x+end.x)/2,y:(start.y+end.y)/2,
    visible:Number.isFinite(n+d) && n>0 && d>0 && centre.x>=0 && centre.x<=width && centre.y>=0 && centre.y<=height,
    segments:[{x1:start.x,y1:start.y,x2:end.x,y2:end.y},extension(p),extension(q),tick(start),tick(end)]};
}
// `room` is {outline, walls?} or a bare four-corner outline.
export function dimensionLayout(room,camera,width,height,offset=28) {
  return roomWalls(room).map(({id,a,b})=>dimensionSegment(a,b,camera,width,height,id,typeof offset==='function'?offset(id):offset));
}
export function openingChainLayout(room,wall,camera,width,height) {
  const openings=room.openings.filter(o=>o.wall===wall).sort((a,b)=>a.offset-b.offset);
  if(!isWallId(room,wall) || !openings.length) return [];
  const {a,length,tx,tz}=wallSegment(room,wall);
  const cuts=[0,...openings.flatMap(o=>[o.offset,o.offset+o.width]),length];
  return cuts.slice(0,-1).map((start,n)=>{
    const end=cuts[n+1],opening=openings.find(o=>(start+end)/2>o.offset && (start+end)/2<o.offset+o.width);
    return {...dimensionSegment({x:a.x+tx*start,z:a.z+tz*start},{x:a.x+tx*end,z:a.z+tz*end},camera,width,height,wall+':'+n),kind:opening?'opening':'gap',openingId:opening?.id||null};
  });
}
