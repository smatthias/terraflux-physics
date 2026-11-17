import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {splitPart,addMachining} from './bench-edits.mjs';
import {SAW_BLADES,ROUTER_BITS,cutFrame,cutMatrix,sawPlan,routerPlan} from './cutting-bench.mjs';
import {partCorners} from './furniture-design.mjs';
import {partCollisions} from './design-cnc.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids('b')});
const byName=(design,name)=>design.parts.find(p=>p.name===name);
const close=(a,b,m)=>assert.ok(Math.abs(a-b)<1e-6,`${m??''} ${a} != ${b}`);

test('sawing a panel in two: the kerf is removed and machining follows the material',()=>{
  const design=box(),side=byName(design,'Left side');   // length 560 (along z), width 720, shelf-pin holes at u 60 and 500, a back groove
  const {design:cut,partIds}=splitPart(design,side.id,{along:'length',at:280,kerf:3.2},ids('s'));
  const [a,b]=partIds.map(id=>cut.parts.find(p=>p.id===id));
  assert.equal(a.id,side.id);assert.equal(a.name,'Left side');assert.equal(b.name,'Left side 2');
  assert.equal(a.length,280);close(b.length,276.8);
  close(b.z,side.z+283.2,'the second piece stays where that stretch was');assert.equal(a.z,side.z);
  assert.deepEqual(a.features.filter(f=>f.type==='drill').map(f=>f.u),[60]);
  assert.deepEqual(b.features.filter(f=>f.type==='drill').map(f=>f.u),[216.8]);
  // The back groove (u 10–16, across the width) stays with the first piece.
  assert.deepEqual(a.features.find(f=>f.type==='groove'),side.features.find(f=>f.type==='groove'));assert.ok(!b.features.some(f=>f.type==='groove'));
  assert.deepEqual(partCollisions(cut),[],'the carcass joints still hold');
  assert.throws(()=>splitPart(design,side.id,{along:'length',at:58,kerf:3.2},ids('x')),/The cut goes through the Ø5 hole at u 60/);
  assert.throws(()=>splitPart(design,side.id,{along:'length',at:.5,kerf:3.2},ids('x')),/at least 1 mm/);
  assert.throws(()=>splitPart(design,side.id,{along:'length',at:600,kerf:3.2},ids('x')),/beyond the end/);
  const trimmed=splitPart(design,side.id,{along:'length',at:558,kerf:3.2},ids('t'));
  assert.equal(trimmed.partIds.length,1,'less than 1 mm left: a trim');assert.equal(trimmed.design.parts.length,design.parts.length);
  const ripped=splitPart(design,side.id,{along:'width',at:300,kerf:3.2},ids('r'));
  const [r1,r2]=ripped.partIds.map(id=>ripped.design.parts.find(p=>p.id===id));
  assert.deepEqual([r1.width,r2.width],[300,416.8]);
  // A rip across the groove (v 18–702) cuts it with the panel: 18–300 and 303.2–702.
  assert.deepEqual([r1,r2].map(p=>p.features.filter(f=>f.type==='groove').map(f=>[f.v,f.width])),[[[18,282]],[[0,398.8]]]);
  assert.deepEqual(r2.features.filter(f=>f.type==='drill').map(f=>f.v),[42.8,42.8],'the shelf-pin holes at v 346 go with the second piece');
});

test('pieces turned at any angle stay in place when cut; profiles are crosscut',()=>{
  let design=box();const shelf=byName(design,'Shelf');
  design=D.rotatePartsAbout(design,[shelf.id],{x:.3,y:1,z:.2},37);
  const turned=design.parts.find(p=>p.id===shelf.id),{design:cut,partIds}=splitPart(design,shelf.id,{along:'length',at:200,kerf:4},ids('s'));
  const [a,b]=partIds.map(id=>cut.parts.find(p=>p.id===id)),whole=partCorners(turned),first=partCorners(a),second=partCorners(b);
  // partCorners lists x-min corners first (the shelf's length runs along x): the cut keeps those of the first piece and the x-max ones of the second.
  for(const i of [0,1,2,3]) for(const k of ['x','y','z']) close(first[i][k],whole[i][k],`start corner ${i} ${k}`);
  for(const i of [4,5,6,7]) for(const k of ['x','y','z']) close(second[i][k],whole[i][k],`end corner ${i} ${k}`);
  let rods=D.createDesign({id:'r',name:'R',makeId:ids('l')}),rod;
  ({design:rods,partId:rod}=D.addPart(rods,'cylinder',{makeId:ids('p'),overrides:{axis:'y',length:700,diameter:30,x:0,y:0,z:0}}));
  const legs=splitPart(rods,rod,{along:'length',at:350,kerf:3},ids('q')).design;
  assert.deepEqual(legs.parts.map(p=>[p.length,p.y]),[[350,0],[347,353]]);
  assert.throws(()=>splitPart(rods,rod,{along:'width',at:10,kerf:3},ids('q')),/Profiles are cut across their length/);
});

