// Editing operations for furniture designs: templates, cabinet conversion and
// pure edit commands. Only the designer loads this module; project validation
// needs just furniture-design.mjs. Everything from that module is re-exported.
import {validateDesign,designBounds,partSize,partCentre,round,isEdgeFace,SHAPES,FEATURE_TYPES,PANEL_ORIENTATIONS,PIECE_FORMS,SECTION_AXES,ROUND_SHAPES,materialColor} from './furniture-design.mjs';
export * from './furniture-design.mjs';
export * from './furniture-design-geometry.mjs';
import {MATERIALS} from './materials.mjs';
import {rotateAboutWorldAxis,turnMatrix,axisDirection} from './design-transform.mjs';
const copy=value=>JSON.parse(JSON.stringify(value));

// ---- Creation -------------------------------------------------------------
// Default layers follow a common CNC convention: machine holes and recesses
// first, then cut the part free with the profile, so parts stay held down.
export function defaultLayers(makeId) {
  return [
    {name:'Drilling',color:'#d9534f',operation:'drill'},
    {name:'Grooves',color:'#f0ad4e',operation:'groove'},
    {name:'Pockets',color:'#5bc0de',operation:'pocket'},
    {name:'Parts',color:'#2e7d5b',operation:'profile'},
    {name:'Reference',color:'#8a8f98',operation:'none'},
  ].map(layer=>({id:makeId(),visible:true,locked:false,...layer}));
}
const layerFor=(design,operation)=>(design.layers.find(l=>l.operation===operation&&!l.locked)??design.layers.find(l=>!l.locked)??design.layers[0]).id;

// A simple carcass that shows every layer: panels, shelf-pin holes on both
// faces (A and B) and grooves that hold the back panel.
function boxParts(design,makeId) {
  const W=600,H=720,D=560,T=18,B=6,G=10,parts=layerFor(design,'profile'),drill=layerFor(design,'drill'),groove=layerFor(design,'groove');
  const shelfY=(H-T)/2;
  const pins=face=>[60,D-60].map(u=>({id:makeId(),type:'drill',face,layer:drill,u,v:shelfY-5,diameter:5,depth:12}));
  const backGroove=face=>({id:makeId(),type:'groove',face,layer:groove,u:10,v:T,length:B,width:H-2*T,depth:G});
  const panel=(name,orientation,x,y,z,length,width,thickness,features=[])=>({id:makeId(),name,shape:'panel',material:'birch',layer:parts,orientation,x,y,z,length,width,thickness,features});
  return [
    panel('Left side','side',0,0,0,D,H,T,[...pins('A'),backGroove('A')]),
    panel('Right side','side',W-T,0,0,D,H,T,[...pins('B'),backGroove('B')]),
    panel('Bottom','horizontal',T,0,0,W-2*T,D,T),
    panel('Top','horizontal',T,H-T,0,W-2*T,D,T),
    panel('Shelf','horizontal',T,shelfY,20,W-2*T,D-20,T),
    panel('Back','front',T-G,T,10,W-2*T+2*G,H-2*T,B),
  ];
}
export function createDesign({id,name='New design',template='empty',makeId}) {
  const design={id,name,layers:defaultLayers(makeId),parts:[]};
  if(template==='box') design.parts=boxParts(design,makeId);
  else if(template!=='empty') throw Error('Unknown design template');
  return validateDesign(design);
}

