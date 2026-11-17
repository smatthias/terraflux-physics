import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import * as D from './furniture-design-edit.mjs';
import {placedDesignItem,designFootprint} from './design-placement.mjs';
import {validateProject} from './project.mjs';
import {rectangle} from './geometry.mjs';
import {manufacturingBatches} from './manufacturing.mjs';
import {createJobManifest} from './job-manifest.mjs';
import {furnitureClearance} from './furniture-clearance.mjs';
import {copyFurniture,pasteFurniture} from './furniture-clipboard.mjs';
import {cabinetFootprint} from './furniture.mjs';
import {createRoom} from '../src/room-scene.js';
import * as designScene from '../src/design-scene.js';
const ids=p=>{let n=0;return ()=>p+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids('b')});
const cabinet={id:'cab',type:'cabinet',name:'Cabinet',width:.8,depth:.5,height:1,thickness:.018,shelves:1,x:-2,z:0,rotation:0};
const project=(design=box(),furniture=[placedDesignItem(design,{id:'placed',x:1,z:0})])=>validateProject({schemaVersion:7,units:'m',name:'Placed',presets:[],furnitureDesigns:[design],
  room:{height:2.8,outline:rectangle(6,4),openings:[],furniture:[cabinet,...furniture]}});

test('a placed design takes its footprint and height from the design',()=>{
  assert.deepEqual(designFootprint(box()),{width:.6,depth:.56,height:.72});
  const p=project(),placed=p.room.furniture[1];
  assert.deepEqual(placed,{id:'placed',type:'design',name:'Box',designId:'box',width:.6,depth:.56,height:.72,x:1,z:0,rotation:0});
  assert.equal(p.schemaVersion,7);
  assert.throws(()=>designFootprint(D.createDesign({id:'e',name:'Empty',makeId:ids('e')})),/too small to place/);
});

test('editing the design resizes the placed copy; impossible edits are refused',()=>{
  const p=project(),design=p.furnitureDesigns[0];
  const wider=D.moveParts(design,[design.parts[1].id],{x:400});
  assert.equal(validateProject({...p,furnitureDesigns:[wider]}).room.furniture[1].width,1,'the right side moved 400 mm out');
  const huge=D.moveParts(design,[design.parts[1].id],{x:5000}); // 5.6 m wide, centred at x = 1: past the east wall at 3 m
  assert.throws(()=>validateProject({...p,furnitureDesigns:[huge]}),/Placed design “Box” must fit inside the room/);
  const tall=D.updatePart(design,design.parts[0].id,{width:3000});
  assert.throws(()=>validateProject({...p,furnitureDesigns:[tall]}),/taller than the room/);
  assert.throws(()=>validateProject({...p,furnitureDesigns:[]}),/refers to a design that does not exist; remove it from the room first/);
  assert.throws(()=>project(box(),[placedDesignItem(box(),{id:'placed',x:-1.9,z:0})]),/Placed design “Box” overlaps other furniture/);
  const tampered={...p,room:{...p.room,furniture:[p.room.furniture[0],{...p.room.furniture[1],width:9}]}};
  assert.equal(validateProject(tampered).room.furniture[1].width,.6,'stored sizes cannot drift from the design');
});

test('manufacturing without the project designs lists cabinets only (see design-manufacturing.test.mjs)',async()=>{
  const p=project();
  assert.deepEqual(manufacturingBatches(p.room.furniture).flatMap(b=>b.cabinets.map(c=>c.id)),['cab']);
  const manifest=await createJobManifest(p,'2024-01-01T00:00:00.000Z');
  assert.equal(manifest.summary.cabinets,1);assert.equal(manifest.summary.designs,1);
});

test('clearance and the clipboard work for placed designs',()=>{
  const p=project(),placed=p.room.furniture[1];
  const report=furnitureClearance(p.room,placed,{minWall:.05});
  assert.ok(report.nearestWall.distance>1.5,'the nearest wall is measured from the design footprint');
  const pasted=pasteFurniture(p.room,copyFurniture(p.room,['placed']),{makeId:ids('n')});
  const copy=pasted.room.furniture.at(-1);
  assert.deepEqual([copy.type,copy.designId,copy.name],['design','box','Box (copy)']);
  assert.doesNotThrow(()=>validateProject({...p,room:pasted.room}));
});

test('the room draws a placed design at its footprint, with or without the design renderer',()=>{
  for(const rotation of [0,90,37]) {
    const p=project(),room={...p.room,furniture:p.room.furniture.map(f=>f.id==='placed'?{...f,rotation}:f)},item=room.furniture[1];
    const corners=cabinetFootprint(item),expected={minX:Math.min(...corners.map(c=>c.x)),maxX:Math.max(...corners.map(c=>c.x)),minZ:Math.min(...corners.map(c=>c.z)),maxZ:Math.max(...corners.map(c=>c.z))};
    for(const renderer of [null,designScene]) {
      const group=createRoom(room,{designs:p.furnitureDesigns,designScene:renderer});
      const meshes=group.children.filter(m=>m.userData.furnitureId==='placed'&&m.userData.role==='detail');
      assert.ok(meshes.length>=(renderer?6:1),renderer?'one mesh per design piece':'a stand-in box');
      assert.ok(meshes.every(m=>!Array.isArray(m.material)&&m.userData.id==='furniture:placed'),'pickable like other furniture');
      const bounds=new THREE.Box3();for(const m of meshes) bounds.expandByObject(m);
      const tol=renderer||rotation%90===0?1e-6:1e-6;
      assert.ok(Math.abs(bounds.min.y)<1e-6&&Math.abs(bounds.max.y-.72)<1e-6,'stands on the floor, 720 mm tall');
      if(rotation%90===0||renderer===null) {
        assert.ok(Math.abs(bounds.min.x-expected.minX)<tol&&Math.abs(bounds.max.x-expected.maxX)<tol,`x at ${rotation}°`);
        assert.ok(Math.abs(bounds.min.z-expected.minZ)<tol&&Math.abs(bounds.max.z-expected.maxZ)<tol,`z at ${rotation}°`);
      }
    }
  }
});
