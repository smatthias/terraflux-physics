import {test} from 'node:test';
import assert from 'node:assert/strict';
import {DEFAULT_WALL_IDS,wallIds,cornerIds,cornerId,wallCorners,cornerWalls,cornerPoint,wallSegment,roomWalls,roomCorners,nextWall,nextCorner,oppositeWall,oppositeCorner,wallIndex,cornerIndex,wallLabel,cornerLabel,isWallId,isCornerId,validateWalls} from './room-topology.mjs';
import {rectangle,moveCorner,moveCorners,scaleOutline,bounds} from './geometry.mjs';
import {validateProject} from './project.mjs';
import {wallLength,doorPose} from './openings.mjs';
import {CABINET_CORNER_IDS,CABINET_SIDE_IDS,cabinetCorners,cabinetSides,cabinetFootprint} from './furniture.mjs';
import {cabinetServiceZone} from './furniture-clearance.mjs';
const close=(a,b)=>assert.ok(Math.abs(a-b)<1e-9,`${a} != ${b}`);
const rotate=(outline,degrees)=>outline.map(({x,z})=>{const r=degrees*Math.PI/180;return {x:x*Math.cos(r)-z*Math.sin(r),z:x*Math.sin(r)+z*Math.cos(r)};});
const irregular=[{x:-3,z:-2},{x:2.5,z:-2.5},{x:3.5,z:2},{x:-2,z:1.5}];
const bay={outline:[{x:-3,z:-2},{x:0,z:-3},{x:3,z:-2},{x:3,z:2},{x:-3,z:2}],walls:['north-1','north-2','east','south','west'].map(id=>({id}))};
const triangle={outline:[{x:0,z:-2},{x:3,z:2},{x:-3,z:2}],walls:[{id:'north'},{id:'south'},{id:'west'}]};

test('the original four walls and corners keep their IDs and relationships',()=>{
  const room=rectangle(6,4);
  assert.deepEqual(DEFAULT_WALL_IDS,['north','east','south','west']);assert.ok(Object.isFrozen(DEFAULT_WALL_IDS));
  assert.deepEqual(wallIds(room),[...DEFAULT_WALL_IDS]);
  assert.deepEqual(cornerIds(room),['north-west','north-east','south-east','south-west']);
  for(const wall of wallIds(room)){
    const {a,b}=wallCorners(room,wall);
    assert.equal(cornerWalls(room,a).outgoing,wall,'a wall starts at its A corner');
    assert.equal(cornerWalls(room,b).incoming,wall,'a wall ends at its B corner');
  }
  assert.deepEqual(wallCorners(room,'west'),{a:'south-west',b:'north-west'});
  assert.deepEqual(cornerWalls(room,'north-west'),{incoming:'west',outgoing:'north'});
  assert.equal(wallLabel('north'),'North wall');assert.equal(cornerLabel('south-west'),'South-west corner');
});

test('corner IDs are derived from the walls that meet, so other rooms get redefined corners',()=>{
  assert.equal(cornerId('west','north'),'north-west');assert.equal(cornerId('north','east'),'north-east');
  assert.equal(cornerId('north','south'),'north+south');assert.equal(cornerId('west','north-1'),'west+north-1');
  assert.deepEqual(cornerIds(bay),['west+north-1','north-1+north-2','north-2+east','south-east','south-west']);
  assert.deepEqual(cornerIds(triangle),['north-west','north+south','south-west'],'west meets north: still the north-west corner');
  assert.deepEqual(cornerWalls(bay,'north-1+north-2'),{incoming:'north-1',outgoing:'north-2'});
  assert.deepEqual(cornerPoint(bay,'north-1+north-2'),{x:0,z:-3});
  for(const room of [bay,triangle]) assert.equal(new Set(cornerIds(room)).size,wallIds(room).length);
  assert.equal(wallLabel('north-2'),'North wall 2');assert.equal(wallLabel('wall-5'),'Wall 5');
  assert.equal(cornerLabel('north-1+north-2'),'Corner North wall 1 / North wall 2');
});

