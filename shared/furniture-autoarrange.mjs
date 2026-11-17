// Auto-arrange: places furniture against free stretches of wall, connected to
// room corners and to each other, while keeping the room usable:
// - a door's opening and the floor in front of it (`doorDepth`, and at least
//   the door's own swing) stay free;
// - in front of a window (`windowDepth`) only furniture lower than the sill may
//   stand, so the window can still be opened;
// - every item keeps `serviceDepth` free in front of it (doors, drawers, use);
// - a walkway `passage` wide must still lead from every door to the others and
//   to the front of every arranged item (checked on a floor grid).
// Items are placed largest first, back to the wall and facing into the room.
// Each goes to the end of a free stretch (a room corner, a neighbour or the
// edge of a kept-clear zone) nearest to where it stood. Items that do not fit
// anywhere stay where they are. Other furniture is not moved.
import {cabinetFootprint,footprintsOverlap,footprintInside,validateFurniture} from './furniture.mjs';
import {roomWalls,wallLabel} from './room-topology.mjs';
import {furnitureClearance,cabinetServiceZone} from './furniture-clearance.mjs';
import {facing} from './furniture-snap.mjs';

import {ARRANGE_DEFAULTS} from './arrange-defaults.mjs';
export {ARRANGE_DEFAULTS};
export function arrangeSettings(input={}) {
  const out={...ARRANGE_DEFAULTS};
  for(const [key,max] of [['wallGap',1],['doorDepth',3],['windowDepth',3],['serviceDepth',3],['passage',3]]) {
    const value=input[key]??out[key];
    if(typeof value!=='number'||!Number.isFinite(value)||value<0||value>max) throw Error(`Arrange ${key} must be 0–${max} m`);
    out[key]=value;
  }
  return out;
}
const EPS=1e-9,round=v=>Math.round(v*1e6)/1e6;
const dist=(p,a,b)=>{const vx=b.x-a.x,vz=b.z-a.z,l=vx*vx+vz*vz,t=l?Math.max(0,Math.min(1,((p.x-a.x)*vx+(p.z-a.z)*vz)/l)):0;return Math.hypot(p.x-a.x-t*vx,p.z-a.z-t*vz);};
function polygonDistance(p,polygon) {
  let inside=true,best=Infinity;
  for(let i=0;i<polygon.length;i++) {
    const a=polygon[i],b=polygon[(i+1)%polygon.length];
    if((b.x-a.x)*(p.z-a.z)-(b.z-a.z)*(p.x-a.x)<0) inside=false;
    best=Math.min(best,dist(p,a,b));
  }
  return inside?0:best;
}

// ---- Walkway: floor cells where a person `passage` wide can stand, and which
// of them connect. Returns a lookup from points to connected regions.
export function walkway(room,furniture,passage) {
  const outline=room.outline,walls=roomWalls(room),r=passage/2;
  const xs=outline.map(p=>p.x),zs=outline.map(p=>p.z),minX=Math.min(...xs),minZ=Math.min(...zs);
  const width=Math.max(...xs)-minX,depth=Math.max(...zs)-minZ,cell=Math.max(.05,Math.sqrt(width*depth/4000));
  const nx=Math.ceil(width/cell),nz=Math.ceil(depth/cell),bodies=furniture.map(cabinetFootprint);
  const open=new Uint8Array(nx*nz),region=new Int32Array(nx*nz).fill(-1);
  const centre=(i,j)=>({x:minX+(i+.5)*cell,z:minZ+(j+.5)*cell});
  for(let i=0;i<nx;i++) for(let j=0;j<nz;j++) {
    const p=centre(i,j);
    open[i*nz+j]=walls.every(w=>(p.x-w.a.x)*w.nx+(p.z-w.a.z)*w.nz>=r-EPS)&&bodies.every(b=>polygonDistance(p,b)>=r-EPS)?1:0;
  }
  let count=0;
  for(let start=0;start<open.length;start++) {
    if(!open[start]||region[start]>=0) continue;
    const queue=[start];region[start]=count;
    while(queue.length) {
      const k=queue.pop(),i=Math.floor(k/nz),j=k%nz;
      for(const [di,dj] of [[1,0],[-1,0],[0,1],[0,-1]]) {
        const a=i+di,b=j+dj,n=a*nz+b;
        if(a>=0&&a<nx&&b>=0&&b<nz&&open[n]&&region[n]<0) {region[n]=count;queue.push(n);}
      }
    }
    count++;
  }
  // The region of a point: its own cell, or an open cell next to it.
  return p=>{
    const i=Math.floor((p.x-minX)/cell),j=Math.floor((p.z-minZ)/cell);let found=null;
    for(const [di,dj] of [[0,0],[1,0],[-1,0],[0,1],[0,-1],[1,1],[1,-1],[-1,1],[-1,-1]]) {
      const a=i+di,b=j+dj;
      if(a>=0&&a<nx&&b>=0&&b<nz&&region[a*nz+b]>=0) {found=region[a*nz+b];break;}
    }
    return found;
  };
}
// Where people stand: just inside each door, and in front of an item.
function doorPoints(room,passage) {
  return (room.openings??[]).filter(o=>o.type==='door').map(o=>{
    const w=roomWalls(room).find(w=>w.id===o.wall),along=o.offset+o.width/2,inward=passage/2+.05;
    return {id:o.id,x:w.a.x+along*w.tx+inward*w.nx,z:w.a.z+along*w.tz+inward*w.nz};
  });
}
function frontPoint(item,passage) {
  const r=item.rotation*Math.PI/180,reach=item.depth/2+passage/2+.05;
  return {id:item.id,x:item.x+Math.sin(r)*reach,z:item.z+Math.cos(r)*reach};
}

