// Design edits made on the workshop benches (DrillingBench.vue, CuttingBench.vue):
// adding machining to a panel and sawing a piece in two. Each returns the new,
// validated design; the designer commits it as one undo step.
import {validateDesign,partSize,partCentre,round,FEATURE_TYPES,PANEL_ORIENTATIONS,SECTION_AXES,materialColor} from './furniture-design.mjs';
import {rotationMatrix} from './design-transform.mjs';

const copy=value=>JSON.parse(JSON.stringify(value));
function findPart(design,partId) {const part=design.parts.find(p=>p.id===partId);if(!part) throw Error('Part not found');return part;}
function assertEditable(design,...layerIds) {for(const id of layerIds) {const layer=design.layers.find(l=>l.id===id);if(layer?.locked) throw Error(`Layer “${layer.name}” is locked`);}}
// The layer for new machining of a type: the first unlocked layer with that operation.
const layerFor=(design,operation)=>(design.layers.find(l=>l.operation===operation&&!l.locked)??design.layers.find(l=>!l.locked)??design.layers[0]).id;
const withParts=(design,parts)=>validateDesign({...design,parts});

// Adds machining to a panel in one edit (one undo step), each feature on the
// layer for its type. Holes are {type:'drill', face, u, v, t, diameter, depth};
// pockets and grooves {type, face, u, v, length, width, depth}.
// `layerId` puts the features on that layer instead of the one for their operation.
export function addMachining(design,partId,features,makeId,verb='machined',layerId=null) {
  const part=findPart(design,partId);
  if(part.shape!=='panel') throw Error(`Only panels can be ${verb}`);
  if(!Array.isArray(features)||!features.length) throw Error('Nothing to machine');
  if(layerId&&!design.layers.some(l=>l.id===layerId)) throw Error('Choose a layer of this design');
  const added=features.map(f=>{
    if(!FEATURE_TYPES.includes(f.type)) throw Error('Feature type must be drill, pocket or groove');
    const layer=layerId||layerFor(design,f.type);assertEditable(design,part.layer,layer);
    return f.type==='drill'?{id:makeId(),type:'drill',layer,face:f.face,u:f.u??0,v:f.v??0,...(f.t!==undefined?{t:f.t}:{}),diameter:f.diameter,depth:f.depth}
      :{id:makeId(),type:f.type,layer,face:f.face,u:f.u,v:f.v,length:f.length,width:f.width,depth:f.depth};
  });
  return {design:withParts(design,design.parts.map(p=>p.id===partId?{...p,features:[...p.features,...added]}:p)),featureIds:added.map(f=>f.id)};
}

