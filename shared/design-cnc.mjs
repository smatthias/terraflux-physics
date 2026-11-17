// Machining plan and checks for furniture designs. This is the machine-neutral
// structure the toolpaths (design-cam.mjs) and NC programs (cam-export.mjs) are
// generated from.
//
// Operations are grouped per panel and ordered by layer (the layer list is the
// machining order), then by face and sheet position for a stable result.
// Coordinates are millimetres in the panel's sheet frame (u along length, v along
// width, from the minimum corner, face A up). Face B operations need the panel
// turned over, and are marked so.
import {validateDesign,partWorldBox,isEdgeFace,PIECE_FORMS} from './furniture-design.mjs';
import {isThrough} from './furniture-design-geometry.mjs';
import {pieceSolids,piecesCollide} from './design-solids.mjs';
import {MATERIALS} from './materials.mjs';

const FEATURE_OPERATION={drill:'drill',pocket:'pocket',groove:'groove'};

// Parts may only interpenetrate where one of them is machined or hollow to
// receive the other (a back panel in grooves, a dowel in a hole, a rod in a
// tube). Touching is allowed. Pieces are tested by their true shapes at any
// angle (design-solids.mjs): round rods, tubes, holes and bores are round, and
// an L-angle is its two legs. The overlap of two pieces must lie within a
// single groove, pocket, hole or bore of either piece.
export function partCollisions(design) {
  const pieces=design.parts.map(part=>({part,box:partWorldBox(part),...pieceSolids(part,1e-4)})),collisions=[];
  const near=(a,b)=>['x','y','z'].every(k=>a.min[k]<b.max[k]&&b.min[k]<a.max[k]);
  for(let i=0;i<pieces.length;i++) for(let j=i+1;j<pieces.length;j++) {
    const a=pieces[i].part,b=pieces[j].part;
    if(near(pieces[i].box,pieces[j].box)&&piecesCollide(pieces[i],pieces[j])) collisions.push({a:a.id,b:b.id,message:`${a.name} and ${b.name} overlap without a groove, pocket, hole or tube to receive them`});
  }
  return collisions;
}

export function cncPlan(input) {
  const design=validateDesign(input),order=new Map(design.layers.map((l,i)=>[l.id,i])),layerById=new Map(design.layers.map(l=>[l.id,l]));
  const issues=[],parts=[];
  for(const part of design.parts) {
    const partLayer=layerById.get(part.layer);
    if(part.shape!=='panel') {
      if(partLayer.operation!=='none') issues.push({code:'NOT_SHEET',severity:'warning',partId:part.id,message:`${part.name} is a ${PIECE_FORMS[part.shape].label.toLowerCase()}; only panels are machined from sheets in this version`});
      continue;
    }
    const operations=[];
    for(const feature of part.features) {
      const layer=layerById.get(feature.layer),expected=FEATURE_OPERATION[feature.type];
      if(layer.operation==='none') issues.push({code:'NOT_MACHINED',severity:'warning',partId:part.id,featureId:feature.id,message:`${part.name}: ${feature.type} on layer “${layer.name}” has no CNC operation`});
      else if(layer.operation!==expected) issues.push({code:'LAYER_OPERATION_MISMATCH',severity:'error',partId:part.id,featureId:feature.id,message:`${part.name}: ${feature.type} feature is on ${layer.operation} layer “${layer.name}”`});
      if(layer.operation==='none') continue;
      const geometry=feature.type==='drill'?{u:feature.u,v:feature.v,...(isEdgeFace(feature.face)?{t:feature.t}:{}),diameter:feature.diameter}:{u:feature.u,v:feature.v,length:feature.length,width:feature.width};
      operations.push({layerId:layer.id,layer:layer.name,operation:layer.operation,type:feature.type,featureId:feature.id,face:feature.face,flip:feature.face==='B',...(isEdgeFace(feature.face)?{edge:true}:{}),...geometry,depth:feature.depth,through:isThrough(part,feature)});
    }
    if(partLayer.operation==='profile') operations.push({layerId:partLayer.id,layer:partLayer.name,operation:'profile',type:'outline',face:'A',flip:false,u:0,v:0,length:part.length,width:part.width,depth:part.thickness,through:true});
    else if(partLayer.operation!=='none') issues.push({code:'LAYER_OPERATION_MISMATCH',severity:'error',partId:part.id,message:`${part.name} outline is on ${partLayer.operation} layer “${partLayer.name}”; part outlines belong on a profile layer`});
    operations.sort((a,b)=>order.get(a.layerId)-order.get(b.layerId)||(a.face<b.face?-1:a.face>b.face?1:0)||a.v-b.v||a.u-b.u);
    if(operations.some(op=>op.flip)) issues.push({code:'FLIP_REQUIRED',severity:'info',partId:part.id,message:`${part.name} has face B operations; the panel must be turned over`});
    const edgeHoles=operations.filter(op=>op.edge).length;
    if(edgeHoles) issues.push({code:'EDGE_DRILLING',severity:'info',partId:part.id,message:`${part.name} has ${edgeHoles} edge hole${edgeHoles===1?'':'s'}; they need a horizontal drilling unit and are not in 3-axis G-code`});
    for(const op of operations) if(!layerById.get(op.layerId).visible) {issues.push({code:'HIDDEN_LAYER',severity:'info',partId:part.id,message:`${part.name} has operations on hidden layer “${op.layer}”`});break;}
    parts.push({partId:part.id,name:part.name,material:part.material,materialLabel:MATERIALS[part.material].label,
      sheet:{lengthMm:part.length,widthMm:part.width,thicknessMm:part.thickness},operations});
  }
  for(const collision of partCollisions(design)) issues.push({code:'PART_COLLISION',severity:'error',partId:collision.a,otherPartId:collision.b,message:collision.message});
  const layers=design.layers.map((layer,index)=>{
    const ops=parts.flatMap(p=>p.operations).filter(op=>op.layerId===layer.id);
    return {id:layer.id,name:layer.name,order:index+1,operation:layer.operation,visible:layer.visible,operations:ops.length,maxDepthMm:ops.length?Math.max(...ops.map(op=>op.depth)):0};
  });
  return {layers,parts,issues,ready:!issues.some(issue=>issue.severity==='error')};
}

export function cncPlanJson(design,generatedAt=new Date().toISOString()) {
  const plan=cncPlan(design);
  return JSON.stringify({format:'terratec-cnc-plan',version:1,units:'mm',generatedAt,
    note:'Machine-neutral machining plan. Not an NC program: tools, feeds, tabs and post-processing are added by the NC program export.',
    design:{id:design.id,name:design.name},...plan},null,2)+'\n';
}
