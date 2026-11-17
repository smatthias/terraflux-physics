import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {dimensionLayout} from './dimensions.js';
import {fitCamera} from './room-scene.js';
import {rectangle} from '../shared/geometry.mjs';
import {previewOpening,openingAtPoint} from '../shared/openings.mjs';
const room={width:6,depth:4,height:3,outline:rectangle(6,4),openings:[]};
test('dimension lines track projected edges at a fixed screen offset with extensions and ticks',()=>{
  const camera=new THREE.OrthographicCamera();fitCamera(camera,room,1.5,'Plan');
  for(const zoom of [.6,2]) {
    camera.zoom=zoom;camera.updateProjectionMatrix();
    const labels=dimensionLayout(room.outline,camera,900,600);
    assert.equal(labels.length,4);
    assert.deepEqual(labels.map(l=>l.length),['6.00','4.00','6.00','4.00']);
    const north=labels[0];assert.equal(north.segments.length,5);
    const p=new THREE.Vector3(-3,0,-2).project(camera);
    const q=new THREE.Vector3(3,0,-2).project(camera);
    assert.ok(Math.abs(north.segments[0].x1-(p.x+1)*450)<1e-9);
    assert.ok(Math.abs(north.segments[0].y1-((1-p.y)*300-28))<1e-9);
    assert.ok(Math.abs(north.segments[0].x2-(q.x+1)*450)<1e-9);
  }
  const angled={...room,outline:[{x:-3,z:-3},{x:3,z:-2},{x:3,z:2},{x:-3,z:2}]};
  fitCamera(camera,angled,1.5,'Plan');
  assert.equal(dimensionLayout(angled.outline,camera,900,600)[0].length,Math.hypot(6,1).toFixed(2));
});
test('custom placement dimensions remain exact while only offset is snapped',()=>{
  const size={width:.83,height:2.03,sill:0,hinge:'B',swing:'out'},point={x:0,z:-2};
  const preview=previewOpening(room,'north','door','sized',point,.25,true,.25,size);
  assert.equal(preview.valid,true);
  assert.deepEqual(openingAtPoint(room,'north','door','sized',point,.25,true,.25,size),preview.opening);
  assert.equal(preview.opening.width,.83);assert.equal(preview.opening.height,2.03);
  assert.equal(preview.opening.offset,2.5);assert.equal(preview.opening.hinge,'B');
  const window=previewOpening(room,'north','window','window',point,.25,true,.25,{width:1.07,height:.91,sill:.77});
  assert.equal(window.valid,true);assert.equal(window.opening.sill,.77);
  assert.equal(previewOpening(room,'north','window','bad',point,.25,true,.25,{width:9,height:1,sill:1}).valid,false);
  assert.equal(previewOpening(room,'north','door','blank',point,.25,true,.25,{width:'',height:2,sill:0}).valid,false);
});