// ---- Sawing a piece in two
// Cuts across a piece's length (any piece) or along it (panels: across the
// width) at `at` mm from its start, removing `kerf` mm. The piece keeps its ID,
// name and the part from 0 to `at`; the rest (if at least 1 mm) becomes a new
// piece in its place, named “name 2” (or the next free number). Both stay
// where they were, also when turned. Machining follows the material: holes go
// with the side they are on (a cut through a hole is refused), pockets and
// grooves are cut with it.
const nextName=(design,name)=>{for(let n=2;;n++) {const candidate=`${name.slice(0,56)} ${n}`;if(!design.parts.some(p=>p.name===candidate)) return candidate;}};
function splitFeatures(part,sheet,cut0,cut1,rest,makeId) {
  const size=sheet==='u'?'length':'width',start=sheet+'0',end=sheet+'1',first=[],second=[];
  for(const f of part.features) {
    if(f.face===start) {first.push(f);continue;}
    if(f.face===end) {if(rest>=1) second.push({...f,id:makeId(),[sheet]:rest});continue;}
    if(f.type==='drill') {
      const c=f[sheet],r=f.diameter/2;
      if(c+r<=cut0+1e-9) first.push(f);
      else if(c-r>=cut1-1e-9) {if(rest>=1) second.push({...f,id:makeId(),[sheet]:round(c-cut1)});}
      else throw Error(`The cut goes through the Ø${f.diameter} hole at ${sheet} ${c}; move the cut or remove the hole`);
      continue;
    }
    const lo=f[sheet],hi=lo+f[size];
    if(Math.min(hi,cut0)-lo>=.5) first.push({...f,[size]:round(Math.min(hi,cut0)-lo)});
    if(rest>=1&&hi-Math.max(lo,cut1)>=.5) second.push({...f,id:makeId(),[sheet]:round(Math.max(lo,cut1)-cut1),[size]:round(hi-Math.max(lo,cut1))});
  }
  return {first,second};
}
// A piece cut from `part` with a new size, placed so it stays where that stretch was.
function placeStretch(part,piece,axis,offset) {
  const min={x:part.x,y:part.y,z:part.z};min[axis]+=offset;
  if(!part.rotation) return {...piece,...Object.fromEntries(Object.entries(min).map(([k,v])=>[k,round(v)]))};
  const m=rotationMatrix(part.rotation),c=partCentre(part),size=partSize(piece);
  const local={x:min.x+size.x/2-c.x,y:min.y+size.y/2-c.y,z:min.z+size.z/2-c.z};
  const world={x:c.x+m[0][0]*local.x+m[0][1]*local.y+m[0][2]*local.z,y:c.y+m[1][0]*local.x+m[1][1]*local.y+m[1][2]*local.z,z:c.z+m[2][0]*local.x+m[2][1]*local.y+m[2][2]*local.z};
  return {...piece,x:round(world.x-size.x/2),y:round(world.y-size.y/2),z:round(world.z-size.z/2)};
}
export function splitPart(design,partId,{along='length',at,kerf=0},makeId) {
  const part=findPart(design,partId);
  assertEditable(design,part.layer);
  if(along!=='length'&&!(along==='width'&&part.shape==='panel')) throw Error('Profiles are cut across their length');
  if(![at,kerf].every(v=>typeof v==='number'&&Number.isFinite(v))||kerf<0||kerf>50) throw Error('Enter the cut position and a kerf of 0–50 mm');
  const total=part[along];
  if(at<1) throw Error('Cut at least 1 mm from the start');
  if(at>=total-1e-9) throw Error('The cut is beyond the end of the piece');
  const rest=round(total-at-kerf),axis=part.shape==='panel'?PANEL_ORIENTATIONS[part.orientation][along==='length'?'u':'v']:part.axis;
  const {first:keep,second:moved}=part.shape==='panel'?splitFeatures(part,along==='length'?'u':'v',at,at+kerf,rest,makeId):{first:[],second:[]};
  const first=placeStretch(part,{...part,[along]:round(at),...(part.shape==='panel'?{features:keep}:{})},axis,0);
  const parts=design.parts.flatMap(p=>{
    if(p.id!==partId) return [p];
    if(rest<1) return [first];
    const second=placeStretch(part,{...copy(part),id:makeId(),name:nextName(design,part.name),[along]:rest,...(part.shape==='panel'?{features:moved}:{})},axis,at+kerf);
    return [first,second];
  });
  const next=withParts(design,parts);
  return {design:next,partIds:next.parts.length>design.parts.length?[partId,parts[parts.findIndex(p=>p.id===partId)+1].id]:[partId]};
}
// Drills holes into a panel in one edit: each hole is {face, u, v, t, diameter, depth}.
export function drillHoles(design,partId,holes,makeId,layerId=null) {
  if(!Array.isArray(holes)||!holes.length) throw Error('Nothing to drill');
  return addMachining(design,partId,holes.map(h=>({...h,type:'drill'})),makeId,'drilled',layerId);
}
// ---- Layers from a bench: show or hide a layer.
export function showLayer(design,layerId,visible) {
  if(!design.layers.some(l=>l.id===layerId)) throw Error('Choose a layer of this design');
  return validateDesign({...design,layers:design.layers.map(l=>l.id===layerId?{...l,visible:!!visible}:l)});
}

