// Builds rooms from their walls. Each wall is an infinite line with a stable ID;
// corner i is where wall i-1 meets wall i. Moving, exchanging or removing a
// wall therefore redefines the neighbouring corners automatically, while every
// other wall keeps its line. Each operation returns a complete, validated room or
// throws and leaves the input untouched; there is no partially edited state.
import {bounds,validateOutline} from './geometry.mjs';
import {roomWalls,wallIds,wallLabel,validWallId,validateWalls,MIN_WALLS,MAX_WALLS} from './room-topology.mjs';
import {validateOpenings} from './openings.mjs';
import {validateFurniture} from './furniture.mjs';
import {assertUnlocked} from './room-lock.mjs';

const round=value=>{const result=Number(value.toFixed(9));return result===0?0:result;};
const finitePoint=p=>p && [p.x,p.z].every(v=>typeof v==='number' && Number.isFinite(v) && Math.abs(v)<=1000);

// Wall lines pass through `point` with unit `direction` (A → B, clockwise).
export const wallLines=room=>roomWalls(room).map(w=>({id:w.id,point:w.a,direction:{x:w.tx,z:w.tz}}));
function lineThrough(id,a,b) {
  if(!finitePoint(a) || !finitePoint(b)) throw Error('Wall points need finite coordinates between -1000 and 1000 m');
  const length=Math.hypot(b.x-a.x,b.z-a.z);
  if(length<.1-1e-9) throw Error('Wall points must be at least 0.1 m apart');
  return {id,point:{x:a.x,z:a.z},direction:{x:(b.x-a.x)/length,z:(b.z-a.z)/length}};
}
function meet(incoming,outgoing) {
  const d1=incoming.direction,d2=outgoing.direction,cross=d1.x*d2.z-d1.z*d2.x;
  if(Math.abs(cross)<1e-9) throw Error(`${wallLabel(incoming.id)} and ${wallLabel(outgoing.id)} are parallel and cannot meet`);
  const dx=outgoing.point.x-incoming.point.x,dz=outgoing.point.z-incoming.point.z,t=(dx*d2.z-dz*d2.x)/cross;
  return {x:round(incoming.point.x+t*d1.x),z:round(incoming.point.z+t*d1.z)};
}

// Corners and stored walls for a clockwise ring of wall lines.
export function roomFromWalls(lines) {
  if(!Array.isArray(lines) || lines.length<MIN_WALLS) throw Error(`A room needs at least ${MIN_WALLS} walls`);
  if(lines.length>MAX_WALLS) throw Error(`A room supports at most ${MAX_WALLS} walls`);
  const n=lines.length,outline=lines.map((line,i)=>meet(lines[(i+n-1)%n],line));
  lines.forEach((line,i)=>{
    const a=outline[i],b=outline[(i+1)%n];
    if((b.x-a.x)*line.direction.x+(b.z-a.z)*line.direction.z<.1-1e-9)
      throw Error(`${wallLabel(line.id)} would be inverted or shorter than 0.1 m`);
  });
  const walls=lines.map(line=>({id:line.id}));
  validateWalls(walls,n);
  return {outline:validateOutline(outline),walls};
}

// Openings keep their world position: each is projected onto the nearest of its
// candidate host walls. Openings whose wall was removed are dropped.
function rehostOpenings(before,after,hosts) {
  const oldWalls=new Map(roomWalls(before).map(w=>[w.id,w])),newWalls=new Map(roomWalls(after).map(w=>[w.id,w]));
  return (before.openings??[]).flatMap(opening=>{
    const old=oldWalls.get(opening.wall),centre={x:old.a.x+old.tx*(opening.offset+opening.width/2),z:old.a.z+old.tz*(opening.offset+opening.width/2)};
    const candidates=(hosts[opening.wall]??[opening.wall]).map(id=>newWalls.get(id)).filter(Boolean);
    if(!candidates.length) return [];
    const best=candidates.map(wall=>{
      const along=(centre.x-wall.a.x)*wall.tx+(centre.z-wall.a.z)*wall.tz,clamped=Math.max(0,Math.min(wall.length,along));
      return {wall,along,distance:Math.hypot(centre.x-wall.a.x-wall.tx*clamped,centre.z-wall.a.z-wall.tz*clamped)};
    }).reduce((a,b)=>b.distance<a.distance-1e-9?b:a);
    return [{...opening,wall:best.wall.id,offset:round(best.along-opening.width/2)}];
  });
}

