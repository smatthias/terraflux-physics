import {validateProject} from './project.mjs';
export function duplicateProject(input) {
  const project=validateProject(input);
  return validateProject({...project,name:project.name.slice(0,113)+' (copy)'});
}
