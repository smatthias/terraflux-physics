// CAM toolpaths for furniture designs: 3-axis machining per panel face, built on
// the machining plan (design-cnc.mjs). Machine output (G-code dialects, Heidenhain
// and DXF) is written by cam-export.mjs, which loads on first export.
//
// Machine frame: X = u and Y = v of the sheet, Z = 0 on the top surface and
// negative into the material. Face B programs are for the panel turned over
// about its length, so Y = width − v. The origin is the sheet corner at u = v = 0.
//
// Strategies: holes the size of the tool are peck drilled; larger holes are
// bored helically; pockets and grooves are cleared with contour-parallel loops
// from the inside out (the last loop finishes the wall); the part outline is
// cut outside by the tool radius in step-down passes, through cuts going
// `throughMm` past the thickness. Holding tabs keep a through-cut part joined
// to the sheet: on the passes below the tab top the tool lifts over each tab.
// Climb cutting runs outside profiles clockwise and pockets anticlockwise.
// Lead-ins and ramping are not included: check every program in a simulator
// before cutting.
import {cncPlan} from './design-cnc.mjs';
import {validateDesign} from './furniture-design.mjs';

// Controllers the programs are written for (cam-export.mjs), with file types.
export const POSTS=Object.freeze({
  grbl:{label:'GRBL 1.1 (manual tool change)',extension:'.nc'},
  linuxcnc:{label:'LinuxCNC',extension:'.ngc'},
  mach:{label:'Mach3 / Mach4',extension:'.tap'},
  fanuc:{label:'Fanuc / Haas / Syntec (ISO)',extension:'.nc'},
  siemens:{label:'Siemens Sinumerik 840D / 828D',extension:'.mpf'},
  heidenhain:{label:'Heidenhain TNC (Klartext)',extension:'.h'},
});
export const CAM_DEFAULTS=Object.freeze({post:'grbl',safeZ:5,throughMm:.3,direction:'climb',stepoverPct:40,
  units:'mm',arcs:'native',lineNumbers:false,spinUpS:2,dwellMs:false,programNumber:1000,tabs:4,tabWidth:8,tabHeight:3});
export const TOOL_DEFAULTS=Object.freeze({
  drill:Object.freeze({diameter:5,feed:600,plunge:300,rpm:12000,stepdown:4}),
  groove:Object.freeze({diameter:6,feed:2000,plunge:500,rpm:18000,stepdown:3}),
  pocket:Object.freeze({diameter:6,feed:2000,plunge:500,rpm:18000,stepdown:3}),
  profile:Object.freeze({diameter:6,feed:2500,plunge:600,rpm:18000,stepdown:6}),
});
export const camSettings=design=>({...CAM_DEFAULTS,...design.cam});
export const layerTool=layer=>layer.operation==='none'?null:{...TOOL_DEFAULTS[layer.operation],...layer.tool};
const CLEAR=1,EPS=1e-6;

