import {edgeBandingNote} from './edge-banding.mjs';
import {shelfJoineryNote} from './shelf-joinery.mjs';
import {cabinetElevationSvg} from './cabinet-elevation.mjs';
import {cabinetCompartments} from './cabinet-compartments.mjs';
import {escapeXml as escape} from './plan-svg.mjs';
export function cabinetDrawingSheets(item,{paper='A4',orientation='landscape'}={}){
 if(!['A4','A3'].includes(paper)||!['landscape','portrait'].includes(orientation))throw Error('Choose A4 or A3 and portrait or landscape');
 const drawing=cabinetElevationSvg(item),layout=cabinetCompartments(item);
 // Consume the controlled, shared elevation groups so sheet geometry matches SVG export.
 const views=[...drawing.matchAll(/<g data-view="([^"]+)"([^>]*)>([\s\S]*?)<\/g>/g)];
 if(views.length!==layout.columns+2)throw Error('Cabinet drawing views are incomplete');
 const short=paper==='A4'?210:297,long=paper==='A4'?297:420,width=orientation==='landscape'?long:short,height=orientation==='landscape'?short:long;
 const pages=views.map(([unused,view,attributes,body],index)=>{
  const bayNumber=view==='side'?Number(attributes.match(/data-side-bay="(\d+)"/)[1]):null;
  const bay=bayNumber?layout.bays[bayNumber-1]:null;
  const title=view==='interior'?'Interior elevation · fronts hidden':view==='front'?'Closed-front elevation':`C${bayNumber} · side cutaway`;
  const span=(view==='side'?item.depth:item.width)*1000,h=item.height*1000;
  const centres=bay?Array.from({length:bay.shelves},(_,n)=>(1000*(item.thickness+(item.height-2*item.thickness)*(n+1)/(bay.shelves+1))).toFixed(1)):[];
  const detail=bay?`Clear bay width ${(bay.width*1000).toFixed(1)} mm · front ${escape(bay.front??'legacy doors')} · ${bay.shelves} shelves. Shelf centres above cabinet bottom (mm): ${centres.join(', ')||'none'}.`:`Clear bay widths (mm): ${layout.bays.map(b=>(b.width*1000).toFixed(1)).join(' / ')}. Shelf counts: ${layout.bays.map(b=>b.shelves).join(' / ')}.`;
  return `<section class="page" data-sheet="${index+1}"><header><h1>${escape(item.name)}</h1><h2>${title}</h2><p>${paper} ${orientation} · dimensions in mm · width ${(item.width*1000).toFixed(1)} × height ${h.toFixed(1)} × depth ${(item.depth*1000).toFixed(1)} · panel ${(item.thickness*1000).toFixed(1)}</p></header><svg xmlns="http://www.w3.org/2000/svg" role="img" aria-label="${title}" viewBox="-120 -140 ${span+240} ${h+210}"><title>${title}</title>${body}</svg><p class="detail">${detail} ${escape(shelfJoineryNote(item))} ${escape(edgeBandingNote(item))}</p><p class="notice">Scaled to fit; no physical drawing scale. Use written dimensions. Applied design geometry only; verify joinery and machining allowances before production.${bay?' Side panels, dividers and handles omitted from cutaway. Back at left, front at right.':''}</p><footer>TerraTec · Cabinet drawing sheets · ${index+1} / ${views.length}</footer></section>`;
 });
 return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${escape(item.name)} · Cabinet drawing sheets</title><style>@page{size:${paper} ${orientation};margin:12mm}*{box-sizing:border-box}body{margin:0;background:#e6eae5;color:#183b34;font:11px Arial,sans-serif}.page{width:${width-24}mm;height:${height-24}mm;margin:8mm auto;padding:0;background:white;display:flex;flex-direction:column;break-after:page}.page:last-child{break-after:auto}header{height:29mm;flex-shrink:0}h1{font-size:16px;line-height:1.15;margin:0 0 2mm;overflow-wrap:anywhere}h2{font-size:13px;margin:0 0 2mm}p{margin:1mm 0;overflow-wrap:anywhere}.page>svg{width:100%;height:${height-104}mm;flex-shrink:0;display:block}.detail{min-height:13mm;font-size:11px}.notice{font-size:10px;line-height:1.3}footer{margin-top:auto;border-top:1px solid #b5c5ba;padding-top:2mm;font-size:10px}@media print{body{background:white}.page{margin:0}}@media screen and (max-width:800px){.page{max-width:100%;height:auto;min-height:${height-24}mm}}</style></head><body>${pages.join('')}</body></html>`;
}
