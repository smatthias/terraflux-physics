import {readFile,writeFile,mkdir,rename,rm,readdir} from 'node:fs/promises';
import {join} from 'node:path';
import {randomUUID} from 'node:crypto';
import {parseStoredProject} from './storage-check.mjs';
import {validateProject} from '../shared/project.mjs';
import {validateTimeline,TIMELINE_LIMITS} from '../shared/timeline.mjs';
export const validProjectId=id=>/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(id);
export const MAX_TIMELINE_BYTES=TIMELINE_LIMITS.bytes+65536;
export function projectStore(dataDir) {
  const directory=join(dataDir,'projects');
  const read=async id=>{
    if(!validProjectId(id)) throw Object.assign(Error('Invalid project ID'),{status:400});
    try {const record=JSON.parse(await readFile(join(directory,id+'.json'),'utf8'));return {...record,project:parseStoredProject(JSON.stringify(record.project),'Project '+id)};}
    catch(error) {if(error.code==='ENOENT') throw Object.assign(Error('Project not found'),{status:404});if(error instanceof SyntaxError)throw Object.assign(Error('Project '+id+' contains invalid JSON. Restore a known-good backup; the original file has been kept.'),{status:422,code:'PROJECT_CORRUPT'});throw error;}
  };
  return {
    read,
    async list() {
      let files;
      try {files=await readdir(directory);} catch(error) {if(error.code==='ENOENT') return [];throw error;}
      const entries=await Promise.all(files.filter(file=>file.endsWith('.json')&&validProjectId(file.slice(0,-5))).map(file=>read(file.slice(0,-5))));
      return entries.map(({id,updatedAt,project})=>({id,updatedAt,name:project.name})).sort((a,b)=>b.updatedAt.localeCompare(a.updatedAt)||a.id.localeCompare(b.id));
    },
    // Version history of a saved project (see shared/timeline.mjs), stored beside it.
    async readTimeline(id) {
      await read(id);
      let text;
      try {text=await readFile(join(directory,id+'.timeline.json'),'utf8');}
      catch(error) {if(error.code==='ENOENT') throw Object.assign(Error('No history saved for this project'),{status:404});throw error;}
      try {return validateTimeline(JSON.parse(text),validateProject);}
      catch(error) {throw Object.assign(Error('The saved history of this project cannot be read ('+(error instanceof SyntaxError?'invalid JSON':error.message)+'). The project itself is unaffected.'),{status:422,code:'TIMELINE_INVALID'});}
    },
    async saveTimeline(id,timeline) {
      await read(id);
      try {validateTimeline(timeline,validateProject);} catch(error) {throw Object.assign(Error(error.message),{status:400});}
      const temporary=join(directory,randomUUID()+'.tmp');
      try {await writeFile(temporary,JSON.stringify(timeline)+'\n');await rename(temporary,join(directory,id+'.timeline.json'));}
      finally {await rm(temporary,{force:true});}
      return {id,versions:timeline.entries.length,savedAt:new Date().toISOString()};
    },
    async save(project,id) {
      if(id) await read(id);
      else id=randomUUID();
      const record={id,updatedAt:new Date().toISOString(),project:validateProject(project)};
      await mkdir(directory,{recursive:true});
      const temporary=join(directory,randomUUID()+'.tmp');
      try {await writeFile(temporary,JSON.stringify(record,null,2)+'\n');await rename(temporary,join(directory,id+'.json'));}
      finally {await rm(temporary,{force:true});}
      return record;
    }
  };
}
