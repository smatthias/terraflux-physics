import {mkdir,readFile,readdir,writeFile,rename,rm} from 'node:fs/promises';
import {join,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomUUID} from 'node:crypto';
import {validateProject} from '../shared/project.mjs';
import {validateTimeline} from '../shared/timeline.mjs';

export function storageError(error){
  const messages={EACCES:'Storage permission denied. Choose a writable TERRATEC_DATA_DIR or correct its permissions.',EPERM:'Storage permission denied. Choose a writable TERRATEC_DATA_DIR or correct its permissions.',EROFS:'Storage is read-only. Choose a writable TERRATEC_DATA_DIR.',ENOSPC:'Storage is full. Free space before saving again.',ENOTDIR:'The configured storage path contains a file where a directory is required. Choose a directory for TERRATEC_DATA_DIR.'};
  return messages[error.code]?Object.assign(Error(messages[error.code]),{status:503,code:'STORAGE_'+error.code}):error;
}
export function parseStoredProject(text,label='Saved project'){
  let value;try{value=JSON.parse(text);}catch{throw Object.assign(Error(`${label} contains invalid JSON. Restore a known-good backup; the original file has been kept.`),{status:422,code:'PROJECT_CORRUPT'});}
  try{return validateProject(value);}catch(error){throw Object.assign(Error(`${label}: ${error.message}. Use a compatible TerraTec release or restore a known-good backup; the original file has been kept.`),{status:422,code:![1,2,3,4,5,6,7].includes(value?.schemaVersion)?'PROJECT_UNSUPPORTED':'PROJECT_INVALID'});}
}
export async function checkStorage(dataDir){
  const issues=[],counts={projects:0,jobs:0,customers:0,timelines:0};
  let writable=false;const probe=join(dataDir,'.terratec-check-'+randomUUID()),renamed=probe+'.renamed';
  try{await mkdir(dataDir,{recursive:true});await writeFile(probe,'storage check',{flag:'wx'});await rename(probe,renamed);writable=true;}
  catch(error){const mapped=storageError(error);issues.push({code:mapped.code||'STORAGE_UNAVAILABLE',message:mapped.message});}
  finally{for(const path of [probe,renamed])try{await rm(path,{force:true});}catch{writable=false;issues.push({code:'STORAGE_CLEANUP',message:'Storage check could not remove its temporary probe. Check directory permissions.'});}}
  for(const collection of ['projects','jobs','customers']){
    let files;try{files=await readdir(join(dataDir,collection));}catch(error){if(error.code==='ENOENT')continue;issues.push({code:'STORAGE_READ',collection,message:storageError(error).message});continue;}
    for(const file of files.filter(f=>/^[0-9a-f-]{36}\.json$/.test(f))){
      counts[collection]++;
      try{const record=JSON.parse(await readFile(join(dataDir,collection,file),'utf8'));if(collection==='projects')parseStoredProject(JSON.stringify(record.project),'Project '+file.slice(0,-5));}
      catch(error){issues.push({code:error.code||'RECORD_CORRUPT',collection,id:file.slice(0,-5),message:error instanceof SyntaxError?'Record contains invalid JSON. Restore a known-good backup.':storageError(error).message});}
    }
  }
  // Project timelines (version history) are stored beside their projects.
  let timelineFiles=[];try{timelineFiles=(await readdir(join(dataDir,'projects'))).filter(f=>/^[0-9a-f-]{36}\.timeline\.json$/.test(f));}catch{}
  for(const file of timelineFiles){
    counts.timelines++;
    try{validateTimeline(JSON.parse(await readFile(join(dataDir,'projects',file),'utf8')),validateProject);}
    catch(error){issues.push({code:'TIMELINE_INVALID',collection:'timelines',id:file.slice(0,36),message:`Project history cannot be read (${error instanceof SyntaxError?'invalid JSON':error.message}). The project itself is unaffected; restore the history file from a backup or delete it to start a new history.`});}
  }
  try{parseStoredProject(await readFile(join(dataDir,'project.json'),'utf8'),'Legacy project');}catch(error){if(error.code!=='ENOENT')issues.push({code:error.code||'PROJECT_INVALID',collection:'legacy-project',message:storageError(error).message});}
  return {status:issues.length?'attention':'ok',writable,counts,issues,scope:'Write/rename/delete probe and JSON readability; project schema and version-history validation. Not a full job/customer integrity audit.'};
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const report=await checkStorage(process.env.TERRATEC_DATA_DIR||fileURLToPath(new URL('../data/',import.meta.url)));
  console.log(JSON.stringify(report,null,2));if(report.status!=='ok')process.exitCode=1;
}
