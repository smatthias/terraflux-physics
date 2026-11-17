import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validateProject} from './project.mjs';
import {parseProjectJson,serializeProjectJson} from './project-json.mjs';
import {RECOVERY_KEY,readDrafts,storeDraft,removeDraft} from './recovery.mjs';
const project=validateProject({schemaVersion:1,units:'m',name:'Studio',room:{width:6,depth:4,height:3}});
const storage=()=>{const map=new Map();return {getItem:key=>map.get(key)??null,setItem:(key,value)=>map.set(key,value),removeItem:key=>map.delete(key)};};
test('JSON roundtrip migrates old formats, strips external IDs and rejects invalid or oversized files',()=>{
  const text=serializeProjectJson({...project,id:'external-server-id'});
  assert.deepEqual(parseProjectJson(text),project);assert.equal(text.includes('external-server-id'),false);
  assert.equal(parseProjectJson(JSON.stringify({schemaVersion:1,units:'m',name:'Old',room:{width:6,depth:4,height:3}})).schemaVersion,6);
  assert.throws(()=>parseProjectJson('{bad'));
  assert.throws(()=>parseProjectJson(JSON.stringify({...project,units:'ft'})));
  assert.throws(()=>parseProjectJson(' '.repeat(65537)));
});
test('recovery isolates drafts, replaces snapshots, retains the last valid data, and clears individual entries',()=>{
  const db=storage();storeDraft(db,'draft-one',project);storeDraft(db,'draft-two',{...project,name:'Second'});
  storeDraft(db,'draft-one',{...project,name:'Edited'});
  let drafts=readDrafts(db.getItem(RECOVERY_KEY));assert.equal(drafts.length,2);
  assert.equal(drafts.find(d=>d.key==='draft-one').project.name,'Edited');
  const before=db.getItem(RECOVERY_KEY);assert.throws(()=>storeDraft(db,'draft-one',{...project,units:'ft'}));
  assert.equal(db.getItem(RECOVERY_KEY),before);
  drafts=removeDraft(db,'draft-one');assert.equal(drafts.length,1);assert.equal(drafts[0].key,'draft-two');
  removeDraft(db,'draft-two');assert.equal(db.getItem(RECOVERY_KEY),null);
  assert.throws(()=>readDrafts('{broken'));
});
test('recovery is bounded and propagates storage failures without claiming a save',()=>{
  const db=storage();for(let n=0;n<25;n++)storeDraft(db,'draft-'+n,project);
  assert.equal(readDrafts(db.getItem(RECOVERY_KEY)).length,20);
  assert.throws(()=>storeDraft({getItem:()=>null,setItem:()=>{throw Error('quota');}},'draft',project),/quota/);
});
