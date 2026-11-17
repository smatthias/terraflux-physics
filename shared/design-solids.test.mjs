import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {partCollisions} from './design-cnc.mjs';
import {pieceSolids,clip,prismSides,ROUND_TOLERANCE} from './design-solids.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const empty=()=>D.createDesign({id:'d',name:'D',makeId:ids('l')});
const add=(design,shape,overrides={})=>D.addPart(design,shape,{makeId:ids('p'+design.parts.length+'-'),overrides});
const drillLayer=design=>design.layers.find(l=>l.operation==='drill').id;
// An 18 mm panel standing in the x–y plane (x and y 0–600, z 0–18).
const withPanel=()=>add(empty(),'panel',{name:'Panel',orientation:'front',x:0,y:0,z:0,length:600,width:600,thickness:18});

test('round rods are round: the corner of their square outline is free space',()=>{
  // A Ø40 rod standing beside the panel's corner, 21.2 mm from it (radius 20).
  let {design}=withPanel();
  ({design}=add(design,'cylinder',{name:'Leg',axis:'y',diameter:40,length:600,x:-35,y:0,z:-35}));
  const rod=D.partWorldBox(design.parts[1]),panel=D.partWorldBox(design.parts[0]);
  assert.ok(['x','y','z'].every(k=>rod.min[k]<panel.max[k]&&panel.min[k]<rod.max[k]),'the square outlines overlap');
  assert.deepEqual(partCollisions(design),[]);
  // 1.2 mm closer, the round leg cuts the corner.
  assert.equal(partCollisions(D.updatePart(design,design.parts[1].id,{x:-34,z:-34})).length,1);
  // Touching the panel face (tangent) is allowed; 0.05 mm into it is not.
  assert.deepEqual(partCollisions(D.updatePart(design,design.parts[1].id,{x:100,z:18})),[]);
  assert.equal(partCollisions(D.updatePart(design,design.parts[1].id,{x:100,z:17.95})).length,1);
});

test('round pieces next to each other are tested as circles, also when turned',()=>{
  let design=empty();
  ({design}=add(design,'cylinder',{axis:'x',diameter:20,length:300,x:0,y:0,z:0}));
  ({design}=add(design,'cylinder',{axis:'x',diameter:20,length:300,x:0,y:15,z:15}));
  assert.deepEqual(partCollisions(design),[],'parallel rods 21.2 mm apart (diameters 20)');
  assert.equal(partCollisions(D.updatePart(design,design.parts[1].id,{y:14,z:14})).length,1,'19.8 mm apart');
  const turned=D.rotatePartsAbout(design,design.parts.map(p=>p.id),'z',33);
  assert.deepEqual(partCollisions(turned),[],'turned together');
  // A tube around a bar: the bar's corners reach into the round wall unless it fits the bore.
  let tube=empty();
  ({design:tube}=add(tube,'tube',{axis:'z',diameter:30,wall:2,length:200,x:0,y:0,z:0}));
  ({design:tube}=add(tube,'bar',{axis:'z',width:18,height:18,length:100,x:6,y:6,z:50}));
  assert.deepEqual(partCollisions(tube),[],'18 × 18 bar: diagonal 25.5 < bore 26');
  assert.equal(partCollisions(D.updatePart(tube,tube.parts[1].id,{width:19,height:19,x:5.5,y:5.5})).length,1,'19 × 19: diagonal 26.9 > bore 26');
});