// Converts a room cabinet (metres, centred, local axes) into an editable design.
// Panels become sheet parts on the Parts layer; handles become metal bars on Reference.
export function designFromCabinet(cabinet,parts,{id,makeId}) {
  const design={id,name:cabinet.name.slice(0,60),layers:defaultLayers(makeId),parts:[]};
  const profile=layerFor(design,'profile'),reference=layerFor(design,'none');
  const mm=value=>round(value*1000),counts={};
  design.parts=parts.map(part=>{
    const size=part.size.map(mm),centre=part.position.map(mm);
    const min={x:round(centre[0]-size[0]/2+mm(cabinet.width)/2),y:round(centre[1]-size[1]/2),z:round(centre[2]-size[2]/2+mm(cabinet.depth)/2)};
    counts[part.label]=(counts[part.label]??0)+1;
    const name=part.label+' '+counts[part.label];
    if(!part.cut) return {id:makeId(),name,...barFromExtents({x:size[0],y:size[1],z:size[2]}),material:cabinet.hardwareMaterial??'steel',layer:reference,...min};
    const thin=size.indexOf(Math.min(...size));
    const orientation=['side','horizontal','front'][thin];
    const axes=PANEL_ORIENTATIONS[orientation],byAxis={x:size[0],y:size[1],z:size[2]};
    return {id:makeId(),name,shape:'panel',material:cabinet.material??'oak',layer:profile,orientation,...min,length:byAxis[axes.u],width:byAxis[axes.v],thickness:byAxis[axes.n],features:[]};
  });
  return validateDesign(design);
}

// ---- Editing --------------------------------------------------------------
const findPart=(design,partId)=>{const part=design.parts.find(p=>p.id===partId);if(!part) throw Error('Part not found');return part;};
const findLayer=(design,layerId)=>{const layer=design.layers.find(l=>l.id===layerId);if(!layer) throw Error('Layer not found');return layer;};
function assertEditable(design,...layerIds) {
  for(const layerId of layerIds) {const layer=findLayer(design,layerId);if(layer.locked) throw Error(`Layer “${layer.name}” is locked`);}
}
const withParts=(design,parts)=>validateDesign({...design,parts});

