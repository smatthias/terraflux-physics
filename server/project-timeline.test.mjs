import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,rm,readdir,writeFile,readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {makeServer} from './index.mjs';
import {checkStorage} from './storage-check.mjs';
import {createTimeline,appendVersion,stateAt} from '../shared/timeline.mjs';
import {validateProject} from '../shared/project.mjs';

const project={schemaVersion:1,units:'m',name:'Kitchen',room:{width:6,depth:4,height:3}};
async function serve(t) {
  const dataDir=await mkdtemp(join(tmpdir(),'terratec-timeline-'));
  const server=makeServer({dataDir});await new Promise(r=>server.listen(0,'127.0.0.1',r));
  t.after(async()=>{await new Promise(r=>server.close(r));await rm(dataDir,{recursive:true,force:true});});
  const base='http://127.0.0.1:'+server.address().port;
  const send=(path,method,value,type='application/json')=>fetch(base+path,{method,headers:{'content-type':type},body:typeof value==='string'?value:JSON.stringify(value)});
  return {dataDir,base,send};
}
const history=saved=>{
  const first=validateProject(saved),second=validateProject({...first,name:'Kitchen v2'});
  return appendVersion(createTimeline(first,{at:Date.UTC(2024,4,1,8)}),first,second,{at:Date.UTC(2024,4,1,9),label:'Renamed'});
};

test('a saved project keeps its version history beside it',async t=>{
  const {dataDir,base,send}=await serve(t);
  const record=await (await send('/api/projects','POST',project)).json();
  const url='/api/projects/'+record.id+'/timeline';
  assert.equal((await fetch(base+url)).status,404);
  const timeline=history(record.project);
  const saved=await send(url,'PUT',timeline);
  assert.equal(saved.status,200);
  assert.equal((await saved.json()).versions,2);
  const loaded=await (await fetch(base+url)).json();
  assert.deepEqual(loaded,JSON.parse(JSON.stringify(timeline)));
  assert.equal(stateAt(loaded,2).name,'Kitchen v2');
  // The history file does not show up as a project and leaves the project untouched.
  const list=await (await fetch(base+'/api/projects')).json();
  assert.deepEqual(list.map(p=>p.id),[record.id]);
  assert.equal((await (await fetch(base+'/api/projects/'+record.id)).json()).project.name,'Kitchen');
  assert.deepEqual((await readdir(join(dataDir,'projects'))).sort(),[record.id+'.json',record.id+'.timeline.json'].sort());
  const report=await checkStorage(dataDir);
  assert.equal(report.status,'ok');assert.deepEqual(report.counts,{projects:1,jobs:0,customers:0,timelines:1});
});

test('project history requests are checked',async t=>{
  const {dataDir,base,send}=await serve(t);
  const record=await (await send('/api/projects','POST',project)).json();
  const url='/api/projects/'+record.id+'/timeline',timeline=history(record.project);
  assert.equal((await send('/api/projects/00000000-0000-0000-0000-000000000000/timeline','PUT',timeline)).status,404);
  assert.equal((await fetch(base+'/api/projects/not-an-id/timeline')).status,400);
  assert.equal((await send(url,'PUT',{...timeline,format:'other'})).status,400);
  assert.equal((await send(url,'PUT','{not json')).status,400);
  assert.equal((await send(url,'PUT',timeline,'text/plain')).status,415);
  assert.equal((await send(url,'POST',timeline)).status,405);
  const broken=structuredClone(timeline);broken.entries[0].keyframe.room.height=-1;
  const rejected=await send(url,'PUT',broken);
  assert.equal(rejected.status,400);assert.match((await rejected.json()).error,/height/i);
  const tooBig={...timeline,padding:'x'.repeat(3*1024*1024)};
  assert.equal((await send(url,'PUT',tooBig)).status,413);
  // Oversized uploads are answered and their connection closed (the server can still shut down).
  assert.equal((await send('/api/projects/'+record.id,'PUT',{...project,name:'x'.repeat(3*1024*1024)})).status,413);
  assert.equal((await fetch(base+url)).status,404,'nothing was stored by rejected requests');
  assert.deepEqual(await readdir(join(dataDir,'projects')),[record.id+'.json']);
});

test('a damaged history file is reported without blocking its project',async t=>{
  const {dataDir,base,send}=await serve(t);
  const record=await (await send('/api/projects','POST',project)).json();
  const file=join(dataDir,'projects',record.id+'.timeline.json');
  await writeFile(file,'{"format":');
  const response=await fetch(base+'/api/projects/'+record.id+'/timeline');
  assert.equal(response.status,422);
  const error=await response.json();assert.equal(error.code,'TIMELINE_INVALID');assert.match(error.error,/unaffected/);
  assert.equal((await fetch(base+'/api/projects/'+record.id)).status,200);
  assert.equal((await fetch(base+'/api/projects')).status,200);
  const report=await checkStorage(dataDir);
  assert.equal(report.status,'attention');assert.equal(report.counts.timelines,1);
  assert.deepEqual(report.issues.map(i=>[i.code,i.id]),[['TIMELINE_INVALID',record.id]]);
  assert.equal(await readFile(file,'utf8'),'{"format":','the damaged file is kept');
  // Saving a new history replaces it.
  assert.equal((await send('/api/projects/'+record.id+'/timeline','PUT',history(record.project))).status,200);
  assert.equal((await checkStorage(dataDir)).status,'ok');
});