test('drill holes are round: a dowel fits, a square peg of the same size does not',()=>{
  let {design,partId:panel}=withPanel();
  design=D.updatePart(design,panel,{features:[{id:'h1',type:'drill',face:'A',layer:drillLayer(design),u:100,v:100,diameter:8,depth:12}]});
  // Face A is z = 18; the hole goes 12 mm down to z = 6.
  const dowel=add(design,'cylinder',{axis:'z',diameter:8,length:30,x:96,y:96,z:6}).design;
  assert.deepEqual(partCollisions(dowel),[],'exact-fit Ø8 dowel');
  assert.equal(partCollisions(D.updatePart(dowel,dowel.parts[1].id,{z:5})).length,1,'1 mm deeper than the hole');
  assert.equal(partCollisions(D.updatePart(dowel,dowel.parts[1].id,{diameter:8.2,x:95.9,y:95.9})).length,1,'Ø8.2 in a Ø8 hole');
  const peg=add(design,'bar',{axis:'z',width:8,height:8,length:30,x:96,y:96,z:6}).design;
  assert.equal(partCollisions(peg).length,1,'an 8 × 8 peg does not fit a Ø8 hole');
  // A dowel tilted by 2° only partly in the hole: its overlap with the panel is what must fit.
  const tilted=D.updatePart(dowel,dowel.parts[1].id,{diameter:6,x:97,y:97,z:7,rotation:{x:2,y:0,z:0}});
  assert.deepEqual(partCollisions(tilted),[],'a thinner tilted dowel still inside the hole');
  assert.equal(partCollisions(D.updatePart(tilted,dowel.parts[1].id,{z:6})).length,1,'its lower rim would dip below the bottom of the hole');
});

test('a round rail sits in round holes of two side panels',()=>{
  let design=empty(),left,right;
  ({design,partId:left}=add(design,'panel',{name:'Left',orientation:'side',x:0,y:0,z:0,length:500,width:800,thickness:18}));
  ({design,partId:right}=add(design,'panel',{name:'Right',orientation:'side',x:582,y:0,z:0,length:500,width:800,thickness:18}));
  const hole=(face,id)=>({id,type:'drill',face,layer:drillLayer(design),u:250,v:700,diameter:25,depth:10});
  design=D.updatePart(D.updatePart(design,left,{features:[hole('A','a')]}),right,{features:[hole('B','b')]});
  ({design}=add(design,'tube',{name:'Rail',axis:'x',diameter:25,wall:1.5,length:584,x:8,y:687.5,z:237.5}));
  assert.deepEqual(partCollisions(design),[]);
  assert.equal(partCollisions(D.updatePart(design,design.parts[2].id,{diameter:25.4,y:687.3,z:237.3})).length,2,'a 25.4 mm rail in 25 mm holes');
});

test('prisms stay within the round tolerance and clipping keeps exact volumes',()=>{
  for(const r of [.5,4,12.5,50,500]) {
    const sides=prismSides(r),error=r*(1-Math.cos(Math.PI/sides));
    assert.ok(error<=ROUND_TOLERANCE+1e-12||sides===256,`r ${r}: ${sides} sides, ${error}`);
  }
  const {solids}=pieceSolids({shape:'bar',axis:'x',length:10,width:10,height:10,x:0,y:0,z:0});
  assert.equal(solids[0].geometry().planes.length,6);
  const cube=[[0,0,0],[0,1,0],[0,1,1],[0,0,1]];
  assert.equal(clip([cube.map(([x,y,z])=>({x,y,z}))],{n:{x:1,y:0,z:0},d:-1}),null,'entirely outside');
});

test('many holes and dowels are checked quickly',()=>{
  let {design,partId:panel}=withPanel();
  const features=[];
  for(let i=0;i<8;i++) for(let j=0;j<8;j++) features.push({id:`h${i}-${j}`,type:'drill',face:'A',layer:drillLayer(design),u:40+i*70,v:40+j*70,diameter:8,depth:12});
  design=D.updatePart(design,panel,{features});
  for(const f of features) ({design}=add(design,'cylinder',{axis:'z',diameter:8,length:30,x:f.u-4,y:f.v-4,z:6}));
  const start=performance.now();
  assert.deepEqual(partCollisions(design),[]);
  const ms=performance.now()-start;
  assert.ok(ms<3000,`64 dowels in 64 holes took ${ms.toFixed(0)} ms`);
});
