import {cabinetBom,cabinetMaterials} from './cabinet-parts.mjs';
import {validateStock} from './stock.mjs';
import {packPanels} from './nesting.mjs';
import {MATERIALS} from './materials.mjs';
import {PIECE_FORMS} from './furniture-design.mjs';
// Placed designs (room furniture of type 'design') are manufactured like
// cabinets: each of their panels is a blank of its length × width, nested with
// cabinet panels of the same material, thickness and stock (the design's sheet
// stock, or the default). Their machining comes from the designer's CNC
// programs. Other pieces (bars, rods, tubes, angles) are cut to length from
// profile stock and listed by profile (designProfiles). Projects without placed
// designs give exactly the same batches as before.
const placed=(furniture,designs)=>{
  const byId=new Map((designs??[]).map(d=>[d.id,d]));
  return furniture.filter(item=>item.type==='design'&&byId.has(item.designId)).sort((a,b)=>a.id<b.id?-1:a.id>b.id?1:0).map((item,index)=>({item,design:byId.get(item.designId),code:'D'+(index+1)}));
};
export function manufacturingBatches(furniture,designs=[]) {
  const groups=new Map();
  const cabinets=furniture.filter(item=>item.type!=='design').sort((a,b)=>a.id<b.id?-1:a.id>b.id?1:0);
  const group=(material,label,thickness,stock)=>{
    const key=JSON.stringify([material,thickness,stock.stockLengthMm,stock.stockWidthMm,stock.stockTrimMm,stock.stockAllowRotation,stock.stockKerfMm]);
    if(!groups.has(key))groups.set(key,{key,material,materialLabel:label,thickness,stock,cabinets:[],panels:[]});
    return groups.get(key);
  };
  cabinets.forEach((item,index)=>{
    const stock=validateStock(item),material=item.material??'oak',target=group(material,cabinetMaterials[material].label,item.thickness,stock);
    target.cabinets.push({id:item.id,name:item.name,code:'C'+(index+1)});
    for(const row of cabinetBom(item).rows)for(let n=1;n<=row.quantity;n++){
      const part=row.part+'-'+n;
      target.panels.push({id:item.id+':'+part,shortId:'C'+(index+1)+':'+part,cabinetId:item.id,cabinetName:item.name,part,label:row.label,length:row.blankLength*1000,width:row.blankWidth*1000});
    }
  });
  for(const {item,design,code} of placed(furniture,designs)){
    const stock=validateStock(design.stock??{}),seen=new Set();
    design.parts.filter(part=>part.shape==='panel').forEach((part,index)=>{
      const target=group(part.material,MATERIALS[part.material].label,part.thickness/1000,stock);
      if(!seen.has(target)){target.cabinets.push({id:item.id,name:item.name,code,kind:'design',designId:design.id});seen.add(target);}
      target.panels.push({id:item.id+':'+part.id,shortId:code+':'+(index+1),cabinetId:item.id,cabinetName:item.name,part:part.name,label:part.name,length:part.length,width:part.width});
    });
  }
  return [...groups.values()].sort((a,b)=>a.key<b.key?-1:1).map((group,i)=>{
    const layout=packPanels(group.panels,group.stock);
    return {number:i+1,material:group.material,materialLabel:group.materialLabel,thickness:group.thickness,cabinets:group.cabinets,panelCount:group.panels.length,layout};
  });
}
// Profile pieces of placed designs, grouped by material, form and section.
const SECTION={cylinder:p=>[`d${p.diameter}`,`Ø${p.diameter} mm`],tube:p=>[`d${p.diameter}x${p.wall}`,`Ø${p.diameter} × ${p.wall} mm`],
  bar:p=>[`${p.width}x${p.height}`,`${p.width} × ${p.height} mm`],'box-tube':p=>[`${p.width}x${p.height}x${p.wall}`,`${p.width} × ${p.height} × ${p.wall} mm`],
  angle:p=>[`${p.width}x${p.height}x${p.wall}`,`${p.width} × ${p.height} × ${p.wall} mm`]};
export const profileKey=part=>`${part.material}|${part.shape}|${SECTION[part.shape](part)[0]}`;
export function designProfiles(furniture,designs=[]) {
  const groups=new Map();
  for(const {item,design,code} of placed(furniture,designs)) for(const part of design.parts.filter(p=>p.shape!=='panel')){
    const key=profileKey(part);
    if(!groups.has(key))groups.set(key,{key,material:part.material,shape:part.shape,label:`${MATERIALS[part.material].label} ${PIECE_FORMS[part.shape].label.toLowerCase()} ${SECTION[part.shape](part)[1]}`,pieces:[]});
    groups.get(key).pieces.push({id:item.id+':'+part.id,code,itemId:item.id,itemName:item.name,part:part.name,lengthMm:part.length});
  }
  return [...groups.values()].sort((a,b)=>a.key<b.key?-1:1).map(g=>({...g,count:g.pieces.length,totalLengthMm:Math.round(g.pieces.reduce((n,p)=>n+p.lengthMm,0)*1000)/1000}));
}
const cell=value=>'"'+String(value).replace(/^[=+\-@\t\r]/,"'$&").replace(/"/g,'""')+'"';
export function manufacturingCsv(furniture,designs=[]) {
  const rows=[['batch','material','thickness_mm','cabinet_id','cabinet','panel','sheet','x_mm','y_mm','width_mm','height_mm','rotated','status','stock_length_mm','stock_width_mm','trim_mm','kerf_mm','rotation_allowed']];
  for(const b of manufacturingBatches(furniture,designs)){
    const s=b.layout.stock;
    const add=(p,sheet,status)=>rows.push([b.number,b.materialLabel,(b.thickness*1000).toFixed(3),p.cabinetId,p.cabinetName,p.part,sheet,p.x?.toFixed(3)??'',p.y?.toFixed(3)??'',(status==='placed'?p.width:p.length).toFixed(3),(status==='placed'?p.height:p.width).toFixed(3),p.rotated??'',status,s.stockLengthMm,s.stockWidthMm,s.stockTrimMm,s.stockKerfMm,s.stockAllowRotation]);
    for(const sheet of b.layout.sheets)for(const p of sheet.panels)add(p,sheet.number,'placed');
    for(const p of b.layout.unplaced)add(p,'','oversize');
  }
  // Profile pieces: width_mm is the cut length; the panel column names the piece and section.
  for(const g of designProfiles(furniture,designs))for(const p of g.pieces)rows.push(['',g.label,'',p.itemId,p.itemName,p.part,'','','',p.lengthMm.toFixed(3),'','','profile','','','','','']);
  return rows.map(r=>r.map(cell).join(',')).join('\r\n')+'\r\n';
}
