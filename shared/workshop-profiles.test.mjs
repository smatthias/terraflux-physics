import {test} from 'node:test';
import assert from 'node:assert/strict';
import {saveWorkshopProfile,validateWorkshopProfiles,applyWorkshopProfile,exportWorkshopProfiles,importWorkshopProfiles,applyWorkshopProfileToCabinets} from './workshop-profiles.mjs';
import {validateProject} from './project.mjs';
import {serializeProjectJson,parseProjectJson} from './project-json.mjs';
import {duplicateProject} from './duplicate-project.mjs';
import {createHistory,commitHistory,undoHistory} from './history.mjs';
const profile={id:'shop',name:'Workshop',stockLengthMm:2800,stockWidthMm:1400,stockTrimMm:10,stockKerfMm:4,stockAllowRotation:false};
test('workshop profile validation, name-based replacement and limits',()=>{
  assert.equal(saveWorkshopProfile([profile],{...profile,id:'new',name:' WORKSHOP ',stockKerfMm:5})[0].id,'shop');
  for(const p of [{...profile,name:''},{...profile,stockKerfMm:-1},{...profile,id:'bad id'}])assert.throws(()=>validateWorkshopProfiles([p]));
  assert.throws(()=>validateWorkshopProfiles([profile,profile]),/unique/);
  assert.throws(()=>validateWorkshopProfiles(Array(33).fill(profile)),/32/);
});
test('profile application preserves cabinet geometry, is mutation-free and supports one undo',()=>{
  const item={id:'cab',name:'A',type:'cabinet',width:.9,height:1.2,depth:.5,thickness:.018,shelves:1,x:0,z:0,rotation:0};
  const base=validateProject({schemaVersion:1,units:'m',name:'Project',room:{width:6,depth:4,height:3}});
  assert.deepEqual(base.workshopProfiles,[]);
  const project=validateProject({...base,workshopProfiles:[profile],room:{...base.room,furniture:[item]}}),before=JSON.stringify(project);
  const room=applyWorkshopProfile(project.room,'cab',profile);
  assert.equal(room.furniture[0].stockKerfMm,4);assert.equal(room.furniture[0].width,.9);assert.equal(room.furniture[0].material,'oak');
  assert.equal(JSON.stringify(project),before);
  assert.deepEqual(undoHistory(commitHistory(createHistory(project.room),room)).present,project.room);
  assert.throws(()=>applyWorkshopProfile(project.room,'missing',profile),/Select/);
  assert.deepEqual(parseProjectJson(serializeProjectJson(project)).workshopProfiles,[profile]);
  assert.deepEqual(duplicateProject(project).workshopProfiles,[profile]);
});

test('portable profile import preserves existing entries, remaps IDs and renames name collisions atomically',()=>{
  const text=exportWorkshopProfiles([profile]);let n=0;
  const merged=importWorkshopProfiles([profile],text,()=> 'import-'+(++n));
  assert.equal(merged[0].id,'shop');assert.equal(merged[1].id,'import-1');assert.equal(merged[1].name,'Workshop (import 1)');
  assert.equal(merged[1].stockKerfMm,4);
  assert.throws(()=>importWorkshopProfiles([profile],text,()=>profile.id),/unique/);
  assert.throws(()=>importWorkshopProfiles([],JSON.stringify({format:'wrong',version:1,profiles:[]})),/format/);
  assert.throws(()=>importWorkshopProfiles([],' '.repeat(32769)),/32 KiB/);
  assert.throws(()=>importWorkshopProfiles([],exportWorkshopProfiles([profile]).replace('"stockKerfMm": 4','"stockKerfMm": -1')));
  assert.equal(profile.name,'Workshop');
});
test('bulk application changes only chosen cabinets and can be undone in one step',()=>{
  const room={furniture:[{id:'a',stockKerfMm:3,width:1},{id:'b',stockKerfMm:5,width:2},{id:'c',stockKerfMm:7,width:3}]};
  const next=applyWorkshopProfileToCabinets(room,['a','b'],profile);
  assert.deepEqual(next.furniture.map(c=>c.stockKerfMm),[4,4,7]);assert.deepEqual(next.furniture.map(c=>c.width),[1,2,3]);
  assert.deepEqual(room.furniture.map(c=>c.stockKerfMm),[3,5,7]);
  assert.deepEqual(undoHistory(commitHistory(createHistory(room),next)).present,room);
  for(const ids of [[],['a','missing'],['a','a']])assert.throws(()=>applyWorkshopProfileToCabinets(room,ids,profile),/valid/);
});
