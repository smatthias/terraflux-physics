// Stable identifiers for a room's walls and corners.
//
// A room is a closed, clockwise ring of walls. Wall IDs are stable identities,
// stored with the room once they differ from the original four (schema 7).
// Wall i runs from corner i (end A) to corner i+1 (end B); corner i is where
// the incoming wall i-1 meets the outgoing wall i.
//
// Corner IDs are derived from the two walls that meet there, so a corner keeps
// its ID for as long as those walls meet, and is redefined automatically when a
// wall is moved away, exchanged or removed:
//   compass pairs   west → north  ⇒  'north-west'
//   any other pair  north → wall-2 ⇒ 'north+wall-2'   (incoming+outgoing)
//
//        north-west ──── north ────▶ north-east
//            ▲                            │
//          west                         east
//            │                            ▼
//        south-west ◀──── south ──── south-east
//
// Functions accept a room ({outline, walls?}) or a bare four-corner outline,
// which uses the original wall IDs.
export const DEFAULT_WALL_IDS=Object.freeze(['north','east','south','west']);
export const MIN_WALLS=3,MAX_WALLS=32;
const WALL_ID=/^[a-z][a-z0-9]*(-[a-z0-9]+)*$/;
export const validWallId=id=>typeof id==='string' && id.length<=48 && WALL_ID.test(id);

const capitalise=text=>text[0].toUpperCase()+text.slice(1);
const at=(list,index)=>list[(index%list.length+list.length)%list.length];
function geometryOf(room) {
  const outline=Array.isArray(room)?room:room?.outline;
  const ids=Array.isArray(room)||!room?.walls?DEFAULT_WALL_IDS:room.walls.map(w=>w.id);
  if(!Array.isArray(outline) || outline.length!==ids.length) throw Error('Room walls must match its corners');
  return {outline,ids};
}
export const wallIds=room=>[...geometryOf(room).ids];
export const usesDefaultWalls=walls=>!walls || (walls.length===4 && walls.every((w,i)=>w.id===DEFAULT_WALL_IDS[i]));

export function cornerId(incoming,outgoing) {
  const pair=[incoming,outgoing],ns=pair.find(id=>id==='north'||id==='south'),ew=pair.find(id=>id==='east'||id==='west');
  return ns&&ew ? ns+'-'+ew : incoming+'+'+outgoing;
}
export const cornerIds=room=>{const {ids}=geometryOf(room);return ids.map((id,i)=>cornerId(at(ids,i-1),id));};

export const isWallId=(room,id)=>geometryOf(room).ids.includes(id);
export const isCornerId=(room,id)=>cornerIds(room).includes(id);
export function wallIndex(room,id) {
  const index=geometryOf(room).ids.indexOf(id);
  if(index<0) throw Error('Unknown wall');
  return index;
}
export function cornerIndex(room,id) {
  const index=cornerIds(room).indexOf(id);
  if(index<0) throw Error('Unknown corner');
  return index;
}

// 'north' → 'North wall', 'north-2' → 'North wall 2', 'wall-5' → 'Wall 5'.
export function wallLabel(id) {
  if(!validWallId(id)) throw Error('Unknown wall');
  const [,name,suffix]=/^([a-z]+)((?:-\d+)*)$/.exec(id)??[null,null,null];
  if(name===null) return 'Wall '+id;
  return (name==='wall'?'Wall':capitalise(name)+' wall')+(suffix?' '+suffix.slice(1):'');
}
export function cornerLabel(id) {
  if(/^(north|south)-(east|west)$/.test(id)) return capitalise(id)+' corner';
  const pair=id.split('+');
  if(pair.length!==2) throw Error('Unknown corner');
  return 'Corner '+pair.map(wallLabel).join(' / ');
}

// Clockwise navigation; negative steps walk anticlockwise.
export const nextWall=(room,id,steps=1)=>at(geometryOf(room).ids,wallIndex(room,id)+steps);
export const nextCorner=(room,id,steps=1)=>at(cornerIds(room),cornerIndex(room,id)+steps);
function opposite(count,kind) {
  if(count%2) throw Error('A room with an odd number of walls has no opposite '+kind);
  return count/2;
}
export const oppositeWall=(room,id)=>nextWall(room,id,opposite(wallIds(room).length,'wall'));
export const oppositeCorner=(room,id)=>nextCorner(room,id,opposite(wallIds(room).length,'corner'));

export function wallCorners(room,wall) {
  const index=wallIndex(room,wall),corners=cornerIds(room);
  return {a:corners[index],b:at(corners,index+1)};
}
export function cornerWalls(room,corner) {
  const index=cornerIndex(room,corner),{ids}=geometryOf(room);
  return {incoming:at(ids,index-1),outgoing:ids[index]};
}

export function cornerPoint(room,corner) {
  const {x,z}=geometryOf(room).outline[cornerIndex(room,corner)];
  return {x,z};
}
// Unit direction (tx, tz) runs A → B; the inward normal (nx, nz) points into the room.
export function wallSegment(room,wall) {
  const {outline}=geometryOf(room),index=wallIndex(room,wall),corners=wallCorners(room,wall);
  const {x:ax,z:az}=outline[index],{x:bx,z:bz}=at(outline,index+1);
  const length=Math.hypot(bx-ax,bz-az),tx=(bx-ax)/length,tz=(bz-az)/length;
  return {id:wall,corners,a:{x:ax,z:az},b:{x:bx,z:bz},length,tx,tz,nx:-tz,nz:tx};
}
export const roomWalls=room=>wallIds(room).map(id=>wallSegment(room,id));
// Interior angle in degrees between the two walls that meet at the corner.
export function roomCorners(room) {
  return cornerIds(room).map(id=>{
    const point=cornerPoint(room,id),before=cornerPoint(room,nextCorner(room,id,-1)),after=cornerPoint(room,nextCorner(room,id));
    const u={x:before.x-point.x,z:before.z-point.z},v={x:after.x-point.x,z:after.z-point.z};
    const cosine=(u.x*v.x+u.z*v.z)/(Math.hypot(u.x,u.z)*Math.hypot(v.x,v.z));
    return {id,...point,walls:cornerWalls(room,id),interiorAngle:Math.acos(Math.max(-1,Math.min(1,cosine)))*180/Math.PI};
  });
}

// Normalises stored walls ([{id}], one per corner). Returns undefined for the
// original four walls so such rooms stay byte-identical to schema 6.
export function validateWalls(walls,cornerCount) {
  if(walls===undefined) {
    if(cornerCount!==4) throw Error('Rooms without four corners need a wall list');
    return undefined;
  }
  if(!Array.isArray(walls) || walls.length!==cornerCount) throw Error('Room needs one wall per corner');
  const seen=new Set();
  const result=walls.map(wall=>{
    if(!wall || !validWallId(wall.id) || seen.has(wall.id)) throw Error('Wall IDs must be unique lowercase words, digits and hyphens (up to 48 characters)');
    seen.add(wall.id);return {id:wall.id};
  });
  return usesDefaultWalls(result)?undefined:result;
}
