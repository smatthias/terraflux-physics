import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {savePreset,validatePresets} from './presets.mjs';
import {validateProject} from './project.mjs';
import {rectangle} from './geometry.mjs';
import {openingChainLayout} from '../src/dimensions.js';
import {fitCamera} from '../src/room-scene.js';
const preset={id:'entry',name:'Entry door',type:'door',width:.83,height:2.03,sill:0,hinge:'B',swing:'out'};
test('presets update by type/name, preserve IDs, validate and persist through project migration',()=>{
  const first=savePreset([],preset);
  const updated=savePreset(first,{...preset,id:'other',name:' entry DOOR ',width:.91});
  assert.equal(updated.length,1);assert.equal(updated[0].id,'entry');assert.equal(updated[0].width,.91);
  assert.equal(first[0].width,.83);
  assert.throws(()=>validatePresets([preset,{...preset,id:'other'}]));
  assert.throws(()=>validatePresets([{...preset,height:NaN}]));
  assert.throws(()=>validatePresets([{...preset,name:''}]));
  const old={schemaVersion:1,units:'m',name:'Project',room:{width:6,depth:4,height:3}};
  const next=validateProject(old);assert.equal(next.schemaVersion,6);assert.deepEqual(next.presets,[]);
  next.presets=updated;
  assert.deepEqual(validateProject(JSON.parse(JSON.stringify(next))).presets,updated);
});
test('opening chains partition the full angled wall into widths and gaps without mutating input',()=>{
  const room={width:6,depth:5,height:3,outline:[{x:-3,z:-3},{x:3,z:-2},{x:3,z:2},{x:-3,z:2}],openings:[
    {id:'second',wall:'north',offset:3,width:1.2},{id:'first',wall:'north',offset:1,width:.83}]};
  const before=JSON.stringify(room),camera=new THREE.OrthographicCamera();fitCamera(camera,room,1.5,'Plan');
  const chain=openingChainLayout(room,'north',camera,900,600);
  assert.equal(chain.length,5);assert.deepEqual(chain.map(d=>d.kind),['gap','opening','gap','opening','gap']);
  assert.deepEqual(chain.filter(d=>d.kind==='opening').map(d=>d.length),['0.83','1.20']);
  assert.ok(Math.abs(chain.reduce((sum,d)=>sum+d.value,0)-Math.hypot(6,1))<1e-9);
  assert.equal(JSON.stringify(room),before);assert.deepEqual(openingChainLayout(room,'floor',camera,900,600),[]);
});