// A bar along the longest of the given extents (ties prefer y, then x, then z).
function barFromExtents(extent) {
  const axis=['y','x','z'].reduce((best,k)=>extent[k]>extent[best]?k:best,'y'),[a,b]=SECTION_AXES[axis];
  return {shape:'bar',axis,length:extent[axis],width:extent[a],height:extent[b]};
}
// New pieces come from the piece library: a base form with optional material,
// colour and length. They are placed 50 mm to the right of the existing design.
export function addPart(design,shape,{makeId,overrides={}}={}) {
  if(!SHAPES.includes(shape)) throw Error('Base form must be panel, bar, rod, tube, rectangular tube or angle');
  const form=PIECE_FORMS[shape],count=design.parts.filter(p=>p.shape===shape).length+1;
  const x=design.parts.length?round(designBounds(design).max.x+50):0;
  const layer=layerFor(design,shape==='panel'?'profile':'none'),material=overrides.material??form.material;
  const part={id:makeId(),shape,...form.defaults,name:form.short+' '+count,material,color:materialColor(material),layer,x,y:0,z:0,...(shape==='panel'?{features:[]}:{}),...overrides};
  assertEditable(design,part.layer);
  return {design:withParts(design,[...design.parts,part]),partId:part.id};
}
// A colour that still matches the old material follows a material change;
// a colour chosen for the piece is kept.
export function updatePart(design,partId,changes) {
  const part=findPart(design,partId),next={...part,...changes,id:part.id,shape:part.shape};
  // Unknown materials fall through to validation, which reports them.
  if(MATERIALS[changes.material] && changes.color===undefined && part.color===materialColor(part.material)) next.color=materialColor(changes.material);
  assertEditable(design,part.layer,next.layer);
  return withParts(design,design.parts.map(p=>p.id===partId?next:p));
}
// Changes a piece's base form, keeping its name, material, colour, layer,
// rotation and corner. The new form fills the same box: profiles keep their length direction
// (panels use their longest side), round forms fit inside the section, and a
// panel takes the orientation whose thickness is the smallest extent. Machining
// features exist only on panels, so they must be removed before converting.
export function convertPart(design,partId,shape) {
  const part=findPart(design,partId);
  if(!SHAPES.includes(shape)) throw Error('Base form must be panel, bar, rod, tube, rectangular tube or angle');
  if(shape===part.shape) return design;
  if(part.features?.length) throw Error('Remove the machining features before changing the base form');
  assertEditable(design,part.layer);
  const extent=partSize(part),{id,name,material,color,layer,x,y,z,rotation}=part,common={id,name,material,color,layer,x,y,z,...(rotation?{rotation}:{})};
  let next;
  if(shape==='panel') {
    const n=['y','z','x'].reduce((best,k)=>extent[k]<extent[best]?k:best,'y');
    const orientation=Object.keys(PANEL_ORIENTATIONS).find(key=>PANEL_ORIENTATIONS[key].n===n),axes=PANEL_ORIENTATIONS[orientation];
    next={...common,shape,orientation,length:extent[axes.u],width:extent[axes.v],thickness:extent[n],features:[]};
  } else {
    const axis=part.axis??barFromExtents(extent).axis,[a,b]=SECTION_AXES[axis],defaults=PIECE_FORMS[shape].defaults;
    const section=ROUND_SHAPES.includes(shape)?{diameter:Math.min(extent[a],extent[b])}:{width:extent[a],height:extent[b]};
    const thinnest=Math.min(extent[a],extent[b]),wall=defaults.wall===undefined?{}:{wall:round(Math.min(defaults.wall,shape==='angle'?thinnest/2:thinnest/4))};
    next={...common,shape,axis,length:extent[axis],...section,...wall};
  }
  return withParts(design,design.parts.map(p=>p.id===partId?next:p));
}
// Centre of the world bounds of some pieces: the pivot for turning them together.
export function selectionCentre(design,partIds) {
  const moving=design.parts.filter(p=>partIds.includes(p.id));
  if(!moving.length) throw Error('Part not found');
  const bounds=designBounds({...design,parts:moving});
  return {x:(bounds.min.x+bounds.max.x)/2,y:(bounds.min.y+bounds.max.y)/2,z:(bounds.min.z+bounds.max.z)/2};
}
// Turns pieces rigidly about an axis through their common centre, keeping their
// joints: each centre orbits the pivot and each rotation composes with the turn.
// `axis` is 'x', 'y', 'z' (world) or a direction, such as a piece's local axis.
// A single piece turns in place.
export function rotatePartsAbout(design,partIds,axis,degrees) {
  const direction=axisDirection(axis);
  if(!direction || ![direction.x,direction.y,direction.z,degrees].every(v=>typeof v==='number'&&Number.isFinite(v))) throw Error('Rotate about x, y, z or a direction by a finite angle');
  const moving=design.parts.filter(p=>partIds.includes(p.id));
  if(!moving.length) throw Error('Part not found');
  assertEditable(design,...moving.map(p=>p.layer));
  const pivot=selectionCentre(design,partIds),turn=turnMatrix(axis,degrees);
  const turned=new Map(moving.map(part=>{
    const size=partSize(part),centre=partCentre(part),offset={x:centre.x-pivot.x,y:centre.y-pivot.y,z:centre.z-pivot.z};
    const moved=[0,1,2].map(i=>turn[i][0]*offset.x+turn[i][1]*offset.y+turn[i][2]*offset.z);
    return [part.id,{...part,x:round(pivot.x+moved[0]-size.x/2),y:round(pivot.y+moved[1]-size.y/2),z:round(pivot.z+moved[2]-size.z/2),rotation:rotateAboutWorldAxis(part.rotation,axis,degrees)}];
  }));
  return withParts(design,design.parts.map(p=>turned.get(p.id)??p));
}
// ---- Multi-piece commands: every piece is validated and checked for locks.
function pick(design,partIds) {
  const parts=design.parts.filter(p=>partIds.includes(p.id));
  if(!parts.length||parts.length!==new Set(partIds).size) throw Error('Part not found');
  return parts;
}
// Moves pieces together by the same offset (mm).
export function moveParts(design,partIds,offset) {
  const parts=pick(design,partIds);assertEditable(design,...parts.map(p=>p.layer));
  const moved=new Set(partIds);
  return withParts(design,design.parts.map(p=>moved.has(p.id)?{...p,x:round(p.x+(offset.x??0)),y:round(p.y+(offset.y??0)),z:round(p.z+(offset.z??0))}:p));
}
// Applies the same changes (layer, material, rotation…) to several pieces.
export function updateParts(design,partIds,changes) {
  let next=design;
  for(const id of pick(design,partIds).map(p=>p.id)) next=updatePart(next,id,changes);
  return next;
}
export function removeParts(design,partIds) {
  const parts=pick(design,partIds);assertEditable(design,...parts.map(p=>p.layer));
  const gone=new Set(partIds);
  return withParts(design,design.parts.filter(p=>!gone.has(p.id)));
}
// Copies pieces as a group 50 mm to the right of the group, keeping their
// relative positions (and so their joints).
export function duplicateParts(design,partIds,makeId) {
  const parts=pick(design,partIds);assertEditable(design,...parts.flatMap(p=>[p.layer,...(p.features??[]).map(f=>f.layer)]));
  const bounds=designBounds({...design,parts}),shift=round(bounds.size.x+50);
  const clones=parts.map(part=>({...copy(part),id:makeId(),name:(part.name.endsWith(' (copy)')?part.name:part.name.slice(0,53)+' (copy)'),x:round(part.x+shift),
    ...(part.features?{features:part.features.map(f=>({...f,id:makeId()}))}:{})}));
  return {design:withParts(design,[...design.parts,...clones]),partIds:clones.map(c=>c.id)};
}
export function removePart(design,partId) {
  assertEditable(design,findPart(design,partId).layer);
  return withParts(design,design.parts.filter(p=>p.id!==partId));
}
export function duplicatePart(design,partId,makeId) {
  const part=findPart(design,partId),size=partSize(part);
  assertEditable(design,part.layer,...(part.features??[]).map(f=>f.layer));
  const clone={...copy(part),id:makeId(),name:(part.name.endsWith(' (copy)')?part.name:part.name.slice(0,53)+' (copy)'),x:round(part.x+size.x+50),
    ...(part.features?{features:part.features.map(f=>({...f,id:makeId()}))}:{})};
  return {design:withParts(design,[...design.parts,clone]),partId:clone.id};
}