// ---- Free stretches of wall
// Kept-clear zones in front of openings, as polygons. Windows only matter for
// items taller than their sill.
function openingZones(room,settings) {
  const zones=[];
  for(const o of room.openings??[]) {
    const w=roomWalls(room).find(w=>w.id===o.wall);
    const depth=o.type==='door'?Math.max(settings.doorDepth,(o.swing??'in')==='in'?o.width:0):settings.windowDepth;
    if(!(depth>0)) continue;
    const point=(along,inward)=>({x:w.a.x+along*w.tx+inward*w.nx,z:w.a.z+along*w.tz+inward*w.nz});
    zones.push({polygon:[point(o.offset,0),point(o.offset+o.width,0),point(o.offset+o.width,depth),point(o.offset,depth)],sill:o.type==='window'?o.sill:null});
  }
  return zones;
}
// Centre positions along a wall where an item (width w, depth d) fits, given
// the room and obstacles, as intervals [from, to] of distance from the wall start.
function freeStretches(room,wall,item,settings,bodies,zones) {
  const g=settings.wallGap,w=item.width,d=item.depth,s=settings.serviceDepth;
  let lo=-Infinity,hi=Infinity;
  // Stay inside every wall (with the gap): a linear bound on the centre c per corner.
  for(const other of roomWalls(room)) {
    const tn=wall.tx*other.nx+wall.tz*other.nz,nn=wall.nx*other.nx+wall.nz*other.nz,base=(wall.a.x-other.a.x)*other.nx+(wall.a.z-other.a.z)*other.nz;
    for(const du of [-w/2,w/2]) for(const v of [g,g+d]) {
      const need=(other.id===wall.id?0:g)-base-du*tn-v*nn;   // c·tn ≥ need
      if(Math.abs(tn)<1e-12) {if(need>1e-9) return [];}
      else if(tn>0) lo=Math.max(lo,need/tn);else hi=Math.min(hi,need/tn);
    }
  }
  if(lo>hi+EPS) return [];
  const local=p=>({u:(p.x-wall.a.x)*wall.tx+(p.z-wall.a.z)*wall.tz,v:(p.x-wall.a.x)*wall.nx+(p.z-wall.a.z)*wall.nz});
  const blocked=[];
  const block=(polygon,vFrom,vTo)=>{
    const pts=polygon.map(local),u0=Math.min(...pts.map(p=>p.u)),u1=Math.max(...pts.map(p=>p.u)),v0=Math.min(...pts.map(p=>p.v)),v1=Math.max(...pts.map(p=>p.v));
    if(v0<vTo-EPS&&v1>vFrom+EPS) blocked.push([u0-w/2,u1+w/2]);
  };
  // Other items block the body and its front service strip; zones block the body.
  for(const b of bodies) block(b.footprint,g,g+d+s);
  for(const b of bodies) if(s>0) block(b.service,g,g+d);
  for(const z of zones) if(z.sill===null||item.height>z.sill+EPS) block(z.polygon,g,g+d);
  blocked.sort((a,b)=>a[0]-b[0]);
  const out=[];let start=lo;
  for(const [a,b] of blocked) {
    if(b<=start+EPS) continue;
    if(a>=hi-EPS) break;
    if(a>=start-EPS) out.push([start,Math.min(a,hi)]);
    start=Math.max(start,b);
  }
  if(start<=hi+EPS) out.push([start,hi]);
  return out.filter(([a,b])=>b>=a-EPS);
}

