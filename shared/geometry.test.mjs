import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {rectangle,moveCorner,snapPoint,scaleOutline,bounds,validateOutline} from './geometry.mjs';
import {validateProject} from './project.mjs';
const old={schemaVersion:1,units:'m',name:'Legacy',room:{width:6,depth:4,height:3}};
test('legacy project migrates to explicit connected corners and schema 6 roundtrips',()=>{
  const next=validateProject(old);
  assert.equal(next.schemaVersion,6); assert.deepEqual(next.room.outline,rectangle(6,4));
  next.room.outline=moveCorner(next.room.outline,'north-west',{x:-3.4,z:-2.4},.25,true);
  const normalized=validateProject(next);
  assert.deepEqual(normalized.room.outline[0],{x:-3.5,z:-2.5});
  assert.equal(normalized.room.width,6.5); assert.equal(normalized.room.depth,4.5);
  assert.deepEqual(validateProject(JSON.parse(JSON.stringify(normalized))),normalized);
});
test('snapping is symmetric around grid lines and can be disabled',()=>{
  assert.deepEqual(snapPoint({x:-1.13,z:2.13},.25),{x:-1.25,z:2.25});
  assert.deepEqual(snapPoint({x:-1.13,z:2.13},.25,false),{x:-1.13,z:2.13});
  assert.throws(()=>snapPoint({x:'',z:1},.25));
});
test('invalid corner moves reject crossings, concavity, reversed winding and collapsed walls',()=>{
  for(const p of [{x:4,z:3},{x:0,z:0},{x:3,z:-2},{x:Infinity,z:0}])
    assert.throws(()=>moveCorner(rectangle(6,4),'north-west',p,.25,false),/convex room|finite/);
  assert.throws(()=>validateOutline(rectangle(6,4).reverse()));
  const original=rectangle(6,4); assert.throws(()=>moveCorner(original,'north-west',original[1],.25,false),/convex room/);
  assert.deepEqual(original,rectangle(6,4));
});
test('outline scaling preserves connectivity and requested bounds',()=>{
  const outline=moveCorner(rectangle(6,4),'north-west',{x:-3.5,z:-2},.25,true);
  const scaled=scaleOutline(outline,'x',10);
  assert.ok(Math.abs(bounds(scaled).width-10)<1e-10);
  assert.equal(bounds(scaled).depth,4);
});
test('native polygon quantities match edited outlines and reject invalid shapes',()=>{
  const binary=new URL('../build/terratec-core',import.meta.url).pathname;
  const outline=moveCorner(rectangle(6,4),'north-west',{x:-3.5,z:-2.5},.25,true);
  const result=JSON.parse(execFileSync(binary,['--polygon','3',...outline.flatMap(p=>[p.x,p.z]).map(String)],{encoding:'utf8'}));
  let area=0,perimeter=0;
  outline.forEach((a,i)=>{const b=outline[(i+1)%4];area+=(a.x*b.z-b.x*a.z)/2;perimeter+=Math.hypot(b.x-a.x,b.z-a.z);});
  assert.ok(Math.abs(result.area-area)<1e-10);
  assert.ok(Math.abs(result.volume-area*3)<1e-10);
  assert.ok(Math.abs(result.perimeter-perimeter)<1e-10);
  assert.throws(()=>execFileSync(binary,['--polygon','3','4','3','3','-2','3','2','-3','2'],{stdio:'pipe'}));
});
