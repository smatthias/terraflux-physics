// Furniture designer model. Designs are measured in millimetres, the CNC unit.
//
// World axes match room cabinets: x to the right, y up, z toward the front. Each
// piece has a base form, a length, a material and a colour (defaulting to the
// material colour). (x, y, z) is the minimum corner of the unrotated piece; an
// optional rotation {x, y, z} in degrees turns it about its centre (see
// design-transform.mjs). Without a rotation a piece is axis-aligned.
//   panel     board or sheet: length × width × thickness with an orientation
//   bar       rectangular bar: section width × height, length along an axis
//   cylinder  round rod or dowel: diameter, length along an axis
//   tube      round tube: diameter and wall, length along an axis
//   box-tube  rectangular tube: section width × height and wall
//   angle     L-angle: leg width × height and wall (corner at the section minimum)
// Profile sections lie across the length axis: along x the section is z × y,
// along y it is x × z and along z it is x × y (width × height).
//
// Panels carry machining features on face A or B in the panel's own sheet frame:
// u runs along the length and v along the width, from the sheet's minimum corner.
// This is the frame the panel has lying on a CNC bed with face A up. Depth is
// measured from the face into the material; depth = thickness cuts through.
//
// Every part and feature belongs to a layer. A layer carries the CNC operation
// and its position in the list is the machining order. Parts and features on a
// locked layer cannot be edited, moved onto or removed.
import {MATERIALS} from './materials.mjs';
import {normaliseRotation,boxCorners,boundsOf} from './design-transform.mjs';
import {validateStock} from './stock.mjs';

export const DESIGN_LIMITS=Object.freeze({designs:32,parts:200,features:64,layers:32});
export const SHAPES=Object.freeze(['panel','bar','cylinder','tube','box-tube','angle']);
// Base forms offered in the piece library, with sensible starting sizes (mm).
export const PIECE_FORMS=Object.freeze({
  panel:{label:'Board / panel',short:'Panel',material:'birch',defaults:{orientation:'horizontal',length:600,width:400,thickness:18}},
  bar:{label:'Rectangular bar',short:'Bar',material:'beech',defaults:{axis:'y',length:700,width:45,height:45}},
  cylinder:{label:'Round rod',short:'Rod',material:'beech',defaults:{axis:'x',length:500,diameter:25}},
  tube:{label:'Round tube',short:'Tube',material:'steel',defaults:{axis:'y',length:700,diameter:25,wall:2}},
  'box-tube':{label:'Rectangular tube',short:'Box tube',material:'steel',defaults:{axis:'y',length:700,width:40,height:40,wall:2}},
  angle:{label:'L-angle',short:'Angle',material:'aluminium',defaults:{axis:'x',length:700,width:40,height:40,wall:3}},
});
export const SECTION_AXES=Object.freeze({x:['z','y'],y:['x','z'],z:['x','y']});
export const ROUND_SHAPES=Object.freeze(['cylinder','tube']);
export const materialColor=id=>'#'+MATERIALS[id].color.toString(16).padStart(6,'0');
export const LAYER_OPERATIONS=Object.freeze(['profile','drill','pocket','groove','none']);
export const FEATURE_TYPES=Object.freeze(['drill','pocket','groove']);
// Face A lies on the positive side of the thickness axis.
export const PANEL_ORIENTATIONS=Object.freeze({
  horizontal:{label:'Horizontal (shelf, top)',u:'x',v:'z',n:'y'},
  front:{label:'Front-facing (back, door)',u:'x',v:'y',n:'z'},
  side:{label:'Side-facing (side, divider)',u:'z',v:'y',n:'x'},
});
const ID=/^[a-zA-Z0-9_-]{1,64}$/;
export const round=value=>{const result=Number(value.toFixed(6));return result===0?0:result;};

function number(value,label,min,max) {
  if(typeof value!=='number' || !Number.isFinite(value) || value<min || value>max) throw Error(`${label} must be between ${min} and ${max} mm`);
  return round(value);
}
function text(value,label,max) {
  if(typeof value!=='string' || !value.trim() || value.trim().length>max) throw Error(`${label} needs 1–${max} characters`);
  return value.trim();
}
function uniqueIds(items,label) {
  const ids=new Set();
  for(const item of items) {
    if(!item || typeof item.id!=='string' || !ID.test(item.id) || ids.has(item.id)) throw Error(`${label} IDs must be unique`);
    ids.add(item.id);
  }
}

