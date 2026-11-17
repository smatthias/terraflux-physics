import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {cncPlan,partCollisions} from './design-cnc.mjs';
import {MATERIALS} from './materials.mjs';
const ids=prefix=>{let n=0;return ()=>prefix+(++n);};
const empty=()=>D.createDesign({id:'d',name:'D',makeId:ids('l')});
const add=(design,shape,overrides={})=>D.addPart(design,shape,{makeId:ids('p'+design.parts.length+'-'),overrides});
const close=(a,b,m)=>assert.ok(Math.abs(a-b)<1e-6,`${m??''} ${a} != ${b}`);

test('every piece has a base form, a length, a material and a colour',()=>{
  let design=empty();
  for(const shape of D.SHAPES) {
    const result=add(design,shape);design=result.design;
    const piece=design.parts.at(-1),form=D.PIECE_FORMS[shape];
    assert.equal(piece.shape,shape);assert.equal(piece.length,form.defaults.length);
    assert.equal(piece.material,form.material);assert.equal(piece.color,D.materialColor(form.material));
    assert.equal(piece.name,form.short+' 1');
  }
  assert.deepEqual(design.parts.map(p=>p.shape),['panel','bar','cylinder','tube','box-tube','angle']);
  const chosen=add(empty(),'bar',{material:'walnut',color:'#ABC123',length:1200}).design.parts[0];
  assert.deepEqual([chosen.material,chosen.color,chosen.length],['walnut','#abc123',1200]);
  assert.equal(add(empty(),'tube',{material:'brass'}).design.parts[0].color,D.materialColor('brass'),'colour defaults to the chosen material');
  assert.throws(()=>add(empty(),'bar',{color:'walnut'}),/colour must be #rrggbb/);
  assert.throws(()=>add(empty(),'sphere'),/Base form must be/);
});

test('early block parts load as bars along their longest side with the same box',()=>{
  const layer={id:'l',name:'L',color:'#000000',visible:true,locked:false,operation:'none'};
  const block=(width,height,depth)=>D.validateDesign({id:'d',name:'D',layers:[layer],parts:[{id:'b',name:'B',shape:'block',material:'oak',layer:'l',x:1,y:2,z:3,width,height,depth}]}).parts[0];
  for(const [w,h,d,axis] of [[1000,50,40,'x'],[50,700,50,'y'],[40,50,900,'z'],[50,50,50,'y']]) {
    const bar=block(w,h,d);
    assert.equal(bar.shape,'bar');assert.equal(bar.axis,axis);
    assert.deepEqual(D.partSize(bar),{x:w,y:h,z:d});assert.deepEqual([bar.x,bar.y,bar.z],[1,2,3]);
    assert.equal(bar.color,D.materialColor('oak'));
    const again=D.validateDesign({id:'d',name:'D',layers:[layer],parts:[bar]}).parts[0];
    assert.deepEqual(again,bar,'normalisation is stable');
  }
});

test('each base form validates its own section',()=>{
  const design=shape=>add(empty(),shape).design;
  const update=(shape,changes)=>{const d=design(shape);return D.updatePart(d,d.parts[0].id,changes).parts[0];};
  assert.throws(()=>update('tube',{wall:12.5}),/Tube wall must be between 0.2 and 12.4 mm/);
  assert.equal(update('tube',{wall:12.4}).wall,12.4);
  assert.throws(()=>update('box-tube',{wall:20}),/Tube wall/);
  assert.throws(()=>update('angle',{wall:40}),/Angle wall must be between 0.2 and 39.9 mm/);
  assert.throws(()=>update('bar',{width:0}),/Section width/);
  assert.throws(()=>update('cylinder',{diameter:0}),/Diameter/);
  assert.throws(()=>update('bar',{length:5001}),/Length must be between 1 and 5000 mm/);
  assert.throws(()=>update('bar',{axis:'w'}),/Length direction/);
  assert.deepEqual(D.partSize(update('bar',{axis:'z'})),{z:700,x:45,y:45});
});

test('colour follows a material change until a piece colour is chosen',()=>{
  let design=add(empty(),'panel').design;const id=design.parts[0].id;
  design=D.updatePart(design,id,{material:'walnut'});assert.equal(design.parts[0].color,D.materialColor('walnut'));
  design=D.updatePart(design,id,{color:'#224466'});
  design=D.updatePart(design,id,{material:'oak'});assert.equal(design.parts[0].color,'#224466','a chosen colour is kept');
  design=D.updatePart(design,id,{material:'pine',color:'#101010'});assert.equal(design.parts[0].color,'#101010');
  design=D.updatePart(design,id,{color:D.materialColor('pine')});
  design=D.updatePart(design,id,{material:'maple'});assert.equal(design.parts[0].color,D.materialColor('maple'),'resetting re-links the colour');
  assert.throws(()=>D.updatePart(design,id,{material:'gold'}),/^Error: Choose a catalogue material$/,'unknown materials are reported, not crashed on');
});

test('changing the base form keeps identity and fills the same box',()=>{
  for(const from of D.SHAPES) for(const to of D.SHAPES) {
    const start=add(empty(),from,{material:'walnut',color:'#345678'}),before=start.design.parts[0];
    const after=D.convertPart(start.design,before.id,to).parts[0];
    assert.equal(after.shape,to);
    for(const key of ['id','name','material','color','layer','x','y','z']) assert.equal(after[key],before[key],`${from}→${to} ${key}`);
    const a=D.partSize(before),b=D.partSize(after);
    if(D.ROUND_SHAPES.includes(to)&&!D.ROUND_SHAPES.includes(from)) {
      close(b[after.axis],a[after.axis],`${from}→${to} length`);
      for(const k of ['x','y','z']) assert.ok(b[k]<=a[k]+1e-9,`${from}→${to} fits inside`);
    } else for(const k of ['x','y','z']) close(b[k],a[k],`${from}→${to} ${k}`);
  }
  const board=add(empty(),'panel').design.parts[0];
  const upright=D.convertPart(D.convertPart(add(empty(),'panel').design,board.id,'bar'),board.id,'panel');
  assert.equal(upright.parts[0].orientation,'horizontal','thinnest extent becomes the thickness');
  const d=add(empty(),'bar').design;
  assert.equal(D.convertPart(d,d.parts[0].id,'bar'),d,'same form is a no-op');
});

test('form changes respect machining features and locked layers',()=>{
  let {design,partId}=add(empty(),'panel');
  design=D.addFeature(design,partId,'drill',ids('f')).design;
  assert.throws(()=>D.convertPart(design,partId,'bar'),/Remove the machining features/);
  const locked=D.updateLayer(add(empty(),'bar').design,empty().layers.find(l=>l.operation==='none').id,{locked:true});
  assert.throws(()=>D.convertPart(locked,locked.parts[0].id,'tube'),/locked/);
});

test('a tube receives a rod inside it; a rod through its wall collides',()=>{
  let design=empty(),tube,rod;
  ({design,partId:tube}=add(design,'tube',{axis:'y',diameter:30,wall:2,length:500,x:0,y:0,z:0}));
  ({design,partId:rod}=add(design,'cylinder',{axis:'y',diameter:26,length:200,x:2,y:100,z:2}));
  assert.deepEqual(partCollisions(design),[]);
  const poking=D.updatePart(design,rod,{diameter:28});
  assert.equal(partCollisions(poking).length,1,'a rod thicker than the bore');
  let boxed=empty(),outer;
  ({design:boxed,partId:outer}=add(boxed,'box-tube',{axis:'x',width:40,height:40,wall:2,length:600,x:0,y:0,z:0}));
  ({design:boxed}=add(boxed,'bar',{axis:'x',width:36,height:36,length:100,x:50,y:2,z:2}));
  assert.deepEqual(partCollisions(boxed),[]);
  // An L-angle is its two 3 mm legs: a bar in its inside corner is clear of them.
  let angle=add(empty(),'angle',{axis:'x',width:40,height:40,wall:3,length:600,x:0,y:0,z:0}).design,bar;
  ({design:angle,partId:bar}=add(angle,'bar',{axis:'x',width:20,height:20,length:100,x:50,y:10,z:10}));
  assert.deepEqual(partCollisions(angle),[],'the inside corner of an angle is free space');
  assert.deepEqual(partCollisions(D.updatePart(angle,bar,{y:3,z:3})),[],'resting on both legs touches them');
  assert.equal(partCollisions(D.updatePart(angle,bar,{y:2})).length,1,'into the flat leg');
  assert.equal(partCollisions(D.updatePart(angle,bar,{z:1})).length,1,'into the upright leg');
  const turned=D.rotatePartsAbout(angle,angle.parts.map(p=>p.id),'z',25);
  assert.deepEqual(partCollisions(turned),[],'still clear when the pair is rotated together');
  assert.match(cncPlan(D.updatePart(design,tube,{layer:design.layers.find(l=>l.operation==='profile').id})).issues.find(i=>i.code==='NOT_SHEET').message,/round tube/);
});

test('the catalogue colour of every material is a valid piece colour',()=>{
  for(const id of Object.keys(MATERIALS)) assert.match(D.materialColor(id),/^#[0-9a-f]{6}$/);
});