test('initial rectangle corners sit at their named compass positions with inward wall normals',()=>{
  const outline=rectangle(6,4),b=bounds(outline);
  assert.deepEqual(cornerPoint(outline,'north-west'),{x:b.minX,z:b.minZ});
  assert.deepEqual(cornerPoint(outline,'north-east'),{x:b.maxX,z:b.minZ});
  assert.deepEqual(cornerPoint(outline,'south-east'),{x:b.maxX,z:b.maxZ});
  assert.deepEqual(cornerPoint(outline,'south-west'),{x:b.minX,z:b.maxZ});
  for(const room of [outline,bay,triangle]) for(const wall of roomWalls(room)){
    const mid={x:(wall.a.x+wall.b.x)/2+wall.nx*.1,z:(wall.a.z+wall.b.z)/2+wall.nz*.1};
    const inside=roomWalls(room).every(w=>(mid.x-w.a.x)*w.nx+(mid.z-w.a.z)*w.nz>0);
    assert.ok(inside,wall.id+' normal must point into the room');
  }
  assert.equal(wallSegment(outline,'north').length,6);assert.equal(wallSegment(outline,'east').length,4);
});

test('IDs stay bound to the same corner through drags, endpoint edits, scaling and rotation',()=>{
  const moved=moveCorner(rectangle(6,4),'south-east',{x:3.5,z:2.5},.25,true);
  assert.deepEqual(cornerPoint(moved,'south-east'),{x:3.5,z:2.5});
  assert.deepEqual(cornerPoint(moved,'north-west'),{x:-3,z:-2},'other corners are untouched');
  const scaled=scaleOutline(moved,'x',10);
  assert.ok(cornerPoint(scaled,'south-east').x>cornerPoint(scaled,'south-west').x);
  const turned=rotate(rectangle(6,4),90);
  close(wallSegment(turned,'north').length,6);
  close(cornerPoint(turned,'north-west').x,turned[0].x);close(cornerPoint(turned,'north-west').z,turned[0].z);
  const edited=moveCorners(rectangle(6,4),{'north-west':{x:-3.3,z:-2.1},'north-east':{x:3.1,z:-2.4}},.25,false);
  assert.deepEqual([cornerPoint(edited,'north-west'),cornerPoint(edited,'north-east')],[{x:-3.3,z:-2.1},{x:3.1,z:-2.4}]);
  const bent=moveCorner(bay,'north-1+north-2',{x:0,z:-3.5},.25,true);
  assert.deepEqual(bent[1],{x:0,z:-3.5});assert.deepEqual(bent[0],bay.outline[0]);
});

test('moving corners by ID validates atomically and never mutates its input',()=>{
  const original=rectangle(6,4),copy=JSON.parse(JSON.stringify(original));
  assert.throws(()=>moveCorners(original,{'north-west':{x:-3,z:-2},'north-east':{x:-3,z:-2}},.25,false),/convex room/);
  assert.throws(()=>moveCorner(original,0,{x:-3,z:-2},.25,false),/Unknown corner/,'positional indexes are not accepted');
  assert.throws(()=>moveCorner(original,'northwest',{x:-3,z:-2},.25,false),/Unknown corner/);
  assert.throws(()=>moveCorner(bay,'north-east',{x:3,z:-2},.25,false),/Unknown corner/,'corners of other rooms do not exist');
  assert.deepEqual(original,copy);
});

