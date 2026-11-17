import {test} from 'node:test';
import assert from 'node:assert/strict';
import {subtract,boxSolid,cylinderSolid,solidVolume} from './csg.js';
const close=(a,b,tol,m)=>assert.ok(Math.abs(a-b)<tol,`${m??''} ${a} != ${b}`);
const polygonArea=(n,r)=>n/2*r*r*Math.sin(2*Math.PI/n);
const panel=()=>boxSolid({x:0,y:0,z:0},{x:600,y:18,z:400},'panel');

test('primitives are closed and face outward',()=>{
  close(solidVolume(panel()),600*18*400,1e-6);
  close(solidVolume(cylinderSolid({x:0,y:0,z:0},'y',0,50,10,32,'c')),polygonArea(32,10)*50,1e-6);
  for(const axis of ['x','z']) close(solidVolume(cylinderSolid({x:0,y:0,z:0},axis,-5,5,3,16,'c')),polygonArea(16,3)*10,1e-6,axis);
});

test('cutting removes exactly the tool volume inside the part',()=>{
  const through=subtract(panel(),cylinderSolid({x:300,y:0,z:200},'y',-1,19,5,32,'hole'));
  close(solidVolume(through),600*18*400-polygonArea(32,5)*18,1e-3,'through hole (tool longer than the panel)');
  const blind=subtract(panel(),boxSolid({x:100,y:12,z:100},{x:200,y:19,z:150},'pocket'));
  close(solidVolume(blind),600*18*400-100*6*50,1e-3,'6 mm pocket from the top');
  let both=subtract(panel(),boxSolid({x:100,y:12,z:100},{x:200,y:19,z:150},'pocket'));
  both=subtract(both,cylinderSolid({x:150,y:0,z:125},'y',-1,19,4,32,'hole'));
  close(solidVolume(both),600*18*400-100*6*50-polygonArea(32,4)*12,1e-3,'a hole through the pocket floor only removes the rest');
  const edge=subtract(panel(),cylinderSolid({x:0,y:9,z:100},'x',-1,20,4,24,'edge'));
  close(solidVolume(edge),600*18*400-polygonArea(24,4)*20,1e-3,'edge hole 20 mm deep');
});

test('cut surfaces carry the tag of the feature that made them',()=>{
  const cut=subtract(panel(),cylinderSolid({x:300,y:0,z:200},'y',6,19,5,32,'hole'));
  const tags=new Set(cut.map(p=>p.shared));
  assert.deepEqual([...tags].sort(),['hole','panel']);
  const walls=cut.filter(p=>p.shared==='hole');
  assert.ok(walls.length>=32,'hole wall and floor');
  assert.ok(walls.every(p=>p.vertices.every(v=>Math.hypot(v.x-300,v.z-200)<=5+1e-6&&v.y>=6-1e-6)),'all inside the hole');
  const floor=walls.filter(p=>Math.abs(p.plane.normal.y-1)<1e-9);
  assert.ok(floor.length>=1&&floor.every(p=>Math.abs(p.vertices[0].y-6)<1e-9),'the hole floor faces up at 6 mm');
});
