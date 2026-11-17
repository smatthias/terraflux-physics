import * as THREE from 'three';
import { rectangle, bounds } from '../shared/geometry.mjs';
import { wallIndex, wallLabel, roomWalls } from '../shared/room-topology.mjs';

import { addOpeningVisuals } from './opening-visuals.js';
import { wallPanels } from '../shared/openings.mjs';

import { addFurnitureVisuals } from './furniture-scene.js';

const withOutline = room => ({ ...room, outline: room.outline || rectangle(room.width, room.depth) });
export const roomSurfaces = room => [
  { id: 'floor', label: 'Floor' },
  ...roomWalls(withOutline(room)).map(({ id }) => ({ id, label: wallLabel(id) }))
];
// The 3D camera looks from the south-east (+x, +z); walls facing it are cut away.
export const cutawayWalls = room => roomWalls(withOutline(room)).filter(w => -w.nx - w.nz > 1e-9).map(w => w.id);

// `designs` and `designScene` (the lazily loaded design renderer) draw placed furniture designs.
export function createRoom(input,{designs=[],designScene=null}={}) {
  const room=withOutline(input), outline=room.outline;
  const group = new THREE.Group();
  group.userData.cutaway=cutawayWalls(room);
  const add = (id, geometry, position, color, angle=0) => {
    const mesh=new THREE.Mesh(geometry,new THREE.MeshStandardMaterial({color,roughness:.85,side:THREE.DoubleSide}));
    mesh.position.set(...position); mesh.rotation.y=angle;
    mesh.userData={id,baseColor:color}; mesh.name=id==='floor'?'Floor':wallLabel(id);
    group.add(mesh); return mesh;
  };
  const shape=new THREE.Shape(outline.map(p=>new THREE.Vector2(p.x,-p.z)));
  const floor=add('floor',new THREE.ShapeGeometry(shape),[0,-.001,0],0x9eb8ac);
  floor.rotation.x=-Math.PI/2;
  roomWalls(room).forEach(({id,a,length,tx,tz})=>{
    for(const panel of wallPanels(length,room.height,(room.openings||[]).filter(o=>o.wall===id))) {
      const along=(panel.left+panel.right)/2;
      const mesh=add(id,new THREE.BoxGeometry(panel.right-panel.left,panel.top-panel.bottom,.16),
        [a.x+tx*along+tz*.08,(panel.bottom+panel.top)/2,a.z+tz*along-tx*.08],
        wallIndex(room,id)%2 ? 0xc4cabb : 0xddd6c8,-Math.atan2(tz,tx));
      const slice=Math.min(1.2,room.height/2);
      mesh.userData.planVisible=panel.bottom<=slice && panel.top>=slice;
    }
  });
  addOpeningVisuals(group,room);
  addFurnitureVisuals(group,room,{designs,designScene});
  return group;
}

export function createGrid(room,spacing) {
  const b=bounds(room.outline || rectangle(room.width,room.depth));
  // Keep the rendered grid bounded; snapping always uses the requested spacing.
  const step=spacing*Math.max(1,Math.ceil(Math.max(b.width,b.depth)/spacing/100));
  const minX=Math.floor((b.minX-1)/step)*step, maxX=Math.ceil((b.maxX+1)/step)*step;
  const minZ=Math.floor((b.minZ-1)/step)*step, maxZ=Math.ceil((b.maxZ+1)/step)*step;
  const vertices=[];
  for(let x=minX;x<=maxX+1e-8;x+=step) vertices.push(x,.005,minZ,x,.005,maxZ);
  for(let z=minZ;z<=maxZ+1e-8;z+=step) vertices.push(minX,.005,z,maxX,.005,z);
  return new THREE.LineSegments(new THREE.BufferGeometry().setAttribute('position',new THREE.Float32BufferAttribute(vertices,3)),
    new THREE.LineBasicMaterial({color:0x66887f,transparent:true,opacity:.35}));
}

