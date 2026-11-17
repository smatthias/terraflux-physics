import {validateStock} from './stock.mjs';
export function validateWorkshopProfiles(value) {
  if(!Array.isArray(value)||value.length>32)throw Error('Expected at most 32 workshop profiles');
  const ids=new Set(),names=new Set();
  return value.map(profile=>{
    if(!profile||typeof profile.id!=='string'||!/^[a-zA-Z0-9_-]{1,64}$/.test(profile.id)||ids.has(profile.id))throw Error('Workshop profile IDs must be unique');
    if(typeof profile.name!=='string'||!profile.name.trim()||profile.name.trim().length>60||names.has(profile.name.trim().toLowerCase()))throw Error('Workshop profile names must be unique and contain 1–60 characters');
    const name=profile.name.trim();ids.add(profile.id);names.add(name.toLowerCase());
    return {id:profile.id,name,...validateStock(profile)};
  });
}
export function saveWorkshopProfile(profiles,profile) {
  const same=profiles.find(p=>p.name.toLowerCase()===profile.name.trim().toLowerCase());
  const next={...profile,id:same?.id??profile.id};
  return validateWorkshopProfiles(same?profiles.map(p=>p.id===same.id?next:p):[...profiles,next]);
}
export function applyWorkshopProfile(room,cabinetId,profile) {
  const settings=validateStock(profile);
  if(!room.furniture.some(c=>c.id===cabinetId))throw Error('Select a cabinet to apply a workshop profile');
  return {...room,furniture:room.furniture.map(c=>c.id===cabinetId?{...c,...settings}:c)};
}
export const MAX_WORKSHOP_BYTES=32768;
export function exportWorkshopProfiles(profiles) {
  return JSON.stringify({format:'terratec-workshop-profiles',version:1,profiles:validateWorkshopProfiles(profiles)},null,2)+'\n';
}
export function importWorkshopProfiles(existing,text,newId=()=>crypto.randomUUID()) {
  if(typeof text!=='string'||new TextEncoder().encode(text).length>MAX_WORKSHOP_BYTES)throw Error('Workshop profile file exceeds 32 KiB');
  const input=JSON.parse(text);
  if(input?.format!=='terratec-workshop-profiles'||input.version!==1)throw Error('Unsupported workshop profile format');
  const incoming=validateWorkshopProfiles(input.profiles),result=validateWorkshopProfiles(existing);
  for(const p of incoming){
    const base=p.name;let name=base,n=1;
    while(result.some(r=>r.name.toLowerCase()===name.toLowerCase())){const suffix=` (import ${n++})`;name=base.slice(0,60-suffix.length)+suffix;}
    result.push({...p,id:newId(),name});
  }
  return validateWorkshopProfiles(result);
}
export function applyWorkshopProfileToCabinets(room,cabinetIds,profile) {
  if(!Array.isArray(cabinetIds)||!cabinetIds.length||new Set(cabinetIds).size!==cabinetIds.length||cabinetIds.some(id=>!room.furniture.some(c=>c.id===id)))throw Error('Choose valid cabinets for bulk application');
  const settings=validateStock(profile),ids=new Set(cabinetIds);
  return {...room,furniture:room.furniture.map(c=>ids.has(c.id)?{...c,...settings}:c)};
}
