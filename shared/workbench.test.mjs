import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {planePiece,finishPiece,cutToSize,planeSides} from './bench-edits.mjs';
import {measure,edgeDistances,markLine,snapToMarks,mortise,rebate,planeSlab,finishColor,FINISHES} from './workbench.mjs';
import {partCorners} from './furniture-design.mjs';
import {partCollisions} from './design-cnc.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids('b')});
const byName=(design,name)=>design.parts.find(p=>p.name===name);
const close=(a,b,m)=>assert.ok(Math.abs(a-b)<1e-6,`${m??''} ${a} != ${b}`);

test('planing a face: thinner, machining on that face shallower, the rest stays through',()=>{
  const design=box(),side=byName(design,'Left side');   // 18 thick along x; face A (x = 18) has 12 mm holes and a 10 mm groove
  const {design:planed,removed,size}=planePiece(design,side.id,'A',2);
  const a=byName(planed,'Left side');
  assert.equal(size,16);assert.equal(a.thickness,16);assert.equal(a.x,side.x,'face B stays put');assert.equal(removed,0);
  assert.deepEqual(a.features.map(f=>f.depth),[10,10,8],'2 mm shallower');
  const fromB=byName(planePiece(design,side.id,'B',2).design,'Left side');
  assert.equal(fromB.x,side.x+2,'planing face B moves that face in');assert.deepEqual(fromB.features.map(f=>f.depth),[12,12,10]);
  // Planed away: a 1 mm pocket on face A disappears after 1.5 mm.
  let withPocket=D.addFeature(design,side.id,'pocket',ids('k'));withPocket=D.updateFeature(withPocket.design,withPocket.featureId,{depth:1});
  const gone=planePiece(withPocket,side.id,'A',1.5);assert.equal(gone.removed,1);assert.equal(byName(gone.design,'Left side').features.length,3);
  assert.throws(()=>planePiece(design,side.id,'A',17.5),/less than 1 mm/);
  assert.throws(()=>planePiece(design,side.id,'top',1),/Plane face A or B/);
});

test('planing an edge or end: the piece stays in place, holes near it are protected',()=>{
  const design=box(),shelf=byName(design,'Shelf');    // 564 × 540 horizontal
  const planed=byName(planePiece(design,shelf.id,'u0',3).design,'Shelf');
  assert.equal(planed.length,561);assert.equal(planed.x,shelf.x+3);
  const side=byName(design,'Left side');
  assert.throws(()=>planePiece(design,side.id,'u0',58),/would cut into the Ø5 hole at u 60/);
  const edge=byName(planePiece(design,side.id,'v0',10).design,'Left side');
  assert.deepEqual(edge.features.filter(f=>f.type==='drill').map(f=>f.v),[336,336],'holes measured from the planed edge');
  const groove=edge.features.find(f=>f.type==='groove');assert.deepEqual([groove.v,groove.width],[8,684],'the groove keeps its place');
  // The box keeps its joints after a small edge planing on the shelf front.
  assert.deepEqual(partCollisions(planePiece(design,shelf.id,'v1',2).design),[]);
  // Bars are planed on their sides; tubes only at their ends.
  let frame=D.createDesign({id:'f',name:'F',makeId:ids('l')}),bar,tube;
  ({design:frame,partId:bar}=D.addPart(frame,'bar',{makeId:ids('p'),overrides:{axis:'x',width:45,height:45,length:700,x:0,y:0,z:0}}));
  ({design:frame,partId:tube}=D.addPart(frame,'tube',{makeId:ids('q'),overrides:{axis:'y',x:200,y:0,z:0}}));
  const slim=planePiece(frame,bar,'height1',5).design.parts[0];assert.equal(slim.height,40);assert.equal(slim.y,0);
  assert.equal(planePiece(frame,bar,'width0',5).design.parts[0].z,5,'a section side planed from below moves in');
  assert.deepEqual(planeSides(frame.parts[1]),['length0','length1']);
  assert.throws(()=>planePiece(frame,tube,'width0',1),/Only the ends/);
  assert.equal(planePiece(frame,tube,'length1',10).design.parts[1].length,690);
});

test('a turned piece is planed in place',()=>{
  let design=box();const shelf=byName(design,'Shelf');
  design=D.rotatePartsAbout(design,[shelf.id],{x:.2,y:1,z:.4},31);
  const before=partCorners(design.parts.find(p=>p.id===shelf.id)),after=partCorners(planePiece(design,shelf.id,'u1',4).design.parts.find(p=>p.id===shelf.id));
  // Corners at the u = 0 end (x minimum: the first four) do not move.
  for(const i of [0,1,2,3]) for(const k of ['x','y','z']) close(after[i][k],before[i][k],`corner ${i} ${k}`);
});

test('hand saw, chisel, rebate plane, brush, tape and square',()=>{
  const design=box(),side=byName(design,'Left side');
  const cut=cutToSize(design,side.id,{size:400},ids('c'));
  assert.equal(byName(cut.design,'Left side').length,400);assert.equal(cut.design.parts.length,design.parts.length,'offcut discarded');
  const kept=cutToSize(design,side.id,{size:400,keepOffcut:true},ids('c'));assert.equal(kept.design.parts.length,design.parts.length+1);
  assert.equal(byName(kept.design,'Left side 2').length,158,'560 − 400 − 2 mm kerf');
  assert.deepEqual(mortise(side,'A',{at:{a:200,b:300},width:10,length:60,along:'b',depth:12}),{type:'pocket',face:'A',u:200,v:300,length:10,width:60,depth:12});
  assert.throws(()=>mortise(side,'A',{at:{a:200,b:300},width:10,length:60,depth:20}),/Depth must be between/);
  assert.deepEqual(rebate(side,'A',{edge:'v1',width:12,depth:9}),{type:'groove',face:'A',u:0,v:708,length:560,width:12,depth:9});
  assert.throws(()=>rebate(side,'A',{edge:'v1',width:12,depth:18}),/cuts the edge off/);
  const painted=finishPiece(design,side.id,'#5a3a22');assert.equal(byName(painted,'Left side').color,'#5a3a22');
  assert.equal(byName(finishPiece(painted,side.id,null),'Left side').color,'#dec49a','natural: the material colour');
  assert.equal(finishColor(side,'natural'),'#dec49a');assert.equal(finishColor(side,'custom','#123456'),'#123456');assert.ok(FINISHES.length>=5);
  assert.deepEqual(measure({a:10,b:20},{a:40,b:60}),{da:30,db:40,distance:50});
  assert.deepEqual(edgeDistances(side,'A',{a:100,b:300}),{start:100,end:460,near:300,far:420});
  const marks=[markLine(side,'A',{direction:'across',at:150}),markLine(side,'A',{direction:'along',at:32})];
  assert.deepEqual(snapToMarks({a:153,b:30},marks),{a:150,b:32});assert.deepEqual(snapToMarks({a:160,b:100},marks),{a:160,b:100});
  assert.throws(()=>markLine(side,'A',{direction:'across',at:600}),/on the piece/);
  assert.deepEqual(planeSlab(side,'A','A',2),{a:[0,560],b:[0,720],y:[16,18]});assert.deepEqual(planeSlab(side,'B','A',2).y,[0,2],'face A is underneath');
});