// ---- Planing: taking a shaving off one side of a piece
// Panels: face A or B (thickness), edge v0 or v1 (width), end u0 or u1 (length).
// Bars: side width0/width1, height0/height1 (section) or end length0/length1;
// other profiles: their ends only. The piece loses `amount` mm on that side and
// stays in place. Machining on the planed side gets shallower (and is gone once
// planed away); through holes and grooves from the other side stay through;
// pockets and grooves running off the planed edge are shortened. Holes that the
// plane would cut into are refused. At least 1 mm must remain.
export const PLANE_SIDES=Object.freeze({
  panel:['A','B','v0','v1','u0','u1'],bar:['width0','width1','height0','height1','length0','length1'],other:['length0','length1'],
});
export const planeSides=part=>PLANE_SIDES[part.shape==='panel'?'panel':part.shape==='bar'?'bar':'other'];
function planeFeatures(part,sheet,x,fromStart,size,newSize) {
  const planed=[],dropped=[];
  if(sheet==='n') {
    const face=fromStart?'B':'A',other=fromStart?'A':'B';
    for(const f of part.features) {
      if(f.face===face) {const depth=round(f.depth-x);if(depth>.009) planed.push({...f,depth});else dropped.push(f);continue;}
      if(f.face===other) {planed.push({...f,depth:round(Math.min(f.depth,newSize))});continue;}
      const t=round(fromStart?f.t-x:f.t),r=f.diameter/2;
      if(t-r<-1e-9||t+r>newSize+1e-9) throw Error(`Planing ${x} mm off face ${face} would cut into the Ø${f.diameter} edge hole; plane less or remove the hole`);
      planed.push({...f,t});
    }
    return {planed,dropped};
  }
  const key=sheet==='u'?'length':'width',edge=sheet+(fromStart?'0':'1'),opposite=sheet+(fromStart?'1':'0');
  for(const f of part.features) {
    if(f.face===edge) {const depth=round(f.depth-x);if(depth>.009) planed.push({...f,depth});else dropped.push(f);continue;}
    if(f.face===opposite) {planed.push({...f,depth:round(Math.min(f.depth,newSize))});continue;}
    if(f.type==='drill') {
      const c=fromStart?round(f[sheet]-x):f[sheet],r=f.diameter/2;
      if(c-r<-1e-9||c+r>newSize+1e-9) throw Error(`Planing ${x} mm off ${edge} would cut into the Ø${f.diameter} hole at ${sheet} ${f[sheet]}; plane less or remove the hole`);
      planed.push({...f,[sheet]:c});continue;
    }
    const lo=fromStart?Math.max(f[sheet]-x,0):f[sheet],hi=fromStart?f[sheet]+f[key]-x:Math.min(f[sheet]+f[key],newSize);
    if(hi-lo>=.5) planed.push({...f,[sheet]:round(lo),[key]:round(hi-lo)});else dropped.push(f);
  }
  return {planed,dropped};
}
export function planePiece(design,partId,side,amount) {
  const part=findPart(design,partId);
  assertEditable(design,part.layer);
  if(!planeSides(part).includes(side)) throw Error(part.shape==='panel'?'Plane face A or B, an edge or an end':part.shape==='bar'?'Plane a side or an end of the bar':'Only the ends of this piece can be planed');
  if(typeof amount!=='number'||!Number.isFinite(amount)||amount<=0) throw Error('Enter how much to plane off, in mm');
  let sheet,key,axis,fromStart;
  if(part.shape==='panel') {
    sheet={A:'n',B:'n'}[side]??side[0];key={n:'thickness',u:'length',v:'width'}[sheet];axis=PANEL_ORIENTATIONS[part.orientation][sheet];fromStart=side==='B'||side.endsWith('0');
  } else {
    key=side.slice(0,-1);fromStart=side.endsWith('0');axis=key==='length'?part.axis:SECTION_AXES[part.axis][key==='width'?0:1];
  }
  const newSize=round(part[key]-amount);
  if(newSize<1) throw Error(`Planing ${amount} mm would leave less than 1 mm`);
  let features,dropped=[];
  if(part.shape==='panel') ({planed:features,dropped}=planeFeatures(part,sheet,amount,fromStart,part[key],newSize));
  const piece=placeStretch(part,{...part,[key]:newSize,...(features?{features}:{})},axis,fromStart?amount:0);
  return {design:withParts(design,design.parts.map(p=>p.id===partId?piece:p)),removed:dropped.length,size:newSize,dimension:key};
}

// ---- Finishing: the piece's colour (null: back to the material colour).
export function finishPiece(design,partId,color) {
  const part=findPart(design,partId);
  assertEditable(design,part.layer);
  return withParts(design,design.parts.map(p=>p.id===partId?{...p,color:color??materialColor(p.material)}:p));
}
// ---- Hand saw: cut a piece to length (or a panel to width) from its start,
// keeping or discarding the offcut.
export function cutToSize(design,partId,{along='length',size,kerf=2,keepOffcut=false},makeId) {
  const result=splitPart(design,partId,{along,at:size,kerf},makeId);
  if(keepOffcut||result.partIds.length<2) return result;
  return {design:withParts(result.design,result.design.parts.filter(p=>p.id!==result.partIds[1])),partIds:[partId]};
}
// ---- Router and shoulder planes: change a groove's or pocket's depth or extent.
export function updateMachining(design,partId,featureId,changes) {
  const part=findPart(design,partId),feature=part.features?.find(f=>f.id===featureId);
  if(!feature) throw Error('Choose a groove or pocket on this piece');
  assertEditable(design,part.layer,feature.layer);
  return withParts(design,design.parts.map(p=>p.id===partId?{...p,features:p.features.map(f=>f.id===featureId?{...f,...changes,id:f.id,type:f.type,face:f.face,layer:f.layer}:f)}:p));
}
