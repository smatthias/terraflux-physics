import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {movePlane,dragOffset,movedCorner,ringAngle,ringDelta,buildRotateGizmo,pickRing,pixelSize,dropPlane,dropCorner,snap} from './design-gizmo.js';
import {rotateAboutWorldAxis,rotationMatrix} from '../shared/design-transform.mjs';
import {createDesign,addPart,partCentre} from '../shared/furniture-design-edit.mjs';
import {fitDesignCamera} from './design-scene.js';
const close=(a,b,m)=>assert.ok(Math.abs(a-b)<1e-9,`${m??''} ${a} != ${b}`);
const piece=()=>{let n=0;return addPart(createDesign({id:'d',name:'D',makeId:()=>'l'+(++n)}),'bar',{makeId:()=>'p',overrides:{axis:'x',length:400,width:40,height:40,x:100,y:200,z:300}}).design;};

test('drags move in the view plane; 3D drags slide on the floor or move vertically',()=>{
  const start={x:10,y:20,z:30},point={x:15,y:27,z:41};
  assert.deepEqual(dragOffset('3D',start,point),{x:5,y:0,z:11});
  assert.deepEqual(dragOffset('3D',start,point,true),{x:0,y:7,z:0});
  assert.deepEqual(dragOffset('Front',start,point),{x:5,y:7,z:0});
  assert.deepEqual(dragOffset('Top',start,point),{x:5,y:0,z:11});
  assert.deepEqual(dragOffset('Side',start,point),{x:0,y:7,z:11});
  assert.deepEqual(movePlane('Top',{x:1,y:50,z:2},new THREE.Vector3()).normal.toArray(),[0,1,0]);
  const vertical=movePlane('3D',{x:0,y:0,z:0},new THREE.Vector3(-1,-1,-1).normalize(),true);
  close(vertical.normal.y,0,'vertical drag plane stands upright');
  assert.deepEqual(movedCorner({x:100,y:200,z:300},{x:13,y:0,z:-26},10),{x:110,y:200,z:270},'moved axes snap, others stay');
  assert.deepEqual(movedCorner({x:101,y:200,z:300},{x:13,y:0,z:0},0),{x:114,y:200,z:300},'snapping off keeps exact values');
  assert.equal(snap(17,5),15);
});

test('ring angles follow the right-hand rule, so dragging a ring turns the piece with the pointer',()=>{
  const c={x:0,y:0,z:0};
  for(const axis of ['x','y','z']) for(const degrees of [30,-75,120]) {
    const m=rotationMatrix(rotateAboutWorldAxis(undefined,axis,degrees));
    const start={x:axis==='x'?0:1,y:axis==='y'?0:axis==='x'?1:0,z:axis==='z'?0:axis==='y'?1:0};
    const end={x:m[0][0]*start.x+m[0][1]*start.y+m[0][2]*start.z,y:m[1][0]*start.x+m[1][1]*start.y+m[1][2]*start.z,z:m[2][0]*start.x+m[2][1]*start.y+m[2][2]*start.z};
    close(ringDelta(ringAngle(start,c,axis),ringAngle(end,c,axis),0),degrees,`${axis} ${degrees}`);
  }
  assert.equal(ringDelta(170,-170,0),20,'wraps across ±180');
  assert.equal(ringDelta(0,22,15),15);assert.equal(ringDelta(0,-8,15),-15);
});

test('rotation rings surround the piece and can be picked in every view',()=>{
  const design=piece(),part=design.parts[0],centre=partCentre(part);
  for(const view of ['3D','Front','Top','Side']) {
    const gizmo=buildRotateGizmo(part,view);
    assert.equal(gizmo.children.length,view==='3D'?3:1);
    const camera=view==='3D'?new THREE.PerspectiveCamera(40,1.5,1,1e6):new THREE.OrthographicCamera();
    fitDesignCamera(camera,design,1.5,view);
    for(const ring of gizmo.children) {
      // 45° round each ring: a point no other ring passes through (rings meet at quarter turns).
      const p=new THREE.Vector3().fromBufferAttribute(ring.geometry.attributes.position,12).add(gizmo.position);
      const screen=p.clone().project(camera);
      assert.equal(pickRing(gizmo,camera,screen.x,screen.y,pixelSize(camera,p,600)*6)?.axis,ring.userData.axis,`${view} ${ring.userData.axis}`);
    }
    const mid=new THREE.Vector3(centre.x,centre.y,centre.z).project(camera);
    if(view!=='3D') assert.equal(pickRing(gizmo,camera,mid.x,mid.y,pixelSize(camera,centre,600)*6),null,'the centre is not a ring');
  }
});

test('dropped pieces rest on the drop plane, centred on the pointer',()=>{
  assert.deepEqual(dropCorner('3D',{x:512,y:0,z:-33},{x:600,y:18,z:400},10),{x:210,y:0,z:-230});
  assert.deepEqual(dropCorner('Front',{x:100,y:400,z:0},{x:40,y:700,z:40},0),{x:80,y:50,z:0});
  assert.deepEqual(dropCorner('Side',{x:0,y:10,z:55},{x:18,y:20,z:100},5),{x:0,y:0,z:5});
  assert.deepEqual(dropPlane('Front').normal.toArray(),[0,0,1]);assert.deepEqual(dropPlane('3D').normal.toArray(),[0,1,0]);
});

import {nudgeAxis} from './design-gizmo.js';
test('arrow keys nudge along the on-screen directions of each view',()=>{
  for(const view of ['3D','Front','Top','Side']) {
    const camera=view==='3D'?new THREE.PerspectiveCamera(40,1.5,1,1e6):new THREE.OrthographicCamera();
    fitDesignCamera(camera,piece(),1.5,view);
    for(const key of ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown']) {
      const {axis,sign}=nudgeAxis(view,key),step=new THREE.Vector3();step[axis]=sign*100;
      const origin=new THREE.Vector3(300,220,320),a=origin.clone().project(camera),b=origin.clone().add(step).project(camera);
      const dx=b.x-a.x,dy=b.y-a.y;
      const expected={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,1],ArrowDown:[0,-1]}[key];
      // The dominant screen movement goes the way of the arrow.
      if(expected[0]) assert.ok(Math.sign(dx)===expected[0]&&Math.abs(dx)>=Math.abs(dy)*(view==='3D'?.3:1),`${view} ${key}`);
      else assert.ok(Math.sign(dy)===expected[1]&&Math.abs(dy)>=Math.abs(dx)*(view==='3D'?.3:1),`${view} ${key}`);
    }
  }
  assert.deepEqual(nudgeAxis('3D','ArrowUp',true),{axis:'y',sign:1});
  assert.deepEqual(nudgeAxis('Front','ArrowUp',true),{axis:'y',sign:1});
  assert.equal(nudgeAxis('Top','KeyA'),null);
});
