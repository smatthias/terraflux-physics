import * as THREE from 'three';
import {cabinetFootprint} from '../shared/furniture.mjs';
import {cabinetParts} from '../shared/cabinet-parts.mjs';
// Placed designs: the design's pieces (from the lazily loaded design renderer),
// scaled from millimetres, centred on the footprint, resting on the floor and
// turned like a cabinet. Meshes are added directly to the room group with their
// transform applied and a single material, as the room's picking and
// highlighting expect. Until the renderer has loaded, a box of the design's size
// stands in.
function addPlacedDesign(group,item,design,designScene,add) {
  const angle=item.rotation*Math.PI/180;
  if(!design||!designScene) {
    const box=add(new THREE.BoxGeometry(item.width,item.height,item.depth),[item.x,item.height/2,item.z],0xc9b48e);box.rotation.y=angle;return;
  }
  const pieces=designScene.buildDesignGroup(design),{min,max}=designScene.designBoundsOf(design);
  const placement=new THREE.Matrix4().makeTranslation(item.x,0,item.z).multiply(new THREE.Matrix4().makeRotationY(angle))
    .multiply(new THREE.Matrix4().makeScale(.001,.001,.001)).multiply(new THREE.Matrix4().makeTranslation(-(min.x+max.x)/2,-min.y,-(min.z+max.z)/2));
  pieces.updateMatrixWorld(true);
  pieces.traverse(object=>{
    if(object.userData.role!=='part') return;
    const material=[object.material].flat()[0],geometry=object.geometry.clone().applyMatrix4(new THREE.Matrix4().multiplyMatrices(placement,object.matrixWorld));
    geometry.clearGroups();
    const mesh=add(geometry,[0,0,0],material.color.getHex());
    mesh.material.metalness=material.metalness;mesh.material.roughness=material.roughness;
  });
  designScene.disposeDesignGroup(pieces);
}
export function addFurnitureVisuals(group,room,{designs=[],designScene=null}={}) {
  for(const item of room.furniture||[]) {
    const angle=item.rotation*Math.PI/180,c=Math.cos(angle),s=Math.sin(angle);
    const add=(geometry,position,color,role='detail')=>{
      const mesh=new THREE.Mesh(geometry,new THREE.MeshStandardMaterial({color,roughness:.7,side:THREE.DoubleSide}));
      mesh.position.set(...position);mesh.userData={id:'furniture:'+item.id,baseColor:color,role,furnitureId:item.id};group.add(mesh);return mesh;
    };
    const panel=(w,h,d,x,y,z,color=0xb88c59)=>{
      const mesh=add(new THREE.BoxGeometry(w,h,d),[item.x+c*x+s*z,y,item.z-s*x+c*z],color);mesh.rotation.y=angle;
    };
    if(item.type==='design') addPlacedDesign(group,item,designs.find(d=>d.id===item.designId),designScene,add);
    else for(const part of cabinetParts(item)) panel(...part.size,...part.position,part.color);
    const footprint=cabinetFootprint(item),shape=new THREE.Shape(footprint.map(p=>new THREE.Vector2(p.x,-p.z)));
    const plan=add(new THREE.ShapeGeometry(shape),[0,.035,0],0xc49c66,'symbol');plan.rotation.x=-Math.PI/2;
  }
}
