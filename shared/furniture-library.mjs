import {validateFurniture} from './furniture.mjs';
import {rectangle,snapPoint} from './geometry.mjs';
function design(input){
  const [item]=validateFurniture([{...input,id:'template',type:'cabinet',name:'Template',x:0,z:0,rotation:0}],rectangle(50,50),10);
  const {id,type,name,x,z,rotation,...parameters}=item;return parameters;
}
export function validateFurniturePresets(value){
  if(!Array.isArray(value)||value.length>32)throw Error('Expected at most 32 furniture presets');
  const ids=new Set(),names=new Set();
  return value.map(p=>{
    if(!p||typeof p.id!=='string'||!/^[a-zA-Z0-9_-]{1,64}$/.test(p.id)||ids.has(p.id))throw Error('Furniture preset IDs must be unique');
    if(typeof p.name!=='string'||!p.name.trim()||p.name.trim().length>60||names.has(p.name.trim().toLowerCase()))throw Error('Furniture preset names must be unique and contain 1–60 characters');
    ids.add(p.id);names.add(p.name.trim().toLowerCase());return {id:p.id,name:p.name.trim(),...design(p)};
  });
}
export function saveFurniturePreset(presets,preset){
  const same=presets.find(p=>p.name.toLowerCase()===preset.name.trim().toLowerCase()),next={...preset,id:same?.id??preset.id};
  return validateFurniturePresets(same?presets.map(p=>p.id===same.id?next:p):[...presets,next]);
}
function addInstance(room,template,id,name,point,spacing,snap){
  const position=snapPoint(point,spacing,snap);
  const item={...template,type:'cabinet',id,name,...position};
  return {...room,furniture:validateFurniture([...room.furniture,item],room.outline,room.height)};
}
export function duplicateCabinet(room,sourceId,id,point,spacing,snap=true){
  const source=room.furniture.find(c=>c.id===sourceId);if(!source)throw Error('Select a cabinet to duplicate');
  return addInstance(room,source,id,source.name.slice(0,53)+' (copy)',point,spacing,snap);
}
export function instantiateFurniturePreset(room,preset,id,point,spacing,snap=true){
  const [validated]=validateFurniturePresets([preset]);
  return addInstance(room,{...validated,rotation:0},id,validated.name,point,spacing,snap);
}
export function previewFurnitureInsertion(room,request,spacing,snap=true){
  let candidate=null;
  try{
    let template,name;
    if(request.kind==='copy'){
      template=room.furniture.find(c=>c.id===request.sourceId);if(!template)throw Error('Select a cabinet to preview');name=template.name.slice(0,53)+' (copy)';
    }else if(request.kind==='preset'){
      [template]=validateFurniturePresets([request.preset]);name=template.name;template={...template,rotation:0};
    }else throw Error('Unknown furniture preview');
    candidate={...template,type:'cabinet',id:request.id,name,...snapPoint(request.point,spacing,snap)};
    if(request.rotation!==undefined){if(typeof request.rotation!=='number'||!Number.isFinite(request.rotation))throw Error('Invalid placement rotation');candidate.rotation=((request.rotation%360)+360)%360;}
    const furniture=validateFurniture([...room.furniture,candidate],room.outline,room.height);
    return {candidate:furniture.at(-1),valid:true,error:''};
  }catch(error){
    if(candidate&&!['x','z','width','depth','height','rotation'].every(k=>Number.isFinite(candidate[k])))candidate=null;
    return {candidate,valid:false,error:error.message};
  }
}
