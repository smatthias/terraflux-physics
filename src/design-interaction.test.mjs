import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from 'three';
import {createDesignInteraction} from './design-interaction.js';
import {buildDesignGroup,fitDesignCamera} from './design-scene.js';
import {buildRotateGizmo,ringRadius,ringCentre} from './design-gizmo.js';
import {createDesign,updateLayer,partCentre,rotatePartsAbout,addPart,updatePart} from '../shared/furniture-design-edit.mjs';
import {rotationMatrix,localAxis} from '../shared/design-transform.mjs';
const ids=()=>{let n=0;return ()=>'id'+(++n);};
const box=()=>createDesign({id:'box',name:'Box',template:'box',makeId:ids()});
function setup({view='Front',tool='move',design=box(),selected=null,grid=10,angle=15,axisMode='world'}={}) {
  const aspect=800/600,camera=view==='3D'?new THREE.PerspectiveCamera(40,aspect,1,1e6):new THREE.OrthographicCamera();
  fitDesignCamera(camera,design,aspect,view);
  const group=buildDesignGroup(design);group.updateMatrixWorld(true);
  const ids=selected===null?[]:[selected].flat(),parts=design.parts.filter(p=>ids.includes(p.id));
  const active=design.parts.find(p=>p.id===ids[0]);
  const gizmo=tool==='rotate'&&parts.length?buildRotateGizmo(parts,view,axisMode==='local'?active.rotation:undefined):null;gizmo?.updateMatrixWorld(true);
  const state={design,previews:0,preview:null,info:'',commits:[],selects:[],toggles:[],captured:0};
  const element={getBoundingClientRect:()=>({left:0,top:0,width:800,height:600}),setPointerCapture(){state.captured++;},releasePointerCapture(){state.captured--;}};
  const it=createDesignInteraction({element,camera:()=>camera,group:()=>group,gizmo:()=>gizmo,view:()=>view,tool:()=>tool,design:()=>state.design,
    selectedIds:()=>ids,axisMode:()=>axisMode,gridStep:()=>grid,angleStep:()=>angle,onPreview:(d,info)=>{state.preview=d;state.info=info;if(d) state.previews++;},
    onCommit:(d,message)=>state.commits.push({d,message}),onSelect:(hit,{toggle}={})=>(toggle?state.toggles:state.selects).push(hit)});
  const at=p=>{const v=new THREE.Vector3(p.x,p.y,p.z).project(camera);return {clientX:(v.x+1)*400,clientY:(1-v.y)*300};};
  const ev=(pos,extra={})=>({button:0,pointerId:7,...pos,preventDefault(){},stopImmediatePropagation(){state.stopped=true;},...extra});
  return {it,state,at,ev,design,gizmo};
}
const shelf=design=>design.parts.find(p=>p.name==='Shelf');

test('dragging a piece moves it on the view plane with grid snapping, as one commit',()=>{
  const {it,state,at,ev,design}=setup({view:'Front'}),part=shelf(design),c=partCentre(part),front={...c,z:560};
  it.pointerDown(ev(at(front)));
  assert.equal(state.stopped,true,'the press is taken from the orbit controls');
  it.pointerMove(ev(at({...front,x:front.x+60,y:front.y+20})));
  it.pointerMove(ev(at({...front,x:front.x+123,y:front.y+37})));
  assert.ok(state.previews>=2);assert.match(state.info,/^Move x 122 · y 39 mm$/);
  it.pointerUp(ev(at({...front,x:front.x+123,y:front.y+37})));
  assert.equal(state.commits.length,1);assert.equal(state.commits[0].message,'Piece moved');
  const moved=shelf(state.commits[0].d);
  assert.deepEqual([moved.x,moved.y,moved.z],[140,390,part.z],'corner snapped to 10 mm; depth unchanged in Front view');
  assert.equal(state.preview,null,'preview cleared on release');assert.equal(state.captured,0,'pointer released');
  assert.deepEqual(state.selects,[{partId:part.id,featureId:null}],'the dragged piece becomes selected');
});