// ---- Toolpaths (lists of moves in the machine frame)
function levels(depth,stepdown) {
  const out=[];for(let z=stepdown;z<depth-EPS;z+=stepdown) out.push(z);
  out.push(depth);return out;
}
const rectLoop=(x0,y0,x1,y1,clockwise)=>clockwise?[[x0,y0],[x0,y1],[x1,y1],[x1,y0],[x0,y0]]:[[x0,y0],[x1,y0],[x1,y1],[x0,y1],[x0,y0]];
function drillMoves(op,tool,cam,at) {
  const {x,y}=at(op.u,op.v),d=tool.diameter,D=op.diameter,moves=[];
  if(d>D+.05) return {error:`${op.layer}: the ${d} mm tool is larger than the ${D} mm hole`};
  const bottom=op.depth+(op.through?cam.throughMm:0);
  if(D-d<=.05) {
    moves.push({g:0,x,y,z:cam.safeZ},{g:0,x,y,z:CLEAR});
    for(const z of levels(bottom,tool.stepdown)) moves.push({g:1,x,y,z:-z,f:tool.plunge},{g:0,x,y,z:CLEAR});
  } else {
    // Helical bore: full circles descending by one step-down each, then a flat pass.
    const R=(D-d)/2,cw=cam.direction!=='climb';
    moves.push({g:0,x:x+R,y,z:cam.safeZ},{g:0,x:x+R,y,z:CLEAR},{g:1,x:x+R,y,z:0,f:tool.plunge});
    for(const z of [...levels(bottom,tool.stepdown),bottom]) moves.push({g:cw?2:3,x:x+R,y,z:-z,i:-R,j:0,f:tool.feed});
    moves.push({g:1,x,y,z:-bottom,f:tool.feed});
  }
  moves.push({g:0,x,y,z:cam.safeZ});
  return {moves};
}
// Pockets and grooves: loops of the tool centre inside the rectangle inset by
// the tool radius, from the centre outward, at every step-down.
function pocketMoves(op,tool,cam,at) {
  const a=at(op.u,op.v),b=at(op.u+op.length,op.v+op.width),r=tool.diameter/2;
  const x0=Math.min(a.x,b.x)+r,x1=Math.max(a.x,b.x)-r,y0=Math.min(a.y,b.y)+r,y1=Math.max(a.y,b.y)-r;
  if(x1<x0-EPS||y1<y0-EPS) return {error:`${op.layer}: the ${tool.diameter} mm tool does not fit the ${op.length} × ${op.width} mm ${op.type}`};
  const step=tool.diameter*cam.stepoverPct/100,limit=Math.max(0,Math.min(x1-x0,y1-y0)/2),offsets=[];
  for(let o=0;o<limit-EPS;o+=step) offsets.push(o);
  offsets.push(limit);
  const loops=offsets.reverse().map(o=>rectLoop(Math.min(x0+o,x1-o),Math.min(y0+o,y1-o),Math.max(x0+o,x1-o),Math.max(y0+o,y1-o),cam.direction!=='climb'));
  const bottom=op.depth+(op.through?cam.throughMm:0),moves=[];
  const [sx,sy]=loops[0][0];
  moves.push({g:0,x:sx,y:sy,z:cam.safeZ},{g:0,x:sx,y:sy,z:CLEAR});
  for(const z of levels(bottom,tool.stepdown)) {
    moves.push({g:1,x:sx,y:sy,z:-z,f:tool.plunge});
    for(const loop of loops) for(const [x,y] of loop) moves.push({g:1,x,y,z:-z,f:tool.feed});
    moves.push({g:1,x:sx,y:sy,z:-z,f:tool.feed});
  }
  moves.push({g:0,x:sx,y:sy,z:cam.safeZ});
  return {moves};
}
// Holding tabs along a closed loop: `count` bridges, each `span` long on the tool
// path (tab width + tool diameter), spread evenly round the perimeter and kept
// clear of corners by the tool diameter. Returns [from, to] distances along the loop.
export function placeTabs(loop,count,span,clearance) {
  const edges=[];let total=0;
  for(let i=0;i<loop.length-1;i++) {const [x0,y0]=loop[i],[x1,y1]=loop[i+1],length=Math.hypot(x1-x0,y1-y0);edges.push({from:total,to:total+length});total+=length;}
  const spans=[];
  for(let k=0;k<count;k++) {
    const want=total*(k+.5)/count,edge=edges.find(e=>want<=e.to+EPS)??edges.at(-1);
    const low=edge.from+clearance+span/2,high=edge.to-clearance-span/2;
    if(low>high+EPS) continue;
    const centre=Math.min(Math.max(want,low),high),from=centre-span/2;
    if(spans.length&&from<spans.at(-1)[1]+clearance) continue;
    spans.push([from,centre+span/2]);
  }
  return spans;
}
// Walks a closed loop at depth z; inside tab spans the tool stays at tabZ (above z).
function walkLoop(loop,z,spans,tabZ,tool) {
  const moves=[],at=(a,b,t)=>[a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t];
  let travelled=0,up=false;
  for(let i=0;i<loop.length-1;i++) {
    const a=loop[i],b=loop[i+1],length=Math.hypot(b[0]-a[0],b[1]-a[1]);
    const cuts=spans.flat().filter(d=>d>travelled+EPS&&d<travelled+length-EPS).map(d=>(d-travelled)/length);
    for(const t of [...cuts,1]) {
      const [x,y]=at(a,b,t);
      moves.push({g:1,x,y,z:up?tabZ:z,f:tool.feed});
      const d=travelled+t*length,inside=spans.some(([from,to])=>d>=from-EPS&&d<to-EPS);
      if(t<1&&inside!==up) {up=inside;moves.push({g:1,x,y,z:up?tabZ:z,f:up?tool.feed:tool.plunge});}
    }
    travelled+=length;
  }
  return moves;
}
function profileMoves(op,tool,cam,length,width) {
  const r=tool.diameter/2,loop=rectLoop(-r,-r,length+r,width+r,cam.direction==='climb'),moves=[];
  const bottom=op.depth+cam.throughMm,tabTop=op.depth-cam.tabHeight;
  const spans=cam.tabs&&tabTop>0?placeTabs(loop,cam.tabs,cam.tabWidth+tool.diameter,tool.diameter):[];
  moves.push({g:0,x:-r,y:-r,z:cam.safeZ},{g:0,x:-r,y:-r,z:CLEAR});
  for(const z of levels(bottom,tool.stepdown)) {
    moves.push({g:1,x:-r,y:-r,z:-z,f:tool.plunge});
    moves.push(...walkLoop(loop,-z,z>tabTop+EPS?spans:[],-tabTop,tool));
  }
  moves.push({g:0,x:-r,y:-r,z:cam.safeZ});
  return {moves,tabs:spans.length,...(cam.tabs&&!(tabTop>0)?{warning:`tabs of ${cam.tabHeight} mm are not lower than the ${op.depth} mm panel; no tabs`}:cam.tabs&&spans.length<cam.tabs?{warning:`only ${spans.length} of ${cam.tabs} tabs fit the outline`}:{})};
}

