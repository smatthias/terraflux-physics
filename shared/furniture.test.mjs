import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {validateProject} from './project.mjs';
import {validateFurniture,cabinetFootprint} from './furniture.mjs';
import {rectangle} from './geometry.mjs';
import {createRoom,updateSurfaces,fitCamera,pickSurface} from '../src/room-scene.js';
import {commitHistory,createHistory,undoHistory,redoHistory} from './history.mjs';
import {exportPlanSvg} from './plan-svg.mjs';
const room={width:6,depth:4,height:3,outline:rectangle(6,4),openings:[],furniture:[]};
const item={id:'cabinet',name:'Cabinet',type:'cabinet',width:.9,depth:.5,height:1.2,thickness:.018,shelves:2,x:0,z:0,rotation:0};
test('furniture validation enforces room containment, rotated body overlap and physical dimensions',()=>{
  assert.equal(validateFurniture([item],room.outline,3).length,1);
  for(const change of [{x:3},{height:4},{width:-1},{shelves:1.5},{thickness:.1},{rotation:360},{name:''}])
    assert.throws(()=>validateFurniture([{...item,...change}],room.outline,3));
  assert.throws(()=>validateFurniture([item,{...item,id:'second',x:.2,rotation:45}],room.outline,3),/overlap/);
  assert.equal(validateFurniture([item,{...item,id:'second',x:1.2,rotation:45}],room.outline,3).length,2);
  assert.throws(()=>validateFurniture([{...item,x:2.6,rotation:45}],room.outline,3),/inside/);
  const corners=cabinetFootprint({...item,rotation:90});assert.ok(Math.abs(corners[0].x+.25)<1e-9);
});
test('legacy projects migrate and furniture participates in history, JSON and SVG',()=>{
  const project=validateProject({schemaVersion:5,units:'m',name:'Furnished',presets:[],room});
  assert.equal(project.schemaVersion,6);assert.deepEqual(project.room.furniture,[]);
  const furnished=validateProject({...project,room:{...room,furniture:[item]}});
  assert.deepEqual(validateProject(JSON.parse(JSON.stringify(furnished))),furnished);
  const history=commitHistory(createHistory(project.room),furnished.room);
  assert.equal(undoHistory(history).present.furniture.length,0);
  assert.equal(redoHistory(undoHistory(history)).present.furniture.length,1);
  assert.match(exportPlanSvg(furnished),/#d8bd8e/);
});
test('cabinet panels and Plan footprint follow parameters and can be selected in both views',()=>{
  const furnished={...room,furniture:[item]},group=createRoom(furnished),id='furniture:'+item.id;
  updateSurfaces(group,'3D',id);
  const pieces=group.children.filter(mesh=>mesh.userData.furnitureId===item.id);
  assert.equal(pieces.filter(mesh=>mesh.userData.role==='detail').length,11);
  assert.equal(pieces.find(mesh=>mesh.userData.role==='symbol').visible,false);
  updateSurfaces(group,'Plan',id);
  assert.equal(pieces.filter(mesh=>mesh.userData.role==='detail'&&mesh.visible).length,0);
  const camera=new THREE.OrthographicCamera();fitCamera(camera,furnished,1.5,'Plan');
  const point=new THREE.Vector3(0,0,0).project(camera);
  assert.equal(pickSurface(group,camera,point.x,point.y),id);
});
