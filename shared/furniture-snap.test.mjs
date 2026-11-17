import {test} from 'node:test';
import assert from 'node:assert/strict';
import {snapFurniture,touchingCorners,describeSnaps,facing} from './furniture-snap.mjs';
import {validateProject} from './project.mjs';
import {rectangle} from './geometry.mjs';
// A 6 × 4 m room: north wall z = −2, east x = 3, south z = 2, west x = −3.
const cabinet=(id,x,z,extra={})=>({id,type:'cabinet',name:'Unit '+id,width:.8,depth:.5,height:.9,thickness:.018,shelves:1,x,z,rotation:0,...extra});
const room=(furniture,openings=[])=>validateProject({schemaVersion:6,units:'m',name:'Room',presets:[],room:{height:2.8,outline:rectangle(6,4),openings,furniture}}).room;
const close=(a,b,m)=>assert.ok(Math.abs(a-b)<1e-9,`${m??''} ${a} != ${b}`);

test('an item near a wall moves flush against it, keeping the wall gap',()=>{
  const r=room([cabinet('a',.3,-1.68)]);
  const snapped=snapFurniture(r,'a');
  close(snapped.item.z,-1.75,'back on the north wall');close(snapped.item.x,.3,'along the wall it stays');
  assert.deepEqual(snapped.snaps,[{kind:'wall',wall:'north'}]);
  close(snapFurniture(r,'a',{wallGap:.05}).item.z,-1.7,'5 cm wall gap');
  assert.equal(snapFurniture(room([cabinet('a',.3,-1.4)]),'a'),null,'30 cm away: no snap');
  assert.equal(describeSnaps(r,snapped.snaps),'North wall');
});

test('near a room corner it connects to both walls',()=>{
  const r=room([cabinet('a',-2.52,-1.7)]),{item,snaps}=snapFurniture(r,'a');
  close(item.x,-2.6);close(item.z,-1.75);
  assert.deepEqual(snaps.map(s=>s.wall).sort(),['north','west']);
});

test('next to another item it connects side to side, corner to corner',()=>{
  const r=room([cabinet('a',0,-1.75),cabinet('b',.87,-1.7)]),{item,snaps}=snapFurniture(r,'b');
  close(item.x,.8,'side against side');close(item.z,-1.75,'backs in line (and on the wall)');
  assert.ok(snaps.some(s=>s.kind==='furniture'&&s.otherId==='a'&&s.contact==='side'));
  assert.deepEqual(touchingCorners(item,r.furniture[0]).sort(),[['back-left','back-right'],['front-left','front-right']]);
  // In the middle of the room: fronts line up and the sides touch.
  const free=room([cabinet('a',0,0),cabinet('b',.9,.06)]),snap=snapFurniture(free,'b');
  close(snap.item.x,.8);close(snap.item.z,0);assert.deepEqual(snap.snaps.map(s=>s.contact).sort(),['edge','side']);
});

test('a nearly square item squares up to the wall; a clearly turned one keeps its angle',()=>{
  const r=room([cabinet('a',0,-1.6,{rotation:8})]),{item,snaps}=snapFurniture(r,'a');
  assert.equal(item.rotation,0);close(item.z,-1.75);assert.equal(snaps[0].kind,'rotation');
  assert.equal(snapFurniture(room([cabinet('a',0,-1.6,{rotation:8})]),'a',{rotate:false})?.item.rotation??8,8,'rotation snapping can be off');
  const turned=room([cabinet('a',0,-1.5,{rotation:30})]);
  assert.equal(snapFurniture(turned,'a'),null,'30° stays 30° (and is not parallel to the wall)');
  assert.equal(facing({x:-1,z:0}),270,'facing west: front towards −x');
  const east=room([cabinet('a',2.6,0,{rotation:85})]),e=snapFurniture(east,'a');
  assert.equal(e.item.rotation,90);close(e.item.x,2.75,'its side is 0.25 m from the centre');
});

test('snapping never moves an item into a door or window zone it kept clear',()=>{
  const door={id:'door',type:'door',wall:'north',offset:2.5,width:.9,height:2,sill:0,hinge:'A',swing:'out'};
  const r=room([cabinet('a',0,-1.62)],[door]);
  const settings={doorDepth:.1};
  // Unsnapped the body (z −1.87 … −1.37) is clear of the 0.1 m access zone; on the wall it would not be.
  assert.equal(snapFurniture(r,'a',{clearance:settings}),null);
  close(snapFurniture(room([cabinet('a',-2,-1.62)],[door]),'a',{clearance:settings}).item.z,-1.75,'away from the door it snaps');
  // Items of a dragged group are ignored.
  const group=room([cabinet('a',0,0),cabinet('b',.87,0)]);
  assert.equal(snapFurniture(group,'b',{ignore:['a']}),null);
});

import {evaluateFurniturePlacement} from './furniture-placement.mjs';
test('previews of new furniture connect too, when asked',()=>{
  const r=room([cabinet('a',0,-1.75)]),request={kind:'copy',sourceId:'a',id:'new',point:{x:.93,z:-1.7}};
  const plain=evaluateFurniturePlacement(r,request,.01,false);
  close(plain.candidate.x,.93);assert.equal(plain.snaps,undefined);
  const connected=evaluateFurniturePlacement(r,request,.01,false,false,{minWall:0},true);
  assert.equal(connected.valid,true);close(connected.candidate.x,.8);close(connected.candidate.z,-1.75);
  assert.ok(connected.snaps.some(s=>s.otherId==='a'));
});
