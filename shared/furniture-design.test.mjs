import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {cncPlan,cncPlanJson,partCollisions} from './design-cnc.mjs';
import {cabinetParts} from './cabinet-parts.mjs';
import {validateProject} from './project.mjs';
import {rectangle} from './geometry.mjs';
const ids=()=>{let n=0;return ()=>'id'+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids()});
const empty=()=>D.createDesign({id:'empty',name:'Empty',makeId:ids()});
const layer=(design,operation)=>design.layers.find(l=>l.operation===operation).id;
const panel=design=>D.addPart(design,'panel',{makeId:()=>'p'+Math.random().toString(36).slice(2)});

test('new designs get CNC-ordered default layers; the box template is complete and collision free',()=>{
  const design=box();
  assert.deepEqual(design.layers.map(l=>[l.name,l.operation]),[['Drilling','drill'],['Grooves','groove'],['Pockets','pocket'],['Parts','profile'],['Reference','none']]);
  assert.deepEqual(design.parts.map(p=>p.name),['Left side','Right side','Bottom','Top','Shelf','Back']);
  assert.deepEqual(D.designBounds(design).size,{x:600,y:720,z:560});
  assert.deepEqual(partCollisions(design),[]);
  assert.equal(empty().parts.length,0);
  assert.throws(()=>D.createDesign({id:'x',template:'sofa',makeId:ids()}),/Unknown design template/);
});

test('panel orientations map sheet length, width and thickness to world axes',()=>{
  const base={id:'p',name:'P',shape:'panel',material:'oak',layer:'l',x:0,y:0,z:0,length:600,width:400,thickness:18};
  assert.deepEqual(D.partSize({...base,orientation:'horizontal'}),{x:600,z:400,y:18});
  assert.deepEqual(D.partSize({...base,orientation:'front'}),{x:600,y:400,z:18});
  assert.deepEqual(D.partSize({...base,orientation:'side'}),{z:600,y:400,x:18});
  assert.deepEqual(D.partSize({shape:'cylinder',axis:'y',diameter:30,length:700}),{x:30,y:700,z:30});
  assert.deepEqual(D.partSize({shape:'bar',axis:'y',length:700,width:50,height:40}),{x:50,y:700,z:40});
  assert.deepEqual(D.partSize({shape:'angle',axis:'x',length:900,width:40,height:30,wall:3}),{x:900,z:40,y:30});
});

test('face A is on the positive thickness side; depth goes into the material',()=>{
  const side={shape:'panel',orientation:'side',x:100,y:0,z:0,length:560,width:720,thickness:18};
  assert.deepEqual(D.faceFrame(side,'A')(10,20,0),{x:118,y:20,z:10});
  assert.deepEqual(D.faceFrame(side,'A')(10,20,12),{x:106,y:20,z:10});
  assert.deepEqual(D.faceFrame(side,'B')(10,20,12),{x:112,y:20,z:10});
  const groove={type:'groove',face:'A',u:10,v:18,length:6,width:684,depth:10};
  assert.deepEqual(D.featureBox({...side,x:0},groove),{min:{x:8,y:18,z:10},max:{x:18,y:702,z:16}});
  const drill={type:'drill',face:'B',u:100,v:50,diameter:8,depth:18};
  assert.equal(D.isThrough(side,drill),true);
});

test('features must lie on the face, within the thickness and on an existing layer',()=>{
  const design=box(),left=design.parts[0],drill=left.features[0];
  const bad=changes=>()=>D.updateFeature(design,drill.id,changes);
  assert.throws(bad({u:1}),/Drill hole must lie within the panel face/);
  assert.throws(bad({depth:19}),/Feature depth must be between 0.01 and 18 mm/);
  assert.throws(bad({depth:0}),/Feature depth/);
  assert.throws(bad({face:'C'}),/face must be A, B or an edge/);
  assert.throws(bad({layer:'nope'}),/Layer not found/);
  assert.equal(D.updateFeature(design,drill.id,{depth:18}).parts[0].features[0].depth,18,'through is depth = thickness');
  const groove=left.features.find(f=>f.type==='groove');
  assert.throws(()=>D.updateFeature(design,groove.id,{u:555}),/Groove must lie within the panel face/);
  const retyped=D.updateFeature(design,groove.id,{type:'drill',diameter:5}).parts[0].features.find(f=>f.id===groove.id);
  assert.equal(retyped.type,'groove','type cannot change');assert.equal('diameter' in retyped,false);
  assert.throws(()=>D.validateDesign({...design,parts:[...design.parts,{id:'b',name:'B',shape:'bar',material:'oak',layer:design.layers[0].id,x:0,y:0,z:0,axis:'x',length:10,width:1,height:1,features:[drill]}]}),/supported on panels/);
});