// `selected` is one surface ID or a list (multi-selected furniture). `isolate` shows
// only that furniture item, for inspection.
export function updateSurfaces(group, view, selected, { isolate = null } = {}) {
  const cutaway = group.userData.cutaway || [], highlighted = Array.isArray(selected) ? selected : [selected];
  for (const mesh of group.children) {
    mesh.visible = isolate ? mesh.userData.furnitureId === isolate && mesh.userData.role !== (view === 'Plan' ? 'detail' : 'symbol') :
      view === 'Plan' ? mesh.userData.role!=='detail' && mesh.userData.planVisible!==false :
      mesh.userData.role!=='symbol' && !cutaway.includes(mesh.userData.id);
    mesh.material.color.setHex(mesh.userData.baseColor);
    mesh.material.emissive?.setHex(mesh.userData.id != null && highlighted.includes(mesh.userData.id) ? 0x766228 : 0x000000);
    if(mesh.material.emissive) mesh.material.emissiveIntensity = .65;
  }
}

export function fitCamera(camera, room, aspect, view) {
  const b=bounds(room.outline || rectangle(room.width,room.depth));
  const w = b.width + .32, d = b.depth + .32;
  if (view === 'Plan') {
    const halfHeight = Math.max(d/2, w/(2*aspect)) * 1.3;
    camera.left = -halfHeight*aspect; camera.right = halfHeight*aspect;
    camera.top = halfHeight; camera.bottom = -halfHeight;
    camera.zoom = 1;
    camera.up.set(0,0,-1);
    camera.position.set(b.x,room.height+10,b.z);
    camera.near = .01; camera.far = room.height+100;
  } else {
    camera.aspect = aspect;
    const extent = Math.max(w,d,room.height);
    // Account for narrow portrait viewports as well as tall rooms.
    const distance = extent*1.8 / Math.min(1,aspect);
    camera.up.set(0,1,0);
    camera.position.set(b.x+distance,distance*.8,b.z+distance);
    camera.near = .01; camera.far = Math.max(2000,distance*10);
  }
  camera.lookAt(b.x,0,b.z);
  camera.updateProjectionMatrix(); camera.updateMatrixWorld();
}

// Frames a box {x, z, width, depth, height} (e.g. a furniture selection) and
// returns the orbit target. Plan keeps north up; 3D looks from the south-east.
export function frameBox(camera, box, aspect, view, roomHeight = box.height) {
  const w = box.width + .6, d = box.depth + .6, target = { x: box.x, y: view === 'Plan' ? 0 : box.height / 2, z: box.z };
  if (view === 'Plan') {
    const halfHeight = Math.max(d/2, w/(2*aspect)) * 1.3;
    camera.left = -halfHeight*aspect; camera.right = halfHeight*aspect;
    camera.top = halfHeight; camera.bottom = -halfHeight; camera.zoom = 1;
    camera.up.set(0,0,-1); camera.position.set(box.x, roomHeight+10, box.z);
    camera.near = .01; camera.far = roomHeight+100;
  } else {
    camera.aspect = aspect;
    const distance = Math.max(w, d, box.height) * 1.8 / Math.min(1, aspect);
    camera.up.set(0,1,0);
    camera.position.set(box.x+distance, target.y+distance*.8, box.z+distance);
    camera.near = .01; camera.far = Math.max(2000, distance*10);
  }
  camera.lookAt(target.x, target.y, target.z);
  camera.updateProjectionMatrix(); camera.updateMatrixWorld();
  return target;
}

export function pickSurface(group, camera, x, y) {
  const ray = new THREE.Raycaster();
  ray.setFromCamera(new THREE.Vector2(x,y), camera);
  group.updateMatrixWorld(true);
  return ray.intersectObjects(group.children.filter(mesh => mesh.visible), false)[0]?.object.userData.id ?? null;
}

export function pickOpening(group,camera,x,y) {
  const ray=new THREE.Raycaster();ray.params.Line.threshold=.08;
  ray.setFromCamera(new THREE.Vector2(x,y),camera);group.updateMatrixWorld(true);
  const hits=ray.intersectObjects(group.children.filter(mesh=>mesh.visible),false);
  const hit=camera.isOrthographicCamera?hits.find(hit=>hit.object.userData.role==='symbol')||hits[0]:hits[0];
  return hit?.object.userData.openingId ?? null;
}
