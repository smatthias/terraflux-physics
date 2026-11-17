import {validateProject} from './project.mjs';
import {bounds} from './geometry.mjs';
import {roomWalls} from './room-topology.mjs';
import {cabinetFootprint} from './furniture.mjs';
import {wallPanels,doorPose} from './openings.mjs';
export const escapeXml=value=>String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
function buildDrawing(input,{scale=null}={}) {
  if(scale!==null && ![20,50,100,200].includes(scale)) throw Error('SVG print scale must be 1:20, 1:50, 1:100, 1:200 or unscaled');
  const project=validateProject(input),room=project.room,box=bounds(room.outline),extent=Math.max(box.width,box.depth);
  const font=scale?2.5*scale/1000:Math.max(.12,extent/70),stroke=scale?0.18*scale/1000:Math.max(.012,extent/1000),offset=scale?8*scale/1000:Math.max(.4,extent/12),items=[],points=[];
  const point=p=>{points.push(p);return p.x.toFixed(6)+','+p.z.toFixed(6);};
  const line=(a,b,color='#263e3c')=>items.push(`<polyline points="${point(a)} ${point(b)}" fill="none" stroke="${color}" stroke-width="${stroke}"/>`);
  const poly=(p,fill)=>items.push(`<polygon points="${p.map(point).join(' ')}" fill="${fill}" stroke="#263e3c" stroke-width="${stroke}"/>`);
  const text=(p,value)=>{point(p);items.push(`<text x="${p.x}" y="${p.z}" text-anchor="middle" font-family="sans-serif" font-size="${font}" fill="#263e3c">${escapeXml(value)}</text>`);};
  poly(room.outline,'#eef2e9');
  for(const item of room.furniture) poly(cabinetFootprint(item),'#d8bd8e');
  roomWalls(room).forEach(({id:wall,a,length,tx,tz})=>{
    const at=(along,inset=0)=>({x:a.x+tx*along-tz*inset,z:a.z+tz*along+tx*inset});
    const openings=room.openings.filter(o=>o.wall===wall).sort((a,b)=>a.offset-b.offset),slice=Math.min(1.2,room.height/2);
    for(const p of wallPanels(length,room.height,openings)) if(p.bottom<=slice && p.top>=slice)
      poly([at(p.left),at(p.right),at(p.right,-.16),at(p.left,-.16)],'#65776c');
    for(const o of openings) {
      if(o.type==='window') {for(const inset of [-.04,-.12]) line(at(o.offset,inset),at(o.offset+o.width,inset),'#287e98');}
      else {
        const pose=doorPose(o),arc=[];
        line(at(pose.hingeAlong),at(pose.hingeAlong,pose.swingSign*o.width),'#8c6529');
        for(let j=0;j<=32;j++) {const angle=j/32*Math.PI/2;arc.push(at(pose.hingeAlong+pose.hingeSign*o.width*Math.cos(angle),pose.swingSign*o.width*Math.sin(angle)));}
        items.push(`<polyline points="${arc.map(point).join(' ')}" fill="none" stroke="#8c6529" stroke-width="${stroke}"/>`);
      }
    }
    const measure=(start,end,distance)=>{
      const p=at(start,-distance),q=at(end,-distance),tick=font*.4;
      line(p,q);line(at(start,-.02),at(start,-distance-font*.3));line(at(end,-.02),at(end,-distance-font*.3));
      for(const s of [start,end]) line(at(s-tick,-distance-tick),at(s+tick,-distance+tick));
      text(at((start+end)/2,-distance-font*.2),(end-start).toFixed(2)+' m');
    };
    measure(0,length,offset*(openings.length?2:1));
    if(openings.length) {const cuts=[0,...openings.flatMap(o=>[o.offset,o.offset+o.width]),length];for(let n=0;n<cuts.length-1;n++) measure(cuts[n],cuts[n+1],offset);}
  });
  const minX=Math.min(...points.map(p=>p.x))-font*3,maxX=Math.max(...points.map(p=>p.x))+font*3;
  const minZ=Math.min(...points.map(p=>p.z))-font*6,maxZ=Math.max(...points.map(p=>p.z))+font*3;
  const width=maxX-minX,height=maxZ-minZ;
  return {project,room,font,minX,minZ,width,height,items};
}
export function planSheetLayout(input,{scale=null,paper='auto',orientation='landscape'}={}) {
  if(!['auto','A4','A3'].includes(paper) || !['portrait','landscape'].includes(orientation)) throw Error('Choose A4/A3 paper and portrait/landscape orientation');
  if(paper!=='auto' && scale===null) throw Error('Choose a print scale for a fixed paper size');
  const drawing=buildDrawing(input,{scale});
  const requiredWidth=scale?drawing.width*1000/scale:null,requiredHeight=scale?drawing.height*1000/scale:null;
  if(paper==='auto') return {fits:true,scale,paper,orientation,requiredWidth,requiredHeight,drawing};
  const short=paper==='A4'?210:297,long=paper==='A4'?297:420;
  const width=orientation==='landscape'?long:short,height=orientation==='landscape'?short:long;
  const availableWidth=width-20,availableHeight=height-32;
  const fits=requiredWidth<=availableWidth+1e-8 && requiredHeight<=availableHeight+1e-8;
  let suggestedScale=null;
  if(!fits) suggestedScale=[20,50,100,200].find(candidate=>{
    const d=buildDrawing(input,{scale:candidate});return d.width*1000/candidate<=availableWidth && d.height*1000/candidate<=availableHeight;
  })??null;
  return {fits,scale,paper,orientation,width,height,availableWidth,availableHeight,requiredWidth,requiredHeight,suggestedScale,drawing};
}
export function exportPlanSvg(input,options={}) {
  const layout=planSheetLayout(input,options),{scale}=layout;
  const {project,room,font,minX,minZ,width,height,items}=layout.drawing;
  if(!layout.fits) throw Error('Plan does not fit '+layout.paper+' '+layout.orientation+' at 1:'+scale+(layout.suggestedScale?'; try 1:'+layout.suggestedScale:''));
  const title=project.name+(scale?' · 1:'+scale:'');
  const content=`<text x="${minX+font}" y="${minZ+font*2}" font-family="sans-serif" font-size="${font*1.5}" textLength="${Math.min(title.length*font*.9,width-font*2)}" lengthAdjust="spacingAndGlyphs">${escapeXml(title)}</text>${items.join('')}`;
  if(layout.paper!=='auto') {
    const factor=1000/scale;
    const x=10+(layout.availableWidth-layout.requiredWidth)/2-minX*factor;
    const y=10+(layout.availableHeight-layout.requiredHeight)/2-minZ*factor;
    return `<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="${layout.width}mm" height="${layout.height}mm" viewBox="0 0 ${layout.width} ${layout.height}" data-paper-size="${layout.paper}" data-orientation="${layout.orientation}" data-print-scale="1:${scale}" role="img"><title>${escapeXml(project.name)} · ${layout.paper} · 1:${scale}</title><metadata>Model coordinates in metres inside scaled group. Print at 100% without fit-to-page.</metadata><rect width="${layout.width}" height="${layout.height}" fill="white"/><rect x="10" y="10" width="${layout.width-20}" height="${layout.height-20}" fill="none" stroke="#263e3c" stroke-width="0.2"/><g data-plan="true" transform="translate(${x} ${y}) scale(${factor})">${content}</g><text x="14" y="${layout.height-14}" font-family="sans-serif" font-size="3">TerraTec · ${layout.paper} ${layout.orientation} · 1:${scale} · Print at 100%</text></svg>\n`;
  }
  const svgWidth=scale?(width*1000/scale).toFixed(6)+'mm':'1200';
  const svgHeight=scale?(height*1000/scale).toFixed(6)+'mm':String(Math.round(1200*height/width));
  const printLabel=scale?'Scale 1:'+scale+'; print at 100% without fit-to-page.':'Drawing is not assigned a print scale.';
  return `<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" viewBox="${minX} ${minZ} ${width} ${height}" width="${svgWidth}" height="${svgHeight}" data-print-scale="${scale?'1:'+scale:'unscaled'}" role="img"><title>${escapeXml(project.name)} · Plan</title><metadata>TerraTec plan; model coordinates in metres; section height ${sliceHeight(room)} m. ${printLabel}</metadata><rect x="${minX}" y="${minZ}" width="${width}" height="${height}" fill="white"/><text x="${minX+font}" y="${minZ+font*2}" font-family="sans-serif" font-size="${font*1.5}" textLength="${Math.min(title.length*font*.9,width-font*2)}" lengthAdjust="spacingAndGlyphs">${escapeXml(title)}</text>${items.join('')}</svg>\n`;
}
function sliceHeight(room) {return Math.min(1.2,room.height/2);}