// ---- Geometry -------------------------------------------------------------
// World extent of a part along x, y and z.
export function partSize(part) {
  if(part.shape==='panel') {
    const axes=PANEL_ORIENTATIONS[part.orientation];
    return {[axes.u]:part.length,[axes.v]:part.width,[axes.n]:part.thickness};
  }
  const [a,b]=SECTION_AXES[part.axis],round=ROUND_SHAPES.includes(part.shape);
  return {[part.axis]:part.length,[a]:round?part.diameter:part.width,[b]:round?part.diameter:part.height};
}
// The piece's box in its own (unrotated) frame.
export function partBox(part) {
  const size=partSize(part);
  return {min:{x:part.x,y:part.y,z:part.z},max:{x:part.x+size.x,y:part.y+size.y,z:part.z+size.z},size};
}
export function partCentre(part) {
  const size=partSize(part);
  return {x:part.x+size.x/2,y:part.y+size.y/2,z:part.z+size.z/2};
}
export const partCorners=part=>boxCorners(partBox(part),part.rotation,partCentre(part));
// World-axis bounds of the piece, including its rotation.
export function partWorldBox(part) {
  const {min,max}=boundsOf(partCorners(part));
  return {min,max,size:{x:max.x-min.x,y:max.y-min.y,z:max.z-min.z}};
}
export function designBounds(design) {
  if(!design.parts.length) return {min:{x:0,y:0,z:0},max:{x:0,y:0,z:0},size:{x:0,y:0,z:0}};
  const boxes=design.parts.map(partWorldBox),min={},max={};
  for(const axis of ['x','y','z']) {min[axis]=Math.min(...boxes.map(b=>b.min[axis]));max[axis]=Math.max(...boxes.map(b=>b.max[axis]));}
  return {min,max,size:{x:max.x-min.x,y:max.y-min.y,z:max.z-min.z}};
}
// Panel edges can take drill holes: u0 and u1 are the ends at u = 0 and u = length,
// v0 and v1 the long edges at v = 0 and v = width. An edge hole drills inward from
// its edge, centred at `t` across the thickness (measured from face B).
export const EDGE_FACES=Object.freeze(['u0','u1','v0','v1']);
export const FEATURE_FACES=Object.freeze(['A','B',...EDGE_FACES]);
export const isEdgeFace=face=>EDGE_FACES.includes(face);


