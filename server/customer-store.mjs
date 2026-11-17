import {readFile,writeFile,mkdir,rename,rm,readdir} from 'node:fs/promises';
import {join} from 'node:path';
import {randomUUID} from 'node:crypto';
import {validProjectId} from './project-store.mjs';
import {validateCustomer,parseCustomerBackup} from '../shared/customers.mjs';
const fail=(message,status=400)=>{throw Object.assign(Error(message),{status});};
export function customerStore(dataDir){
  const directory=join(dataDir,'customers');let queue=Promise.resolve();
  const read=async id=>{if(!validProjectId(id))fail('Invalid customer ID');try{return JSON.parse(await readFile(join(directory,id+'.json'),'utf8'));}catch(e){if(e.code==='ENOENT')fail('Customer not found',404);throw e;}};
  const list=async()=>{let files;try{files=await readdir(directory);}catch(e){if(e.code==='ENOENT')return [];throw e;}const records=await Promise.all(files.filter(f=>f.endsWith('.json')&&validProjectId(f.slice(0,-5))).map(f=>read(f.slice(0,-5))));return records.sort((a,b)=>a.name.localeCompare(b.name)||a.id.localeCompare(b.id));};
  return {list,read,restore(input){const operation=queue.then(async()=>{
    let details;try{details=parseCustomerBackup(input);}catch(e){fail(e.message);}
    const existing=await list();if(existing.length>=200)fail('Maximum 200 customer records');
    const base=details.name;let name=base,n=1;
    while(existing.some(c=>c.name.toLowerCase()===name.toLowerCase())){const suffix=` (restore ${n++})`;name=base.slice(0,120-suffix.length)+suffix;}
    const record={...details,name,id:randomUUID(),version:1,updatedAt:new Date().toISOString()};
    await mkdir(directory,{recursive:true});const temporary=join(directory,randomUUID()+'.tmp');
    try{await writeFile(temporary,JSON.stringify(record)+'\n');await rename(temporary,join(directory,record.id+'.json'));}finally{await rm(temporary,{force:true});}return record;
  });queue=operation.catch(()=>{});return operation;},save(input,id){const operation=queue.then(async()=>{
    let details;try{details=validateCustomer(input);}catch(e){fail(e.message);}
    const old=id?await read(id):null;if(old&&input.expectedVersion!==old.version)fail('Customer changed; reload before editing',409);
    if(!old&&(await list()).length>=200)fail('Maximum 200 customer records');
    const record={...details,id:old?.id??randomUUID(),version:(old?.version??0)+1,updatedAt:new Date().toISOString()};
    await mkdir(directory,{recursive:true});const temporary=join(directory,randomUUID()+'.tmp');
    try{await writeFile(temporary,JSON.stringify(record)+'\n');await rename(temporary,join(directory,record.id+'.json'));}finally{await rm(temporary,{force:true});}return record;
  });queue=operation.catch(()=>{});return operation;}};
}