test('design, part and layer validation',()=>{
  const design=box();
  assert.deepEqual(D.validateDesign(JSON.parse(JSON.stringify(design))),design,'validation is idempotent');
  assert.throws(()=>D.validateDesign({...design,name:' '}),/Design name/);
  assert.throws(()=>D.validateDesign({...design,layers:[]}),/1–32 layers/);
  assert.throws(()=>D.validateDesign({...design,layers:[...design.layers,{...design.layers[0],id:'dup'}]}),/Layer names must be unique/);
  assert.throws(()=>D.updateLayer(design,design.layers[0].id,{color:'red'}),/#rrggbb/);
  assert.equal(D.updateLayer(design,design.layers[0].id,{color:'#ABCDEF'}).layers[0].color,'#abcdef');
  assert.throws(()=>D.updatePart(design,design.parts[0].id,{material:'gold'}),/catalogue material/);
  assert.throws(()=>D.updatePart(design,design.parts[0].id,{thickness:0}),/Panel thickness/);
  assert.throws(()=>D.updatePart(design,design.parts[0].id,{orientation:'diagonal'}),/orientation/);
  assert.throws(()=>D.updatePart(design,design.parts[0].id,{x:NaN}),/Position x/);
  assert.throws(()=>D.validateDesigns([design,design]),/Design IDs must be unique/);
  const sharedFeature={...design,parts:design.parts.map((p,i)=>i===1?{...p,features:[{...design.parts[0].features[0]}]}:p)};
  assert.throws(()=>D.validateDesign(sharedFeature),/Feature IDs must be unique/);
});

test('editing adds, changes, duplicates and removes parts without touching the input',()=>{
  const design=empty(),before=JSON.stringify(design),makeId=ids();
  let {design:next,partId}=D.addPart(design,'panel',{makeId});
  assert.equal(JSON.stringify(design),before);
  assert.deepEqual(next.parts[0],{id:partId,name:'Panel 1',shape:'panel',material:'birch',color:'#dec49a',layer:layer(design,'profile'),x:0,y:0,z:0,orientation:'horizontal',length:600,width:400,thickness:18,features:[]});
  const cyl=D.addPart(next,'cylinder',{makeId});
  assert.equal(cyl.design.parts[1].x,650,'new parts go 50 mm to the right');
  assert.equal(cyl.design.parts[1].layer,layer(design,'none'),'solid parts default to the reference layer');
  next=D.updatePart(cyl.design,partId,{length:800,orientation:'side',name:' Side '});
  assert.equal(next.parts[0].name,'Side');assert.deepEqual(D.partSize(next.parts[0]),{x:18,y:400,z:800});
  const withFeature=D.addFeature(next,partId,'drill',makeId);
  const dup=D.duplicatePart(withFeature.design,partId,makeId);
  const clone=dup.design.parts.find(p=>p.id===dup.partId);
  assert.equal(clone.name,'Side (copy)');assert.notEqual(clone.features[0].id,withFeature.featureId);
  assert.equal(clone.x,68,'duplicate goes 50 mm beyond the original');
  assert.equal(D.removePart(dup.design,partId).parts.length,2);
  assert.throws(()=>D.removePart(dup.design,'missing'),/Part not found/);
});

test('new features have sensible defaults on face A and their matching layer',()=>{
  const start=panel(empty()),makeId=ids();
  const drill=D.addFeature(start.design,start.partId,'drill',makeId);
  const f=drill.design.parts[0].features[0];
  assert.deepEqual({type:f.type,face:f.face,u:f.u,v:f.v,diameter:f.diameter,depth:f.depth,layer:f.layer},{type:'drill',face:'A',u:300,v:200,diameter:5,depth:12,layer:layer(start.design,'drill')});
  const groove=D.addFeature(drill.design,start.partId,'groove',makeId).design.parts[0].features[1];
  assert.deepEqual([groove.u,groove.length,groove.v,groove.width,groove.layer],[0,600,196,8,layer(start.design,'groove')]);
  const thin=D.updatePart(start.design,start.partId,{thickness:6});
  assert.equal(D.addFeature(thin,start.partId,'drill',makeId).design.parts[0].features[0].depth,6,'default depth never exceeds the panel');
  const block=D.addPart(start.design,'bar',{makeId});
  assert.throws(()=>D.addFeature(block.design,block.partId,'drill',makeId),/supported on panels/);
});

test('locked layers protect their parts and features; the layer itself can be unlocked',()=>{
  let design=box();const parts=layer(design,'profile'),drill=layer(design,'drill'),left=design.parts[0];
  design=D.updateLayer(design,parts,{locked:true});
  assert.throws(()=>D.updatePart(design,left.id,{x:5}),/Layer “Parts” is locked/);
  assert.throws(()=>D.removePart(design,left.id),/locked/);
  assert.throws(()=>D.addPart(design,'panel',{makeId:ids(),overrides:{layer:parts}}),/locked/);
  assert.equal(D.addPart(design,'panel',{makeId:ids()}).design.parts.at(-1).layer!==parts,true,'new panels avoid locked layers');
  design=D.updateLayer(design,parts,{locked:false});
  design=D.updateLayer(design,drill,{locked:true});
  assert.throws(()=>D.updateFeature(design,left.features[0].id,{depth:10}),/Drilling/);
  assert.throws(()=>D.removeFeature(design,left.features[0].id),/Drilling/);
  const groove=left.features.find(f=>f.type==='groove');
  assert.throws(()=>D.updateFeature(design,groove.id,{layer:drill}),/Drilling/,'cannot move onto a locked layer');
  assert.doesNotThrow(()=>D.updatePart(design,left.id,{x:5}),'unlocked part with a locked feature stays movable');
});

test('layers can be added, reordered and removed only when empty',()=>{
  let design=box();
  const added=D.addLayer(design,{makeId:()=>'new'});
  assert.equal(added.design.layers.at(-1).name,'Layer 6');
  assert.throws(()=>D.addLayer(design,{makeId:()=>'n2',name:'drilling'}),/unique/);
  assert.throws(()=>D.removeLayer(design,layer(design,'drill')),/in use/);
  assert.equal(D.removeLayer(design,layer(design,'pocket')).layers.length,4);
  const moved=D.moveLayer(design,layer(design,'profile'),-3);
  assert.deepEqual(moved.layers.map(l=>l.name),['Parts','Drilling','Grooves','Pockets','Reference'],'a move keeps the other layers in order');
  assert.equal(D.moveLayer(design,design.layers[0].id,-1),design,'moving past the end is a no-op');
  let one=empty();for(const l of one.layers.slice(1)) one=D.removeLayer(one,l.id);
  assert.throws(()=>D.removeLayer(one,one.layers[0].id),/at least one layer/);
});

test('a room cabinet converts into an editable design with matching dimensions',()=>{
  const cabinet={id:'c',type:'cabinet',name:'Storage',width:.8,depth:.5,height:1,thickness:.018,shelves:1,x:0,z:0,rotation:0,material:'walnut',hardwareMaterial:'brass'};
  const design=D.designFromCabinet(cabinet,cabinetParts(cabinet),{id:'d',makeId:ids()});
  const panels=design.parts.filter(p=>p.shape==='panel'),blocks=design.parts.filter(p=>p.shape==='bar');
  assert.equal(panels.length,cabinetParts(cabinet).filter(p=>p.cut).length);
  assert.ok(panels.every(p=>p.material==='walnut'&&p.thickness===18));
  assert.ok(blocks.length&&blocks.every(p=>p.material==='brass'&&p.color==='#b5953f'&&p.layer===layer(design,'none')),'handles are brass reference bars');
  assert.ok(blocks.every(p=>p.axis==='y'&&p.length===120),'handle bars run along their longest side');
  assert.deepEqual(panels.filter(p=>p.name.startsWith('Side')).map(p=>p.orientation),['side','side']);
  assert.equal(panels.find(p=>p.name.startsWith('Back')).orientation,'front');
  const body=D.designBounds({...design,parts:panels});
  assert.deepEqual([body.min.x,body.max.x,body.min.y,body.max.y,body.min.z,body.max.z],[0,800,0,1000,0,500]);
});

test('the CNC plan orders operations by layer and flags problems',()=>{
  let design=box();
  const plan=cncPlan(design),left=plan.parts[0];
  assert.equal(plan.ready,true);
  assert.deepEqual(left.operations.map(o=>o.operation),['drill','drill','groove','profile']);
  assert.deepEqual(left.sheet,{lengthMm:560,widthMm:720,thicknessMm:18});
  assert.deepEqual(plan.layers.map(l=>[l.order,l.operations]),[[1,4],[2,2],[3,0],[4,6],[5,0]]);
  assert.ok(plan.parts[1].operations.filter(o=>o.operation!=='profile').every(o=>o.face==='B'&&o.flip));
  assert.deepEqual(plan.issues.map(i=>i.code),['FLIP_REQUIRED']);
  // Reordering layers reorders machining.
  design=D.moveLayer(design,layer(design,'profile'),-3);
  assert.deepEqual(cncPlan(design).parts[0].operations.map(o=>o.operation),['profile','drill','drill','groove']);
  // Mismatched and unmachined layers.
  const drillFeature=design.parts[0].features[0].id;
  const wrong=D.updateFeature(design,drillFeature,{layer:layer(design,'pocket')});
  const issues=cncPlan(wrong).issues;
  assert.ok(issues.some(i=>i.code==='LAYER_OPERATION_MISMATCH'&&i.severity==='error'));assert.equal(cncPlan(wrong).ready,false);
  const none=cncPlan(D.updateFeature(design,drillFeature,{layer:layer(design,'none')}));
  assert.ok(none.issues.some(i=>i.code==='NOT_MACHINED'));assert.equal(none.parts[0].operations.filter(o=>o.type==='drill').length,1);
  const hidden=cncPlan(D.updateLayer(design,layer(design,'drill'),{visible:false}));
  assert.ok(hidden.issues.some(i=>i.code==='HIDDEN_LAYER'));
});

test('collisions are allowed only where a feature receives the other part',()=>{
  let design=box();const makeId=ids();
  const groove=design.parts[0].features.find(f=>f.type==='groove');
  assert.deepEqual(cncPlan(D.removeFeature(design,groove.id)).issues.filter(i=>i.code==='PART_COLLISION').map(i=>i.partId+'/'+i.otherPartId),[design.parts[0].id+'/'+design.parts[5].id]);
  // A dowel sitting in a drilled hole is a joint, not a collision.
  const hole=design.parts[2];
  let withHole=D.addFeature(design,hole.id,'drill',makeId);
  withHole={design:D.updateFeature(withHole.design,withHole.featureId,{u:100,v:100,diameter:8,depth:12})};
  const dowel=D.addPart(withHole.design,'cylinder',{makeId,overrides:{axis:'y',diameter:8,length:30,x:18+100-4,y:18-12,z:100-4}});
  assert.deepEqual(partCollisions(dowel.design),[]);
  const offset=D.updatePart(dowel.design,dowel.partId,{x:18+100});
  assert.equal(partCollisions(offset).length,1,'a dowel beside its hole collides');
  const block=cncPlan(D.updatePart(dowel.design,dowel.partId,{layer:layer(design,'profile')}));
  assert.ok(block.issues.some(i=>i.code==='NOT_SHEET'));
});

test('CNC plan JSON is a versioned, machine-neutral export',()=>{
  const json=JSON.parse(cncPlanJson(box(),'2024-01-01T00:00:00.000Z'));
  assert.equal(json.format,'terratec-cnc-plan');assert.equal(json.units,'mm');assert.match(json.note,/Not an NC program/);
  assert.equal(json.parts.length,6);assert.equal(json.generatedAt,'2024-01-01T00:00:00.000Z');
});

test('designs persist as schema 7 project data; projects without designs are unchanged',()=>{
  const base={schemaVersion:6,units:'m',name:'Designs',presets:[],room:{height:3,outline:rectangle(6,4),openings:[],furniture:[]}};
  const plain=validateProject(base);
  assert.equal(plain.schemaVersion,6);assert.equal('furnitureDesigns' in plain,false);
  const withDesign=validateProject({...plain,schemaVersion:7,furnitureDesigns:[box()]});
  assert.equal(withDesign.schemaVersion,7);assert.deepEqual(withDesign.furnitureDesigns,[box()]);
  assert.deepEqual(validateProject(JSON.parse(JSON.stringify(withDesign))),withDesign);
  assert.deepEqual(validateProject({...plain,schemaVersion:7,furnitureDesigns:[]}),plain,'an empty list is not stored');
  assert.throws(()=>validateProject({...withDesign,schemaVersion:6}),/require schemaVersion 7/);
  assert.throws(()=>validateProject({...withDesign,furnitureDesigns:[{...box(),layers:[]}]}),/layers/);
  assert.ok(JSON.stringify(box()).length<4000,'a six-panel design stays small within the 64 KiB project limit');
});

test('duplicated designs get fresh IDs throughout and keep layer links',()=>{
  let n=0;const design=box(),copy=D.duplicateDesign(design,{id:'copy',makeId:()=>'dup'+(++n)});
  assert.equal(copy.name,'Box (copy)');
  const original=new Set([...design.layers,...design.parts,...design.parts.flatMap(p=>p.features??[])].map(x=>x.id));
  assert.ok([...copy.layers,...copy.parts,...copy.parts.flatMap(p=>p.features??[])].every(x=>!original.has(x.id)));
  const layerIds=new Set(copy.layers.map(l=>l.id));
  assert.ok(copy.parts.every(p=>layerIds.has(p.layer)&&(p.features??[]).every(f=>layerIds.has(f.layer))));
  assert.deepEqual(cncPlan(copy).layers.map(l=>l.operations),cncPlan(design).layers.map(l=>l.operations));
});
