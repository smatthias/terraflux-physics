import * as THREE from 'three';
import {wallSegment} from '../shared/room-topology.mjs';
export function createPlacementPreview(room,result) {
  const o=result.opening;
  if(![o.offset,o.width].every(Number.isFinite) || o.width<=0) return new THREE.Group();
  const {a,tx,tz}=wallSegment(room,o.wall);
  const point=(along,inset)=>new THREE.Vector3(a.x+tx*along-tz*inset,.07,a.z+tz*along+tx*inset);
  const points=[point(o.offset,-.2),point(o.offset+o.width,-.2),point(o.offset+o.width,.2),point(o.offset,.2)];
  const color=result.valid?0x6ce0a0:0xff776b,group=new THREE.Group();
  const outline=new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color,depthTest:false}));
  const vertices=points.flatMap(p=>[p.x,p.y,p.z]);
  const geometry=new THREE.BufferGeometry().setAttribute('position',new THREE.Float32BufferAttribute(vertices,3));
  geometry.setIndex([0,1,2,0,2,3]);
  const fill=new THREE.Mesh(geometry,new THREE.MeshBasicMaterial({color,transparent:true,opacity:.3,side:THREE.DoubleSide,depthTest:false,depthWrite:false}));
  outline.renderOrder=11;fill.renderOrder=10;group.add(outline,fill);return group;
}
