import {test} from 'node:test';
import assert from 'node:assert/strict';
import {isLocked,lockRoom,unlockRoom,checkRoomEdit,assertUnlocked,SHELL_KEYS,LOCKED_MESSAGE} from './room-lock.mjs';
import {moveWall,removeWall,replaceWall} from './room-construction.mjs';
import {moveCorner,rectangle} from './geometry.mjs';
import {validateProject} from './project.mjs';
import {moveFurniture} from './furniture.mjs';
import {duplicateCabinet} from './furniture-library.mjs';
import {arrangeCabinet} from './cabinet-arrange.mjs';
import {parseProjectJson,serializeProjectJson} from './project-json.mjs';
import {duplicateProject} from './duplicate-project.mjs';
import {createHistory,commitHistory,undoHistory,redoHistory} from './history.mjs';
const cabinet=(id,x,z)=>({id,type:'cabinet',name:id,width:.8,depth:.5,height:1,thickness:.018,shelves:1,x,z,rotation:0});
const base={schemaVersion:7,units:'m',name:'Lock',presets:[]};
const project=room=>validateProject({...base,room:{height:3,outline:rectangle(6,4),openings:[{id:'d',type:'door',wall:'north',offset:1,width:.9,height:2,sill:0}],furniture:[cabinet('a',0,0),cabinet('b',2,1)],...room}});
const normal=room=>validateProject({...base,room}).room;

test('locking and unlocking are explicit, pure state changes',()=>{
  const room=project().room,locked=lockRoom(room);
  assert.equal(isLocked(room),false);assert.equal(isLocked(locked),true);
  assert.deepEqual(unlockRoom(locked),room);
  assert.throws(()=>lockRoom(locked),/already locked/);assert.throws(()=>unlockRoom(room),/already unlocked/);
  assert.doesNotThrow(()=>checkRoomEdit(room,locked));assert.doesNotThrow(()=>checkRoomEdit(locked,room));
  assert.deepEqual(SHELL_KEYS,['outline','walls','height','openings']);
});

test('while locked the shell is fixed and the interior stays editable',()=>{
  const locked=normal(lockRoom(project().room));
  const edits={
    outline:{...locked,outline:moveCorner(rectangle(6,4),'north-west',{x:-3.5,z:-2},.25,true)},
    walls:{...locked,outline:[{x:-3,z:-2},{x:0,z:-3},{x:3,z:-2},{x:3,z:2},{x:-3,z:2}],walls:['a','b','c','d','e'].map(id=>({id})),openings:[]},
    height:{...locked,height:2.9},
    openings:{...locked,openings:[]},
  };
  for(const [key,after] of Object.entries(edits)) assert.throws(()=>checkRoomEdit(locked,after),new RegExp(LOCKED_MESSAGE),key);
  const furnished={...locked,furniture:[...locked.furniture,cabinet('c',-2,1)]};
  assert.doesNotThrow(()=>checkRoomEdit(locked,furnished));
  assert.doesNotThrow(()=>checkRoomEdit(locked,{...locked,furniture:[]}));
  // Key order is irrelevant; only content counts.
  assert.doesNotThrow(()=>checkRoomEdit(locked,Object.fromEntries(Object.entries(locked).reverse())));
});

test('lock changes cannot carry other edits',()=>{
  const room=project().room;
  assert.throws(()=>checkRoomEdit(room,{...room,locked:true,height:2.9}),/separate step/);
  assert.throws(()=>checkRoomEdit(room,{...room,locked:true,furniture:[]}),/separate step/);
  const locked=lockRoom(room);
  assert.throws(()=>checkRoomEdit(locked,{...room,height:2.9}),/separate step/,'dropping the flag cannot smuggle in a wall edit');
});

