// Machine output for furniture designs: NC programs in the dialect of the chosen
// controller, and a DXF drawing for CAM software. The toolpaths come from
// camPlan (design-cam.mjs) in millimetres; posts only translate them.
//
// Posts (CAM setting `post`):
//   grbl        GRBL 1.1 hobby/desktop routers: no tool changer, so each tool
//               change pauses (M0) for a manual change and re-zeroing of Z.
//   linuxcnc    LinuxCNC: T… M6, G43 H… tool length offsets.
//   mach        Mach3 / Mach4: as LinuxCNC; the G4 dwell unit follows the Mach
//               setting “G04 dwell in milliseconds” (CAM setting `dwellMs`).
//   fanuc       Fanuc-compatible ISO (Fanuc 0i/30i, Haas, Syntec and other ISO
//               controls): % and O-number, upper-case comments, G43 H… Z…,
//               G4 P in milliseconds, G49 at the end.
//   siemens     Siemens Sinumerik 840D / 828D: ; comments, G70/G71 units,
//               T… M6 D1 (length offset from D1), G4 F in seconds.
//   heidenhain  Heidenhain TNC Klartext (conversational): BEGIN PGM, BLK FORM,
//               TOOL CALL, L / CC / C blocks, dwell cycle 9, line numbers.
// Every post writes millimetres or inches (G21/G20, G71/G70, MM/INCH), optional
// line numbers, native arcs or short lines for controls without them, ASCII
// text only (accents removed) and CR LF line ends for the industrial controls.
// Helical arcs are always written as lines for Heidenhain.
import {camPlan,POSTS} from './design-cam.mjs';
export {POSTS};
import {cncPlan} from './design-cnc.mjs';
import {validateDesign} from './furniture-design.mjs';

// ---- Text and numbers
export const ascii=text=>String(text).replace(/[·•–—]/g,'-').replace(/[“”„«»]/g,'"').replace(/[‘’‚]/g,"'").replace(/×/g,'x').replace(/[Øø⌀]/g,'D').replace(/°/g,' deg').normalize('NFKD').replace(/[\u0300-\u036f]/g,'').replace(/ß/g,'ss').replace(/[^\x20-\x7e]/g,'?');
function numbers(cam) {
  const inch=cam.units==='inch',scale=inch?1/25.4:1,places=inch?4:3;
  const n=v=>{const s=(Math.round(v*scale*10**places)/10**places).toFixed(places);return /^-0\.0+$/.test(s)?s.slice(1):s;};
  return {inch,n,signed:v=>{const s=n(v);return s.startsWith('-')?s:'+'+s;},feed:v=>inch?(Math.round(v*scale*10)/10).toFixed(1):String(Math.round(v))};
}
// Arcs as short lines (within 0.01 mm of the circle), for controls without
// arcs and for helices on Heidenhain.
export function arcPoints(from,move,tolerance=.01) {
  const c={x:from.x+move.i,y:from.y+move.j},r=Math.hypot(move.i,move.j);
  let a0=Math.atan2(from.y-c.y,from.x-c.x),a1=Math.atan2(move.y-c.y,move.x-c.x),sweep=move.g===3?a1-a0:a0-a1;
  while(sweep<=1e-9) sweep+=2*Math.PI;
  const step=2*Math.acos(Math.max(-1,1-tolerance/r)),count=Math.max(8,Math.ceil(sweep/step)),out=[];
  for(let k=1;k<=count;k++) {
    const t=k/count,a=a0+(move.g===3?1:-1)*sweep*t;
    out.push(k===count?{x:move.x,y:move.y,z:move.z}:{x:c.x+r*Math.cos(a),y:c.y+r*Math.sin(a),z:from.z+(move.z-from.z)*t});
  }
  return out;
}

