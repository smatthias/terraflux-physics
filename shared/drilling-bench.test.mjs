import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {drillHoles} from './bench-edits.mjs';
import {benchFace,holeAt,holePosition,rowOfHoles,handPosition,benchTransform,BENCH_FACES,DRILL_BITS} from './drilling-bench.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids('b')});
const side=design=>design.parts.find(p=>p.name==='Left side');   // length 560, width 720, 18 thick

test('each face or edge up has its own coordinates, depth and height',()=>{
  const part=side(box());
  assert.deepEqual(BENCH_FACES.map(f=>[f,benchFace(part,f).a.key,benchFace(part,f).b.key,benchFace(part,f).depthLimit]),
    [['A','u','v',18],['B','u','v',18],['v0','u','t',720],['v1','u','t',720],['u0','v','t',560],['u1','v','t',560]]);
  // Where b = 0 lies follows from laying the real panel down unmirrored, so it depends on the panel's orientation.
  assert.deepEqual(BENCH_FACES.map(f=>benchFace(part,f).bZero),['back','front','back','front','front','back'],'side-facing panel');
  const back=box().parts.find(p=>p.name==='Back');
  assert.deepEqual(BENCH_FACES.map(f=>benchFace(back,f).bZero),['front','back','front','back','back','front'],'front-facing panel');
  assert.throws(()=>benchFace({shape:'bar'},'A'),/Only panels/);assert.throws(()=>benchFace(part,'C'),/Choose a face/);
  assert.deepEqual(holeAt(part,'A',{a:100,b:309,diameter:8,depth:12}),{face:'A',diameter:8,depth:12,u:100,v:309});
  assert.deepEqual(holeAt(part,'u0',{a:100,b:9,diameter:8,depth:30}),{face:'u0',diameter:8,depth:30,v:100,t:9});
  assert.deepEqual(holeAt(part,'B',{a:10,b:10,diameter:5,depth:3,through:true}).depth,18,'through: the full thickness');
  assert.deepEqual(holePosition(part,'u0',{type:'drill',face:'u0',u:0,v:100,t:9}),{a:100,b:9});
  assert.equal(holePosition(part,'A',{type:'drill',face:'B',u:1,v:1}),null);
  assert.ok(DRILL_BITS.some(b=>b.diameter===35&&b.kind==='forstner'),'hinge cup bit');
});

test('the panel rests on the table with the chosen face up and the target under the bit',()=>{
  const part=side(box()),{length:L,width:W,thickness:T}=part;
  const corners=[];for(const u of [0,L]) for(const v of [0,W]) for(const n of [0,T]) corners.push([u,v,n]);
  for(const face of BENCH_FACES) {
    const f=benchFace(part,face),target={a:f.a.length/3,b:f.b.length/4},bench=benchTransform(part,face,target);
    const points=corners.map(bench.apply);
    assert.equal(Math.min(...points.map(p=>p.y)),0,`${face}: on the table`);assert.equal(Math.max(...points.map(p=>p.y)),f.height,`${face}: face up`);
    // The target point on the face is at the bit (x = z = 0) on top.
    const hole=holeAt(part,face,{...target,diameter:1,depth:1}),sheet={A:[hole.u,hole.v,T],B:[hole.u,hole.v,0],v0:[hole.u,0,hole.t],v1:[hole.u,W,hole.t],u0:[0,hole.v,hole.t],u1:[L,hole.v,hole.t]}[face];
    assert.deepEqual(bench.apply(sheet),{x:0,y:f.height,z:0},face);
    // Moving along a goes right; b goes to the back or the front as bZero says.
    const moved=holeAt(part,face,{a:target.a+10,b:target.b+10,diameter:1,depth:1}),p={A:[moved.u,moved.v,T],B:[moved.u,moved.v,0],v0:[moved.u,0,moved.t],v1:[moved.u,W,moved.t],u0:[0,moved.v,moved.t],u1:[L,moved.v,moved.t]}[face];
    const q=bench.apply(p);assert.equal(q.x,10,face);assert.equal(q.z,f.bZero==='back'?10:-10,`${face}: b grows away from its zero side`);
  }
});

