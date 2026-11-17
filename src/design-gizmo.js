// Direct manipulation in the furniture designer: drag planes, snapping, rotation
// rings and drop placement. Kept free of component state so it can be tested.
import * as THREE from 'three';
import {designBounds} from '../shared/furniture-design.mjs';
import {localAxis} from '../shared/design-transform.mjs';

export const AXIS_COLORS=Object.freeze({x:0xe0564b,y:0x5bbf6a,z:0x4b8de0});
// The world axis each orthographic view looks along.
export const VIEW_NORMAL=Object.freeze({Front:'z',Top:'y',Side:'x'});
const UNIT={x:new THREE.Vector3(1,0,0),y:new THREE.Vector3(0,1,0),z:new THREE.Vector3(0,0,1)};

export const snap=(value,step)=>step>0?Math.round(value/step)*step:value;

// Pieces move in the view plane of orthographic views. In 3D they slide on a
// horizontal plane, or, with `vertical`, up and down on a vertical plane facing
// the camera. Pass the grabbed point so it stays under the pointer.
export function movePlane(view,centre,cameraDirection,vertical=false) {
  let normal=UNIT[VIEW_NORMAL[view]]??UNIT.y;
  if(view==='3D'&&vertical) {
    normal=new THREE.Vector3(cameraDirection.x,0,cameraDirection.z);
    if(normal.lengthSq()<1e-9) normal=UNIT.z.clone();
    normal.normalize();
  }
  return new THREE.Plane().setFromNormalAndCoplanarPoint(normal.clone(),new THREE.Vector3(centre.x,centre.y,centre.z));
}
// The drag offset allowed by the plane: floor drags never change height and
// vertical drags only change height.
export function dragOffset(view,start,point,vertical=false) {
  const d={x:point.x-start.x,y:point.y-start.y,z:point.z-start.z};
  if(view==='3D') return vertical?{x:0,y:d.y,z:0}:{x:d.x,y:0,z:d.z};
  return {...d,[VIEW_NORMAL[view]]:0};
}
// New corner after a drag: moved axes snap to the grid, the others stay exact.
export function movedCorner(part,offset,step) {
  const out={};
  for(const axis of ['x','y','z']) out[axis]=offset[axis]===0?part[axis]:snap(part[axis]+offset[axis],step);
  return out;
}

// In-plane basis (u, v) for a ring about `axis`, with u × v = axis, so angles
// measured from u toward v follow the right-hand rule of rotation about it.
const BASIS={x:['y','z'],y:['z','x'],z:['x','y']};
export function ringBasis(axis,rotation) {
  const [u,v]=BASIS[axis];
  return {axis:localAxis(rotation,axis),u:localAxis(rotation,u),v:localAxis(rotation,v)};
}
// Angle in degrees of a point around an axis through `centre`. `basis` is a world
// axis name or a ringBasis().
export function ringAngle(point,centre,basis) {
  const {u,v}=typeof basis==='string'?ringBasis(basis):basis,d={x:point.x-centre.x,y:point.y-centre.y,z:point.z-centre.z};
  const dot=w=>d.x*w.x+d.y*w.y+d.z*w.z;
  return Math.atan2(dot(v),dot(u))*180/Math.PI;
}
// Shortest signed difference between two angles, snapped to `step` degrees.
export function ringDelta(from,to,step) {
  const delta=((to-from+540)%360)-180;
  return step>0?snap(delta,step):delta;
}

