import {validateFurniture} from './furniture.mjs';
import {rectangle} from './geometry.mjs';
import {cabinetCompartments} from './cabinet-compartments.mjs';
import {cabinetParts} from './cabinet-parts.mjs';
import {escapeXml as escape} from './plan-svg.mjs';
export function cabinetElevationSvg(input){
 const [item]=validateFurniture([{...input,id:'elevation',x:0,z:0,rotation:0}],rectangle(50,50),10);
 const layout=cabinetCompartments(item),parts=cabinetParts(item),w=item.width*1000,h=item.height*1000,d=item.depth*1000,t=item.thickness*1000;
 const font=Math.max(8,Math.min(28,Math.max(w,h)/65,...layout.bays.map(b=>b.width*200))),margin=180,gap=200;
 const width=Math.max(2*w+2*margin+gap,layout.columns*(d+gap)+2*margin),height=2*h+1300+layout.columns*55;
 const text=(x,y,value,extra='')=>`<text x="${x}" y="${y}" font-family="sans-serif" font-size="${font}" fill="#183b34" ${extra}>${escape(value)}</text>`;
 const line=(x1,y1,x2,y2)=>`<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="#38554c" stroke-width="1.5"/>`;
 function horizontal(x1,x2,y,label){return line(x1,y,x2,y)+line(x1,y-8,x1,y+8)+line(x2,y-8,x2,y+8)+text((x1+x2)/2,y-10,label,'text-anchor="middle"');}
 function drawing(fronts){
  let body=`<rect x="0" y="0" width="${w}" height="${h}" fill="#f3eee2" stroke="#38554c" stroke-width="2"/>`;
  for(const part of parts){
   if(part.part==='back'||(!fronts&&(part.part.startsWith('front')||part.part==='handle')))continue;
   const [pw,ph]=part.size,[x,y]=part.position;
   body+=`<rect data-part="${escape(part.part)}" x="${(x+item.width/2-pw/2)*1000}" y="${(item.height-y-ph/2)*1000}" width="${pw*1000}" height="${ph*1000}" fill="${part.part.startsWith('front')?'#d0b481':part.part==='handle'?'#414940':'#b99562'}" stroke="#5a654f" stroke-width="1"/>`;
  }
  body+=horizontal(0,w,-100,w.toFixed(1)+' mm');
  layout.bays.forEach((bay,i)=>{const x=(bay.x+item.width/2-bay.width/2)*1000;body+=horizontal(x,x+bay.width*1000,-40,(bay.width*1000).toFixed(1));body+=text((bay.x+item.width/2)*1000,h+40,'C'+(i+1),'text-anchor="middle"');});
  body+=line(-65,0,-65,h)+line(-73,0,-57,0)+line(-73,h,-57,h)+`<text transform="translate(-85 ${h/2}) rotate(-90)" text-anchor="middle" font-family="sans-serif" font-size="${font}">${h.toFixed(1)} mm</text>`;
  return body;
 }
 const sideSections=layout.bays.map((bay,i)=>{
  const hasFront=bay.front!=='open',shelves=parts.filter(p=>p.part.startsWith('shelf')&&Math.abs(p.position[0]-bay.x)<1e-10);
  const rect=(part,x,y,pw,ph)=>`<rect data-side-part="${part}" x="${x}" y="${y}" width="${pw}" height="${ph}" fill="${part==='front'?'#d0b481':'#b99562'}" stroke="#5a654f" stroke-width="1"/>`;
  let body=`<rect width="${d}" height="${h}" fill="#f3eee2" stroke="#38554c" stroke-width="1"/>`;
  body+=rect('top',0,0,d,t)+rect('bottom',0,h-t,d,t)+rect('back',0,t,t,h-2*t);
  if(hasFront)body+=rect('front',d-t,t,t,h-2*t);
  for(const shelf of shelves)body+=rect('shelf',(shelf.position[2]+item.depth/2-shelf.size[2]/2)*1000,(item.height-shelf.position[1]-shelf.size[1]/2)*1000,shelf.size[2]*1000,shelf.size[1]*1000);
  body+=horizontal(0,d,-100,d.toFixed(1)+' mm');
  body+=horizontal(t,d-t,-40,(d-2*t).toFixed(1)+' clear');
  body+=text(d/2,h+40,'C'+(i+1)+' · '+(bay.front??'legacy doors'),'text-anchor="middle"');
  return `<g data-view="side" data-side-bay="${i+1}" transform="translate(${margin+i*(d+gap)} ${h+700})">${body}</g>`;
 }).join('');
 const register=layout.bays.map((bay,i)=>{
  const centres=Array.from({length:bay.shelves},(_,n)=>(1000*(item.thickness+(item.height-2*item.thickness)*(n+1)/(bay.shelves+1))).toFixed(1));
  const label=`C${i+1}: clear width ${(bay.width*1000).toFixed(1)} mm; shelves ${bay.shelves}; shelf centres above cabinet bottom (mm): ${centres.join(', ')||'none'}; front ${bay.front??'legacy full-cabinet doors'}`;
  return text(margin,2*h+1100+i*55,label,`textLength="${Math.min(label.length*font*.55,width-2*margin)}" lengthAdjust="spacingAndGlyphs" data-bay-register="${i+1}"`);
 }).join('');
 return `<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" role="img" viewBox="0 0 ${width} ${height}" width="1600" height="${Math.round(1600*height/width)}"><title>${escape(item.name)} · Cabinet elevations</title><metadata>Model drawing coordinates are millimetres. Scaled to fit; no physical print scale. Dimensions describe applied design geometry, not machining allowances or joinery.</metadata><rect width="100%" height="100%" fill="white"/>${text(margin,40,item.name+' · Cabinet elevations')}${text(margin,85,`Width ${w.toFixed(1)} · height ${h.toFixed(1)} · depth ${(item.depth*1000).toFixed(1)} · panel ${(item.thickness*1000).toFixed(1)} mm`)}${text(margin,125,'Interior · fronts hidden')}${text(margin+w+gap,125,'Closed front view')}<g data-view="interior" transform="translate(${margin} 300)">${drawing(false)}</g><g data-view="front" transform="translate(${margin+w+gap} 300)">${drawing(true)}</g>${text(margin,h+480,'Per-bay side cutaways · back at left, front at right · sides/dividers and handles omitted')}${sideSections}${text(margin,2*h+820,`Depth from back outer face: back panel 0–${t.toFixed(1)} mm; shelf/divider ${t.toFixed(1)}–${(d-t).toFixed(1)} mm; closed front ${(d-t).toFixed(1)}–${d.toFixed(1)} mm.`)}${text(margin,2*h+875,`Shelf/divider depth ${(d-2*t).toFixed(1)} mm · top/bottom depth ${d.toFixed(1)} mm · open bays have no front panel.`)}${register}${text(margin,height-35,'Scaled to fit · use written dimensions · workshop review required')}</svg>\n`;
}
