import {test} from 'node:test';
import assert from 'node:assert/strict';
import {jobSummary,filterJobs,jobRollup} from './job-register.mjs';
const revision={number:2,status:'planned',manifest:{summary:{panels:8,sheets:2,unplaced:0},validation:{status:'review-required',issues:[]}}};
const a={id:'a',name:'Kitchen',updatedAt:'2026-11-06T10:00:00Z',revisions:[{...revision,status:'reviewed',fulfilment:{workshop:'Old shop',status:'completed',dueDate:'2026-11-01'}},revision]};
test('register summaries use latest revisions rather than prior fulfilment history',()=>{
  const summary=jobSummary(a);assert.equal(summary.workflowState,'planned');assert.equal(summary.workshop,'');assert.equal(summary.revision,2);
  const active=jobSummary({...a,revisions:[{...revision,status:'reviewed',fulfilment:{workshop:'Berlin',status:'assigned',dueDate:'2026-12-01'}}]});assert.equal(active.workflowState,'assigned');assert.equal(active.workshop,'Berlin');
  const blocked=jobSummary({...a,revisions:[{...revision,manifest:{...revision.manifest,validation:{status:'blocked',issues:[{severity:'blocking'}]}}}]});assert.equal(blocked.workflowState,'blocked');assert.equal(blocked.blockingIssues,1);
});
test('search combines terms/status, sorts predictably and keeps inputs unchanged',()=>{
  const jobs=[jobSummary(a),{...jobSummary(a),id:'b',name:'Berlin kitchen',workshop:'Wood studio',workflowState:'completed',dueDate:'2026-12-01',updatedAt:'2026-11-06T12:00:00Z'}],before=JSON.stringify(jobs);
  assert.deepEqual(filterJobs(jobs,{query:'  BERLIN studio ',status:'completed'}).map(j=>j.id),['b']);
  assert.equal(filterJobs(jobs,{query:'missing'}).length,0);assert.equal(filterJobs(jobs,{status:'assigned'}).length,0);
  assert.deepEqual(filterJobs(jobs,{sort:'due'}).map(j=>j.id),['b','a']);
  assert.deepEqual(filterJobs(jobs,{sort:'name'}).map(j=>j.id),['b','a']);
  assert.deepEqual(filterJobs(jobs).map(j=>j.id),['b','a']);
  assert.deepEqual(jobRollup(jobs),{total:2,planned:1,completed:1});assert.equal(JSON.stringify(jobs),before);
});