// World size of one screen pixel at `point` (for picking tolerances and sizes).
export function pixelSize(camera,point,heightPx) {
  if(camera.isOrthographicCamera) return (camera.top-camera.bottom)/camera.zoom/heightPx;
  const distance=camera.position.distanceTo(new THREE.Vector3(point.x,point.y,point.z));
  return 2*distance*Math.tan(camera.fov*Math.PI/360)/heightPx;
}
const boundsOfParts=parts=>designBounds({parts:Array.isArray(parts)?parts:[parts]});
export const ringRadius=parts=>{const {size}=boundsOfParts(parts);return Math.hypot(size.x,size.y,size.z)/2*1.08+15;};
export function ringCentre(parts) {const {min,max}=boundsOfParts(parts);return {x:(min.x+max.x)/2,y:(min.y+max.y)/2,z:(min.z+max.z)/2};}
// Rings that can be turned in a view. 3D shows all three. An orthographic view
// looks along one world axis: with world axes it shows the ring turning within
// the view; with local axes (`frame`) it shows each of the piece's own rings
// that is not seen edge-on (at least one always is), drawn as an ellipse.
export const EDGE_ON=.25;
export function ringAxes(view,frame) {
  if(view==='3D') return ['x','y','z'];
  if(!frame) return [VIEW_NORMAL[view]];
  const normal=VIEW_NORMAL[view];
  return ['x','y','z'].filter(axis=>Math.abs(localAxis(frame,axis)[normal])>=EDGE_ON);
}
// Rotation rings around one piece or a selection, centred on its world bounds
// (the pivot used to turn the pieces together). With `frame` (a rotation, for
// local axes) the rings follow that frame. Each ring records its axis name and
// world basis for measuring drags.
export function buildRotateGizmo(parts,view,frame) {
  const centre=ringCentre(parts),radius=ringRadius(parts),group=new THREE.Group();
  group.position.set(centre.x,centre.y,centre.z);
  for(const axis of ringAxes(view,frame)) {
    const basis=ringBasis(axis,frame);
    const points=Array.from({length:96},(_,i)=>{
      const t=i/96*Math.PI*2,c=Math.cos(t)*radius,s=Math.sin(t)*radius;
      return new THREE.Vector3(basis.u.x*c+basis.v.x*s,basis.u.y*c+basis.v.y*s,basis.u.z*c+basis.v.z*s);
    });
    const ring=new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color:AXIS_COLORS[axis],depthTest:false,transparent:true}));
    ring.renderOrder=10;ring.userData={role:'ring',axis,basis};group.add(ring);
  }
  return group;
}
// The picked ring's {axis, basis}, or null.
export function pickRing(gizmo,camera,x,y,tolerance) {
  const ray=new THREE.Raycaster();ray.params.Line.threshold=tolerance;ray.setFromCamera(new THREE.Vector2(x,y),camera);
  gizmo.updateMatrixWorld(true);
  const ring=ray.intersectObjects(gizmo.children,false)[0]?.object;
  return ring?{axis:ring.userData.axis,basis:ring.userData.basis}:null;
}

// Plane that dropped pieces land on: the floor in 3D and Top, the back (z = 0)
// in Front and the left side (x = 0) in Side.
export const dropPlane=view=>new THREE.Plane(UNIT[VIEW_NORMAL[view]]??UNIT.y,0);
// Corner for a dropped piece of world `size` centred on the drop point, resting
// on the drop plane.
export function dropCorner(view,point,size,step) {
  const normal=VIEW_NORMAL[view]??'y',out={};
  for(const axis of ['x','y','z']) out[axis]=axis===normal?0:snap(point[axis]-size[axis]/2,step);
  return out;
}
// Arrow keys nudge along the axes as they appear on screen in each view. In
// Side view (looking from +x) screen-right is world −z. Shift+Up/Down moves
// vertically in 3D and Top.
export function nudgeAxis(view,key,shift=false) {
  const vertical={ArrowUp:['y',1],ArrowDown:['y',-1]};
  const maps={
    '3D':{ArrowLeft:['x',-1],ArrowRight:['x',1],ArrowUp:['z',-1],ArrowDown:['z',1]},
    Top:{ArrowLeft:['x',-1],ArrowRight:['x',1],ArrowUp:['z',-1],ArrowDown:['z',1]},
    Front:{ArrowLeft:['x',-1],ArrowRight:['x',1],...vertical},
    Side:{ArrowLeft:['z',1],ArrowRight:['z',-1],...vertical},
  };
  const entry=(shift&&(view==='3D'||view==='Top')?vertical[key]:undefined)??maps[view]?.[key];
  return entry?{axis:entry[0],sign:entry[1]}:null;
}
