import {cabinetFootprint,validateFurniture} from './furniture.mjs';
export function cabinetInterval(item,axis){
  if(!['x','z'].includes(axis))throw Error('Choose X or Z axis');
  const values=cabinetFootprint(item).map(p=>p[axis]);
  return {min:Math.min(...values),center:item[axis],max:Math.max(...values)};
}
export function arrangeCabinet(room,id,referenceId,options){
  const item=room.furniture.find(c=>c.id===id),reference=room.furniture.find(c=>c.id===referenceId);
  if(!item||!reference||id===referenceId)throw Error('Choose two different cabinets');
  const {axis,mode}=options,a=cabinetInterval(item,axis),b=cabinetInterval(reference,axis);
  let delta;
  if(mode==='align'){
    if(!['min','center','max'].includes(options.edge))throw Error('Choose a valid alignment edge');
    delta=b[options.edge]-a[options.edge];
  }else if(mode==='gap'){
    if(!['before','after'].includes(options.side)||typeof options.gap!=='number'||!Number.isFinite(options.gap)||options.gap<0||options.gap>10)throw Error('Gap must be between 0 and 10 metres');
    delta=options.side==='after'?b.max+options.gap-a.min:b.min-options.gap-a.max;
  }else throw Error('Unknown cabinet arrangement');
  const furniture=validateFurniture(room.furniture.map(c=>c.id===id?{...c,[axis]:c[axis]+delta}:c),room.outline,room.height);
  return {...room,furniture};
}
export function arrangeCabinetGroup(room,ids,options){
  if(!Array.isArray(ids)||ids.length<2||new Set(ids).size!==ids.length||ids.some(id=>!room.furniture.some(c=>c.id===id)))throw Error('Choose at least two different cabinets');
  const selected=room.furniture.filter(c=>ids.includes(c.id)),{axis,mode}=options;
  const intervals=new Map(selected.map(c=>[c.id,cabinetInterval(c,axis)])),moves=new Map();
  if(mode==='align-group'){
    if(!ids.includes(options.anchorId)||!['min','center','max'].includes(options.edge))throw Error('Choose a selected anchor and alignment edge');
    const target=intervals.get(options.anchorId)[options.edge];
    for(const c of selected)if(c.id!==options.anchorId)moves.set(c.id,c[axis]+target-intervals.get(c.id)[options.edge]);
  }else if(mode==='distribute'){
    if(selected.length<3)throw Error('Choose at least three cabinets to distribute');
    const ordered=[...selected].sort((a,b)=>intervals.get(a.id).min-intervals.get(b.id).min||a.id.localeCompare(b.id));
    const first=intervals.get(ordered[0].id),last=intervals.get(ordered.at(-1).id),widths=ordered.map(c=>{const i=intervals.get(c.id);return i.max-i.min;});
    const gap=(last.max-first.min-widths.reduce((sum,w)=>sum+w,0))/(ordered.length-1);
    if(gap< -1e-9)throw Error('Not enough projected span for non-negative equal gaps');
    let cursor=first.min+widths[0]+Math.max(0,gap);
    for(let i=1;i<ordered.length-1;i++){const c=ordered[i];moves.set(c.id,c[axis]+cursor-intervals.get(c.id).min);cursor+=widths[i]+Math.max(0,gap);}
  }else throw Error('Unknown group arrangement');
  return {...room,furniture:validateFurniture(room.furniture.map(c=>moves.has(c.id)?{...c,[axis]:moves.get(c.id)}:c),room.outline,room.height)};
}
