import * as THREE from 'three';
import { doorPose } from '../shared/openings.mjs';
import { wallSegment } from '../shared/room-topology.mjs';
export function addOpeningVisuals(group,room) {
  for(const o of room.openings||[]) {
    const {a,tx,tz,nx,nz}=wallSegment(room,o.wall);
    const position=(along,y,inset=-.08)=>new THREE.Vector3(a.x+tx*along+nx*inset,y,a.z+tz*along+nz*inset);
    const tag=(object,role,kind,color)=>{
      object.userData={id:o.wall,openingId:o.id,role,kind,baseColor:color};group.add(object);return object;
    };
    const box=(along,y,w,h,depth,color,kind,inset=-.08,angle=-Math.atan2(tz,tx))=>{
      const mesh=new THREE.Mesh(new THREE.BoxGeometry(w,h,depth),new THREE.MeshStandardMaterial({color,roughness:.6}));
      mesh.position.copy(position(along,y,inset));mesh.rotation.y=angle;
      return tag(mesh,'detail',kind,color);
    };
    const t=Math.min(.045,o.width/5,o.height/5),bottom=o.sill,top=o.sill+o.height;
    box(o.offset+t/2,(bottom+top)/2,t,o.height,.18,0x795e40,'frame');
    box(o.offset+o.width-t/2,(bottom+top)/2,t,o.height,.18,0x795e40,'frame');
    box(o.offset+o.width/2,top-t/2,o.width,t,.18,0x795e40,'frame');
    if(o.type==='window') {
      box(o.offset+o.width/2,bottom+t/2,o.width,t,.18,0x795e40,'frame');
      const glass=box(o.offset+o.width/2,(bottom+top)/2,o.width-2*t,o.height-2*t,.012,0x87cbd9,'glazing');
      glass.material.transparent=true;glass.material.opacity=.3;glass.material.depthWrite=false;
    } else {
      const clear=o.width-2*t,pose=doorPose(o);
      box(pose.hingeAlong+pose.hingeSign*t,(bottom+top-t)/2,clear,o.height-t,.035,0xbba078,'leaf',pose.swingSign*clear/2,-Math.atan2(nz*pose.swingSign,nx*pose.swingSign));
    }
    const line=(points,kind)=>{
      const color=o.type==='door'?0xe3c58c:0x98d4e3;
      const mesh=new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color,depthTest:false}));
      mesh.renderOrder=5;tag(mesh,'symbol',kind,color);
    };
    if(o.type==='door') {
      const pose=doorPose(o);
      const hinge=position(pose.hingeAlong,.04,0),tip=position(pose.hingeAlong,.04,pose.swingSign*o.width);
      line([hinge,tip],'door-leaf-symbol');
      const points=[];
      for(let j=0;j<=32;j++) {
        const angle=j/32*Math.PI/2;
        points.push(position(pose.hingeAlong+pose.hingeSign*o.width*Math.cos(angle),.04,pose.swingSign*o.width*Math.sin(angle)));
      }
      line(points,'door-swing');
    } else {
      for(const inset of [-.04,-.12]) line([position(o.offset,.04,inset),position(o.offset+o.width,.04,inset)],'window-symbol');
    }
  }
}