test('saw and router plans: splits, kerf grooves, routed grooves and pockets',()=>{
  const side=byName(box(),'Left side');
  assert.ok(SAW_BLADES.some(b=>b.kerf===3.2)&&ROUTER_BITS.some(b=>b.diameter===8));
  assert.deepEqual(sawPlan(side,'A',{direction:'cross',at:280,kerf:3.2}),{kind:'split',along:'length',at:280,kerf:3.2,rest:276.8});
  assert.deepEqual(sawPlan(side,'A',{direction:'rip',at:100,kerf:6,through:false,depth:6}).feature,{type:'groove',face:'A',u:0,v:100,length:560,width:6,depth:6});
  assert.deepEqual(sawPlan(side,'B',{direction:'cross',at:50,kerf:3.2,through:false,depth:5}).feature,{type:'groove',face:'B',u:50,v:0,length:3.2,width:720,depth:5});
  assert.throws(()=>sawPlan(side,'A',{direction:'cross',at:50,kerf:3.2,through:false,depth:18}),/Blade height/);
  assert.deepEqual(routerPlan(side,'A',{mode:'groove',from:{a:100,b:50},to:{a:300,b:55},diameter:8,depth:6}),{type:'groove',face:'A',u:96,v:46,length:208,width:8,depth:6},'snapped along the length');
  assert.deepEqual(routerPlan(side,'A',{mode:'groove',from:{a:100,b:0},to:{a:102,b:200},diameter:8,depth:6}),{type:'groove',face:'A',u:96,v:0,length:8,width:204,depth:6},'from the edge across');
  assert.throws(()=>routerPlan(side,'A',{mode:'groove',from:{a:0,b:50},to:{a:560,b:50},diameter:8,through:true}),/divides the panel; use the saw/);
  assert.deepEqual(routerPlan(side,'A',{mode:'pocket',from:{a:300,b:200},to:{a:200,b:260},diameter:12,depth:8}),{type:'pocket',face:'A',u:200,v:200,length:100,width:60,depth:8});
  assert.throws(()=>routerPlan(side,'A',{mode:'pocket',from:{a:0,b:0},to:{a:10,b:50},diameter:12,depth:8}),/needs a pocket at least 12 × 12 mm/);
  // Features from the plans go into the design like any other machining.
  const design=box(),added=addMachining(design,byName(design,'Left side').id,[routerPlan(side,'A',{mode:'pocket',from:{a:200,b:200},to:{a:300,b:260},diameter:12,depth:8})],ids('m'));
  assert.equal(byName(added.design,'Left side').features.at(-1).type,'pocket');
  assert.throws(()=>cutFrame(side,'v0'),/face A or face B/);
});

test('profiles lie along the table, unmirrored, with the target at the blade',()=>{
  let design=D.createDesign({id:'r',name:'R',makeId:ids('l')});
  for(const [shape,overrides] of [['bar',{axis:'y',width:45,height:30,length:700}],['tube',{axis:'z',diameter:25,wall:2,length:500}],['angle',{axis:'x',width:40,height:30,wall:3,length:600}]]) {
    const result=D.addPart(design,shape,{makeId:ids(shape),overrides:{...overrides,x:0,y:0,z:0}});design=result.design;
    const part=design.parts.at(-1),f=cutFrame(part),m=cutMatrix(part,'top',{a:100,b:f.b.length/2});
    const det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    assert.equal(det,1,`${shape}: a rotation`);
    const size={x:0,y:0,z:0};for(const [k,v] of Object.entries(D.partSize(part))) size[k]=v;
    const corners=[];for(const x of [0,size.x]) for(const y of [0,size.y]) for(const z of [0,size.z]) corners.push(m.map(r=>r[0]*x+r[1]*y+r[2]*z+r[3]));
    assert.equal(Math.min(...corners.map(c=>c[1])),0,`${shape} on the table`);assert.equal(Math.max(...corners.map(c=>c[1])),f.height);
    assert.equal(Math.min(...corners.map(c=>c[0])),-100,'a measured from the start');
    close(Math.min(...corners.map(c=>c[2])),-f.b.length/2,'b measured from the back');
  }
});
