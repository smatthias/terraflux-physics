import {validateProject} from './project.mjs';
import {serializeProjectJson} from './project-json.mjs';
export const RECOVERY_KEY='terratec.recovery.v1';
export function readDrafts(raw) {
  if(!raw) return [];
  const value=JSON.parse(raw);
  if(value.version!==1 || !Array.isArray(value.drafts) || value.drafts.length>20) throw Error('Unsupported recovery data');
  return value.drafts.map(d=>{
    if(!d || typeof d.key!=='string' || !/^[a-zA-Z0-9_-]{1,64}$/.test(d.key) || typeof d.updatedAt!=='string' || !Number.isFinite(Date.parse(d.updatedAt))) throw Error('Invalid recovery entry');
    return {key:d.key,updatedAt:d.updatedAt,project:validateProject(d.project)};
  }).sort((a,b)=>b.updatedAt.localeCompare(a.updatedAt));
}
export function storeDraft(storage,key,project) {
  serializeProjectJson(project);
  const drafts=readDrafts(storage.getItem(RECOVERY_KEY)).filter(d=>d.key!==key);
  const record={key,updatedAt:new Date().toISOString(),project:validateProject(project)};
  const next=[record,...drafts].slice(0,20);
  storage.setItem(RECOVERY_KEY,JSON.stringify({version:1,drafts:next}));return next;
}
export function removeDraft(storage,key) {
  const drafts=readDrafts(storage.getItem(RECOVERY_KEY)).filter(d=>d.key!==key);
  if(drafts.length) storage.setItem(RECOVERY_KEY,JSON.stringify({version:1,drafts}));
  else storage.removeItem(RECOVERY_KEY);
  return drafts;
}
