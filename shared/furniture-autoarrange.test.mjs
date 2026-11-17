import {test} from 'node:test';
import assert from 'node:assert/strict';
import {autoArrange,arrangeSettings,walkway,ARRANGE_DEFAULTS} from './furniture-autoarrange.mjs';
import {furnitureClearance} from './furniture-clearance.mjs';
import {cabinetFootprint,footprintsOverlap} from './furniture.mjs';
import {roomWalls} from './room-topology.mjs';
import {touchingCorners} from './furniture-snap.mjs';
import {validateProject} from './project.mjs';
import {rectangle} from './geometry.mjs';
const cab=(id,x,z,w=.8,d=.5,h=.9)=>({id,type:'cabinet',name:'Unit '+id,width:w,depth:d,height:h,thickness:.018,shelves:1,x,z,rotation:0});
const door=(id,wall,offset,extra={})=>({id,type:'door',wall,offset,width:.9,height:2,sill:0,hinge:'A',swing:'in',...extra});
const window_=(id,wall,offset,width=1.6,sill=.9)=>({id,type:'window',wall,offset,width,height:1.2,sill});
const room=(w,d,openings,furniture)=>validateProject({schemaVersion:6,units:'m',name:'R',presets:[],room:{height:2.8,outline:rectangle(w,d),openings,furniture}}).room;
// A 5 × 4 m room: door on the south wall, window on the north wall, five items dropped in the middle.
const living=()=>room(5,4,[door('door','south',.6),window_('win','north',1.5)],
  [cab('a',-1,0),cab('b',0,0,1.2,.6,2),cab('c',1,0),cab('d',0,1,.6,.6,.8),cab('e',1.5,1,2,.45,2.1)]);
// Which wall an item's back is against (within 1 mm of the gap), or null.
function backWall(r,item,gap) {
  const back=cabinetFootprint(item).slice(0,2);
  return roomWalls(r).find(w=>back.every(p=>Math.abs((p.x-w.a.x)*w.nx+(p.z-w.a.z)*w.nz-gap)<.001))?.id??null;
}

test('five items go against free walls, facing the room, connected, clear of the door and window',()=>{
  const start=living(),{room:r,placed,unplaced}=autoArrange(start,[]);
  assert.equal(placed.length,5);assert.deepEqual(unplaced,[]);
  const items=r.furniture;
  for(const item of items) assert.ok(backWall(r,item,ARRANGE_DEFAULTS.wallGap),`${item.id} stands against a wall`);
  for(let i=0;i<items.length;i++) for(let j=i+1;j<items.length;j++) assert.equal(footprintsOverlap(cabinetFootprint(items[i]),cabinetFootprint(items[j])),false);
  // Door: its opening, swing and 1 m in front stay clear; window: nothing taller than the sill within 0.6 m.
  for(const item of items) {
    const issues=furnitureClearance(r,item,{doorDepth:1,windowDepth:.6}).issues.map(i=>i.code);
    assert.ok(!issues.includes('DOOR_ZONE')&&!issues.includes('WINDOW_ZONE'),`${item.id}: ${issues}`);
  }
  // Connected: every item touches a neighbour, a room corner or the edge of a kept-clear zone.
  const touching=items.filter(a=>items.some(b=>b!==a&&touchingCorners(a,b).length));
  assert.ok(touching.length>=4,`${touching.map(i=>i.id)} share corners`);
  // Every item can be reached from the door along a 0.8 m walkway.
  const region=walkway(r,items,.8),entry=region({x:1.45,z:1.5});
  assert.notEqual(entry,null);
  for(const item of items) {const a=item.rotation*Math.PI/180,reach=item.depth/2+.45;assert.equal(region({x:item.x+Math.sin(a)*reach,z:item.z+Math.cos(a)*reach}),entry,`front of ${item.id} reachable`);}
  // One undo step's worth: the same input arranges the same way.
  assert.deepEqual(autoArrange(start,[]).room,r);
});

