import {test} from 'node:test';
import assert from 'node:assert/strict';
import {copyFurniture,cutFurniture,deleteFurniture,pasteFurniture,duplicateFurniture,rotateFurniture,moveFurnitureGroup,selectionBounds,roomSelectionBounds,CLIPBOARD_FORMAT} from './furniture-clipboard.mjs';
import {cabinetFootprint,footprintsOverlap} from './furniture.mjs';
import {validateProject} from './project.mjs';
import {rectangle} from './geometry.mjs';
import {lockRoom,checkRoomEdit} from './room-lock.mjs';
const cab=(id,x,z,rotation=0,extra={})=>({id,type:'cabinet',name:'Unit '+id,width:.8,depth:.5,height:1,thickness:.018,shelves:1,x,z,rotation,...extra});
const roomWith=(furniture,outline=rectangle(6,4))=>validateProject({schemaVersion:6,units:'m',name:'Clip',presets:[],room:{height:3,outline,openings:[],furniture}}).room;
const ids=()=>{let n=0;return ()=>'new-'+(++n);};
const close=(a,b,m)=>assert.ok(Math.abs(a-b)<1e-9,`${m??''} ${a} != ${b}`);

test('copy captures the selection without changing the room',()=>{
  const room=roomWith([cab('a',0,0),cab('b',1,0),cab('c',-2,1)]),before=structuredClone(room);
  const clipboard=copyFurniture(room,['b','a']);
  assert.equal(clipboard.format,CLIPBOARD_FORMAT);assert.equal(clipboard.mode,'copy');
  assert.deepEqual(clipboard.items.map(i=>i.id),['a','b'],'room order, not click order');
  clipboard.items[0].x=99;assert.deepEqual(room,before,'the clipboard is a deep copy');
  assert.throws(()=>copyFurniture(room,[]),/Select furniture first/);
  assert.throws(()=>copyFurniture(room,['a','gone']),/no longer exists/);
});

test('cut removes the selection and paste puts it back in place with new IDs and original names',()=>{
  const room=roomWith([cab('a',0,0,30),cab('b',1.5,0)]);
  const cut=cutFurniture(room,['a']);
  assert.deepEqual(cut.room.furniture.map(i=>i.id),['b']);assert.equal(cut.clipboard.mode,'cut');
  const pasted=pasteFurniture(cut.room,cut.clipboard,{makeId:ids()});
  const item=pasted.room.furniture.find(i=>i.id==='new-1');
  assert.deepEqual(pasted.ids,['new-1']);
  assert.deepEqual({...item,id:'a'},room.furniture[0],'same parameters, position and name');
});

test('copy and paste offsets to the nearest free position and names copies once',()=>{
  const room=roomWith([cab('a',0,0),cab('b',1,0)]),makeId=ids();
  const first=pasteFurniture(room,copyFurniture(room,['a','b']),{makeId,spacing:.25,snap:true});
  const added=first.room.furniture.filter(i=>first.ids.includes(i.id));
  assert.deepEqual(added.map(i=>i.name),['Unit a (copy)','Unit b (copy)']);
  // The group moves rigidly by one grid-aligned offset and overlaps nothing.
  const dx=added[0].x-0,dz=added[0].z-0;close(added[1].x-1,dx);close(added[1].z,dz);
  assert.ok(Math.abs(dx/.25-Math.round(dx/.25))<1e-9&&Math.abs(dz/.25-Math.round(dz/.25))<1e-9,'offset stays on the grid');
  const footprints=first.room.furniture.map(cabinetFootprint);
  for(let i=0;i<footprints.length;i++)for(let j=i+1;j<footprints.length;j++) assert.equal(footprintsOverlap(footprints[i],footprints[j]),false);
  const again=pasteFurniture(first.room,copyFurniture(first.room,first.ids),{makeId});
  assert.equal(again.room.furniture.at(-1).name,'Unit b (copy)','(copy) is not stacked');
  const long=roomWith([cab('a',0,0,0,{name:'x'.repeat(60)})]);
  assert.equal(duplicateFurniture(long,['a'],{makeId:ids()}).room.furniture.at(-1).name.length,60);
});

test('paste at a point centres the group on the snapped point or explains why it cannot',()=>{
  const room=roomWith([cab('a',0,0),cab('b',1,0)]),clipboard=copyFurniture(room,['a','b']);
  const placed=pasteFurniture(room,clipboard,{makeId:ids(),point:{x:-1.9,z:1.4},spacing:.25,snap:true});
  const bounds=roomSelectionBounds(placed.room,placed.ids);
  close(bounds.x,-2);close(bounds.z,1.5);
  assert.throws(()=>pasteFurniture(room,clipboard,{makeId:ids(),point:{x:0,z:0}}),/^Error: Cannot paste here: Cabinets cannot overlap/);
  assert.throws(()=>pasteFurniture(room,clipboard,{makeId:ids(),point:{x:2.9,z:0}}),/Cannot paste here: Cabinet must fit inside the room/);
});

