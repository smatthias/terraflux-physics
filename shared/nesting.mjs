import {validateStock} from './stock.mjs';
import {cabinetBom} from './cabinet-parts.mjs';
// Deterministic guillotine heuristic. Coordinates are millimetres from sheet top-left.
export function packPanels(panels,settings) {
  const stock=validateStock(settings),kerf=stock.stockKerfMm,trim=stock.stockTrimMm;
  const width=stock.stockLengthMm-2*trim,height=stock.stockWidthMm-2*trim;
  const sheets=[],unplaced=[];
  const ordered=[...panels].sort((a,b)=>b.length*b.width-a.length*a.width || b.length-a.length || a.id.localeCompare(b.id));
  for(const panel of ordered) {
    if(![panel.length,panel.width].every(v=>Number.isFinite(v)&&v>0)) throw Error('Invalid panel blank dimensions');
    const options=[{width:panel.length,height:panel.width,rotated:false}];
    if(stock.stockAllowRotation)options.push({width:panel.width,height:panel.length,rotated:true});
    if(!options.some(o=>o.width<=width+1e-7&&o.height<=height+1e-7)){unplaced.push(panel);continue;}
    let choice;
    const find=sheet=>{
      for(let i=0;i<sheet.free.length;i++)for(const o of options){const r=sheet.free[i];
        if(o.width<=r.width+1e-7&&o.height<=r.height+1e-7){const score=r.width*r.height-o.width*o.height;
          if(!choice||score<choice.score)choice={sheet,index:i,rect:r,...o,score};
        }
      }
    };
    for(const sheet of sheets)find(sheet);
    if(!choice){const sheet={number:sheets.length+1,panels:[],free:[{x:trim,y:trim,width,height}]};sheets.push(sheet);find(sheet);}
    const c=choice,r=c.rect;
    c.sheet.panels.push({...panel,x:r.x,y:r.y,width:c.width,height:c.height,rotated:c.rotated});
    c.sheet.free.splice(c.index,1);
    const right=r.width-c.width-kerf,bottom=r.height-c.height-kerf;
    if(right>1e-7)c.sheet.free.push({x:r.x+c.width+kerf,y:r.y,width:right,height:c.height});
    if(bottom>1e-7)c.sheet.free.push({x:r.x,y:r.y+c.height+kerf,width:r.width,height:bottom});
  }
  const usedArea=sheets.flatMap(s=>s.panels).reduce((sum,p)=>sum+p.width*p.height,0);
  return {stock,sheets:sheets.map(({free,...s})=>s),unplaced,utilisation:sheets.length?usedArea/(sheets.length*stock.stockLengthMm*stock.stockWidthMm):0};
}
export function nestCabinet(item) {
  const panels=cabinetBom(item).rows.flatMap(r=>Array.from({length:r.quantity},(_,i)=>({id:r.part+'-'+(i+1),label:r.label,length:r.blankLength*1000,width:r.blankWidth*1000})));
  return packPanels(panels,item);
}
const cell=value=>'"'+String(value).replace(/^[=+\-@\t\r]/,"'$&").replace(/"/g,'""')+'"';
export function nestingCsv(item) {
  const layout=nestCabinet(item),rows=[['cabinet','panel','sheet','x_mm','y_mm','blank_length_mm','blank_width_mm','rotated','status','kerf_mm']];
  for(const sheet of layout.sheets)for(const p of sheet.panels)rows.push([item.name,p.id,sheet.number,p.x.toFixed(3),p.y.toFixed(3),p.width.toFixed(3),p.height.toFixed(3),p.rotated,'placed',layout.stock.stockKerfMm]);
  for(const p of layout.unplaced)rows.push([item.name,p.id,'','','',p.length.toFixed(3),p.width.toFixed(3),'','oversize',layout.stock.stockKerfMm]);
  return rows.map(r=>r.map(cell).join(',')).join('\r\n')+'\r\n';
}
