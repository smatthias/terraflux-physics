import * as THREE from 'three';
import {addFurnitureVisuals} from './furniture-scene.js';
export function createFurnitureGhost(candidate,valid,view,guides=[],clearance){
  const group=new THREE.Group();group.name='furniture-insertion-preview';
  if(!candidate)return group;
  addFurnitureVisuals(group,{furniture:[candidate]});
  group.traverse(mesh=>{
    if(!mesh.isMesh)return;
    mesh.visible=view==='Plan'?mesh.userData.role==='symbol':mesh.userData.role!=='symbol';
    mesh.material.color.setHex(valid?0x6fe5b0:0xff5c60);mesh.material.transparent=true;mesh.material.opacity=.45;mesh.material.depthWrite=false;mesh.material.depthTest=false;mesh.renderOrder=1000;
    mesh.userData={preview:true};mesh.raycast=()=>{};
  });
  for(const guide of guides){
    const geometry=new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(guide.from.x,.045,guide.from.z),new THREE.Vector3(guide.to.x,.045,guide.to.z)]);
    const line=new THREE.Line(geometry,new THREE.LineBasicMaterial({color:0xffe189,transparent:true,opacity:.9,depthTest:false,depthWrite:false}));line.renderOrder=1001;line.userData={preview:true,guide:true};line.raycast=()=>{};group.add(line);
  }
  if(clearance)group.add(createClearanceOverlay(clearance));
  return group;
}
export function disposeFurnitureGhost(group){group?.traverse(mesh=>{mesh.geometry?.dispose();mesh.material?.dispose();});group?.removeFromParent();}

export function createClearanceOverlay(clearance){
  const group=new THREE.Group();group.name='furniture-clearance-overlay';
  const wall=clearance?.nearestWall;
  if(wall&&wall.distance>1e-8){const line=new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(wall.from.x,.06,wall.from.z),new THREE.Vector3(wall.to.x,.06,wall.to.z)]),new THREE.LineBasicMaterial({color:0x89d5ff,depthTest:false,depthWrite:false}));line.userData={preview:true};line.raycast=()=>{};line.renderOrder=1001;group.add(line);}
  for(const zone of clearance?.zones??[]){const line=new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints(zone.polygon.map(p=>new THREE.Vector3(p.x,.06,p.z))),new THREE.LineBasicMaterial({color:0xffa75a,depthTest:false,depthWrite:false}));line.userData={preview:true};line.raycast=()=>{};line.renderOrder=1001;group.add(line);}
  return group;
}