test('paste reports a full room, the item limit and invalid clipboards',()=>{
  const tiny=roomWith([cab('a',0,0)],rectangle(1,1));
  assert.throws(()=>duplicateFurniture(tiny,['a'],{makeId:ids()}),/No free space in the room for the pasted item/);
  const room=roomWith([cab('a',0,0)]);
  assert.throws(()=>pasteFurniture(room,{format:'other'},{makeId:ids()}),/does not contain furniture/);
  assert.throws(()=>pasteFurniture(room,copyFurniture(room,['a']),{}),/ID generator/);
  const tall=copyFurniture(room,['a']);tall.items[0].height=3.5;
  assert.throws(()=>pasteFurniture(roomWith([],rectangle(6,4)),tall,{makeId:ids(),point:{x:0,z:0}}),/Invalid cabinet dimensions/,'room-height check still applies');
  assert.throws(()=>pasteFurniture(roomWith([],rectangle(6,4)),tall,{makeId:ids()}),/Invalid cabinet dimensions/);
  const many=roomWith(Array.from({length:64},(_,n)=>cab('c'+n,-14.5+(n%16)*1.9,-7+Math.floor(n/16)*3.5)),rectangle(31,16));
  assert.throws(()=>duplicateFurniture(many,['c0'],{makeId:ids()}),/at most 64 furniture items/);
});

test('pasting works across rooms and keeps the wall lock',()=>{
  const source=roomWith([cab('a',0,0,45)]),target=lockRoom(roomWith([],[{x:-2,z:-2},{x:2,z:-1.5},{x:1.5,z:2},{x:-2,z:1.5}]));
  const pasted=pasteFurniture(target,copyFurniture(source,['a']),{makeId:ids()});
  assert.equal(pasted.room.locked,true);assert.doesNotThrow(()=>checkRoomEdit(target,pasted.room));
  assert.equal(pasted.room.furniture[0].rotation,45);
});

test('delete removes exactly the selection',()=>{
  const room=roomWith([cab('a',0,0),cab('b',1,0),cab('c',-2,1)]);
  assert.deepEqual(deleteFurniture(room,['a','c']).furniture.map(i=>i.id),['b']);
  assert.throws(()=>deleteFurniture(room,['zzz']),/no longer exists/);
});

test('rotation turns a selection rigidly about its centre',()=>{
  const room=roomWith([cab('a',0,0,10),cab('b',1.2,.3,0),cab('c',-2,1)]);
  for(const degrees of [90,-15,15,180,-90]){
    const before=roomSelectionBounds(room,['a','b']),turned=rotateFurniture(room,['a','b'],degrees);
    const angle=degrees*Math.PI/180,c=Math.cos(angle),s=Math.sin(angle);
    for(const id of ['a','b']){
      const old=cabinetFootprint(room.furniture.find(i=>i.id===id)),now=cabinetFootprint(turned.furniture.find(i=>i.id===id));
      old.forEach((p,k)=>{const dx=p.x-before.x,dz=p.z-before.z;close(now[k].x,before.x+c*dx+s*dz,id);close(now[k].z,before.z-s*dx+c*dz,id);});
    }
    assert.deepEqual(turned.furniture.find(i=>i.id==='c'),room.furniture.find(i=>i.id==='c'),'unselected items stay');
    assert.ok(turned.furniture.every(i=>i.rotation>=0&&i.rotation<360));
  }
  const single=rotateFurniture(room,['c'],15).furniture.find(i=>i.id==='c');
  assert.deepEqual([single.x,single.z,single.rotation],[-2,1,15],'a single item turns in place');
  assert.equal(rotateFurniture(rotateFurniture(room,['a'],350),['a'],20).furniture[0].rotation,20,'angles wrap');
  assert.throws(()=>rotateFurniture(roomWith([cab('a',0,-1.7)]),['a'],90),/fit inside the room/,'long side swings into the north wall');
  assert.throws(()=>rotateFurniture(room,['a'],NaN),/finite rotation/);
});

test('group moves keep relative positions and snap the dragged item',()=>{
  const room=roomWith([cab('a',0,0),cab('b',1,.25),cab('c',-2,1)]);
  const moved=moveFurnitureGroup(room,['a','b'],'a',{x:-1.1,z:-1.1},.25,true).furniture;
  assert.deepEqual(moved.slice(0,2).map(i=>[i.x,i.z]),[[-1,-1],[0,-.75]]);
  assert.deepEqual(moved[2],room.furniture[2]);
  assert.throws(()=>moveFurnitureGroup(room,['a','b'],'c',{x:0,z:0},.25),/Drag a selected cabinet/);
  assert.throws(()=>moveFurnitureGroup(room,['a','b'],'a',{x:-2,z:1},.25),/overlap/);
});

test('selection bounds cover rotated footprints',()=>{
  const b=selectionBounds([cab('a',0,0,90),cab('b',2,0,0,{height:2})]);
  close(b.minX,-.25);close(b.maxX,2.4);close(b.minZ,-.4);close(b.maxZ,.4);assert.equal(b.height,2);
  assert.throws(()=>selectionBounds([]),/Select furniture/);
});
