import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {createRoom,updateSurfaces,fitCamera,pickOpening} from './room-scene.js';
import {rectangle} from '../shared/geometry.mjs';
import {openingAtPoint} from '../shared/openings.mjs';
const room={width:6,depth:4,height:3,outline:rectangle(6,4),openings:[]};
test('click placement projects onto a wall, snaps its offset and rejects bad positions',()=>{
  const door=openingAtPoint(room,'north','door','placed',{x:0,z:-2},.25,true);
  assert.equal(door.offset,2.5);assert.equal(door.width,.9);
  assert.ok(Math.abs(openingAtPoint(room,'north','door','free',{x:0,z:-2},.25,false).offset-2.55)<1e-9);
  assert.throws(()=>openingAtPoint(room,'north','door','far',{x:0,z:0},.25,true));
  assert.throws(()=>openingAtPoint(room,'north','door','end',{x:3,z:-2},.25,true));
  assert.throws(()=>openingAtPoint({...room,openings:[door]},'north','window','overlap',{x:0,z:-2},.25,true));
  const sloped={...room,outline:[{x:-3,z:-3},{x:3,z:-2},{x:3,z:2},{x:-3,z:2}]};
  const len=Math.hypot(6,1),p={x:-3+6/len*3,z:-3+1/len*3};
  assert.equal(openingAtPoint(sloped,'north','door','angle',p,.25,true).offset,2.5);
});
test('opening details and plan symbols switch views with their host wall',()=>{
  const door=openingAtPoint(room,'north','door','door',{x:-1.5,z:-2},.25,true);
  const window={id:'window',wall:'north',type:'window',offset:3,width:1.2,height:1.2,sill:.9};
  const model={...room,openings:[door,window]},group=createRoom(model);
  updateSurfaces(group,'3D',null);
  assert.equal(group.children.filter(m=>m.userData.kind==='frame').length,7);
  const glass=group.children.find(m=>m.userData.kind==='glazing');
  assert.equal(glass.visible,true);assert.equal(glass.material.transparent,true);
  assert.ok(group.children.some(m=>m.userData.kind==='leaf' && m.visible));
  assert.equal(group.children.find(m=>m.userData.kind==='door-swing').visible,false);
  updateSurfaces(group,'Plan',null);
  assert.equal(glass.visible,false);
  const swing=group.children.find(m=>m.userData.kind==='door-swing');
  assert.equal(swing.visible,true);assert.equal(swing.geometry.attributes.position.count,33);
  const camera=new THREE.OrthographicCamera();fitCamera(camera,model,1.5,'Plan');
  const point=new THREE.Vector3(-3+door.offset,.04,.9-2).project(camera);
  assert.equal(pickOpening(group,camera,point.x,point.y),'door');
  const cutaway=createRoom({...room,openings:[{...door,wall:'south'}]});
  updateSurfaces(cutaway,'3D',null);
  assert.equal(cutaway.children.some(m=>m.userData.openingId==='door' && m.visible),false);
});

test('all hinge/swing combinations align leaf positions and plan arc endpoints',()=>{
  for(const hinge of ['A','B']) for(const swing of ['in','out']) {
    const door={id:'pose',wall:'north',type:'door',offset:1,width:.9,height:2.1,sill:0,hinge,swing};
    const group=createRoom({...room,openings:[door]});
    const leaf=group.children.find(m=>m.userData.kind==='leaf');
    assert.ok(swing==='in'?leaf.position.z>-2:leaf.position.z<-2);
    assert.ok(Math.abs(leaf.position.x-(-3+1+(hinge==='B'?.9-.045:.045)))<1e-9);
    const arc=group.children.find(m=>m.userData.kind==='door-swing').geometry.attributes.position;
    const first=new THREE.Vector3().fromBufferAttribute(arc,0),last=new THREE.Vector3().fromBufferAttribute(arc,32);
    assert.ok(Math.abs(first.x-(-3+1+(hinge==='A'?.9:0)))<1e-6);
    assert.ok(Math.abs(last.x-(-3+1+(hinge==='B'?.9:0)))<1e-6);
    assert.ok(Math.abs(last.z-(-2+(swing==='in'?.9:-.9)))<1e-6);
  }
});