function rebuild(room,lines,hosts,action) {
  try {
    const {outline,walls}=roomFromWalls(lines),{width,depth}=bounds(outline);
    const stored=validateWalls(walls,walls.length),geometry={outline,walls:stored};
    const {walls:_previous,...rest}=room;
    const openings=validateOpenings(rehostOpenings(room,geometry,hosts),geometry,room.height);
    const furniture=validateFurniture(room.furniture??[],outline,room.height);
    return {...rest,width,depth,outline,...(stored?{walls:stored}:{}),openings,furniture};
  } catch(error) { throw Error(`Cannot ${action}: ${error.message}`); }
}
function lineIndex(lines,wall) {
  const index=lines.findIndex(line=>line.id===wall);
  if(index<0) throw Error('Unknown wall');
  return index;
}

// Parallel move; positive distances push the wall outward, negative pull it in.
// Neighbouring walls keep their direction and are extended or shortened.
export function moveWall(room,wall,distance) {
  assertUnlocked(room,`move ${wallLabel(wall)}`);
  if(typeof distance!=='number' || !Number.isFinite(distance) || Math.abs(distance)>1000) throw Error('Enter a wall move distance between -1000 and 1000 m');
  const lines=wallLines(room),index=lineIndex(lines,wall),line=lines[index];
  lines[index]={...line,point:{x:line.point.x+line.direction.z*distance,z:line.point.z-line.direction.x*distance}};
  return rebuild(room,lines,{},`move ${wallLabel(wall)}`);
}

// The two neighbouring walls are extended until they meet at a new corner.
export function removeWall(room,wall) {
  assertUnlocked(room,`remove ${wallLabel(wall)}`);
  const lines=wallLines(room),index=lineIndex(lines,wall);
  lines.splice(index,1);
  return rebuild(room,lines,{[wall]:[]},`remove ${wallLabel(wall)}`);
}

// A single replacement keeps the wall's identity. A split retires it: the new
// walls get unused IDs, 'north' → 'north-1', 'north-2' (never the replaced ID).
function replacementIds(wall,count,taken) {
  if(count===1) return [wall];
  const base=/^[a-z]+/.exec(wall)[0],ids=[];
  for(let k=1;ids.length<count;k++) if(!taken.has(base+'-'+k) && base+'-'+k!==wall) ids.push(base+'-'+k);
  return ids;
}
// Exchanges one wall for one or more walls, each given as a line through two
// points {a, b} (clockwise, A → B) with an optional `id`. The neighbouring walls
// keep their lines and meet the replacement at redefined corners. A single
// replacement keeps the wall ID; openings move to the nearest replacement wall.
export function replaceWall(room,wall,segments) {
  assertUnlocked(room,`replace ${wallLabel(wall)}`);
  if(!Array.isArray(segments) || segments.length<1 || segments.length>8) throw Error('Replace a wall with 1–8 walls');
  const lines=wallLines(room),index=lineIndex(lines,wall);
  const taken=new Set(wallIds(room).filter(id=>id!==wall)),defaults=replacementIds(wall,segments.length,taken);
  const ids=segments.map((segment,i)=>segment.id??defaults[i]);
  for(const id of ids) {
    if(!validWallId(id)) throw Error('Wall IDs must be lowercase words, digits and hyphens (up to 48 characters)');
    if(taken.has(id)) throw Error(`Wall ID ${id} is already used in this room`);
  }
  if(new Set(ids).size!==ids.length) throw Error('Replacement wall IDs must be unique');
  try {lines.splice(index,1,...segments.map((segment,i)=>lineThrough(ids[i],segment?.a,segment?.b)));}
  catch(error) {throw Error(`Cannot replace ${wallLabel(wall)}: ${error.message}`);}
  return rebuild(room,lines,{[wall]:ids},`replace ${wallLabel(wall)}`);
}
