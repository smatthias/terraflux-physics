import {furnitureClearance,clearanceSettings} from './furniture-clearance.mjs';
import {previewFurnitureInsertion} from './furniture-library.mjs';
import {cabinetInterval} from './cabinet-arrange.mjs';
export function placementRotation(value){if(typeof value!=='number'||!Number.isFinite(value))throw Error('Enter a finite placement rotation');return ((value%360)+360)%360;}
export function placementGuides(candidate,furniture,tolerance=.08){
  if(!candidate)return [];
  if(!Number.isFinite(tolerance)||tolerance<0||tolerance>.5)throw Error('Invalid alignment tolerance');
  const guides=[];
  for(const axis of ['x','z']){
    const own=cabinetInterval(candidate,axis);let best;
    for(const other of [...furniture].sort((a,b)=>a.id.localeCompare(b.id))){
      const target=cabinetInterval(other,axis);
      for(const a of ['center','min','max'])for(const b of ['center','min','max']){
        const delta=target[b]-own[a];
        if(Math.abs(delta)<=tolerance+1e-9&&(!best||Math.abs(delta)<Math.abs(best.delta)-1e-9))best={axis,position:target[b],delta,referenceId:other.id,referenceName:other.name,candidateEdge:a,referenceEdge:b};
      }
    }
    if(best){
      const other=furniture.find(c=>c.id===best.referenceId),cross=axis==='x'?'z':'x',a=cabinetInterval(candidate,cross),b=cabinetInterval(other,cross),start=Math.min(a.min,b.min)-.25,end=Math.max(a.max,b.max)+.25;
      guides.push({...best,from:{[axis]:best.position,[cross]:start},to:{[axis]:best.position,[cross]:end}});
    }
  }
  return guides;
}
export function evaluateFurniturePlacement(room,request,spacing,snap=true,guideSnap=false,clearanceOptions){
  let result=previewFurnitureInsertion(room,request,spacing,snap);
  let guides=placementGuides(result.candidate,room.furniture);
  if(guideSnap&&guides.length&&result.candidate){
    const point={x:result.candidate.x,z:result.candidate.z};for(const guide of guides)point[guide.axis]+=guide.delta;
    result=previewFurnitureInsertion(room,{...request,point},spacing,false);guides=placementGuides(result.candidate,room.furniture);
  }
  let clearance=null;try{const settings=clearanceSettings(clearanceOptions);if(result.candidate)clearance=furnitureClearance(room,result.candidate,settings);if(result.valid&&settings.enforce&&clearance?.issues.length)result={...result,valid:false,error:'Clearance: '+clearance.issues[0].message};}catch(error){result={...result,valid:false,error:error.message};}
  return {...result,guides,clearance};
}