// ---- Validation -----------------------------------------------------------
// Optional cutting tool of a machining layer; unset values use the CAM
// defaults for the layer's operation (design-cam.mjs).
// Post-processors (machine dialects); see cam-export.mjs.
export const CAM_POSTS=Object.freeze(['grbl','linuxcnc','mach','fanuc','siemens','heidenhain']);
function validateTool(tool) {
  if(!tool||typeof tool!=='object'||Array.isArray(tool)) throw Error('Layer tool settings are invalid');
  const out={};
  if(tool.diameter!==undefined) out.diameter=number(tool.diameter,'Tool diameter',.1,50);
  if(tool.feed!==undefined) out.feed=number(tool.feed,'Feed rate (mm/min)',1,50000);
  if(tool.plunge!==undefined) out.plunge=number(tool.plunge,'Plunge rate (mm/min)',1,20000);
  if(tool.stepdown!==undefined) out.stepdown=number(tool.stepdown,'Step-down',.05,50);
  if(tool.rpm!==undefined) {if(!Number.isInteger(tool.rpm)||tool.rpm<0||tool.rpm>100000) throw Error('Spindle speed must be a whole number from 0 to 100000 rpm');out.rpm=tool.rpm;}
  return Object.keys(out).length?out:undefined;
}
function validateCam(cam) {
  if(!cam||typeof cam!=='object'||Array.isArray(cam)) throw Error('CAM settings are invalid');
  const out={};
  if(cam.post!==undefined) {if(!CAM_POSTS.includes(cam.post)) throw Error('Choose a listed post-processor');out.post=cam.post;}
  const choice=(key,values,label)=>{if(cam[key]!==undefined) {if(!values.includes(cam[key])) throw Error(label);out[key]=cam[key];}};
  const whole=(key,min,max,label)=>{if(cam[key]!==undefined) {if(!Number.isInteger(cam[key])||cam[key]<min||cam[key]>max) throw Error(label);out[key]=cam[key];}};
  choice('units',['mm','inch'],'Program units must be mm or inch');
  choice('arcs',['native','lines'],'Arcs must be native or lines');
  choice('lineNumbers',[true,false],'Line numbers must be on or off');
  choice('dwellMs',[true,false],'Dwell unit must be seconds or milliseconds');
  whole('programNumber',1,9999,'Program number must be a whole number from 1 to 9999');
  whole('tabs',0,16,'Tabs must be a whole number from 0 to 16 per part');
  if(cam.spinUpS!==undefined) {if(typeof cam.spinUpS!=='number'||!Number.isFinite(cam.spinUpS)||cam.spinUpS<0||cam.spinUpS>60) throw Error('Spindle start wait must be between 0 and 60 s');out.spinUpS=round(cam.spinUpS);}
  if(cam.tabWidth!==undefined) out.tabWidth=number(cam.tabWidth,'Tab width',1,50);
  if(cam.tabHeight!==undefined) out.tabHeight=number(cam.tabHeight,'Tab height',.2,20);
  if(cam.safeZ!==undefined) out.safeZ=number(cam.safeZ,'Safe Z',1,200);
  if(cam.throughMm!==undefined) out.throughMm=number(cam.throughMm,'Through-cut allowance',0,5);
  if(cam.direction!==undefined) {if(!['climb','conventional'].includes(cam.direction)) throw Error('Cutting direction must be climb or conventional');out.direction=cam.direction;}
  if(cam.stepoverPct!==undefined) {if(!Number.isInteger(cam.stepoverPct)||cam.stepoverPct<10||cam.stepoverPct>90) throw Error('Stepover must be a whole number from 10 to 90 %');out.stepoverPct=cam.stepoverPct;}
  return Object.keys(out).length?out:undefined;
}
function validateLayer(layer) {
  if(!LAYER_OPERATIONS.includes(layer.operation)) throw Error('Layer operation must be profile, drill, pocket, groove or none');
  if(typeof layer.color!=='string' || !/^#[0-9a-fA-F]{6}$/.test(layer.color)) throw Error('Layer colour must be #rrggbb');
  if(typeof layer.visible!=='boolean' || typeof layer.locked!=='boolean') throw Error('Layer visibility and lock must be true or false');
  const tool=layer.tool===undefined?undefined:validateTool(layer.tool);
  return {id:layer.id,name:text(layer.name,'Layer name',40),color:layer.color.toLowerCase(),visible:layer.visible,locked:layer.locked,operation:layer.operation,...(tool?{tool}:{})};
}
function validateFeature(feature,part,layers) {
  if(!FEATURE_TYPES.includes(feature.type)) throw Error('Feature type must be drill, pocket or groove');
  if(!FEATURE_FACES.includes(feature.face)) throw Error('Feature face must be A, B or an edge (u0, u1, v0, v1)');
  if(!layers.has(feature.layer)) throw Error('Feature layer does not exist');
  if(isEdgeFace(feature.face)) return validateEdgeHole(feature,part);
  const result={id:feature.id,type:feature.type,face:feature.face,layer:feature.layer,u:number(feature.u,'Feature u',-10000,10000),v:number(feature.v,'Feature v',-10000,10000),
    depth:number(feature.depth,'Feature depth',.01,part.thickness)};
  if(feature.type==='drill') {
    result.diameter=number(feature.diameter,'Drill diameter',.5,200);
    const r=result.diameter/2;
    if(result.u-r<-1e-9||result.u+r>part.length+1e-9||result.v-r<-1e-9||result.v+r>part.width+1e-9) throw Error('Drill hole must lie within the panel face');
  } else {
    result.length=number(feature.length,'Feature length',.5,part.length);
    result.width=number(feature.width,'Feature width',.5,part.width);
    if(result.u<-1e-9||result.u+result.length>part.length+1e-9||result.v<-1e-9||result.v+result.width>part.width+1e-9) throw Error(`${feature.type==='pocket'?'Pocket':'Groove'} must lie within the panel face`);
  }
  return result;
}
// Edge holes: the coordinate across the edge is fixed by the face; the hole must
// fit within the thickness and along the edge, and may go as deep as the panel.
function validateEdgeHole(feature,part) {
  if(feature.type!=='drill') throw Error('Only drill holes can go into panel edges');
  const alongU=feature.face[0]==='v',diameter=number(feature.diameter,'Drill diameter',.5,200),r=diameter/2;
  const t=number(feature.t,'Hole position across the thickness',0,part.thickness);
  if(t-r<-1e-9||t+r>part.thickness+1e-9) throw Error('Edge hole must fit within the panel thickness');
  const along=number(alongU?feature.u:feature.v,alongU?'Feature u':'Feature v',-10000,10000),edge=alongU?part.length:part.width;
  if(along-r<-1e-9||along+r>edge+1e-9) throw Error('Edge hole must lie along the edge');
  const u=feature.face==='u0'?0:feature.face==='u1'?part.length:along,v=feature.face==='v0'?0:feature.face==='v1'?part.width:along;
  const depth=number(feature.depth,'Feature depth',.01,alongU?part.width:part.length);
  return {id:feature.id,type:'drill',face:feature.face,layer:feature.layer,u,v,t,depth,diameter};
}
// Early designs stored solid blocks as width × height × depth; they load as
// rectangular bars along their longest side with exactly the same box.
function fromBlock(part) {
  const extent={x:part.width,y:part.height,z:part.depth},axis=['y','x','z'].reduce((best,k)=>extent[k]>extent[best]?k:best,'y');
  const [a,b]=SECTION_AXES[axis],{width:_w,height:_h,depth:_d,...rest}=part;
  return {...rest,shape:'bar',axis,length:extent[axis],width:extent[a],height:extent[b]};
}
function validatePart(input,layers) {
  const part=input?.shape==='block'?fromBlock(input):input;
  if(!SHAPES.includes(part.shape)) throw Error('Base form must be panel, bar, rod, tube, rectangular tube or angle');
  if(!MATERIALS[part.material]) throw Error('Choose a catalogue material');
  if(!layers.has(part.layer)) throw Error('Part layer does not exist');
  const color=part.color??materialColor(part.material);
  if(typeof color!=='string' || !/^#[0-9a-fA-F]{6}$/.test(color)) throw Error('Piece colour must be #rrggbb');
  const base={id:part.id,name:text(part.name,'Part name',60),shape:part.shape,material:part.material,color:color.toLowerCase(),layer:part.layer,
    x:number(part.x,'Position x',-10000,10000),y:number(part.y,'Position y',-10000,10000),z:number(part.z,'Position z',-10000,10000)};
  const rotation=normaliseRotation(part.rotation);
  if(rotation) base.rotation=rotation;
  if(part.shape==='panel') {
    if(!PANEL_ORIENTATIONS[part.orientation]) throw Error('Panel orientation must be horizontal, front or side');
    const panel={...base,orientation:part.orientation,length:number(part.length,'Panel length',1,5000),width:number(part.width,'Panel width',1,5000),thickness:number(part.thickness,'Panel thickness',1,200)};
    const features=part.features??[];
    if(!Array.isArray(features) || features.length>DESIGN_LIMITS.features) throw Error(`A panel supports at most ${DESIGN_LIMITS.features} features`);
    uniqueIds(features,'Feature');
    return {...panel,features:features.map(feature=>validateFeature(feature,panel,layers))};
  }
  if(part.features?.length) throw Error('Machining features are supported on panels');
  if(!['x','y','z'].includes(part.axis)) throw Error('Length direction must be x, y or z');
  const profile={...base,axis:part.axis,length:number(part.length,'Length',1,5000)};
  if(part.shape==='cylinder') return {...profile,diameter:number(part.diameter,'Diameter',1,1000)};
  if(part.shape==='tube') {
    const diameter=number(part.diameter,'Tube diameter',2,1000);
    return {...profile,diameter,wall:number(part.wall,'Tube wall',.2,diameter/2-.1)};
  }
  const min=part.shape==='bar'?1:2;
  const width=number(part.width,'Section width',min,1000),height=number(part.height,'Section height',min,1000);
  if(part.shape==='bar') return {...profile,width,height};
  const wallMax=part.shape==='angle'?Math.min(width,height)-.1:Math.min(width,height)/2-.1;
  return {...profile,width,height,wall:number(part.wall,part.shape==='angle'?'Angle wall':'Tube wall',.2,wallMax)};
}
export function validateDesign(design) {
  if(!design || typeof design.id!=='string' || !ID.test(design.id)) throw Error('Design ID is invalid');
  const name=text(design.name,'Design name',60);
  if(!Array.isArray(design.layers) || !design.layers.length || design.layers.length>DESIGN_LIMITS.layers) throw Error(`A design needs 1–${DESIGN_LIMITS.layers} layers`);
  uniqueIds(design.layers,'Layer');
  const layers=design.layers.map(validateLayer),names=new Set();
  for(const layer of layers) {if(names.has(layer.name.toLowerCase())) throw Error('Layer names must be unique');names.add(layer.name.toLowerCase());}
  if(!Array.isArray(design.parts) || design.parts.length>DESIGN_LIMITS.parts) throw Error(`A design supports at most ${DESIGN_LIMITS.parts} parts`);
  uniqueIds(design.parts,'Part');
  const layerIds=new Set(layers.map(layer=>layer.id)),featureIds=new Set();
  const parts=design.parts.map(part=>validatePart(part,layerIds));
  for(const feature of parts.flatMap(part=>part.features??[])) {if(featureIds.has(feature.id)) throw Error('Feature IDs must be unique');featureIds.add(feature.id);}
  const cam=design.cam===undefined?undefined:validateCam(design.cam);
  // Optional sheet stock for nesting the design's panels (defaults as for cabinets).
  if(design.stock!==undefined&&(!design.stock||typeof design.stock!=='object'||Array.isArray(design.stock))) throw Error('Design sheet stock is invalid');
  const stock=design.stock===undefined?undefined:validateStock(design.stock);
  return {id:design.id,name,layers,parts,...(cam?{cam}:{}),...(stock?{stock}:{})};
}
export function validateDesigns(value) {
  if(!Array.isArray(value) || value.length>DESIGN_LIMITS.designs) throw Error(`A project supports at most ${DESIGN_LIMITS.designs} furniture designs`);
  uniqueIds(value,'Design');
  return value.map(validateDesign);
}
