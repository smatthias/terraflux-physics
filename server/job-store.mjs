import {quoteRegister} from '../shared/quote-register.mjs';
import {nextQuoteLifecycle} from '../shared/quote-lifecycle.mjs';
import {readFile,writeFile,mkdir,rename,rm,readdir} from 'node:fs/promises';
import {join} from 'node:path';
import {randomUUID} from 'node:crypto';
import {validProjectId} from './project-store.mjs';
import {quoteCosting,validateQuoteDetails} from '../shared/quote.mjs';
import {jobSummary} from '../shared/job-register.mjs';
import {validateJobBackup} from '../shared/job-backup.mjs';
import {createJobManifest} from '../shared/job-manifest.mjs';
const fail=(message,status=400)=>{throw Object.assign(Error(message),{status});};
export function jobStore(dataDir){
  const directory=join(dataDir,'jobs');let queue=Promise.resolve();
  const serial=fn=>{const next=queue.then(fn);queue=next.catch(()=>{});return next;};
  const read=async id=>{
    if(!validProjectId(id))fail('Invalid job ID');
    try{return JSON.parse(await readFile(join(directory,id+'.json'),'utf8'));}
    catch(e){if(e.code==='ENOENT')fail('Workshop job not found',404);throw e;}
  };
  const list=async()=>{
    let files;try{files=await readdir(directory);}catch(e){if(e.code==='ENOENT')return [];throw e;}
    const records=await Promise.all(files.filter(f=>f.endsWith('.json')&&validProjectId(f.slice(0,-5))).map(f=>read(f.slice(0,-5))));
    return records.map(jobSummary).sort((a,b)=>b.updatedAt.localeCompare(a.updatedAt)||a.id.localeCompare(b.id));
  };
  const write=async record=>{
    await mkdir(directory,{recursive:true});const temporary=join(directory,randomUUID()+'.tmp');
    try{await writeFile(temporary,JSON.stringify(record)+'\n');await rename(temporary,join(directory,record.id+'.json'));}finally{await rm(temporary,{force:true});}
    return record;
  };
  const manifest=async project=>{try{return await createJobManifest(project);}catch(e){fail(e.message);}};
  const expected=(record,value)=>{if(!Number.isInteger(value)||value!==record.revisions.length)fail('Job changed; reload before editing',409);};
  return {read,list,quotes:async()=>quoteRegister(await Promise.all((await list()).map(j=>read(j.id)))),
    restore:input=>serial(async()=>{
      let imported;try{imported=await validateJobBackup(input);}catch(e){fail(e.message);}
      const existing=await list();if(existing.length>=50)fail('Maximum 50 local workshop jobs');
      const originalId=imported.id,base=imported.name;let name=base,n=1;
      while(existing.some(r=>r.name.toLowerCase()===name.toLowerCase())){const suffix=` (restore ${n++})`;name=base.slice(0,80-suffix.length)+suffix;}
      const now=new Date().toISOString();
      return write({...imported,id:randomUUID(),name,restoredFrom:{id:originalId,backupSha256:input.backupSha256,restoredAt:now}});
    }),
    create:input=>serial(async()=>{
      if(typeof input?.name!=='string'||!input.name.trim()||input.name.trim().length>80)fail('Job name must contain 1–80 characters');
      if((await list()).length>=50)fail('Maximum 50 local workshop jobs');
      const snapshot=await manifest(input.project),now=new Date().toISOString();
      return write({id:randomUUID(),name:input.name.trim(),createdAt:now,updatedAt:now,revisions:[{number:1,status:'planned',manifest:snapshot,review:null}]});
    }),
    revise:(id,input)=>serial(async()=>{
      const record=await read(id);expected(record,input?.expectedRevision);
      if(['assigned','in-production'].includes(record.revisions.at(-1).fulfilment?.status))fail('Close the active workshop assignment before creating a revision',409);
      if(record.revisions.length>=20)fail('Maximum 20 revisions per workshop job');
      const snapshot=await manifest(input.project);
      if(snapshot.sourceSha256===record.revisions.at(-1).manifest.sourceSha256)fail('Current project already matches the latest revision',409);
      record.revisions.push({number:record.revisions.length+1,status:'planned',manifest:snapshot,review:null});record.updatedAt=new Date().toISOString();return write(record);
    }),
    quoteStatus:(id,input)=>serial(async()=>{
      const record=await read(id);expected(record,input?.expectedRevision);
      const revision=record.revisions.find(r=>r.number===input.revision),quote=revision?.quotes?.find(q=>q.id===input.quoteId);
      if(!quote)fail('Quote snapshot not found',404);
      const now=new Date().toISOString();try{quote.lifecycle=nextQuoteLifecycle(quote,input,now);}catch(e){fail(e.message,e.message.includes('changed')?409:400);}
      record.updatedAt=now;return write(record);
    }),
    quote:(id,input)=>serial(async()=>{
      const record=await read(id);expected(record,input?.expectedRevision);
      const revision=record.revisions.find(r=>r.number===input.revision);
      if(!revision)fail('Job revision not found');
      const quotes=revision.quotes??[];
      if(input.expectedQuoteCount!==quotes.length)fail('Quotes changed; reload before editing',409);
      if(quotes.length>=20)fail('Maximum 20 quote snapshots per revision');
      let costing,details;try{costing=quoteCosting(revision.manifest,revision.number,input.pricing);details=validateQuoteDetails(input.details);}catch(e){fail(e.message);}
      const now=new Date().toISOString();revision.quotes=[...quotes,{id:randomUUID(),createdAt:now,costing,details}];record.updatedAt=now;return write(record);
    }),
    fulfil:(id,input)=>serial(async()=>{
      const record=await read(id);expected(record,input?.expectedRevision);
      const revision=record.revisions.find(r=>r.number===input.revision);
      if(!revision||revision.number!==record.revisions.length||revision.status!=='reviewed')fail('Only the latest reviewed revision can be assigned or tracked',409);
      const events=revision.fulfilment?.events??[];
      if(!Number.isInteger(input.expectedEvent)||input.expectedEvent!==events.length)fail('Workshop tracking changed; reload before editing',409);
      if(typeof input.actor!=='string'||!input.actor.trim()||input.actor.trim().length>80||typeof input.note!=='string'||!input.note.trim()||input.note.trim().length>1000)fail('Tracking needs an actor (1–80) and notes (1–1000 characters)');
      if(!revision.fulfilment){
        if(input.status!=='assigned'||typeof input.workshop!=='string'||!input.workshop.trim()||input.workshop.trim().length>80)fail('Assignment needs a workshop name');
        const dueDate=input.dueDate??'';
        if(typeof dueDate!=='string'||(dueDate&&(!/^\d{4}-\d{2}-\d{2}$/.test(dueDate)||!Number.isFinite(Date.parse(dueDate))||new Date(dueDate).toISOString().slice(0,10)!==dueDate)))fail('Invalid workshop due date');
        revision.fulfilment={workshop:input.workshop.trim(),dueDate,status:'assigned',events:[]};
      }else{
        const allowed={'assigned':['in-production','cancelled'],'in-production':['completed','cancelled']};
        if(!allowed[revision.fulfilment.status]?.includes(input.status))fail('Invalid workshop progress transition',409);
      }
      const now=new Date().toISOString();revision.fulfilment.status=input.status;
      revision.fulfilment.events.push({number:events.length+1,status:input.status,at:now,actor:input.actor.trim(),note:input.note.trim()});
      record.updatedAt=now;return write(record);
    }),
    review:(id,input)=>serial(async()=>{
      const record=await read(id);expected(record,input?.expectedRevision);
      const revision=record.revisions.at(-1);
      if(input.revision!==revision.number||revision.status!=='planned')fail('Only the latest planned revision can be reviewed',409);
      if(revision.manifest.validation.status==='blocked')fail('Resolve handoff blockers before marking reviewed');
      if(typeof input.reviewer!=='string'||!input.reviewer.trim()||input.reviewer.trim().length>80||typeof input.note!=='string'||!input.note.trim()||input.note.trim().length>1000)fail('Review needs a name (1–80) and notes (1–1000 characters)');
      revision.status='reviewed';revision.review={reviewer:input.reviewer.trim(),note:input.note.trim(),reviewedAt:new Date().toISOString()};record.updatedAt=revision.review.reviewedAt;return write(record);
    })
  };
}