test('windows stay openable: tall pieces keep away, low pieces may stand below the sill',()=>{
  // The only free wall is the north wall, mostly window.
  // The north wall is mostly window; the other walls are taken up by wide doors.
  const wide=(id,wall)=>door(id,wall,.3,{width:2.4,swing:'out'});
  const r=room(3,3,[window_('win','north',.2,2.6,.9),wide('d1','east'),wide('d2','south'),wide('d3','west')],[cab('low',0,0,.8,.5,.8),cab('tall',-.4,.8,.6,.4,2)]);
  const result=autoArrange(r,['low','tall']);
  const low=result.room.furniture.find(f=>f.id==='low');
  assert.equal(backWall(result.room,low,.05),'north','the low piece goes under the window');
  // The tall piece finds no wall space; where it stood blocks the doors, so it moves to free floor space.
  assert.deepEqual(result.unplaced,[{id:'tall',name:'Unit tall',moved:true}]);
  const tall=result.room.furniture.find(f=>f.id==='tall');
  assert.deepEqual(furnitureClearance(r,r.furniture[1],{doorDepth:1}).issues.map(i=>i.code).filter(c=>c==='DOOR_ZONE').length>0,true,'its old spot was in a door zone');
  assert.deepEqual(furnitureClearance(result.room,tall,{doorDepth:1,windowDepth:.6}).issues.filter(i=>/DOOR|WINDOW/.test(i.code)),[]);
  // A piece that is clear where it stands keeps its place.
  const kept=autoArrange(room(3,3,r.openings,[cab('low',0,-.6,.8,.5,.8),cab('tall',0,.2,.4,.3,2)]),['low','tall']);
  assert.deepEqual(kept.unplaced,[{id:'tall',name:'Unit tall',moved:false}]);
});

test('a walkway between two doors stays open',()=>{
  // A 1.6 m wide corridor with a door at each end and two 0.4 m deep pieces near the west end.
  const r=room(6,1.6,[door('west','west',.35),door('east','east',.35)],[cab('a',-1.4,-.2,1,.4,.9),cab('b',-1.4,.25,1,.4,.9)]);
  const facingEachOther=autoArrange(r,[],{passage:0}).room.furniture;
  assert.equal(facingEachOther[0].x,facingEachOther[1].x,'without a walkway they would stand opposite each other (0.7 m apart)');
  const {room:result}=autoArrange(r,[]),[a,b]=result.furniture;
  assert.ok(Math.abs(a.x-b.x)>=1-1e-9,'with it they do not overlap along the corridor');
  const region=walkway(result,result.furniture,.8);
  assert.notEqual(region({x:-3+.45,z:0}),null);assert.equal(region({x:-3+.45,z:0}),region({x:3-.45,z:0}),'the doors stay connected');
});

test('only the chosen items move; settings are checked',()=>{
  const start=living(),{room:r,placed}=autoArrange(start,['a','c']);
  assert.deepEqual(placed.map(p=>p.id).sort(),['a','c']);
  for(const id of ['b','d','e']) assert.deepEqual(r.furniture.find(f=>f.id===id),start.furniture.find(f=>f.id===id));
  assert.throws(()=>autoArrange(start,['nope']),/Choose furniture/);
  assert.throws(()=>arrangeSettings({passage:-1}),/passage/);
  assert.deepEqual(arrangeSettings({}),{...ARRANGE_DEFAULTS});
});

test('a full room: what does not fit stays where it was, and the rest is valid',()=>{
  const items=Array.from({length:7},(_,i)=>cab('k'+i,-1.6+(i%4)*1.05,-.45+Math.floor(i/4)*.9,1,.5,.9));
  const start=room(4.4,3.2,[door('door','south',1.5)],items),t=performance.now();
  const {room:r,placed,unplaced}=autoArrange(start,[]);
  const ms=performance.now()-t;
  assert.equal(placed.length+unplaced.length,7);assert.ok(placed.length>=6,`${placed.length} placed`);
  // Left-over pieces stay where they were, or move to the nearest free floor space.
  for(const u of unplaced) if(!u.moved) assert.deepEqual(r.furniture.find(f=>f.id===u.id),start.furniture.find(f=>f.id===u.id));
  const result=r.furniture;
  for(let i=0;i<result.length;i++) for(let j=i+1;j<result.length;j++) assert.equal(footprintsOverlap(cabinetFootprint(result[i]),cabinetFootprint(result[j])),false);
  for(const item of result) assert.ok(!furnitureClearance(r,item,{doorDepth:1}).issues.some(i=>i.code==='DOOR_ZONE'),`${item.id} keeps the door clear`);
  assert.ok(ms<5000,`${ms.toFixed(0)} ms`);
  // Far too much for the room: nothing changes and the reason is given.
  const crowded=room(3,3,[door('door','south',1)],Array.from({length:9},(_,i)=>cab('k'+i,-1+(i%3),-1+Math.floor(i/3),.9,.6,.9)));
  assert.throws(()=>autoArrange(crowded,[]),/Not enough free floor space to arrange .* clear of doors, windows and the walkway/);
});
