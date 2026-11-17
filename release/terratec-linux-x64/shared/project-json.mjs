import {validateProject} from './project.mjs';
export const MAX_PROJECT_BYTES=65536;
export function parseProjectJson(text) {
  if(typeof text!=='string' || new TextEncoder().encode(text).length>MAX_PROJECT_BYTES) throw Error('Project JSON exceeds 64 KiB');
  return validateProject(JSON.parse(text));
}
export function serializeProjectJson(project) {
  const text=JSON.stringify(validateProject(project),null,2)+'\n';
  if(new TextEncoder().encode(text).length>MAX_PROJECT_BYTES) throw Error('Project JSON exceeds 64 KiB');
  return text;
}