// Machining plan per panel face with toolpaths and CAM issues.
export function camPlan(input) {
  const design=validateDesign(input),cam=camSettings(design),plan=cncPlan(design),layers=new Map(design.layers.map(l=>[l.id,l]));
  const tools=design.layers.filter(l=>l.operation!=='none').map((l,i)=>({layerId:l.id,number:i+1,...layerTool(l)}));
  const programs=[],issues=[];
  for(const part of plan.parts) {
    for(const face of ['A','B']) {
      const ops=part.operations.filter(op=>!op.edge&&(face==='A'?op.face==='A':op.face==='B'));
      if(!ops.length) continue;
      const {lengthMm:L,widthMm:W}=part.sheet,at=face==='A'?(u,v)=>({x:u,y:v}):(u,v)=>({x:u,y:W-v});
      const blocks=[];let tabs=0;
      for(const op of ops) {
        const layer=layers.get(op.layerId),tool=tools.find(t=>t.layerId===op.layerId);
        const result=op.operation==='profile'?profileMoves(op,tool,cam,L,W):op.type==='drill'?drillMoves(op,tool,cam,at):pocketMoves(op,tool,cam,at);
        if(result.error) {issues.push({code:'TOOL_TOO_LARGE',severity:'error',partId:part.partId,featureId:op.featureId,message:`${part.name}: ${result.error}`});continue;}
        if(result.warning) issues.push({code:'TABS',severity:'warning',partId:part.partId,message:`${part.name}: ${result.warning}`});
        if(result.tabs!==undefined) tabs=result.tabs;
        const last=blocks.at(-1);
        if(last?.tool.layerId===tool.layerId) last.moves.push(...result.moves);
        else blocks.push({tool,layer:layer.name,operation:op.operation,moves:result.moves});
      }
      programs.push({partId:part.partId,name:part.name,face,material:part.materialLabel,sheet:part.sheet,blocks,tabs,edgeHoles:face==='A'?part.operations.filter(op=>op.edge).length:0});
    }
  }
  return {cam,tools,programs,issues:[...plan.issues.filter(i=>i.severity==='error'),...issues],ready:plan.ready&&!issues.some(i=>i.severity==='error')};
}

