import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import { createRoom, updateSurfaces, fitCamera, pickSurface } from './room-scene.js';

const room = { width: 6, depth: 4, height: 2.8 };
test('orthographic plan keeps all room corners in frame across aspect ratios and room sizes', () => {
  for (const dimensions of [room, { width: 1000, depth: .1, height: 1000 }]) {
    for (const aspect of [.35, 1, 2.5]) {
      const camera = new THREE.OrthographicCamera();
      fitCamera(camera, dimensions, aspect, 'Plan');
      for (const x of [-dimensions.width/2-.16, dimensions.width/2+.16])
        for (const z of [-dimensions.depth/2-.16, dimensions.depth/2+.16]) {
          const point = new THREE.Vector3(x,0,z).project(camera);
          assert.ok(Math.abs(point.x) < 1 && Math.abs(point.y) < 1 && Math.abs(point.z) < 1);
        }
      const low = new THREE.Vector3(1,0,0).project(camera);
      const high = new THREE.Vector3(1,dimensions.height,0).project(camera);
      assert.ok(Math.abs(low.x-high.x) < 1e-12, 'height must not change apparent scale');
    }
  }
});
test('plan ray picking identifies floor and all four walls; empty space clears selection', () => {
  const group = createRoom(room), camera = new THREE.OrthographicCamera();
  updateSurfaces(group,'Plan',null); fitCamera(camera,room,1.5,'Plan');
  for (const [id,x,z] of [['floor',0,0],['north',0,-2.08],['south',0,2.08],['west',-3.08,0],['east',3.08,0]]) {
    const point = new THREE.Vector3(x,0,z).project(camera);
    assert.equal(pickSurface(group,camera,point.x,point.y),id);
  }
  assert.equal(pickSurface(group,camera,.99,.99),null);
});
test('selection persists while cutaway hides walls and reappears in plan', () => {
  const group = createRoom(room), south = group.children.find(mesh => mesh.userData.id === 'south');
  updateSurfaces(group,'3D','south');
  assert.equal(south.visible,false);
  updateSurfaces(group,'Plan','south');
  assert.equal(south.visible,true);
  assert.notEqual(south.material.emissive.getHex(),0);
  updateSurfaces(group,'Plan',null);
  assert.equal(south.material.emissive.getHex(),0);
});

import { frameBox } from './room-scene.js';
test('framing a selection keeps its whole box in view in plan and 3D', () => {
  for (const box of [{x:2,z:-1,width:.8,depth:.5,height:1},{x:-40,z:30,width:12,depth:3,height:2.5}])
    for (const aspect of [.4,1,2.2]) for (const view of ['Plan','3D']) {
      const camera = view === 'Plan' ? new THREE.OrthographicCamera() : new THREE.PerspectiveCamera(45,aspect,.01,10000);
      const target = frameBox(camera, box, aspect, view, 3);
      assert.equal(target.x, box.x); assert.equal(target.z, box.z);
      for (const x of [box.x-box.width/2, box.x+box.width/2]) for (const z of [box.z-box.depth/2, box.z+box.depth/2]) for (const y of [0, box.height]) {
        const p = new THREE.Vector3(x,y,z).project(camera);
        assert.ok(Math.abs(p.x) < 1 && Math.abs(p.y) < 1, `${view} ${aspect} corner in view`);
      }
      if (view === 'Plan') assert.ok(camera.position.y > 3, 'plan camera stays above the room');
    }
});
test('inspection isolates one furniture item and multi-selection highlights every item', () => {
  const cabinet = (id,x) => ({id,type:'cabinet',name:id,width:.8,depth:.5,height:1,thickness:.018,shelves:1,x,z:0,rotation:0});
  const group = createRoom({ ...room, outline: [{x:-3,z:-2},{x:3,z:-2},{x:3,z:2},{x:-3,z:2}], openings: [], furniture: [cabinet('a',-1), cabinet('b',1)] });
  updateSurfaces(group, '3D', null, { isolate: 'a' });
  const visible = group.children.filter(m => m.visible);
  assert.ok(visible.length > 0 && visible.every(m => m.userData.furnitureId === 'a' && m.userData.role !== 'symbol'));
  updateSurfaces(group, '3D', ['furniture:a','furniture:b']);
  const lit = new Set(group.children.filter(m => m.material.emissive?.getHex()).map(m => m.userData.id));
  assert.deepEqual([...lit].sort(), ['furniture:a','furniture:b']);
  assert.ok(group.children.some(m => m.userData.id === 'north' && m.visible), 'isolation ends when not requested');
});