test('clockwise navigation wraps, walks backwards and finds opposites',()=>{
  const room=rectangle(6,4);
  assert.equal(nextWall(room,'west'),'north');assert.equal(nextWall(room,'north',-1),'west');
  assert.equal(nextCorner(room,'south-west'),'north-west');assert.equal(nextCorner(room,'north-west',-1),'south-west');
  assert.equal(nextCorner(room,'north-east',4),'north-east');assert.equal(nextWall(room,'east',-6),'west');
  assert.equal(oppositeWall(room,'north'),'south');assert.equal(oppositeCorner(room,'north-east'),'south-west');
  assert.equal(nextWall(bay,'north-2'),'east');assert.equal(nextWall(bay,'north-1',-1),'west');
  assert.throws(()=>oppositeWall(bay,'east'),/odd number of walls/);
  for(const r of [room,bay,triangle])for(const corner of cornerIds(r)) assert.equal(wallCorners(r,cornerWalls(r,corner).outgoing).b,nextCorner(r,corner));
});

test('unknown IDs are rejected, including stale positional indexes',()=>{
  const room=rectangle(6,4);
  for(const bad of [0,1,'0','North',null,undefined,'floor']) {
    assert.throws(()=>wallIndex(room,bad),/^Error: Unknown wall$/);
    assert.throws(()=>cornerIndex(room,bad),/^Error: Unknown corner$/);
    assert.equal(isWallId(room,bad),false);assert.equal(isCornerId(room,bad),false);
  }
  assert.equal(isWallId(bay,'north'),false);assert.equal(isWallId(bay,'north-2'),true);
  assert.throws(()=>wallLength(rectangle(6,4),'ceiling'),/Unknown wall/);
  assert.throws(()=>cornerPoint(rectangle(6,4).slice(0,3),'north-west'),/walls must match its corners/);
  assert.throws(()=>wallIds({outline:rectangle(6,4),walls:[{id:'north'}]}),/walls must match its corners/);
});

test('corner descriptors report joining walls and interior angles',()=>{
  const corners=roomCorners(rectangle(6,4));
  for(const c of corners) close(c.interiorAngle,90);
  assert.deepEqual(corners[0].walls,{incoming:'west',outgoing:'north'});
  for(const room of [irregular,bay,triangle]){
    const angles=roomCorners(room).map(c=>c.interiorAngle);
    close(angles.reduce((n,a)=>n+a,0),(wallIds(room).length-2)*180);
  }
});

test('wall segments match the legacy index arithmetic on irregular outlines',()=>{
  irregular.forEach((a,i)=>{
    const b=irregular[(i+1)%4],wall=wallSegment(irregular,DEFAULT_WALL_IDS[i]);
    assert.deepEqual([wall.a,wall.b],[a,b]);
    close(wall.length,Math.hypot(b.x-a.x,b.z-a.z));close(wallLength(irregular,DEFAULT_WALL_IDS[i]),wall.length);
  });
});

test('door hinge end A is the wall A corner',()=>{
  const outline=rectangle(6,4),wall=wallSegment(outline,'east');
  const hinge=doorPose({offset:0,width:.9,hinge:'A'}).hingeAlong;
  assert.deepEqual({x:wall.a.x+wall.tx*hinge,z:wall.a.z+wall.tz*hinge},cornerPoint(outline,'north-east'));
});

test('wall lists are validated; the original four walls are not stored',()=>{
  assert.equal(validateWalls(undefined,4),undefined);
  assert.equal(validateWalls(DEFAULT_WALL_IDS.map(id=>({id,extra:1})),4),undefined);
  assert.deepEqual(validateWalls(bay.walls,5),bay.walls);
  assert.throws(()=>validateWalls(undefined,5),/need a wall list/);
  assert.throws(()=>validateWalls(bay.walls,4),/one wall per corner/);
  for(const id of ['North','a+b','-x','x-','',null,'a'.repeat(49)]) assert.throws(()=>validateWalls([{id},{id:'b'},{id:'c'}],3),/Wall IDs/);
  assert.throws(()=>validateWalls([{id:'a'},{id:'a'},{id:'c'}],3),/unique/);
});

