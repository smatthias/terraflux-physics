export function validatePresets(value) {
  if(!Array.isArray(value) || value.length>32) throw Error('Expected at most 32 opening presets');
  const ids=new Set(),names=new Set();
  return value.map(p=>{
    if(!p || typeof p.id!=='string' || !/^[a-zA-Z0-9_-]{1,64}$/.test(p.id) || ids.has(p.id)) throw Error('Preset IDs must be unique');
    if(!['door','window'].includes(p.type) || typeof p.name!=='string' || !p.name.trim() || p.name.trim().length>60) throw Error('Preset needs a type and a name of 1–60 characters');
    const name=p.name.trim(),key=p.type+':'+name.toLowerCase();
    if(names.has(key)) throw Error('Preset names must be unique within each type');
    if(![p.width,p.height,p.sill].every(v=>typeof v==='number' && Number.isFinite(v)) || p.width<.1 || p.width>1000 || p.height<.1 || p.height>1000 || p.sill<0 || p.sill>1000 ||
      (p.type==='door' && p.sill!==0) || (p.type==='window' && p.sill<.1)) throw Error('Invalid preset dimensions');
    if(p.type==='door' && (!['A','B'].includes(p.hinge) || !['in','out'].includes(p.swing))) throw Error('Invalid preset hinge or swing');
    ids.add(p.id);names.add(key);
    return {id:p.id,name,type:p.type,width:p.width,height:p.height,sill:p.sill,...(p.type==='door'?{hinge:p.hinge,swing:p.swing}:{})};
  });
}
export function savePreset(presets,preset) {
  const same=presets.find(p=>p.type===preset.type && p.name.toLowerCase()===preset.name.trim().toLowerCase());
  const next={...preset,id:same?.id||preset.id};
  return validatePresets(same?presets.map(p=>p.id===same.id?next:p):[...presets,next]);
}
