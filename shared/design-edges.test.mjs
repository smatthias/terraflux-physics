import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {cncPlan,partCollisions} from './design-cnc.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const layerOf=(design,op)=>design.layers.find(l=>l.operation===op).id;
// A shelf (x 18–582) joined to a left side (x 0–18) by two 8 mm dowels.
function dowelJoint({shelfHoles=true,sideHoles=true}={}) {
  let design=D.createDesign({id:'d',name:'Joint',makeId:ids('l')}),side,shelf;
  const drill=layerOf(design,'drill');
  ({design,partId:side}=D.addPart(design,'panel',{makeId:ids('s'),overrides:{name:'Side',orientation:'side',x:0,y:0,z:0,length:500,width:700,thickness:18}}));
  ({design,partId:shelf}=D.addPart(design,'panel',{makeId:ids('h'),overrides:{name:'Shelf',orientation:'horizontal',x:18,y:300,z:0,length:564,width:500,thickness:18}}));
  const hole=(face,extra)=>({id:'f'+Math.random().toString(36).slice(2),type:'drill',face,layer:drill,diameter:8,...extra});
  const sideFeatures=sideHoles?[hole('A',{u:100,v:309,depth:12}),hole('A',{u:400,v:309,depth:12})]:[];
  const shelfFeatures=shelfHoles?[hole('u0',{v:100,t:9,depth:20}),hole('u0',{v:400,t:9,depth:20})]:[];
  design=D.updatePart(D.updatePart(design,side,{features:sideFeatures}),shelf,{features:shelfFeatures});
  for(const z of [96,396]) ({design}=D.addPart(design,'cylinder',{makeId:ids('w'+z),overrides:{name:'Dowel',axis:'x',diameter:8,length:30,x:6,y:305,z}}));
  return design;
}

test('edge holes are drill holes that fit the thickness and lie along the edge',()=>{
  const design=dowelJoint(),shelf=design.parts.find(p=>p.name==='Shelf'),hole=shelf.features[0];
  assert.deepEqual({face:hole.face,u:hole.u,v:hole.v,t:hole.t,depth:hole.depth},{face:'u0',u:0,v:100,t:9,depth:20},'u is fixed to the edge');
  const edit=changes=>()=>D.updateFeature(design,hole.id,changes);
  assert.throws(edit({t:3}),/fit within the panel thickness/,'a Ø8 hole needs t between 4 and 14');
  assert.throws(edit({diameter:20}),/fit within the panel thickness/);
  assert.throws(edit({v:498}),/lie along the edge/);
  assert.throws(edit({depth:565}),/Feature depth must be between 0.01 and 564 mm/,'as deep as the panel length');
  assert.equal(D.updateFeature(design,hole.id,{u:250}).parts[1].features[0].u,0,'the edge fixes u');
  for(const [face,u,v] of [['u1',564,100],['v0',100,0],['v1',100,500]]) {
    const moved=D.updateFeature(design,hole.id,{face,u:100,v:100}).parts[1].features[0];
    assert.deepEqual([moved.u,moved.v],[u,v],face);
  }
  const groove=D.addFeature(design,shelf.id,'groove',ids('g'));
  assert.throws(()=>D.updateFeature(groove.design,groove.featureId,{face:'v0'}),/Only drill holes can go into panel edges/);
});

test('edge holes drill inward from their edge, along the panel',()=>{
  const design=dowelJoint(),shelf=design.parts.find(p=>p.name==='Shelf');
  const boxFor=(face,extra)=>D.featureBox(shelf,{type:'drill',face,diameter:8,t:9,depth:20,u:100,v:100,...extra});
  assert.deepEqual(boxFor('u0'),{min:{x:18,y:305,z:96},max:{x:38,y:313,z:104}});
  assert.deepEqual(boxFor('u1'),{min:{x:562,y:305,z:96},max:{x:582,y:313,z:104}});
  assert.deepEqual(boxFor('v0'),{min:{x:114,y:305,z:0},max:{x:122,y:313,z:20}});
  assert.deepEqual(boxFor('v1'),{min:{x:114,y:305,z:480},max:{x:122,y:313,z:500}});
  assert.equal(D.drillAxis(shelf,{face:'u0'}),'x');assert.equal(D.drillAxis(shelf,{face:'v1'}),'z');assert.equal(D.drillAxis(shelf,{face:'A'}),'y');
  assert.equal(D.isThrough(shelf,{face:'v0',depth:500}),true);assert.equal(D.isThrough(shelf,{face:'v0',depth:499}),false);
});

test('dowels in matching face and edge holes make a valid joint, also when turned',()=>{
  assert.deepEqual(partCollisions(dowelJoint()),[]);
  assert.equal(partCollisions(dowelJoint({shelfHoles:false})).length,2,'no edge holes: both dowels collide with the shelf');
  assert.equal(partCollisions(dowelJoint({sideHoles:false})).length,2,'no face holes: both collide with the side');
  const design=dowelJoint(),turned=D.rotatePartsAbout(design,design.parts.map(p=>p.id),'y',35);
  assert.deepEqual(partCollisions(turned),[],'the whole joint turned by 35°');
  const tilted=D.rotatePartsAbout(turned,turned.parts.map(p=>p.id),'x',-20);
  assert.deepEqual(partCollisions(tilted),[]);
});

test('switching a hole between a face and an edge keeps it valid',()=>{
  let {design,partId}=D.addPart(D.createDesign({id:'d',name:'D',makeId:ids('l')}),'panel',{makeId:ids('p')});
  let added=D.addFeature(design,partId,'drill',ids('f'));design=added.design;
  design=D.updateFeature(design,added.featureId,{face:'v1'});
  let hole=design.parts[0].features[0];
  assert.deepEqual([hole.face,hole.u,hole.v,hole.t],['v1',300,400,9],'centred across the 18 mm thickness');
  design=D.updateFeature(design,added.featureId,{face:'A'});hole=design.parts[0].features[0];
  assert.deepEqual([hole.face,hole.u,hole.v,'t' in hole],['A',300,200,false],'back on the face, centred');
});

test('the CNC plan lists edge holes for a horizontal drilling unit',()=>{
  const plan=cncPlan(dowelJoint()),shelf=plan.parts.find(p=>p.name==='Shelf');
  const edge=shelf.operations.filter(o=>o.edge);
  assert.equal(edge.length,2);assert.deepEqual({face:edge[0].face,t:edge[0].t,depth:edge[0].depth,diameter:edge[0].diameter},{face:'u0',t:9,depth:20,diameter:8});
  assert.ok(plan.issues.some(i=>i.code==='EDGE_DRILLING'&&i.severity==='info'&&/2 edge holes/.test(i.message)));
  assert.equal(plan.ready,true);
});
