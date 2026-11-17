import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {createFurnitureGhost,disposeFurnitureGhost} from './furniture-ghost.js';
const item={id:'ghost',name:'Ghost',width:1,depth:.5,height:1.2,thickness:.018,shelves:1,x:0,z:0,rotation:0};
test('preview ghosts are view-aware, non-pickable and dispose their resources',()=>{
 for(const [view,valid] of [['Plan',false],['3D',true]]){
  const group=createFurnitureGhost(item,valid,view),meshes=group.children,scene=new THREE.Scene();scene.add(group);
  assert.equal(meshes.filter(m=>m.visible).length,view==='Plan'?1:meshes.length-1);
  assert.equal(meshes[0].material.depthWrite,false);assert.equal(meshes[0].material.color.getHex(),valid?0x6fe5b0:0xff5c60);
  const ray=new THREE.Raycaster(new THREE.Vector3(0,4,0),new THREE.Vector3(0,-1,0));assert.equal(ray.intersectObject(group,true).length,0);
  let disposed=0;meshes[0].geometry.addEventListener('dispose',()=>disposed++);disposeFurnitureGhost(group);assert.equal(disposed,1);assert.equal(group.parent,null);
 }
});

test('alignment guide lines are non-pickable and dispose with the preview',()=>{
 const group=createFurnitureGhost(item,true,'Plan',[{from:{x:0,z:-1},to:{x:0,z:1}}]);
 const line=group.children.find(c=>c.userData.guide);assert.ok(line?.isLine);assert.equal(line.material.depthWrite,false);
 const ray=new THREE.Raycaster(new THREE.Vector3(0,4,0),new THREE.Vector3(0,-1,0));assert.equal(ray.intersectObject(group,true).length,0);
 let disposed=0;line.geometry.addEventListener('dispose',()=>disposed++);disposeFurnitureGhost(group);assert.equal(disposed,1);
});

test('standalone selected clearance overlays draw sweep curves, ignore picking and release resources',async()=>{
 const {createClearanceOverlay}=await import('./furniture-ghost.js');
 const {furnitureClearance}=await import('../shared/furniture-clearance.mjs');
 const {rectangle}=await import('../shared/geometry.mjs');
 const report=furnitureClearance({outline:rectangle(6,4),openings:[{id:'d',type:'door',wall:'north',offset:2.5,width:1,height:2,sill:0,hinge:'A',swing:'in'}]},{...item,x:-.35,z:-1.5,width:.2,depth:.2},{doorDepth:0});
 const overlay=createClearanceOverlay(report),scene=new THREE.Scene();scene.add(overlay);
 assert.equal(overlay.children.length,2);
 const arc=overlay.children.find(line=>line.isLineLoop);assert.equal(arc.geometry.attributes.position.count,66);
 assert.equal(arc.material.color.getHex(),0xffa75a);
 const ray=new THREE.Raycaster(new THREE.Vector3(-.35,4,-1.5),new THREE.Vector3(0,-1,0));assert.equal(ray.intersectObject(overlay,true).length,0);
 let disposed=0;overlay.traverse(line=>{line.geometry?.addEventListener('dispose',()=>disposed++);line.material?.addEventListener('dispose',()=>disposed++);});
 disposeFurnitureGhost(overlay);assert.equal(disposed,4);assert.equal(overlay.parent,null);
 assert.equal(createClearanceOverlay({error:'Invalid settings'}).children.length,0);
});
