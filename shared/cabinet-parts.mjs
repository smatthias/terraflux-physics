import {panelEdgeBanding} from './edge-banding.mjs';
import {shelfJoinery} from './shelf-joinery.mjs';
import {cabinetCompartments} from './cabinet-compartments.mjs';
import {validateStock,panelStockFit} from './stock.mjs';
import {MATERIALS,WOOD_IDS,DEFAULT_WOOD,DEFAULT_METAL} from './materials.mjs';
// Panel materials ({label, color}) keyed by wood ID; see materials.mjs for the catalogue.
export const cabinetMaterials=Object.fromEntries(WOOD_IDS.map(id=>[id,{label:MATERIALS[id].label,color:MATERIALS[id].color}]));
const HANDLE_SIZE=[.015,.12,.025];
// Nominal 4 × 40 mm wood screw, modelled as a solid cylinder for weight estimates.
export const SCREW_SPEC=Object.freeze({label:'4 × 40 mm wood screw',diameterMm:4,lengthMm:40});
const SCREW_VOLUME=Math.PI*(SCREW_SPEC.diameterMm/2000)**2*SCREW_SPEC.lengthMm/1000;
export function cabinetParts(item) {
  const {width:w,height:h,depth:d,thickness:t}=item,parts=[],hardwareColor=MATERIALS[item.hardwareMaterial??DEFAULT_METAL].color;
  const add=(part,label,size,position,cut,color=0xb88c59)=>parts.push({part,label,size,position,cut,color});
  for(const sign of [-1,1]) add('side','Side',[t,h,d],[sign*(w/2-t/2),h/2,0],[h,d]);
  for(const y of [t/2,h-t/2]) add('horizontal','Top/bottom',[w-2*t,t,d],[0,y,0],[w-2*t,d]);
  add('back','Back',[w-2*t,h-2*t,t],[0,h/2,-d/2+t/2],[h-2*t,w-2*t]);
  const layout=cabinetCompartments(item);
  for(const x of layout.dividers)add('divider','Divider',[t,h-2*t,d-2*t],[x,h/2,0],[h-2*t,d-2*t]);
  for(const [index,bay] of layout.bays.entries())for(let n=1;n<=bay.shelves;n++)add(item.columnWeights?'shelf-column-'+(index+1):'shelf',item.columnWeights?'Shelf column '+(index+1):'Shelf',[bay.width,t,d-2*t],[bay.x,t+(h-2*t)*n/(bay.shelves+1),0],[bay.width,d-2*t]);
  if(item.columnFronts){
    for(const [index,bay] of layout.bays.entries()){
      if(bay.front==='open')continue;
      const count=bay.front==='double'?2:1,leafWidth=count===2?bay.width/2-.003:bay.width-.006;
      for(let leaf=0;leaf<count;leaf++){
        const x=bay.x+(count===2?(leaf===0?-1:1)*bay.width/4:0),sign=count===2?(leaf===0?1:-1):1;
        add('front-column-'+(index+1),'Front column '+(index+1),[leafWidth,h-2*t,t],[x,h/2,d/2-t/2],[h-2*t,leafWidth],0xd8b788);
        add('handle','Handle',HANDLE_SIZE,[x+sign*(leafWidth/2-Math.min(.04,leafWidth/3)),h*.55,d/2+.012],null,hardwareColor);
      }
    }
  }else{
  for(const sign of [-1,1]) {
    add('front','Front',[(w-2*t)/2-.003,h-2*t,t],[sign*(w-2*t)/4,h/2,d/2-t/2],[h-2*t,(w-2*t)/2-.003],0xd8b788);
    add('handle','Handle',HANDLE_SIZE,[sign*.04,h*.55,d/2+.012],null,hardwareColor);
  }
  }
  const material=cabinetMaterials[item.material??DEFAULT_WOOD];
  for(const part of parts) if(part.cut) part.color=material.color;
  return parts;
}
export function cabinetBom(item) {
  const stock=validateStock(item),joint=shelfJoinery(item),rows=[];let handles=0;
  for(const piece of cabinetParts(item)) {
    if(!piece.cut) {handles++;continue;}
    const [length,width]=piece.cut;
    const existing=rows.find(row=>row.part===piece.part && row.length===length && row.width===width);
    if(existing) existing.quantity++;
    else rows.push({part:piece.part,label:piece.label,quantity:1,length,width,thickness:item.thickness});
  }
  for(const row of rows) {row.area=row.quantity*row.length*row.width;row.volume=row.area*row.thickness;row.joineryLengthMm=row.part.startsWith('shelf')?2*joint.engagementMm:0;Object.assign(row,panelEdgeBanding(item,row));if(row.coreLength<=0||row.coreWidth<=0)throw Error('Edge banding leaves no usable panel core');row.blankLength=row.coreLength+(item.lengthAllowanceMm??0)/1000;row.blankWidth=row.coreWidth+(item.widthAllowanceMm??0)/1000;row.blankArea=row.quantity*row.blankLength*row.blankWidth;row.material=cabinetMaterials[item.material??'oak'].label;row.stockFit=panelStockFit(row.blankLength,row.blankWidth,stock);}
  return {stock,hardware:cabinetHardware(item,handles),allFit:rows.every(r=>r.stockFit!=='oversize'),oversizeCount:rows.filter(r=>r.stockFit==='oversize').reduce((n,r)=>n+r.quantity,0),blankArea:rows.reduce((n,r)=>n+r.blankArea,0),rows,handles,panelCount:rows.reduce((n,r)=>n+r.quantity,0),area:rows.reduce((n,r)=>n+r.area,0),volume:rows.reduce((n,r)=>n+r.volume,0)};
}
const csvCell=value=>{const text=String(value),safe=/^[=+\-@\t\r]/.test(text)?"'"+text:text;return '"'+safe.replace(/"/g,'""')+'"';};
export function cabinetBomCsv(item) {
  const bom=cabinetBom(item);
  const rows=[['cabinet','part','quantity','length_mm','width_mm','thickness_mm','area_m2','volume_m3','material','blank_length_mm','blank_width_mm','blank_area_m2','stock_length_mm','stock_width_mm','edge_trim_mm','rotation_allowed','stock_fit','shelf_joint','joinery_length_mm','core_length_mm','core_width_mm','edge_thickness_mm','edge_length_edges','edge_width_edges','band_length_m','finished_length_mm','finished_width_mm']];
  for(const row of bom.rows) rows.push([item.name,row.label,row.quantity,(row.length*1000).toFixed(3),(row.width*1000).toFixed(3),(row.thickness*1000).toFixed(3),row.area.toFixed(6),row.volume.toFixed(9),row.material,(row.blankLength*1000).toFixed(3),(row.blankWidth*1000).toFixed(3),row.blankArea.toFixed(6),bom.stock.stockLengthMm,bom.stock.stockWidthMm,bom.stock.stockTrimMm,bom.stock.stockAllowRotation,row.stockFit,shelfJoinery(item).type,row.joineryLengthMm,(row.coreLength*1000).toFixed(3),(row.coreWidth*1000).toFixed(3),row.edgeThicknessMm,row.edgeLengthEdges,row.edgeWidthEdges,row.bandLengthMetres.toFixed(3),(row.finishedLength*1000).toFixed(3),(row.finishedWidth*1000).toFixed(3)]);
  const metal=bom.hardware.materialLabel;
  rows.push([item.name,'Handle',bom.handles,'','','','','',metal,...Array(18).fill('')]);
  rows.push([item.name,'Screw '+SCREW_SPEC.label+' (estimate)',bom.hardware.screws,'','','','','',metal,...Array(18).fill('')]);
  return rows.map(row=>row.map(csvCell).join(',')).join('\r\n')+'\r\n';
}

// Screws are a planning estimate: screwsPerJoint (default 4) at each structural
// joint, meaning each top/bottom-to-side corner and each divider end. Shelves use
// their shelf joinery, and backs, hinges and fittings are not counted.
export function cabinetHardware(item,handles) {
  const material=item.hardwareMaterial??DEFAULT_METAL,screwsPerJoint=item.screwsPerJoint??4;
  const joints=4+2*cabinetCompartments(item).dividers.length;
  return {material,materialLabel:MATERIALS[material].label,handles,joints,screwsPerJoint,screws:joints*screwsPerJoint,screwSpec:SCREW_SPEC.label};
}
// Estimated mass in kg from design panel volumes and typical material densities.
export function cabinetMass(item) {
  const bom=cabinetBom(item),wood=MATERIALS[item.material??DEFAULT_WOOD],metal=MATERIALS[bom.hardware.material];
  const panelsKg=bom.volume*wood.densityKgM3;
  const handlesKg=bom.handles*HANDLE_SIZE.reduce((n,v)=>n*v,1)*metal.densityKgM3;
  const screwsKg=bom.hardware.screws*SCREW_VOLUME*metal.densityKgM3;
  return {panelsKg,handlesKg,screwsKg,totalKg:panelsKg+handlesKg+screwsKg,wood:wood.label,metal:metal.label};
}