test('original-wall projects stay schema 6; other rooms are stored as schema 7',()=>{
  const base={units:'m',name:'IDs',presets:[],room:{height:3,openings:[],furniture:[]}};
  const classic=validateProject({...base,schemaVersion:6,room:{...base.room,outline:irregular}});
  assert.equal(classic.schemaVersion,6);assert.deepEqual(classic.room.outline,irregular);assert.equal('walls' in classic.room,false);
  assert.deepEqual(validateProject({...base,schemaVersion:7,room:{...base.room,outline:irregular,walls:DEFAULT_WALL_IDS.map(id=>({id}))}}),classic);
  const five=validateProject({...base,schemaVersion:7,room:{...base.room,...bay,openings:[{id:'w',type:'window',wall:'north-2',offset:.5,width:1,height:1,sill:1}]}});
  assert.equal(five.schemaVersion,7);assert.deepEqual(five.room.walls,bay.walls);assert.equal(five.room.openings[0].wall,'north-2');
  assert.deepEqual(validateProject(JSON.parse(JSON.stringify(five))),five);
  assert.throws(()=>validateProject({...base,schemaVersion:6,room:{...base.room,...bay}}),/four corners/);
  const renamed={...base.room,outline:irregular,walls:['a','b','c','d'].map(id=>({id}))};
  assert.throws(()=>validateProject({...base,schemaVersion:6,room:renamed}),/require schemaVersion 7/,'custom walls are never silently renamed');
  assert.equal(validateProject({...base,schemaVersion:7,room:renamed}).room.walls.length,4);
  assert.throws(()=>validateProject({...base,schemaVersion:7,room:{...base.room,outline:bay.outline}}),/wall list/);
  assert.throws(()=>validateProject({...five,room:{...five.room,openings:[{...five.room.openings[0],wall:'north'}]}}),/valid wall/);
});

test('cabinet footprint corners and sides have stable front/back/left/right IDs',()=>{
  assert.deepEqual(CABINET_CORNER_IDS,['back-left','back-right','front-right','front-left']);
  assert.deepEqual(CABINET_SIDE_IDS,['back','right','front','left']);
  const item={x:1,z:2,width:.8,depth:.5,rotation:0};
  const corners=Object.fromEntries(cabinetCorners(item).map(c=>[c.id,c]));
  assert.deepEqual(corners['back-left'],{id:'back-left',x:.6,z:1.75});
  assert.deepEqual(corners['front-right'],{id:'front-right',x:1.4,z:2.25});
  for(const rotation of [0,37,90,180,270]){
    const rotated={...item,rotation},sides=Object.fromEntries(cabinetSides(rotated).map(s=>[s.id,s]));
    assert.deepEqual(cabinetCorners(rotated).map(({x,z})=>({x,z})),cabinetFootprint(rotated));
    const zone=cabinetServiceZone(rotated,.4),zoneCentre={x:zone.reduce((n,p)=>n+p.x,0)/4,z:zone.reduce((n,p)=>n+p.z,0)/4};
    const mid=s=>({x:(s.a.x+s.b.x)/2,z:(s.a.z+s.b.z)/2}),d=s=>Math.hypot(mid(s).x-zoneCentre.x,mid(s).z-zoneCentre.z);
    assert.ok(d(sides.front)<d(sides.back)&&d(sides.front)<d(sides.left)&&d(sides.front)<d(sides.right),'front at '+rotation);
    assert.deepEqual(sides.front.corners,{a:'front-right',b:'front-left'});
    // Unrotated, the front faces south, so a viewer facing it has east (+x) on their right.
    if(rotation===0) assert.ok(mid(sides.right).x>rotated.x&&mid(sides.left).x<rotated.x);
    const cross=(mid(sides.right).x-rotated.x)*(mid(sides.front).z-rotated.z)-(mid(sides.right).z-rotated.z)*(mid(sides.front).x-rotated.x);
    assert.ok(cross>0,'right side at '+rotation);
  }
});
