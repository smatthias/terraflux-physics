import assert from 'node:assert/strict';
import {mkdtemp,rm,readFile,cp,mkdir,writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {resolve,join} from 'node:path';
import {execFileSync,spawn} from 'node:child_process';
import {once} from 'node:events';
import {createServer} from 'node:net';

const temporary=await mkdtemp(join(tmpdir(),'terratec-install-'));
const name=`terratec-${process.platform}-${process.arch}`;
const archive=resolve('release',name+'.tar.gz');
const dataDir=join(temporary,'saved-data');
const backupDir=join(temporary,'offline-backup');
const compatibility=JSON.parse(await readFile(new URL('../fixtures/project-compatibility.json',import.meta.url),'utf8')).cases;
const frozenRecords=compatibility.map(fixture=>({fixture,bytes:JSON.stringify({id:fixture.id,updatedAt:'2026-01-01T00:00:00.000Z',project:fixture.project},null,2)+'\n'}));
let child,base,currentStorage;
const phases=[];
async function stop(){
  if(!child || !child.pid || child.exitCode!==null || child.signalCode!==null)return;
  const exited=once(child,'exit');
  const timeout=setTimeout(()=>child.kill('SIGKILL'),5000);
  child.kill();
  try{await exited;}finally{clearTimeout(timeout);}
}
async function start(installation,storage){
  // Select an available local port rather than assuming a fixed port is unused.
  const probe=createServer();await new Promise(r=>probe.listen(0,'127.0.0.1',r));
  const port=probe.address().port;await new Promise(r=>probe.close(r));
  currentStorage=storage;
  base=`http://127.0.0.1:${port}`;
  child=spawn(process.execPath,[join(installation,'start.mjs')],{cwd:tmpdir(),env:{...process.env,PORT:String(port),TERRATEC_DATA_DIR:storage},stdio:['ignore','pipe','pipe']});
  let log='',spawnError;child.on('error',error=>{spawnError=error;});
  child.stdout.on('data',chunk=>log+=chunk);child.stderr.on('data',chunk=>log+=chunk);
  let ready=false;
  for(let i=0;i<100;i++){
    if(spawnError)throw spawnError;
    if(child.exitCode!==null)throw new Error(log);
    try{ready=log.includes('TerraTec:')&&(await fetch(base+'/api/health')).ok;}catch{}
    if(ready)break;await new Promise(r=>setTimeout(r,50));
  }
  assert.ok(ready,log);
}
async function json(path,body,method='POST',expected=200){
  const response=await fetch(base+path,body===undefined?{}:{method,headers:{'content-type':'application/json'},body:JSON.stringify(body)});
  assert.equal(response.status,expected,await response.clone().text());return response.json();
}
function extract(destination){execFileSync('tar',['-xzf',archive,'-C',destination]);return join(destination,name);}
const fixture={schemaVersion:1,units:'m',name:'Installed studio',room:{width:6,depth:4,height:2.8}};
let records;
async function verify(phase){
  const html=await (await fetch(base)).text();assert.match(html,/<div id="app">/);
  for(const asset of [...html.matchAll(/(?:src|href)="(\/assets\/[^" ]+)"/g)])assert.equal((await fetch(base+asset[1])).status,200);
  assert.equal((await fetch(base+'/missing.js')).status,404);
  assert.equal((await fetch(base+'/server/index.mjs')).status,404);
  assert.equal((await json('/api/geometry',fixture)).area,24);
  for(const [collection,record] of Object.entries(records)){
    assert.deepEqual(await json(`/api/${collection}/${record.id}`),record,`${phase}: ${collection} changed`);
    assert.ok((await json('/api/'+collection)).some(item=>item.id===record.id),`${phase}: missing list entry`);
  }
  for(const {fixture,bytes} of frozenRecords){
    assert.deepEqual(await json('/api/projects/'+fixture.id),{id:fixture.id,updatedAt:'2026-01-01T00:00:00.000Z',project:fixture.expected});
    assert.equal(await readFile(join(currentStorage,'projects',fixture.id+'.json'),'utf8'),bytes,`${phase}: legacy source file rewritten`);
  }
  assert.deepEqual(await json('/api/project'),compatibility[0].expected);
  assert.equal(await readFile(join(currentStorage,'project.json'),'utf8'),JSON.stringify(compatibility[0].project)+'\n');
  const storage=await json('/api/storage/check',{});assert.equal(storage.status,'ok');assert.equal(storage.writable,true);assert.equal(storage.counts.projects,compatibility.length+1);
  phases.push(phase);console.log('Extracted release passed: '+phase);
}
try{
  const installation=extract(temporary);
  await mkdir(join(dataDir,'projects'),{recursive:true});
  for(const {fixture,bytes} of frozenRecords)await writeFile(join(dataDir,'projects',fixture.id+'.json'),bytes);
  await writeFile(join(dataDir,'project.json'),JSON.stringify(compatibility[0].project)+'\n');
  await start(installation,dataDir);
  const project=await json('/api/projects',fixture,'POST',201);
  const customer=await json('/api/customers',{name:'Recovery customer',email:'recovery@example.test',notes:'Keep across upgrades'},'POST',201);
  const cabinet={id:'recovery-cabinet',name:'Cabinet',type:'cabinet',width:.9,height:1.2,depth:.5,thickness:.018,shelves:1,x:0,z:0,rotation:0};
  const source={...project.project,room:{...project.project.room,furniture:[cabinet]}};
  const job=await json('/api/jobs',{name:'Recovery workshop job',project:source},'POST',201);
  await json(`/api/jobs/${job.id}/review`,{revision:1,expectedRevision:1,reviewer:'Install smoke',note:'Verified before backup'});
  records={projects:project,customers:customer,jobs:await json('/api/jobs/'+job.id)};
  assert.ok(records.jobs.revisions[0].manifest.summary.panels>0);
  await verify('fresh installation');
  await stop();
  await start(installation,dataDir);
  await verify('process restart');
  records.customers=await json('/api/customers/'+customer.id,{...customer,notes:'Updated after restart',expectedVersion:1},'PUT');
  assert.equal(records.customers.version,2);
  await json('/api/customers/'+customer.id,{...customer,expectedVersion:1},'PUT',409);
  await stop();
  // Copy only while stopped so the backup contains one consistent filesystem state.
  await cp(dataDir,backupDir,{recursive:true});
  // Reinstall the current archive into a new directory, remove the old application,
  // then reuse external data. This is installation replacement, not schema migration.
  const replacementRoot=join(temporary,'replacement');await mkdir(replacementRoot);
  const replacement=extract(replacementRoot);await rm(installation,{recursive:true});
  await start(replacement,dataDir);
  await verify('replacement installation with external data');
  await stop();
  // Simulate loss of the working data directory; recover into a separate location.
  await rm(dataDir,{recursive:true});
  const restored=join(temporary,'restored-data');await cp(backupDir,restored,{recursive:true});
  await start(replacement,restored);
  await verify('offline backup recovery');
  const offlineReport=JSON.parse(execFileSync(process.execPath,[join(replacement,'server/storage-check.mjs')],{cwd:tmpdir(),env:{...process.env,TERRATEC_DATA_DIR:restored},encoding:'utf8'}));assert.equal(offlineReport.status,'ok');
  for(const [collection,record] of Object.entries(records))assert.deepEqual(JSON.parse(await readFile(join(restored,collection,record.id+'.json'),'utf8')),record);
  // Reading is non-destructive; only an explicit save upgrades the stored project.
  const oldest=compatibility[0];
  const upgraded=await json('/api/projects/'+oldest.id,oldest.expected,'PUT');
  assert.equal(upgraded.id,oldest.id);
  assert.deepEqual(upgraded.project,oldest.expected);
  const upgradedBytes=await readFile(join(restored,'projects',oldest.id+'.json'),'utf8');
  assert.equal(JSON.parse(upgradedBytes).project.schemaVersion,6);
  await json('/api/projects/'+oldest.id,{...oldest.expected,schemaVersion:8},'PUT',400);
  assert.equal(await readFile(join(restored,'projects',oldest.id+'.json'),'utf8'),upgradedBytes);
  assert.equal(await readFile(join(backupDir,'projects',oldest.id+'.json'),'utf8'),frozenRecords[0].bytes);
  await mkdir('test-results' ,{recursive:true});
  await writeFile('test-results/package-recovery.json',JSON.stringify({platform:process.platform,architecture:process.arch,phases,collections:Object.keys(records),projectSchemaVersions:compatibility.map(f=>f.project.schemaVersion),explicitSaveUpgrade:true,scope:'Synthetic project schema 1–6 compatibility, non-destructive reads, explicit schema-6 save and current-archive recovery. Historical release binaries are not tested.'},null,2)+'\n');
}finally{await stop();await rm(temporary,{recursive:true,force:true});}