const FEATURE_DEFAULTS={drill:{diameter:5,depth:12},pocket:{length:100,width:50,depth:6},groove:{width:8,depth:8}};
// New features are centred on face A; a groove runs the full panel length.
export function addFeature(design,partId,type,makeId) {
  const part=findPart(design,partId);
  if(part.shape!=='panel') throw Error('Machining features are supported on panels');
  if(!FEATURE_TYPES.includes(type)) throw Error('Feature type must be drill, pocket or groove');
  const d=FEATURE_DEFAULTS[type],layer=layerFor(design,type),depth=Math.min(d.depth,part.thickness);
  let feature;
  if(type==='drill') feature={type,diameter:Math.min(d.diameter,part.length,part.width),u:part.length/2,v:part.width/2};
  else {
    const length=type==='groove'?part.length:Math.min(d.length,part.length),width=Math.min(d.width,part.width);
    feature={type,length,width,u:round((part.length-length)/2),v:round((part.width-width)/2)};
  }
  feature={id:makeId(),face:'A',layer,depth,...feature};
  assertEditable(design,part.layer,layer);
  return {design:withParts(design,design.parts.map(p=>p.id===partId?{...p,features:[...p.features,feature]}:p)),featureId:feature.id};
}
function findFeature(design,featureId) {
  for(const part of design.parts) {const feature=part.features?.find(f=>f.id===featureId);if(feature) return {part,feature};}
  throw Error('Feature not found');
}
export function updateFeature(design,featureId,changes) {
  const {part,feature}=findFeature(design,featureId),next={...feature,...changes,id:feature.id,type:feature.type};
  // Onto an edge: centre across the thickness. Back onto a face: centre the
  // coordinate the edge had fixed, so the hole lands on the face.
  if(isEdgeFace(next.face)&&!isEdgeFace(feature.face)&&changes.t===undefined) next.t=part.thickness/2;
  if(!isEdgeFace(next.face)&&isEdgeFace(feature.face)) {
    if(feature.face[0]==='u'&&changes.u===undefined) next.u=part.length/2;
    if(feature.face[0]==='v'&&changes.v===undefined) next.v=part.width/2;
    delete next.t;
  }
  assertEditable(design,feature.layer,next.layer);
  return withParts(design,design.parts.map(p=>p.id===part.id?{...p,features:p.features.map(f=>f.id===featureId?next:f)}:p));
}
export function removeFeature(design,featureId) {
  const {part,feature}=findFeature(design,featureId);
  assertEditable(design,feature.layer);
  return withParts(design,design.parts.map(p=>p.id===part.id?{...p,features:p.features.filter(f=>f.id!==featureId)}:p));
}

