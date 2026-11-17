import {validatePricing} from './quote.mjs';
export const PRICING_PRESETS_KEY='terratec-pricing-presets-v1',MAX_PRICING_PRESETS_BYTES=131072;
export function validatePricingPresets(value){
 if(!Array.isArray(value)||value.length>32)throw Error('Expected at most 32 pricing presets');
 const ids=new Set(),names=new Set();
 return value.map(p=>{
  if(!p||typeof p.id!=='string'||!/^[a-zA-Z0-9_-]{1,64}$/.test(p.id)||ids.has(p.id))throw Error('Pricing preset IDs must be unique');
  if(typeof p.name!=='string'||!p.name.trim()||p.name.trim().length>60||names.has(p.name.trim().toLowerCase()))throw Error('Pricing preset names must be unique and contain 1–60 characters');
  ids.add(p.id);names.add(p.name.trim().toLowerCase());return {id:p.id,name:p.name.trim(),pricing:validatePricing(p.pricing)};
 });
}
export function savePricingPreset(existing,preset){
 const current=validatePricingPresets(existing),same=current.find(p=>p.name.toLowerCase()===String(preset.name).trim().toLowerCase());
 const next={...preset,id:same?.id??preset.id};
 return validatePricingPresets(same?current.map(p=>p.id===same.id?next:p):[...current,next]);
}
export function exportPricingPresets(presets){return JSON.stringify({format:'terratec-pricing-presets',version:1,presets:validatePricingPresets(presets)},null,2)+'\n';}
export function importPricingPresets(existing,text,newId=()=>crypto.randomUUID()){
 if(typeof text!=='string'||new TextEncoder().encode(text).length>MAX_PRICING_PRESETS_BYTES)throw Error('Pricing preset file exceeds 128 KiB');
 const value=JSON.parse(text);if(value?.format!=='terratec-pricing-presets'||value.version!==1)throw Error('Unsupported pricing preset file');
 const incoming=validatePricingPresets(value.presets),result=validatePricingPresets(existing);
 for(const p of incoming){let name=p.name,n=1;while(result.some(v=>v.name.toLowerCase()===name.toLowerCase())){const suffix=` (import ${n++})`;name=p.name.slice(0,60-suffix.length)+suffix;}
  result.push({...p,id:newId(),name});
 }
 return validatePricingPresets(result);
}
