import {validateProject} from './project.mjs';
import {manufacturingBatches,designProfiles} from './manufacturing.mjs';
export const MAX_MANIFEST_BYTES=2097152;
const canonical=value=>JSON.stringify(normalise(value));
function normalise(value){
  if(Array.isArray(value))return value.map(normalise);
  if(value&&typeof value==='object')return Object.fromEntries(Object.keys(value).sort().map(k=>[k,normalise(value[k])]));
  return value;
}
async function digest(value){
  const bytes=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(canonical(value)));
  return Array.from(new Uint8Array(bytes),b=>b.toString(16).padStart(2,'0')).join('');
}
export function handoffValidation(batches,{profiles=[],designs=0}={}){
  const issues=[];
  if(!batches.length&&!profiles.length)issues.push({code:'NO_PANELS',severity:'blocking',message:'No cabinet panels to hand off'});
  for(const batch of batches)if(batch.layout.unplaced.length)issues.push({code:'OVERSIZE',severity:'blocking',batch:batch.number,count:batch.layout.unplaced.length,message:'Oversize panels remain unplaced'});
  if(designs)issues.push({code:'DESIGN_MACHINING',severity:'review',message:'Placed designs are nested as flat blanks: machine their holes, grooves and pockets with the designer\'s CNC programs and check its CNC plan'});
  issues.push({code:'WORKSHOP_REVIEW',severity:'review',message:'Verify material, grain, edging, joinery, tooling and cutting sequence before production'});
  return {status:issues.some(i=>i.severity==='blocking')?'blocked':'review-required',issues};
}
export async function createJobManifest(input,generatedAt=new Date().toISOString()){
  const source=validateProject(input),designs=source.furnitureDesigns??[],batches=manufacturingBatches(source.room.furniture,designs);
  // Placed designs add their profile cut list and counts; other manifests are unchanged.
  const placedDesigns=source.room.furniture.filter(item=>item.type==='design').length,profiles=designProfiles(source.room.furniture,designs);
  const sourceSha256=await digest(source);
  const summary={cabinets:source.room.furniture.filter(item=>item.type!=='design').length,panels:batches.reduce((n,b)=>n+b.panelCount,0),sheets:batches.reduce((n,b)=>n+b.layout.sheets.length,0),unplaced:batches.reduce((n,b)=>n+b.layout.unplaced.length,0),
    ...(placedDesigns?{designs:placedDesigns,profilePieces:profiles.reduce((n,g)=>n+g.count,0)}:{})};
  const payload={format:'terratec-workshop-job',version:1,algorithm:'guillotine-v1',units:'mm',generatedAt,sourceSha256,source,batches,...(placedDesigns?{profiles}:{}),
    summary,validation:handoffValidation(batches,{profiles,designs:placedDesigns})};
  return {...payload,manifestSha256:await digest(payload)};
}
export async function exportJobManifest(project){return JSON.stringify(await createJobManifest(project),null,2)+'\n';}
export async function verifyJobManifest(text,currentProject){
  if(typeof text!=='string'||new TextEncoder().encode(text).length>MAX_MANIFEST_BYTES)throw Error('Job manifest exceeds 2 MiB');
  const manifest=JSON.parse(text);
  if(manifest?.format!=='terratec-workshop-job'||manifest.version!==1||manifest.algorithm!=='guillotine-v1'||typeof manifest.generatedAt!=='string'||!Number.isFinite(Date.parse(manifest.generatedAt)))throw Error('Unsupported job manifest format');
  const expected=await createJobManifest(manifest.source,manifest.generatedAt);
  if(canonical(manifest)!==canonical(expected))throw Error('Job manifest integrity or derived layout check failed');
  const current=await digest(validateProject(currentProject));
  return {sourceSha256:expected.sourceSha256,matchesCurrent:current===expected.sourceSha256,validation:expected.validation};
}