test('a press without dragging selects; empty space clears; locked layers do not move',()=>{
  let {it,state,at,ev,design}=setup({view:'Front'});const c={...partCentre(shelf(design)),z:560};
  it.pointerDown(ev(at(c)));it.pointerMove(ev({...at(c),clientX:at(c).clientX+3}));it.pointerUp(ev({...at(c),clientX:at(c).clientX+3}));
  assert.equal(state.commits.length,0);assert.equal(state.previews,0);
  assert.deepEqual(state.selects,[{partId:shelf(design).id,featureId:null}]);
  it.pointerDown(ev({clientX:5,clientY:5}));it.pointerUp(ev({clientX:5,clientY:5}));
  assert.equal(state.selects.at(-1),null,'clicking empty space clears the selection');
  const locked=updateLayer(box(),box().layers.find(l=>l.operation==='profile').id,{locked:true});
  ({it,state,at,ev}=setup({view:'Front',design:locked}));
  it.pointerDown(ev(at(c)));it.pointerMove(ev(at({...c,x:c.x+200})));it.pointerUp(ev(at({...c,x:c.x+200})));
  assert.equal(state.commits.length,0,'locked pieces stay put');assert.equal(state.stopped,undefined,'the camera can still orbit');
});

test('in 3D a piece slides on the floor, and Shift lifts it vertically',()=>{
  for(const shift of [false,true]) {
    const {it,state,at,ev,design}=setup({view:'3D',grid:0}),part=shelf(design),c=partCentre(part),top={...c,y:part.y+18};
    it.pointerDown(ev(at(top)));
    const target=shift?{...top,y:top.y+100}:{...top,x:top.x+100,z:top.z-50};
    it.pointerMove(ev(at({...top,x:top.x+30}),{shiftKey:shift}));it.pointerMove(ev(at(target),{shiftKey:shift}));it.pointerUp(ev(at(target)));
    const moved=shelf(state.commits[0].d);
    if(shift) {assert.equal(moved.x,part.x);assert.equal(moved.z,part.z);assert.ok(moved.y>part.y+50,'lifted');}
    else {assert.equal(moved.y,part.y,'height unchanged');assert.ok(Math.abs(moved.x-(part.x+100))<1e-6&&Math.abs(moved.z-(part.z-50))<1e-6);}
  }
});

test('dragging a rotation ring turns the selected piece with angle snapping',()=>{
  const design=box(),part=shelf(design),c=partCentre(part),r=ringRadius(part);
  const {it,state,at,ev}=setup({view:'Top',tool:'rotate',selected:part.id,design});
  const ring=degrees=>({x:c.x+r*Math.sin(degrees*Math.PI/180),y:c.y,z:c.z+r*Math.cos(degrees*Math.PI/180)});
  it.pointerDown(ev(at(ring(0))));
  assert.equal(state.stopped,true);
  it.pointerMove(ev(at(ring(20))));assert.match(state.info,/^Rotate about y \+15°$/);
  it.pointerMove(ev(at(ring(40))));it.pointerUp(ev(at(ring(40))));
  assert.equal(state.commits.length,1);assert.equal(state.commits[0].message,'Piece rotated');
  assert.deepEqual(shelf(state.commits[0].d).rotation,{x:0,y:45,z:0});
  const inside=setup({view:'Top',tool:'rotate',selected:part.id,design});
  inside.it.pointerDown(inside.ev(inside.at(c)));inside.it.pointerUp(inside.ev(inside.at(c)));
  assert.equal(inside.state.commits.length,0,'pressing the piece itself does not rotate');
});

test('cancel discards a drag without committing',()=>{
  const {it,state,at,ev,design}=setup({view:'Front'}),c={...partCentre(shelf(design)),z:560};
  it.pointerDown(ev(at(c)));it.pointerMove(ev(at({...c,x:c.x+100})));
  assert.ok(state.preview);assert.equal(it.dragging(),true);
  assert.equal(it.cancel(),true);assert.equal(state.preview,null);
  it.pointerUp(ev(at({...c,x:c.x+100})));
  assert.equal(state.commits.length,0);assert.equal(it.cancel(),false);
});

