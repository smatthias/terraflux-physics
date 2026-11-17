// Shared 3D scene of the workshop benches (drilling, cutting): a table with a
// fence and a left stop, the piece placed by a 3 × 4 matrix (see
// drilling-bench.mjs / cutting-bench.mjs), a tool group above it, a laser cross
// where the tool goes in and an optional preview of the material to remove.
// Millimetres; y up from the table, z toward the operator, the tool at x = z = 0.
import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {buildDesignGroup,disposeDesignGroup} from './design-scene.js';
import './bench.css';

export const steel=()=>new THREE.MeshStandardMaterial({color:0xb8bec4,metalness:.8,roughness:.3});
export const paint=color=>new THREE.MeshStandardMaterial({color,roughness:.6});
const dispose=object=>object?.traverse(o=>{o.geometry?.dispose();for(const m of [o.material??[]].flat()) m.dispose?.();});

// Throws when WebGL is not available. `handlers` get pointer events on the canvas
// (pointerdown in the capture phase, so a press on the piece does not orbit).
export function createBenchScene(host,handlers={}) {
  const renderer=new THREE.WebGLRenderer({antialias:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));host.appendChild(renderer.domElement);
  const scene=new THREE.Scene();scene.background=new THREE.Color('#22302d');
  scene.add(new THREE.HemisphereLight(0xffffff,0x4a3c2c,2.4));const sun=new THREE.DirectionalLight(0xfff1da,2);sun.position.set(800,2000,1200);scene.add(sun);
  const camera=new THREE.PerspectiveCamera(40,1,1,1e5),controls=new OrbitControls(camera,renderer.domElement);controls.enableDamping=true;
  const wood=new THREE.MeshStandardMaterial({color:0xc49a62,roughness:.8}),metal=new THREE.MeshStandardMaterial({color:0x6b7378,metalness:.5,roughness:.5});
  const table=new THREE.Mesh(new THREE.BoxGeometry(2400,40,1200),wood);table.position.y=-20;scene.add(table);
  const base=new THREE.Mesh(new THREE.BoxGeometry(2300,500,1100),paint(0x3d4a45));base.position.y=-290;scene.add(base);
  const fence=new THREE.Mesh(new THREE.BoxGeometry(2200,60,30),metal);scene.add(fence);
  const stop=new THREE.Mesh(new THREE.BoxGeometry(20,60,120),paint(0xd9822b));scene.add(stop);
  const laser=new THREE.Group(),red=new THREE.MeshBasicMaterial({color:0xff3b30});
  for(const [w,d] of [[300,1.2],[1.2,300]]) laser.add(new THREE.Mesh(new THREE.BoxGeometry(w,.2,d),red));
  scene.add(laser);
  let piece=null,tool=null,overlay=null,frame=0;
  const events=[['pointerdown',handlers.down,true],['pointermove',handlers.move],['pointerup',handlers.up],['dblclick',handlers.double]].filter(e=>e[1]);
  for(const [type,fn,capture] of events) renderer.domElement.addEventListener(type,fn,!!capture);
  const resize=()=>{if(!host.clientWidth) return;renderer.setSize(host.clientWidth,host.clientHeight);camera.aspect=host.clientWidth/host.clientHeight;camera.updateProjectionMatrix();};
  const observer=new ResizeObserver(resize);observer.observe(host);resize();
  const animate=()=>{frame=requestAnimationFrame(animate);controls.update();renderer.render(scene,camera);};animate();
  return {
    // The piece (design coordinates of the unrotated part, minimum corner at the
    // origin) placed by `matrix`; the fence behind it and the stop at its left end.
    setPiece(design,part,matrix,{height,backZ,leftX}) {
      disposeDesignGroup(piece);
      const flat={...part,x:0,y:0,z:0};delete flat.rotation;
      piece=buildDesignGroup({layers:design.layers,parts:[flat]});
      piece.matrixAutoUpdate=false;piece.matrix.copy(new THREE.Matrix4().set(...matrix[0],...matrix[1],...matrix[2],0,0,0,1));scene.add(piece);
      fence.position.set(0,30,backZ-15);stop.position.set(leftX-10,30,backZ+60);laser.position.set(0,height+.3,0);
    },
    setTool(group) {if(tool) {scene.remove(tool);dispose(tool);}tool=group;if(group) scene.add(group);},
    setOverlay(group) {if(overlay) {scene.remove(overlay);dispose(overlay);}overlay=group;if(group) scene.add(group);},
    showLaser(visible) {laser.visible=visible;},
    frame({height,size,top}) {
      controls.target.set(0,height,0);
      if(top) camera.position.set(0,height+size*1.6,1);else camera.position.set(size*.55,height+size*.9,size*1.25);
      camera.near=1;camera.far=size*20+5000;camera.updateProjectionMatrix();controls.update();
    },
    // Where the pointer meets the top of the piece (y = height), or null.
    topPoint(event,height) {
      const rect=renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster();
      ray.setFromCamera(new THREE.Vector2((event.clientX-rect.left)/rect.width*2-1,-(event.clientY-rect.top)/rect.height*2+1),camera);
      return ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0,1,0),-height),new THREE.Vector3());
    },
    capture(pointerId) {renderer.domElement.setPointerCapture?.(pointerId);},
    // Runs `step(t)` for t from 0 to 1 over `duration` ms.
    play(duration,step) {return new Promise(resolve=>{const start=performance.now(),tick=now=>{const t=Math.min(1,(now-start)/duration);step(t);if(t<1) requestAnimationFrame(tick);else resolve();};requestAnimationFrame(tick);});},
    dispose() {
      cancelAnimationFrame(frame);observer.disconnect();controls.dispose();
      for(const [type,fn,capture] of events) renderer.domElement.removeEventListener(type,fn,!!capture);
      disposeDesignGroup(piece);dispose(scene);renderer.dispose();renderer.domElement.remove?.();
    },
  };
}
// Translucent box of material a tool will remove, from bench corners.
export function removalBox(min,max,color=0xff5a3c) {
  const size=[max[0]-min[0],max[1]-min[1],max[2]-min[2]].map(v=>Math.max(v,.2));
  const mesh=new THREE.Mesh(new THREE.BoxGeometry(...size),new THREE.MeshBasicMaterial({color,transparent:true,opacity:.45,depthWrite:false}));
  mesh.position.set((min[0]+max[0])/2,(min[1]+max[1])/2,(min[2]+max[2])/2);mesh.renderOrder=5;
  const group=new THREE.Group();group.add(mesh);return group;
}