// Arranges the items `ids` (all furniture when empty). Returns the new room,
// the items placed against a wall, and the items for which no wall space was
// left: they keep their position if it is still free, or else move to the
// nearest free floor space (clear of doors and windows, keeping the walkway).
export function autoArrange(room,ids=[],input={}) {
  const settings=arrangeSettings(input),moving=new Set(ids.length?ids:room.furniture.map(f=>f.id));
  if(![...moving].every(id=>room.furniture.some(f=>f.id===id))) throw Error('Choose furniture in this room to arrange');
  const zones=openingZones(room,settings),walls=roomWalls(room),doors=doorPoints(room,settings.passage);
  const clearance={minWall:0,doorDepth:settings.doorDepth,windowDepth:settings.windowDepth,serviceDepth:settings.serviceDepth};
  const body=item=>({item,footprint:cabinetFootprint(item),service:cabinetServiceZone(item,settings.serviceDepth)});
  const bodies=room.furniture.filter(f=>!moving.has(f.id)).map(body),placed=[],left=[];
  // Tries candidate positions in order; the first that keeps everything usable wins.
  const accept=(candidates,{checkService=true,reach=true}={})=>{
    const others=bodies.map(b=>b.item),points=[...doors,...placed.map(p=>frontPoint(p.item,settings.passage))];
    const before=walkway(room,others,settings.passage),linked=points.map(p=>before(p));
    let walks=0;
    for(const candidate of candidates) {
      const shape=cabinetFootprint(candidate.item);
      if(!footprintInside(shape,room.outline)||bodies.some(b=>footprintsOverlap(shape,b.footprint))) continue;
      const issues=furnitureClearance({...room,furniture:[...others,candidate.item]},candidate.item,checkService?clearance:{...clearance,serviceDepth:0}).issues;
      if(issues.some(i=>i.code!=='WALL_GAP')) continue;
      if(++walks>80) break;
      // The walkway: what was connected stays connected, and the item can be reached.
      const after=walkway(room,[...others,candidate.item],settings.passage),now=points.map(p=>after(p)),front=after(frontPoint(candidate.item,settings.passage));
      if(reach&&front===null) continue;
      if(points.some((p,i)=>points.some((q,j)=>j>i&&linked[i]!==null&&linked[i]===linked[j]&&(now[i]===null||now[i]!==now[j])))) continue;
      if(reach&&doors.length&&linked.slice(0,doors.length).some(r=>r!==null)&&!doors.some((d,i)=>linked[i]!==null&&now[i]===front)) continue;
      return candidate;
    }
    return null;
  };
  const items=room.furniture.filter(f=>moving.has(f.id)).sort((a,b)=>b.width*b.depth-a.width*a.depth||a.id.localeCompare(b.id));
  for(const item of items) {
    const candidates=[];
    for(const wall of walls) {
      const rotation=round(facing({x:wall.nx,z:wall.nz})),inward=settings.wallGap+item.depth/2;
      for(const [a,b] of freeStretches(room,wall,item,settings,bodies,zones)) for(const c of Math.abs(a-b)<EPS?[a]:[a,b]) {
        if(!Number.isFinite(c)) continue;
        const x=round(wall.a.x+c*wall.tx+inward*wall.nx),z=round(wall.a.z+c*wall.tz+inward*wall.nz);
        candidates.push({wall:wall.id,item:{...item,x,z,rotation},score:Math.hypot(x-item.x,z-item.z)});
      }
    }
    candidates.sort((a,b)=>a.score-b.score||a.wall.localeCompare(b.wall));
    const chosen=accept(candidates);
    if(chosen) {bodies.push(body(chosen.item));placed.push({id:item.id,name:item.name,wall:chosen.wall,item:chosen.item});}
    else left.push(item);
  }
  // No wall space: stay put if still free, else the nearest free floor space.
  const unplaced=[];
  for(const item of left) {
    // Its own spot first (if free, clear of doors and windows and off the walkway), then the nearest other one.
    let spot=accept([{item}],{checkService:false,reach:false});
    if(!spot) {
      const xs=room.outline.map(p=>p.x),zs=room.outline.map(p=>p.z),step=.1,candidates=[];
      for(let x=Math.min(...xs);x<=Math.max(...xs);x+=step) for(let z=Math.min(...zs);z<=Math.max(...zs);z+=step)
        candidates.push({item:{...item,x:round(x),z:round(z)},score:Math.hypot(x-item.x,z-item.z)});
      spot=accept(candidates.sort((a,b)=>a.score-b.score),{checkService:false,reach:false});
      if(!spot) throw Error(`Not enough free floor space to arrange “${item.name}” clear of doors, windows and the walkway; arrange fewer items or make room first`);
    }
    bodies.push(body(spot.item));unplaced.push({id:item.id,name:item.name,moved:spot.item!==item,item:spot.item});
  }
  const byId=new Map([...placed,...unplaced].map(p=>[p.id,p.item]));
  const furniture=validateFurniture(room.furniture.map(f=>byId.get(f.id)??f),room.outline,room.height);
  return {room:{...room,furniture},placed:placed.map(({id,name,wall})=>({id,name,wall,wallLabel:wallLabel(wall)})),unplaced:unplaced.map(({id,name,moved})=>({id,name,moved}))};
}