const byName=(design,name)=>design.parts.find(p=>p.name===name);
test('dragging one piece of a multi-selection moves the whole selection rigidly',()=>{
  const design=box(),left=byName(design,'Left side'),right=byName(design,'Right side');
  const {it,state,at,ev}=setup({view:'Front',selected:[left.id,right.id],design});
  const grab={x:4,y:100,z:560};
  it.pointerDown(ev(at(grab)));it.pointerMove(ev(at({...grab,x:grab.x+40})));it.pointerMove(ev(at({...grab,x:grab.x+103,y:grab.y+12})));
  assert.match(state.info,/· 2 pieces$/);
  it.pointerUp(ev(at({...grab,x:grab.x+103,y:grab.y+12})));
  const after=state.commits[0].d;
  for(const part of [left,right]) {const moved=byName(after,part.name);assert.deepEqual([moved.x-part.x,moved.y-part.y,moved.z-part.z],[100,10,0],part.name);}
  for(const name of ['Bottom','Top','Shelf','Back']) assert.deepEqual(byName(after,name),byName(design,name),name+' stays');
  assert.deepEqual(state.selects,[],'the selection is kept');
});
test('Ctrl/⌘ toggles without dragging; Shift toggles on click but still drags',()=>{
  const design=box(),shelf=byName(design,'Shelf'),c={...partCentre(shelf),z:560};
  let {it,state,at,ev}=setup({view:'Front',design});
  it.pointerDown(ev(at(c),{ctrlKey:true}));it.pointerMove(ev(at({...c,x:c.x+80})));it.pointerUp(ev(at({...c,x:c.x+80})));
  assert.deepEqual(state.toggles,[{partId:shelf.id,featureId:null}]);assert.equal(state.commits.length,0,'no drag with Ctrl');
  it.pointerDown(ev(at(c),{shiftKey:true}));it.pointerUp(ev(at(c)));
  assert.equal(state.toggles.length,2,'Shift-click toggles');assert.deepEqual(state.selects,[]);
  it.pointerDown(ev({clientX:3,clientY:3},{shiftKey:true}));it.pointerUp(ev({clientX:3,clientY:3}));
  assert.deepEqual(state.selects,[],'Shift-click on empty space keeps the selection');
  ({it,state,at,ev}=setup({view:'Front',design}));
  it.pointerDown(ev(at(c),{shiftKey:true}));it.pointerMove(ev(at({...c,x:c.x+100}),{shiftKey:true}));it.pointerUp(ev(at({...c,x:c.x+100})));
  assert.equal(state.commits.length,1,'Shift-drag still moves');assert.equal(state.toggles.length,0);
});
test('rings turn a whole selection about its common centre',()=>{
  const design=box(),ids=design.parts.map(p=>p.id),c=ringCentre(design.parts),r=ringRadius(design.parts);
  const {it,state,at,ev,gizmo}=setup({view:'Top',tool:'rotate',selected:ids,design,angle:15});
  assert.deepEqual(gizmo.position.toArray(),[c.x,c.y,c.z]);
  const ring=d=>({x:c.x+r*Math.sin(d*Math.PI/180),y:c.y,z:c.z+r*Math.cos(d*Math.PI/180)});
  it.pointerDown(ev(at(ring(10))));it.pointerMove(ev(at(ring(60))));it.pointerMove(ev(at(ring(101))));it.pointerUp(ev(at(ring(101))));
  assert.deepEqual(state.commits[0].d,rotatePartsAbout(design,ids,'y',90),'the same as turning the group by 90°');
});
test('local axes turn a rotated piece about its own axis',()=>{
  let design=createDesign({id:'d',name:'D',makeId:ids()}),partId;
  ({design,partId}=addPart(design,'bar',{makeId:()=>'bar',overrides:{axis:'x',length:400,width:40,height:40,x:0,y:200,z:0}}));
  design=updatePart(design,partId,{rotation:{x:0,y:0,z:30}});
  const part=design.parts[0],c=partCentre(part);
  const {it,state,at,ev,gizmo}=setup({view:'3D',tool:'rotate',selected:partId,design,angle:15,axisMode:'local'});
  const xRing=gizmo.children.find(r=>r.userData.axis==='x'),{u,v,axis}=xRing.userData.basis,radius=ringRadius(part);
  const lx=localAxis(part.rotation,'x');for(const k of ['x','y','z']) assert.ok(Math.abs(axis[k]-lx[k])<1e-9,'the x ring follows the local x axis');
  const point=d=>{const a=d*Math.PI/180;return {x:c.x+radius*(u.x*Math.cos(a)+v.x*Math.sin(a)),y:c.y+radius*(u.y*Math.cos(a)+v.y*Math.sin(a)),z:c.z+radius*(u.z*Math.cos(a)+v.z*Math.sin(a))};};
  it.pointerDown(ev(at(point(20))));it.pointerMove(ev(at(point(50))));it.pointerMove(ev(at(point(65))));it.pointerUp(ev(at(point(65))));
  assert.match(state.commits[0].message,/Piece rotated/);
  const turned=state.commits[0].d.parts[0],got=rotationMatrix(turned.rotation);
  const rz=rotationMatrix({x:0,y:0,z:30}),rx=rotationMatrix({x:45,y:0,z:0}),want=[0,1,2].map(i=>[0,1,2].map(j=>rz[i][0]*rx[0][j]+rz[i][1]*rx[1][j]+rz[i][2]*rx[2][j]));
  for(let i=0;i<3;i++) for(let j=0;j<3;j++) assert.ok(Math.abs(got[i][j]-want[i][j])<1e-6,`R = R_old · Rx(45°) [${i}][${j}]`);
  const centre=partCentre(turned);for(const k of ['x','y','z']) assert.ok(Math.abs(centre[k]-c[k])<1e-6,'turns in place');
});
test('orthographic views turn about local axes too, with rings not seen edge-on',()=>{
  let design=createDesign({id:'d',name:'D',makeId:ids()}),partId;
  ({design,partId}=addPart(design,'bar',{makeId:()=>'bar',overrides:{axis:'x',length:400,width:40,height:40,x:0,y:200,z:0}}));
  design=updatePart(design,partId,{rotation:{x:0,y:30,z:0}});
  const part=design.parts[0],c=partCentre(part);
  const world=setup({view:'Front',tool:'rotate',selected:partId,design});
  assert.deepEqual(world.gizmo.children.map(r=>r.userData.axis),['z'],'world axes: the ring within the view');
  const {it,state,at,ev,gizmo}=setup({view:'Front',tool:'rotate',selected:partId,design,angle:15,axisMode:'local'});
  assert.deepEqual(gizmo.children.map(r=>r.userData.axis),['x','z'],'local y lies in the view plane, so its ring is edge-on and left out');
  const zRing=gizmo.children.find(r=>r.userData.axis==='z'),{u,v}=zRing.userData.basis,radius=ringRadius(part);
  const point=d=>{const a=d*Math.PI/180;return {x:c.x+radius*(u.x*Math.cos(a)+v.x*Math.sin(a)),y:c.y+radius*(u.y*Math.cos(a)+v.y*Math.sin(a)),z:c.z+radius*(u.z*Math.cos(a)+v.z*Math.sin(a))};};
  it.pointerDown(ev(at(point(10))));it.pointerMove(ev(at(point(40))));it.pointerMove(ev(at(point(55))));it.pointerUp(ev(at(point(55))));
  assert.match(state.commits[0].message,/Piece rotated/);
  const got=rotationMatrix(state.commits[0].d.parts[0].rotation);
  const ry=rotationMatrix({x:0,y:30,z:0}),rz=rotationMatrix({x:0,y:0,z:45}),want=[0,1,2].map(i=>[0,1,2].map(j=>ry[i][0]*rz[0][j]+ry[i][1]*rz[1][j]+ry[i][2]*rz[2][j]));
  for(let i=0;i<3;i++) for(let j=0;j<3;j++) assert.ok(Math.abs(got[i][j]-want[i][j])<1e-6,`R = R_old · Rz(45°) about the local z axis [${i}][${j}]`);
});
