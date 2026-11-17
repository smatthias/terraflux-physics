import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createHistory,commitHistory,undoHistory,redoHistory} from './history.mjs';
test('geometry history roundtrips snapshots without retaining mutable references',()=>{
  const room={height:3,outline:[{x:1,z:2}]};
  let state=createHistory(room);
  room.outline[0].x=7;
  assert.equal(state.present.outline[0].x,1);
  state=commitHistory(state,room);
  room.outline[0].x=99;
  assert.equal(state.present.outline[0].x,7);
  state=undoHistory(state); assert.equal(state.present.outline[0].x,1);
  state=redoHistory(state); assert.equal(state.present.outline[0].x,7);
});
test('no-op edits preserve redo while a new geometry branch clears it',()=>{
  let state=commitHistory(createHistory({height:3}),{height:4});
  state=undoHistory(state);
  assert.equal(commitHistory(state,{height:3}),state);
  state=commitHistory(state,{height:5});
  assert.equal(state.future.length,0); assert.equal(redoHistory(state),state);
  state=undoHistory(state); assert.equal(state.present.height,3);
  assert.equal(undoHistory(state),state);
});
test('history is bounded and fresh project load discards previous edits',()=>{
  let state=createHistory({height:1});
  for(let n=2;n<=150;n++) state=commitHistory(state,{height:n});
  assert.equal(state.past.length,100);
  const fresh=createHistory({height:8});
  assert.equal(fresh.past.length,0); assert.equal(fresh.future.length,0);
});
