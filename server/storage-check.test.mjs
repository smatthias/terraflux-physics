import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,readdir,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {checkStorage} from './storage-check.mjs';
import {makeServer} from './index.mjs';
const id='00000000-0000-4000-8000-000000000001';
const project={schemaVersion:1,units:'m',name:'Protected source',room:{width:6,depth:4,height:2.8}};
async function setup(t){const dir=await mkdtemp(join(tmpdir(),'terratec-storage-'));t.after(()=>rm(dir,{recursive:true,force:true}));return dir;}
test('storage probe verifies actual writes and cleans up without changing saved records',async t=>{
 const dir=await setup(t);await mkdir(join(dir,'projects'));const bytes=JSON.stringify({id,project});await writeFile(join(dir,'projects',id+'.json'),bytes);
 const report=await checkStorage(dir);assert.equal(report.status,'ok');assert.equal(report.writable,true);assert.equal(report.counts.projects,1);
 assert.equal(await readFile(join(dir,'projects',id+'.json'),'utf8'),bytes);assert.deepEqual(await readdir(dir),['projects']);
});
test('storage path occupied by a file yields actionable diagnostics and preserves file',async t=>{
 const dir=await setup(t),path=join(dir,'occupied');await writeFile(path,'keep');const report=await checkStorage(path);
 assert.equal(report.status,'attention');assert.equal(report.writable,false);assert.ok(report.issues.some(i=>i.message.includes('directory')));assert.equal(await readFile(path,'utf8'),'keep');
});
test('unreadable project formats surface structured API feedback and block replacement saves',async t=>{
 const dir=await setup(t);await mkdir(join(dir,'projects'));const file=join(dir,'projects',id+'.json');const server=makeServer({dataDir:dir});await new Promise(r=>server.listen(0,'127.0.0.1',r));t.after(()=>new Promise(r=>server.close(r)));const url='http://127.0.0.1:'+server.address().port;
 for(const [bytes,code] of [[JSON.stringify({id,project:{...project,schemaVersion:99}}),'PROJECT_UNSUPPORTED'],['{broken','PROJECT_CORRUPT']]){
  await writeFile(file,bytes);
  const response=await fetch(url+'/api/projects/'+id);assert.equal(response.status,422);const error=await response.json();assert.equal(error.code,code);assert.match(error.error,/backup/);
  const rejected=await fetch(url+'/api/projects/'+id,{method:'PUT',headers:{'content-type':'application/json'},body:JSON.stringify(project)});assert.equal(rejected.status,422);
  const diagnostics=await (await fetch(url+'/api/storage/check',{method:'POST'})).json();assert.equal(diagnostics.status,'attention');assert.ok(diagnostics.issues.some(i=>i.id===id));
  assert.equal(await readFile(file,'utf8'),bytes);
 }
});
test('legacy project format errors use the same actionable response without rewriting data',async t=>{
 const dir=await setup(t),bytes=JSON.stringify({...project,schemaVersion:99});await writeFile(join(dir,'project.json'),bytes);
 const server=makeServer({dataDir:dir});await new Promise(r=>server.listen(0,'127.0.0.1',r));t.after(()=>new Promise(r=>server.close(r)));
 const response=await fetch('http://127.0.0.1:'+server.address().port+'/api/project');assert.equal(response.status,422);assert.equal((await response.json()).code,'PROJECT_UNSUPPORTED');assert.equal(await readFile(join(dir,'project.json'),'utf8'),bytes);
});
