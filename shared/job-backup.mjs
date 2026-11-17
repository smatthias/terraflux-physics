import {validateQuoteLifecycle} from './quote-lifecycle.mjs';
import {quoteCosting,validateQuoteDetails} from './quote.mjs';
import {verifyJobManifest} from './job-manifest.mjs';
export const MAX_JOB_BACKUP_BYTES=16777216;
const uuid=value=>typeof value==='string'&&/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(value);
const text=(value,max)=>typeof value==='string'&&value.trim().length>0&&value.length<=max;
const date=value=>typeof value==='string'&&value.length<=40&&Number.isFinite(Date.parse(value));
function canonical(value){if(Array.isArray(value))return value.map(canonical);if(value&&typeof value==='object')return Object.fromEntries(Object.keys(value).sort().map(k=>[k,canonical(value[k])]));return value;}
async function hash(value){const bytes=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(JSON.stringify(canonical(value))));return [...new Uint8Array(bytes)].map(b=>b.toString(16).padStart(2,'0')).join('');}
export async function exportJobBackup(record){
  const {id,name,createdAt,updatedAt,revisions}=record;
  const payload={format:'terratec-job-backup',version:1,record:{id,name,createdAt,updatedAt,revisions}};
  const output=JSON.stringify({...payload,backupSha256:await hash(payload)},null,2)+'\n';
  if(new TextEncoder().encode(output).length>MAX_JOB_BACKUP_BYTES)throw Error('Job backup exceeds 16 MiB');
  return output;
}
export async function validateJobBackup(input){
  if(!input||input.format!=='terratec-job-backup'||input.version!==1)throw Error('Unsupported job backup format');
  const {backupSha256,...payload}=input;
  if(typeof backupSha256!=='string'||backupSha256!==await hash(payload))throw Error('Job backup checksum failed');
  const r=input.record;
  if(!r||!uuid(r.id)||!text(r.name,80)||!date(r.createdAt)||!date(r.updatedAt)||Date.parse(r.createdAt)>Date.parse(r.updatedAt)||!Array.isArray(r.revisions)||!r.revisions.length||r.revisions.length>20)throw Error('Invalid job backup record');
  let previous=0,previousHash=null;
  for(const [index,revision] of r.revisions.entries()){
    if(revision.number!==index+1||!['planned','reviewed'].includes(revision.status))throw Error('Invalid job revision sequence');
    await verifyJobManifest(JSON.stringify(revision.manifest),revision.manifest?.source);
    const snapshotTime=Date.parse(revision.manifest.generatedAt);
    if(snapshotTime<previous||revision.manifest.sourceSha256===previousHash)throw Error('Invalid job revision history');
    previous=snapshotTime;previousHash=revision.manifest.sourceSha256;
    if(revision.status==='reviewed'){
      const review=revision.review;
      if(revision.manifest.validation.status==='blocked'||!review||!text(review.reviewer,80)||!text(review.note,1000)||!date(review.reviewedAt)||Date.parse(review.reviewedAt)<previous)throw Error('Invalid job review history');
      previous=Date.parse(review.reviewedAt);
    }else if(revision.review!=null||revision.fulfilment!=null)throw Error('Planned revision cannot have review or tracking');
    const quotes=revision.quotes??[];
    if(!Array.isArray(quotes)||quotes.length>20||new Set(quotes.map(q=>q.id)).size!==quotes.length)throw Error('Invalid quote history');
    for(const q of quotes){
      validateQuoteDetails(q.details);validateQuoteLifecycle(q,r.updatedAt);
      if(!uuid(q.id)||!date(q.createdAt)||Date.parse(q.createdAt)<snapshotTime||Date.parse(q.createdAt)>Date.parse(r.updatedAt)||JSON.stringify(canonical(q.costing))!==JSON.stringify(canonical(quoteCosting(revision.manifest,revision.number,q.costing?.pricing))))throw Error('Invalid quote snapshot');
    }
    const f=revision.fulfilment;
    if(f!=null){
      if(!text(f.workshop,80)||typeof f.dueDate!=='string'||(f.dueDate&&(!/^\d{4}-\d{2}-\d{2}$/.test(f.dueDate)||!date(f.dueDate)||new Date(f.dueDate).toISOString().slice(0,10)!==f.dueDate))||!Array.isArray(f.events)||!f.events.length||f.events.length>3)throw Error('Invalid workshop assignment');
      let state=null;
      for(const [i,event] of f.events.entries()){
        const allowed=state===null?['assigned']:state==='assigned'?['in-production','cancelled']:state==='in-production'?['completed','cancelled']:[];
        if(event.number!==i+1||!allowed.includes(event.status)||!text(event.actor,80)||!text(event.note,1000)||!date(event.at)||Date.parse(event.at)<previous)throw Error('Invalid workshop tracking history');
        state=event.status;previous=Date.parse(event.at);
      }
      if(f.status!==state||(index<r.revisions.length-1&&['assigned','in-production'].includes(state)))throw Error('Invalid workshop tracking state');
    }
  }
  if(Date.parse(r.updatedAt)<previous)throw Error('Invalid job update time');
  return structuredClone(r);
}
