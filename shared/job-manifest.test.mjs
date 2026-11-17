import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createJobManifest,verifyJobManifest} from './job-manifest.mjs';
import {validateProject} from './project.mjs';
const base=validateProject({schemaVersion:1,units:'m',name:'Job',room:{width:6,depth:4,height:3}});
const project=validateProject({...base,room:{...base.room,furniture:[{id:'a',name:'Cabinet',type:'cabinet',width:.9,height:1.2,depth:.5,thickness:.018,shelves:1,x:0,z:0,rotation:0}]}});
test('job snapshot hashes are stable, include derived placements and detect stale models',async()=>{
  const job=await createJobManifest(project,'2026-11-06T12:00:00Z');
  assert.match(job.sourceSha256,/^[a-f0-9]{64}$/);assert.equal(job.summary.panels,8);assert.equal(job.validation.status,'review-required');
  assert.deepEqual(await createJobManifest(project,'2026-11-06T12:00:00Z'),job);
  assert.equal((await verifyJobManifest(JSON.stringify(job),project)).matchesCurrent,true);
  assert.equal((await verifyJobManifest(JSON.stringify(job),{...project,name:'Changed'})).matchesCurrent,false);
  assert.notEqual((await createJobManifest({...project,name:'Changed'})).sourceSha256,job.sourceSha256);
});
test('verification rejects altered source, derived layout, metadata and invalid formats',async()=>{
  const job=await createJobManifest(project);
  for(const mutate of [j=>j.source.name='Changed',j=>j.batches[0].layout.sheets[0].panels[0].x++,j=>j.summary.panels++,j=>j.generatedAt='2020-01-01T00:00:00Z',j=>j.validation.status='approved']){
    const changed=structuredClone(job);mutate(changed);await assert.rejects(()=>verifyJobManifest(JSON.stringify(changed),project),/integrity/);
  }
  await assert.rejects(()=>verifyJobManifest('{}',project),/format/);
  await assert.rejects(()=>verifyJobManifest(' '.repeat(2097153),project),/2 MiB/);
});
test('handoff blockers retain oversize panel identities and empty jobs',async()=>{
  assert.equal((await createJobManifest(base)).validation.status,'blocked');
  const oversize={...project,room:{...project.room,furniture:project.room.furniture.map(c=>({...c,stockLengthMm:100,stockWidthMm:100}))}};
  const job=await createJobManifest(oversize);assert.equal(job.summary.unplaced,8);assert.equal(job.validation.status,'blocked');assert.equal(job.batches[0].layout.unplaced[0].cabinetId,'a');
});