test('rows, hand positions and drilling holes into the design',()=>{
  assert.deepEqual(rowOfHoles({a:37,b:50},{count:3,spacing:32}),[{a:37,b:50},{a:69,b:50},{a:101,b:50}]);
  assert.deepEqual(rowOfHoles({a:37,b:50},{count:2,spacing:32,along:'b'}).map(h=>h.b),[50,82]);
  assert.throws(()=>rowOfHoles({a:0,b:0},{count:0}),/1–50/);
  const part=side(box());
  assert.deepEqual(handPosition(part,'A',{a:100.26,b:-4},.5),{a:100.5,b:0},'snapped and kept on the face');
  assert.deepEqual(handPosition(part,'A',{a:9999,b:7.3},0),{a:560,b:7.3});
  let design=box();const before=side(design).features.length;
  const holes=rowOfHoles({a:37,b:50},{count:3}).map(p=>holeAt(side(design),'A',{...p,diameter:5,depth:12}));
  const result=drillHoles(design,side(design).id,holes,ids('h'));
  assert.equal(side(result.design).features.length,before+3);assert.equal(result.featureIds.length,3);
  assert.ok(side(result.design).features.slice(-3).every(f=>f.type==='drill'&&f.layer===design.layers.find(l=>l.operation==='drill').id));
  assert.throws(()=>drillHoles(design,side(design).id,[holeAt(side(design),'A',{a:2,b:50,diameter:8,depth:12})],ids('x')),/within the panel face/);
  assert.throws(()=>drillHoles(design,side(design).id,[holeAt(side(design),'A',{a:50,b:50,diameter:8,depth:19})],ids('x')),/depth/);
  const edge=drillHoles(design,side(design).id,[holeAt(side(design),'v1',{a:100,b:9,diameter:8,depth:30})],ids('e')).design;
  assert.deepEqual(side(edge).features.at(-1),{id:'e1',type:'drill',face:'v1',layer:side(edge).features.at(-1).layer,u:100,v:720,t:9,depth:30,diameter:8});
  const bar=D.addPart(design,'bar',{makeId:ids('r')});
  assert.throws(()=>drillHoles(bar.design,bar.partId,[{face:'A',u:1,v:1,diameter:5,depth:5}],ids('y')),/Only panels/);
});

import {benchMatrix} from './drilling-bench.mjs';
import {sheetPoint} from './furniture-design-geometry.mjs';
test('the drawing matrix places every panel orientation like the bench frame',()=>{
  let design=box();
  for(const part of design.parts.filter(p=>p.shape==='panel')) for(const face of BENCH_FACES) {
    const f=benchFace(part,face),target={a:f.a.length*.3,b:f.b.length*.6},m=benchMatrix(part,face,target),bench=benchTransform(part,face,target);
    const flat={...part,x:0,y:0,z:0};
    for(const [u,v,n] of [[0,0,0],[part.length,0,0],[0,part.width,part.thickness],[part.length*.4,part.width*.7,part.thickness*.2]]) {
      const d=sheetPoint(flat,u,v,n),p=m.map(r=>r[0]*d.x+r[1]*d.y+r[2]*d.z+r[3]),q=bench.apply([u,v,n]);
      assert.ok(Math.abs(p[0]-q.x)<1e-6&&Math.abs(p[1]-q.y)<1e-6&&Math.abs(p[2]-q.z)<1e-6,`${part.name} ${face} (${u},${v},${n})`);
    }
    // A proper rotation: determinant +1.
    const r=m.map(row=>row.slice(0,3)),det=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    assert.ok(Math.abs(det-1)<1e-9,`${part.name} ${face}: not mirrored`);
  }
});