export function addLayer(design,{makeId,name,operation='none',color='#7a6cc4'}) {
  let label=name;
  if(!label) for(let n=design.layers.length+1;;n++) {label='Layer '+n;if(!design.layers.some(l=>l.name.toLowerCase()===label.toLowerCase())) break;}
  const layer={id:makeId(),name:label,color,visible:true,locked:false,operation};
  return {design:validateDesign({...design,layers:[...design.layers,layer]}),layerId:layer.id};
}
export function updateLayer(design,layerId,changes) {
  const layer=findLayer(design,layerId);
  return validateDesign({...design,layers:design.layers.map(l=>l.id===layerId?{...layer,...changes,id:layer.id}:l)});
}
// Only empty layers can be removed; a design keeps at least one layer.
export function removeLayer(design,layerId) {
  const layer=findLayer(design,layerId);
  const used=design.parts.some(p=>p.layer===layerId||p.features?.some(f=>f.layer===layerId));
  if(used) throw Error(`Layer “${layer.name}” is in use; move its parts and features first`);
  if(design.layers.length===1) throw Error('A design needs at least one layer');
  return validateDesign({...design,layers:design.layers.filter(l=>l.id!==layerId)});
}
// Layer order is the machining order.
export function moveLayer(design,layerId,delta) {
  const index=design.layers.findIndex(l=>l.id===layerId);
  if(index<0) throw Error('Layer not found');
  const target=index+delta;
  if(!Number.isInteger(delta) || target<0 || target>=design.layers.length) return design;
  const layers=[...design.layers],[layer]=layers.splice(index,1);
  layers.splice(target,0,layer);
  return validateDesign({...design,layers});
}
export const renameDesign=(design,name)=>validateDesign({...design,name});
// CAM settings and layer tools merge with what is set; a null value clears it.
const merge=(current,changes)=>Object.fromEntries(Object.entries({...current,...changes}).filter(([,v])=>v!==null&&v!==undefined));
export const updateCam=(design,changes)=>validateDesign({...design,cam:merge(design.cam,changes)});
// Sheet stock for nesting; clearing every value returns to the defaults.
export function updateStock(design,changes) {
  const stock=merge(design.stock,changes),{stock:_,...rest}=design;
  return validateDesign(Object.keys(stock).length?{...rest,stock}:rest);
}
export function updateLayerTool(design,layerId,changes) {
  const layer=findLayer(design,layerId);
  return validateDesign({...design,layers:design.layers.map(l=>l.id===layerId?{...layer,tool:merge(layer.tool,changes)}:l)});
}
export function duplicateDesign(design,{id,makeId}) {
  const layerIds=new Map(design.layers.map(l=>[l.id,makeId()]));
  return validateDesign({id,name:(design.name.endsWith(' (copy)')?design.name:design.name.slice(0,53)+' (copy)'),...(design.cam?{cam:design.cam}:{}),...(design.stock?{stock:design.stock}:{}),
    layers:design.layers.map(l=>({...l,id:layerIds.get(l.id)})),
    parts:design.parts.map(p=>({...copy(p),id:makeId(),layer:layerIds.get(p.layer),...(p.features?{features:p.features.map(f=>({...f,id:makeId(),layer:layerIds.get(f.layer)}))}:{})}))});
}
