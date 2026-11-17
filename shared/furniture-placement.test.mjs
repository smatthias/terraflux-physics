import {test} from 'node:test';
import assert from 'node:assert/strict';
import {rectangle} from './geometry.mjs';
import {evaluateFurniturePlacement,placementGuides,placementRotation} from './furniture-placement.mjs';
const item={id:'source',name:'Source',type:'cabinet',width:1,depth:.4,height:1.2,thickness:.018,shelves:1,x:0,z:0,rotation:0};
const room={height:3,outline:rectangle(10,8),furniture:[item]};
test('placement rotation wraps and revalidates candidates without changing the source',()=>{
 assert.equal(placementRotation(-15),345);assert.equal(placementRotation(375),15);assert.throws(()=>placementRotation(NaN));
 const request={kind:'copy',sourceId:'source',id:'new',point:{x:2,z:0},rotation:90};
 const result=evaluateFurniturePlacement(room,request,.25);assert.equal(result.valid,true);assert.equal(result.candidate.rotation,90);assert.equal(item.rotation,0);
 const edge=evaluateFurniturePlacement(room,{...request,point:{x:4.8,z:0},rotation:0},.1);assert.equal(edge.valid,false);
});
test('guides are passive by default, optional snap is exact and collisions remain invalid',()=>{
 const request={kind:'copy',sourceId:'source',id:'new',point:{x:.04,z:2}};
 const passive=evaluateFurniturePlacement(room,request,.01,false);assert.equal(passive.candidate.x,.04);assert.ok(passive.guides.some(g=>g.axis==='x'));
 const snapped=evaluateFurniturePlacement(room,request,.01,false,true);assert.ok(Math.abs(snapped.candidate.x)<1e-10);assert.equal(snapped.valid,true);
 const blocked=evaluateFurniturePlacement(room,{...request,point:{x:.04,z:.04}},.01,false,true);assert.equal(blocked.valid,false);assert.match(blocked.error,/overlap/);
 assert.equal(placementGuides(null,room.furniture).length,0);
 assert.deepEqual(placementGuides(passive.candidate,room.furniture),placementGuides(passive.candidate,[...room.furniture].reverse()));
});
