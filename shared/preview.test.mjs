import {test} from 'node:test';
import assert from 'node:assert/strict';
import {rectangle} from './geometry.mjs';
import {previewOpening,openingAtPoint} from './openings.mjs';
import {validateProject} from './project.mjs';
import {createPlacementPreview} from '../src/placement-preview.js';
const room={width:6,depth:4,height:3,outline:rectangle(6,4),openings:[]};
test('schema 3 doors migrate to explicit defaults and schema 6 orientation persists',()=>{
  const old={schemaVersion:3,units:'m',name:'Old',room:{...room,openings:[{id:'door',wall:'north',type:'door',offset:1,width:.9,height:2.1,sill:0}]}};
  const migrated=validateProject(old);assert.equal(migrated.schemaVersion,6);
  assert.equal(migrated.room.openings[0].hinge,'A');assert.equal(migrated.room.openings[0].swing,'in');
  migrated.room.openings[0].hinge='B';migrated.room.openings[0].swing='out';
  assert.deepEqual(validateProject(JSON.parse(JSON.stringify(migrated))),migrated);
  migrated.room.openings[0].hinge='C';assert.throws(()=>validateProject(migrated));
});
test('preview and commit use the same snapped result without mutating room state',()=>{
  const before=JSON.stringify(room),p={x:0,z:-2};
  const result=previewOpening(room,'north','door','new',p,.25,true);
  assert.equal(result.valid,true);assert.equal(result.opening.offset,2.5);
  assert.deepEqual(openingAtPoint(room,'north','door','new',p,.25,true),result.opening);
  const occupied={...room,openings:[result.opening]};
  const invalid=previewOpening(occupied,'north','window','overlap',p,.25,true);
  assert.equal(invalid.valid,false);assert.match(invalid.error,/separation/);
  assert.equal(JSON.stringify(room),before);
  assert.equal(createPlacementPreview(room,result).children[0].material.color.getHex(),0x6ce0a0);
  assert.equal(createPlacementPreview(room,invalid).children[0].material.color.getHex(),0xff776b);
});