// ---- ISO G-code dialects
const ISO={
  grbl:{start:u=>[`${u} G90 G17 G94`],comment:t=>`(${t})`,eol:'\n',end:['M30'],
    tool:(t,label)=>[`M0 (Fit ${label} and set Z zero)`],dwell:(s)=>`G4 P${s.toFixed(1)}`},
  linuxcnc:{start:u=>[`G17 ${u} G40 G49 G80 G90 G94`],comment:t=>`(${t})`,eol:'\n',end:['M30'],
    tool:(t,label,z)=>[`T${t} M6 (${label})`,`G43 H${t}`],dwell:s=>`G4 P${s.toFixed(1)}`},
  mach:{start:u=>[`G17 ${u} G40 G49 G80 G90 G94`],comment:t=>`(${t})`,eol:'\r\n',end:['M30'],
    tool:(t,label)=>[`T${t} M6 (${label})`,`G43 H${t}`],dwell:(s,cam)=>cam.dwellMs?`G4 P${Math.round(s*1000)}`:`G4 P${s.toFixed(1)}`},
  fanuc:{start:u=>[`G17 ${u} G40 G49 G80 G90 G94`,'G54'],comment:t=>`(${t.toUpperCase().replace(/[^A-Z0-9 .,:;+\-=/*#%_]/g,' ')})`,eol:'\r\n',end:['G49','M30'],
    tool:(t,label,z)=>[`T${t} M6`,`(${label.toUpperCase().replace(/[^A-Z0-9 .,:;+\-=/*#%_]/g,' ')})`,`G43 H${t} Z${z}`],dwell:s=>`G4 P${Math.round(s*1000)}`,percent:true},
  siemens:{start:u=>[`G17 ${u} G40 G90 G94`,'G54'],comment:t=>`; ${t}`,eol:'\r\n',end:['M30'],
    tool:(t,label)=>[`T${t} ; ${label}`,'M6','D1'],dwell:s=>`G4 F${s.toFixed(1)}`},
};
function isoProgram(design,programs,cam,post) {
  const dialect=ISO[post],{inch,n,feed}=numbers(cam),units=post==='siemens'?(inch?'G70':'G71'):(inch?'G20':'G21'),lines=[];
  const clean=text=>ascii(text).replace(/[()]/g,'').replace(/[\r\n;]+/g,' ');
  const note=text=>lines.push(dialect.comment(clean(text)));
  let number=0;
  const block=text=>lines.push(cam.lineNumbers&&!/^[(;%O]/.test(text)?`N${(number+=10)} ${text}`:text);
  if(dialect.percent) lines.push('%',`O${String(cam.programNumber).padStart(4,'0')} ${dialect.comment(clean(design.name).slice(0,40))}`);
  note(`TerraTec ${POSTS[post].label} · design ${design.name}`);
  note(`Units ${inch?'inch':'mm'} · origin: sheet corner u=v=0 on the top surface · Z negative into the material`);
  note('Check in a simulator and against your machine before cutting');
  for(const line of dialect.start(units)) block(line);
  block(`G0 Z${n(cam.safeZ)}`);
  let pos={x:0,y:0,z:cam.safeZ};
  const move=m=>{
    const xyz=p=>`X${n(p.x)} Y${n(p.y)} Z${n(p.z)}`;
    if(m.g===0) block(`G0 ${xyz(m)}`);
    else if(m.g===1) block(`G1 ${xyz(m)} F${feed(m.f)}`);
    else if(cam.arcs==='lines') {let first=true;for(const p of arcPoints(pos,m)) {block(`G1 ${xyz(p)}${first?' F'+feed(m.f):''}`);first=false;}}
    else block(`G${m.g} ${xyz(m)} I${n(m.i)} J${n(m.j)} F${feed(m.f)}`);
    pos={x:m.x,y:m.y,z:m.z};
  };
  programs.forEach((program,index)=>{
    if(index) {block('M5');block(`G0 Z${n(cam.safeZ)}`);block(post==='siemens'?'M0 ; Load '+clean(program.name)+' face '+program.face:`M0 ${dialect.comment(clean(`Load ${program.name} face ${program.face}`))}`);}
    note(`${program.name} · face ${program.face} · stock ${program.sheet.lengthMm} x ${program.sheet.widthMm} x ${program.sheet.thicknessMm} mm ${program.material}`);
    if(program.face==='B') note('Face B: panel turned over about its length; Y = width - v');
    if(program.edgeHoles) note(`${program.edgeHoles} edge holes need a horizontal drilling unit and are not included`);
    if(program.tabs) note(`${program.tabs} holding tabs ${cam.tabWidth} mm wide, ${cam.tabHeight} mm high: cut them after removing the part`);
    for(const b of program.blocks) {
      const t=b.tool,label=`T${t.number}: ${inch?(t.diameter/25.4).toFixed(4)+' in':t.diameter+' mm'} tool for ${b.layer} ${b.operation}`;
      block('M5');block(`G0 Z${n(cam.safeZ)}`);
      for(const line of dialect.tool(t.number,clean(label),n(cam.safeZ))) block(line);
      block(`S${t.rpm} M3`);
      if(cam.spinUpS>0) block(dialect.dwell(cam.spinUpS,cam));
      for(const m of b.moves) move(m);
    }
  });
  block('M5');block(`G0 Z${n(cam.safeZ)}`);
  for(const line of dialect.end) block(line);
  if(dialect.percent) lines.push('%');
  return lines.join(dialect.eol)+dialect.eol;
}

// ---- Heidenhain Klartext
const pgmName=text=>(ascii(text).toUpperCase().replace(/[^A-Z0-9_]+/g,'_').replace(/^_+|_+$/g,'')||'TERRATEC').slice(0,16);
function heidenhainProgram(design,programs,cam,name) {
  const {inch,n,signed,feed}=numbers(cam),unit=inch?'INCH':'MM',lines=[];
  const block=text=>lines.push(`${lines.length} ${text}`);
  const note=text=>block('; '+ascii(text).replace(/[\r\n]+/g,' '));
  const sheet=programs.reduce((a,p)=>({L:Math.max(a.L,p.sheet.lengthMm),W:Math.max(a.W,p.sheet.widthMm),T:Math.max(a.T,p.sheet.thicknessMm)}),{L:0,W:0,T:0});
  block(`BEGIN PGM ${name} ${unit}`);
  note(`TerraTec ${POSTS.heidenhain.label} - design ${design.name}`);
  note('Origin: sheet corner u=v=0 on the top surface, Z negative into the material');
  note('Check in a simulator and against your machine before cutting');
  block(`BLK FORM 0.1 Z X+0 Y+0 Z${signed(-sheet.T)}`);block(`BLK FORM 0.2 X${signed(sheet.L)} Y${signed(sheet.W)} Z+0`);
  let pos={x:0,y:0,z:cam.safeZ};
  const L=(p,f,extra='')=>block(`L X${signed(p.x)} Y${signed(p.y)} Z${signed(p.z)} R0 ${f===undefined?'FMAX':'F'+feed(f)}${extra}`);
  const move=m=>{
    if(m.g===0) L(m);
    else if(m.g===1) L(m,m.f);
    else if(cam.arcs==='lines'||Math.abs(m.z-pos.z)>1e-9) for(const p of arcPoints(pos,m)) L(p,m.f);
    else {block(`CC X${signed(pos.x+m.i)} Y${signed(pos.y+m.j)}`);block(`C X${signed(m.x)} Y${signed(m.y)} DR${m.g===3?'+':'-'} R0 F${feed(m.f)}`);}
    pos={x:m.x,y:m.y,z:m.z};
  };
  programs.forEach((program,index)=>{
    if(index) {block(`L Z${signed(cam.safeZ)} R0 FMAX M5`);note(`Load ${program.name} face ${program.face}`);block('STOP');}
    note(`${program.name} - face ${program.face} - stock ${program.sheet.lengthMm} x ${program.sheet.widthMm} x ${program.sheet.thicknessMm} mm ${program.material}`);
    if(program.face==='B') note('Face B: panel turned over about its length; Y = width - v');
    if(program.edgeHoles) note(`${program.edgeHoles} edge holes need a horizontal drilling unit and are not included`);
    if(program.tabs) note(`${program.tabs} holding tabs ${cam.tabWidth} mm wide, ${cam.tabHeight} mm high`);
    for(const b of program.blocks) {
      const t=b.tool;
      block(`L Z${signed(cam.safeZ)} R0 FMAX M5`);
      note(`T${t.number}: ${t.diameter} mm tool for ${b.layer} ${b.operation}`);
      block(`TOOL CALL ${t.number} Z S${t.rpm}`);
      block(`L Z${signed(cam.safeZ)} R0 FMAX M3`);
      if(cam.spinUpS>0) {block('CYCL DEF 9.0 DWELL TIME');block(`CYCL DEF 9.1 DWELL ${cam.spinUpS.toFixed(1)}`);}
      for(const m of b.moves) move(m);
    }
  });
  block(`L Z${signed(cam.safeZ)} R0 FMAX M5`);block(`L Z${signed(cam.safeZ)} R0 FMAX M30`);
  block(`END PGM ${name} ${unit}`);
  return lines.join('\r\n')+'\r\n';
}

// ---- Programs
function write(design,programs,cam,name) {
  return cam.post==='heidenhain'?heidenhainProgram(design,programs,cam,pgmName(name)):isoProgram(design,programs,cam,cam.post);
}
function requireReady(plan,programs) {
  const blocked=plan.issues.filter(i=>i.severity==='error'&&(!programs||programs.some(p=>p.partId===i.partId)));
  if(blocked.length) throw Error('Resolve CAM errors before exporting: '+blocked[0].message);
}
// One panel face.
export function partProgram(input,partId,face='A') {
  const design=validateDesign(input),plan=camPlan(design),program=plan.programs.find(p=>p.partId===partId&&p.face===face);
  if(!program) throw Error('This panel face has no machining');
  requireReady(plan,[program]);
  return write(design,[program],plan.cam,`${program.name}_${face}`);
}
// Every panel face in one program, stopping to load the next sheet.
export function designProgram(input) {
  const design=validateDesign(input),plan=camPlan(design);
  if(!plan.programs.length) throw Error('The design has no machining to export');
  requireReady(plan);
  return write(design,plan.programs,plan.cam,design.name);
}

// ---- DXF (R12 ASCII, millimetres)
// One drawing per panel face, laid out in a row (face B drawings below, already
// turned over). Every operation is on a layer named after its TerraTec layer,
// operation and depth, e.g. Drilling_DRILL_Z12 or Parts_PROFILE_Z18_THRU, the
// naming CAM software maps to toolpaths. Labels and notes are on TT_ layers.
const num=v=>{const s=(Math.round(v*1000)/1000).toFixed(3);return s==='-0.000'?'0.000':s;};
export const dxfLayerName=(name,op,depth,through)=>`${ascii(name).replace(/[^A-Za-z0-9_-]+/g,'_')}_${op.toUpperCase()}_Z${String(Math.round(depth*100)/100).replace('.','p')}${through?'_THRU':''}`;
const ACI=[[1,255,0,0],[2,255,255,0],[3,0,255,0],[4,0,255,255],[5,0,0,255],[6,255,0,255],[8,128,128,128]];
function aci(hex) {
  const r=parseInt(hex.slice(1,3),16),g=parseInt(hex.slice(3,5),16),b=parseInt(hex.slice(5,7),16);
  return ACI.reduce((best,c)=>((c[1]-r)**2+(c[2]-g)**2+(c[3]-b)**2)<best.d?{n:c[0],d:(c[1]-r)**2+(c[2]-g)**2+(c[3]-b)**2}:best,{n:7,d:Infinity}).n;
}
export function designDxf(input) {
  const design=validateDesign(input),plan=cncPlan(design),layers=new Map(design.layers.map(l=>[l.id,l])),used=new Map([['TT_LABELS',7],['TT_NOTES',8]]);
  const entities=[],pair=(code,value)=>entities.push(String(code),String(value));
  const use=(name,color)=>{if(!used.has(name)) used.set(name,color);return name;};
  const polyline=(layer,points)=>{pair(0,'POLYLINE');pair(8,layer);pair(66,1);pair(70,1);for(const [x,y] of points){pair(0,'VERTEX');pair(8,layer);pair(10,num(x));pair(20,num(y));pair(30,'0.0');}pair(0,'SEQEND');pair(8,layer);};
  const circle=(layer,x,y,r)=>{pair(0,'CIRCLE');pair(8,layer);pair(10,num(x));pair(20,num(y));pair(30,'0.0');pair(40,num(r));};
  const textAt=(layer,x,y,h,value)=>{pair(0,'TEXT');pair(8,layer);pair(10,num(x));pair(20,num(y));pair(30,'0.0');pair(40,num(h));pair(1,ascii(value).replace(/[\r\n]+/g,' '));};
  const maxWidth=Math.max(0,...plan.parts.map(p=>p.sheet.widthMm)),rowB=-(maxWidth+200);
  let x=0;
  for(const part of plan.parts) {
    const {lengthMm:L,widthMm:W,thicknessMm:T}=part.sheet;
    for(const face of ['A','B']) {
      const ops=part.operations.filter(op=>!op.edge&&op.face===face);
      if(face==='B'&&!ops.length) continue;
      const oy=face==='A'?0:rowB,at=face==='A'?(u,v)=>[x+u,oy+v]:(u,v)=>[x+u,oy+W-v];
      const profile=ops.find(op=>op.operation==='profile');
      polyline(profile?use(dxfLayerName(profile.layer,'profile',profile.depth,true),aci(layers.get(profile.layerId).color)):use('TT_OUTLINE',8),[[0,0],[L,0],[L,W],[0,W]].map(([u,v])=>at(u,v)));
      for(const op of ops.filter(op=>op.operation!=='profile')) {
        const name=use(dxfLayerName(op.layer,op.operation,op.depth,op.through),aci(layers.get(op.layerId).color));
        if(op.type==='drill') circle(name,...at(op.u,op.v),op.diameter/2);
        else polyline(name,[[op.u,op.v],[op.u+op.length,op.v],[op.u+op.length,op.v+op.width],[op.u,op.v+op.width]].map(([u,v])=>at(u,v)));
      }
      textAt('TT_LABELS',x,oy+W+20,12,`${part.name} · ${part.materialLabel} · ${L} x ${W} x ${T} mm · face ${face}${face==='B'?' (turned over about its length)':''}`);
      const edge=part.operations.filter(op=>op.edge).length;
      if(face==='A'&&edge) textAt('TT_NOTES',x,oy-25,10,`${edge} edge holes: horizontal drilling, not drawn`);
    }
    x+=L+100;
  }
  const out=[];const add=(code,value)=>out.push(String(code),String(value));
  add(0,'SECTION');add(2,'HEADER');add(9,'$ACADVER');add(1,'AC1009');add(9,'$INSUNITS');add(70,4);add(0,'ENDSEC');
  add(0,'SECTION');add(2,'TABLES');add(0,'TABLE');add(2,'LAYER');add(70,used.size);
  for(const [name,color] of used) {add(0,'LAYER');add(2,name);add(70,0);add(62,color);add(6,'CONTINUOUS');}
  add(0,'ENDTAB');add(0,'ENDSEC');add(0,'SECTION');add(2,'ENTITIES');
  out.push(...entities);add(0,'ENDSEC');add(0,'EOF');
  return out.join('\n')+'\n';
}
