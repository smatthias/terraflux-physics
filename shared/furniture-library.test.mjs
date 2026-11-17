import {test} from 'node:test';
import assert from 'node:assert/strict';
import {rectangle} from './geometry.mjs';
import {duplicateCabinet,saveFurniturePreset,validateFurniturePresets,instantiateFurniturePreset,previewFurnitureInsertion} from './furniture-library.mjs';
import {validateProject} from './project.mjs';
import {serializeProjectJson,parseProjectJson} from './project-json.mjs';
import {createHistory,commitHistory,undoHistory} from './history.mjs';
const item={id:'a',name:'Storage',type:'cabinet',width:1,depth:.5,height:1.2,thickness:.018,shelves:2,x:-2,z:0,rotation:30,material:'birch'};
const room={width:10,depth:8,height:3,outline:rectangle(10,8),openings:[],furniture:[item]};
test('cabinet copies use new identities, snap positions and reject invalid placement without mutation',()=>{
 const before=JSON.stringify(room),next=duplicateCabinet(room,'a','b',{x:.63,z:.37},.25,true);
 assert.equal(next.furniture[1].id,'b');assert.equal(next.furniture[1].x,.75);assert.equal(next.furniture[1].z,.25);assert.equal(next.furniture[1].rotation,30);assert.equal(next.furniture[1].material,'birch');
 assert.deepEqual(undoHistory(commitHistory(createHistory(room),next)).present,room);
 assert.throws(()=>duplicateCabinet(room,'a','b',{x:-2,z:0},.25),/overlap/);assert.throws(()=>duplicateCabinet(room,'a','b',{x:20,z:0},.25),/inside/);assert.equal(JSON.stringify(room),before);
});
test('furniture presets validate and persist independently of instance identity, position and rotation',()=>{
 const presets=saveFurniturePreset([],{...item,id:'preset',name:'Birch unit'});
 assert.equal(presets[0].x,undefined);assert.equal(presets[0].rotation,undefined);assert.equal(presets[0].width,1);
 const next=instantiateFurniturePreset(room,presets[0],'new',{x:1,z:0},.25);assert.equal(next.furniture[1].rotation,0);assert.equal(next.furniture[1].id,'new');assert.equal(next.furniture[1].material,'birch');
 assert.equal(saveFurniturePreset(presets,{...item,id:'ignored',name:'BIRCH UNIT',width:1.4})[0].id,'preset');
 assert.throws(()=>validateFurniturePresets([{...presets[0],thickness:1}]));assert.throws(()=>validateFurniturePresets([presets[0],presets[0]]),/unique/);
 const project={schemaVersion:6,units:'m',name:'Project',presets:[],room,furniturePresets:presets};assert.deepEqual(parseProjectJson(serializeProjectJson(project)).furniturePresets,presets);
 assert.equal(Object.hasOwn(validateProject({schemaVersion:6,units:'m',name:'Legacy',presets:[],room}),'furniturePresets'),false,'legacy manifest snapshot shape remains unchanged');
});

test('insertion preview is mutation-free, retains invalid candidates and agrees with committed placement',()=>{
 const before=JSON.stringify(room),request={kind:'copy',sourceId:'a',id:'b',point:{x:.63,z:.37}};
 const good=previewFurnitureInsertion(room,request,.25);assert.equal(good.valid,true);assert.equal(good.candidate.x,.75);
 assert.deepEqual(good.candidate,duplicateCabinet(room,'a','b',request.point,.25).furniture.at(-1));
 const bad=previewFurnitureInsertion(room,{...request,point:{x:-2,z:0}},.25);assert.equal(bad.valid,false);assert.match(bad.error,/overlap/);assert.equal(bad.candidate.x,-2);
 assert.equal(previewFurnitureInsertion(room,{...request,point:{x:NaN,z:0}},.25).candidate,null);assert.equal(JSON.stringify(room),before);
});
