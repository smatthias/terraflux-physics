import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {rectangle} from './geometry.mjs';
import {validateOpenings,wallPanels} from './openings.mjs';
import {validateProject} from './project.mjs';
import {createRoom,updateSurfaces} from '../src/room-scene.js';
import {createHistory,commitHistory,undoHistory,redoHistory} from './history.mjs';
const outline=rectangle(6,4);
const door={hinge:'A',swing:'in',id:'door-1',type:'door',wall:'north',offset:1,width:.9,height:2.1,sill:0};
const window={id:'window-1',type:'window',wall:'north',offset:3,width:1.2,height:1.2,sill:.9};
const room={width:6,depth:4,height:3,outline,openings:[door,window]};
test('openings migrate, persist and participate in undo/redo',()=>{
  const old={schemaVersion:2,units:'m',name:'Studio',room:{...room,openings:undefined}};
  const migrated=validateProject(old); assert.equal(migrated.schemaVersion,6); assert.deepEqual(migrated.room.openings,[]);
  const project=validateProject({...old,schemaVersion:3,room});
  assert.deepEqual(validateProject(JSON.parse(JSON.stringify(project))),project);
  const history=commitHistory(createHistory(migrated.room),project.room);
  assert.equal(undoHistory(history).present.openings.length,0);
  assert.deepEqual(redoHistory(undoHistory(history)).present.openings,[door,window]);
});
test('opening validation rejects overlap, insufficient clearances, bad IDs and invalid edited walls',()=>{
  for(const change of [{offset:-1},{offset:5.2},{height:3},{sill:.2},{width:NaN},{wall:'roof'}])
    assert.throws(()=>validateOpenings([{...door,...change}],outline,3));
  assert.throws(()=>validateOpenings([door,{...window,id:door.id}],outline,3));
  assert.throws(()=>validateOpenings([door,{...window,offset:1.92}],outline,3));
  assert.throws(()=>validateOpenings([{...window,sill:0}],outline,3));
  assert.throws(()=>validateOpenings([window],rectangle(3,4),3));
  assert.throws(()=>validateOpenings([door],outline,2));
  assert.equal(validateOpenings([door,window],outline,3).length,2);
});
test('wall panel area removes exactly the opening area',()=>{
  const panels=wallPanels(6,3,[door,window]);
  const area=panels.reduce((sum,p)=>sum+(p.right-p.left)*(p.top-p.bottom),0);
  assert.ok(Math.abs(area-(6.16*3-.9*2.1-1.2*1.2))<1e-10);
});
test('3D mesh rays pass through openings but hit headers, sills and solid wall',()=>{
  const group=createRoom(room); updateSurfaces(group,'3D',null); group.updateMatrixWorld(true);
  const north=group.children.filter(mesh=>mesh.userData.id==='north' && !mesh.userData.role);
  const ray=(along,y)=>new THREE.Raycaster(new THREE.Vector3(-3+along,y,-4),new THREE.Vector3(0,0,1)).intersectObjects(north,false);
  assert.equal(ray(1.4,1).length,0,'door void');
  assert.ok(ray(1.4,2.5).length>0,'door header');
  assert.equal(ray(3.5,1.4).length,0,'window void');
  assert.ok(ray(3.5,.5).length>0,'window sill wall');
  assert.ok(ray(.5,1).length>0,'solid wall');
  updateSurfaces(group,'Plan',null);
  assert.equal(north.some(mesh=>mesh.visible && mesh.position.x===-3+1.45),false,'door header hidden in plan section');
});