test('wall operations refuse locked rooms with a clear reason',()=>{
  const locked=lockRoom(project().room);
  assert.throws(()=>moveWall(locked,'north',1),/^Error: Cannot move North wall: walls are locked/);
  assert.throws(()=>removeWall(locked,'east'),/^Error: Cannot remove East wall: walls are locked/);
  assert.throws(()=>replaceWall(locked,'north',[{a:{x:-3,z:-2.5},b:{x:3,z:-2}}]),/^Error: Cannot replace North wall: walls are locked/);
  assert.throws(()=>moveCorner(locked,'north-west',{x:-3.5,z:-2},.25,true),/^Error: Cannot move corners: walls are locked/);
  assert.throws(()=>assertUnlocked(locked),/Cannot edit the walls/);
  assert.doesNotThrow(()=>moveWall(unlockRoom(locked),'north',1));
});

test('interior operations keep the lock and pass the lock check',()=>{
  const locked=normal(lockRoom(project().room));
  const results=[
    moveFurniture(locked,'a',{x:-1,z:0},.25,true),
    duplicateCabinet(locked,'a','copy',{x:-2,z:1},.25,true),
    arrangeCabinet(locked,'b','a',{mode:'align',axis:'x',edge:'min'}),
  ];
  for(const room of results) {
    const next=normal(room);
    assert.equal(isLocked(next),true);assert.doesNotThrow(()=>checkRoomEdit(locked,next));
    assert.notDeepEqual(next.furniture,locked.furniture);
  }
});

test('the lock is saved as schema 7; unlocked rooms keep their schema 6 form',()=>{
  const unlocked=validateProject({...base,schemaVersion:6,room:{height:3,outline:rectangle(6,4),openings:[],furniture:[]}});
  assert.equal(unlocked.schemaVersion,6);assert.equal('locked' in unlocked.room,false);
  const locked=validateProject({...unlocked,schemaVersion:7,room:lockRoom(unlocked.room)});
  assert.equal(locked.schemaVersion,7);assert.equal(locked.room.locked,true);
  assert.deepEqual(validateProject({...locked,room:{...locked.room,locked:false}}),unlocked,'an explicit false is the default');
  assert.deepEqual(parseProjectJson(serializeProjectJson(locked)),locked);
  assert.equal(duplicateProject(locked).room.locked,true);
  assert.throws(()=>validateProject({...locked,schemaVersion:6}),/Locked rooms require schemaVersion 7/);
  assert.throws(()=>validateProject({...locked,room:{...locked.room,locked:'yes'}}),/true or false/);
});

test('undo and redo never change walls while they are locked',()=>{
  // Edit walls, lock, furnish, unlock, edit walls again: a linear history gated like the app.
  let room=project().room,history=createHistory(room);
  const commit=next=>{next=normal(next);checkRoomEdit(room,next);history=commitHistory(history,next);room=next;};
  commit(moveWall(room,'north',.5));
  commit(lockRoom(room));
  commit(moveFurniture(room,'a',{x:-1,z:0},.25,true));
  commit(duplicateCabinet(room,'b','b2',{x:-2,z:1},.25,true));
  commit(unlockRoom(room));
  commit(moveWall(room,'east',.5));
  commit(lockRoom(room));
  const states=[...history.past,history.present];
  assert.equal(states.length,8);
  const walk=[];
  while(history.past.length){const before=history.present;history=undoHistory(history);walk.push([before,history.present]);}
  while(history.future.length){const before=history.present;history=redoHistory(history);walk.push([before,history.present]);}
  for(const [before,after] of walk) assert.doesNotThrow(()=>checkRoomEdit(before,after));
  // Undoing from the end steps back through each lock before reaching a wall edit.
  const locks=states.map(isLocked);assert.deepEqual(locks,[false,false,true,true,true,false,false,true]);
  for(let i=1;i<states.length;i++) if(isLocked(states[i-1])&&isLocked(states[i]))
    for(const key of SHELL_KEYS) assert.deepEqual(states[i][key],states[i-1][key],`step ${i} ${key}`);
});
