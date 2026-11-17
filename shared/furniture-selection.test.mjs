import {test} from 'node:test';
import assert from 'node:assert/strict';
import {toggleSelection,selectAll,pruneSelection} from './furniture-selection.mjs';
const invariant=({ids,active})=>assert.ok(ids.length?ids.includes(active):active===null,'active item is a member');

test('modifier clicks add and remove items while keeping an active item',()=>{
  let state={ids:['a'],active:'a'};
  state=toggleSelection(state,'b');assert.deepEqual(state,{ids:['a','b'],active:'b'});invariant(state);
  state=toggleSelection(state,'c');assert.deepEqual(state,{ids:['a','b','c'],active:'c'});
  state=toggleSelection(state,'a');assert.deepEqual(state,{ids:['b','c'],active:'c'},'removing a non-active item keeps the active one');
  state=toggleSelection(state,'c');assert.deepEqual(state,{ids:['b'],active:'b'},'removing the active item activates the last remaining');
  state=toggleSelection(state,'b');assert.deepEqual(state,{ids:[],active:null});invariant(state);
  assert.deepEqual(toggleSelection({ids:[],active:null},'x'),{ids:['x'],active:'x'});
});
test('select all keeps the active item when possible',()=>{
  assert.deepEqual(selectAll({active:'b'},['a','b','c']),{ids:['a','b','c'],active:'b'});
  assert.deepEqual(selectAll({active:null},['a','b']),{ids:['a','b'],active:'a'});
  assert.deepEqual(selectAll({active:'gone'},['a']),{ids:['a'],active:'a'});
  assert.deepEqual(selectAll({active:null},[]),{ids:[],active:null});
});
test('pruning drops missing items and repairs the active item',()=>{
  assert.deepEqual(pruneSelection({ids:['a','b','c'],active:'b'},['a','c']),{ids:['a','c'],active:'a'});
  assert.deepEqual(pruneSelection({ids:['a','b'],active:'a'},['a','b','z']),{ids:['a','b'],active:'a'});
  assert.deepEqual(pruneSelection({ids:['a'],active:'a'},[]),{ids:[],active:null});
  for(const [ids,present] of [[['a','b'],['b']],[['a'],['a']],[['x','y','z'],['z','x']]]) invariant(pruneSelection({ids,active:ids[0]},present));
});
