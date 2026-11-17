import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {camPlan,CAM_DEFAULTS,placeTabs} from './design-cam.mjs';
import {partProgram,designProgram,POSTS,arcPoints,ascii} from './cam-export.mjs';
import {CAM_POSTS} from './furniture-design.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids('b')});
const byName=(design,name)=>design.parts.find(p=>p.name===name);
// A panel with a Ø10 bore (helical), a Ø5 hole (pecked) and a pocket, cut free with tabs.
function machined(cam={}) {
  let {design,partId}=D.addPart(D.createDesign({id:'d',name:'Tisch für Café',makeId:ids('l')}),'panel',{makeId:ids('p'),overrides:{name:'Platte Ø10'}});
  let added=D.addFeature(design,partId,'drill',ids('f'));
  design=D.updateFeature(added.design,added.featureId,{diameter:10,depth:9,u:100,v:100});
  added=D.addFeature(design,partId,'drill',ids('g'));
  design=D.updateFeature(added.design,added.featureId,{diameter:5,depth:12,u:400,v:300});
  added=D.addFeature(design,partId,'pocket',ids('h'));
  design=D.updateFeature(added.design,added.featureId,{u:200,v:150,length:120,width:60,depth:7});
  return {design:D.updateCam(design,cam),partId};
}

// ---- Readers: every dialect back into tool-centre segments in millimetres.
function readIso(text) {
  let scale=1,pos={x:0,y:0,z:0},modal=null;const segments=[],words=[];
  for(const raw of text.split(/\r?\n/)) {
    const line=raw.replace(/\([^)]*\)/g,'').replace(/;.*$/,'').replace(/^N\d+\s*/,'').trim();
    if(!line||line==='%'||/^O\d+$/.test(line)) continue;
    words.push(line);
    if(/\bG(20|70)\b/.test(line)) scale=25.4;
    if(/\bG(21|71)\b/.test(line)) scale=1;
    const value=letter=>{const m=line.match(new RegExp('\\b'+letter+'(-?\\d+(?:\\.\\d+)?)'));return m?Number(m[1]):undefined;};
    const g=[...line.matchAll(/\bG(\d+)\b/g)].map(m=>Number(m[1])).find(n=>n<=3);
    if(g!==undefined) modal=g;
    if(value('X')===undefined&&value('Y')===undefined&&value('Z')===undefined) continue;
    if(/\bG(4|43)\b/.test(line)&&value('X')===undefined) {if(value('Z')!==undefined) pos={...pos,z:value('Z')*scale};continue;}
    const end={x:(value('X')??pos.x/scale)*scale,y:(value('Y')??pos.y/scale)*scale,z:(value('Z')??pos.z/scale)*scale};
    if(modal===2||modal===3) segments.push({kind:'arc',from:pos,to:end,centre:{x:pos.x+value('I')*scale,y:pos.y+value('J')*scale},dir:modal===3?1:-1});
    else segments.push({kind:modal===0?'rapid':'line',from:pos,to:end});
    pos=end;
  }
  return {segments,words};
}
function readHeidenhain(text) {
  const lines=text.split('\r\n').filter(Boolean);let scale=/INCH$/.test(lines[0])?25.4:1,pos={x:0,y:0,z:0},centre=null;const segments=[];
  for(const line of lines) {
    const body=line.replace(/^\d+ /,'');
    const value=letter=>{const m=body.match(new RegExp('\\b'+letter+'([+-]\\d+(?:\\.\\d+)?)'));return m?Number(m[1])*scale:undefined;};
    if(body.startsWith('CC ')) {centre={x:value('X'),y:value('Y')};continue;}
    if(body.startsWith('L ')||body.startsWith('C ')) {
      const end={x:value('X')??pos.x,y:value('Y')??pos.y,z:value('Z')??pos.z};
      if(body.startsWith('C ')) segments.push({kind:'arc',from:pos,to:end,centre,dir:/DR\+/.test(body)?1:-1});
      else segments.push({kind:/FMAX/.test(body)?'rapid':'line',from:pos,to:end});
      pos=end;
    }
  }
  return {segments,lines};
}
// Points along segments (arcs in their direction), for comparing paths.
function sample(segments,step=.5) {
  const out=[];
  for(const s of segments) {
    if(s.kind!=='arc') {const n=Math.max(1,Math.ceil(Math.hypot(s.to.x-s.from.x,s.to.y-s.from.y,s.to.z-s.from.z)/step));for(let k=1;k<=n;k++) {const t=k/n;out.push({x:s.from.x+(s.to.x-s.from.x)*t,y:s.from.y+(s.to.y-s.from.y)*t,z:s.from.z+(s.to.z-s.from.z)*t});}continue;}
    const r=Math.hypot(s.from.x-s.centre.x,s.from.y-s.centre.y);
    let a0=Math.atan2(s.from.y-s.centre.y,s.from.x-s.centre.x),a1=Math.atan2(s.to.y-s.centre.y,s.to.x-s.centre.x),sweep=s.dir>0?a1-a0:a0-a1;
    while(sweep<=1e-9) sweep+=2*Math.PI;
    const n=Math.max(8,Math.ceil(sweep*r/step));
    for(let k=1;k<=n;k++) {const t=k/n,a=a0+s.dir*sweep*t;out.push({x:s.centre.x+r*Math.cos(a),y:s.centre.y+r*Math.sin(a),z:s.from.z+(s.to.z-s.from.z)*t});}
  }
  return out;
}
// Largest distance from the points `a` to the path `segments` (exact for lines,
// arcs split into chords within 0.0005 mm), using a grid of segment bounds.
function farthest(a,segments,cell=4) {
  const pieces=[];
  for(const s of segments) {
    if(s.kind!=='arc') {pieces.push([s.from,s.to]);continue;}
    let prev=s.from;for(const p of sample([s],.1)) {pieces.push([prev,p]);prev=p;}
  }
  const grid=new Map(),key=(i,j,k)=>i+','+j+','+k,cellOf=v=>Math.floor(v/cell);
  for(const piece of pieces) {
    const [p,q]=piece;
    for(let i=cellOf(Math.min(p.x,q.x));i<=cellOf(Math.max(p.x,q.x));i++) for(let j=cellOf(Math.min(p.y,q.y));j<=cellOf(Math.max(p.y,q.y));j++) for(let k=cellOf(Math.min(p.z,q.z));k<=cellOf(Math.max(p.z,q.z));k++) {
      const id=key(i,j,k);if(!grid.has(id)) grid.set(id,[]);grid.get(id).push(piece);
    }
  }
  const distance=(p,[a,b])=>{const d={x:b.x-a.x,y:b.y-a.y,z:b.z-a.z},l=d.x*d.x+d.y*d.y+d.z*d.z,t=l?Math.max(0,Math.min(1,((p.x-a.x)*d.x+(p.y-a.y)*d.y+(p.z-a.z)*d.z)/l)):0;
    return Math.hypot(p.x-a.x-d.x*t,p.y-a.y-d.y*t,p.z-a.z-d.z*t);};
  let worst=0;
  for(const p of a) {
    let best=Infinity;const [i,j,k]=[p.x,p.y,p.z].map(cellOf);
    for(let di=-1;di<=1;di++) for(let dj=-1;dj<=1;dj++) for(let dk=-1;dk<=1;dk++) for(const piece of grid.get(key(i+di,j+dj,k+dk))??[]) best=Math.min(best,distance(p,piece));
    worst=Math.max(worst,best);
  }
  return worst;
}
const same=(a,b,tol)=>['x','y','z'].every(k=>Math.abs(a[k]-b[k])<=tol);
// Segments of the reference program (camPlan moves) in the same form.
function reference(design,partId,face='A') {
  const program=camPlan(design).programs.find(p=>p.partId===partId&&p.face===face);
  let pos={x:0,y:0,z:design.cam?.safeZ??CAM_DEFAULTS.safeZ};const segments=[];
  for(const m of program.blocks.flatMap(b=>b.moves)) {
    const to={x:m.x,y:m.y,z:m.z};
    segments.push(m.g>=2?{kind:'arc',from:pos,to,centre:{x:pos.x+m.i,y:pos.y+m.j},dir:m.g===3?1:-1}:{kind:m.g===0?'rapid':'line',from:pos,to});
    pos=to;
  }
  return segments;
}

test('every listed post has a label and file type',()=>{
  assert.deepEqual(Object.keys(POSTS),[...CAM_POSTS]);
  for(const post of CAM_POSTS) assert.match(POSTS[post].extension,/^\.[a-z]+$/);
});

test('ISO dialects move the tool exactly like the machining plan, in mm and inches',()=>{
  const base=machined().design,partId=base.parts[0].id,expected=reference(base,partId).filter(s=>!(s.kind==='rapid'&&s.from.z===s.to.z&&s.from.x===s.to.x&&s.from.y===s.to.y));
  for(const post of ['grbl','linuxcnc','mach','fanuc','siemens']) for(const units of ['mm','inch']) {
    const design=D.updateCam(base,{post,units}),{segments}=readIso(partProgram(design,partId,'A'));
    const cutting=segments.filter(s=>!(s.kind==='rapid'&&same(s.from,s.to,1e-9)));
    // The program starts with a rapid to safe Z from the machine position; skip it.
    const moves=cutting.slice(cutting.findIndex(s=>s.kind!=='rapid'||s.to.x!==0||s.to.y!==0)-0);
    const tol=units==='inch'?.003:.0006;
    const tail=moves.slice(-expected.length);
    assert.equal(tail.length,expected.length,`${post} ${units}: segment count`);
    tail.forEach((s,i)=>{
      const e=expected[i];
      assert.equal(s.kind==='arc',e.kind==='arc',`${post} ${units} #${i} kind`);
      assert.ok(same(s.to,e.to,tol),`${post} ${units} #${i} end ${JSON.stringify(s.to)} vs ${JSON.stringify(e.to)}`);
      if(e.kind==='arc') {assert.equal(s.dir,e.dir,`${post} ${units} #${i} direction`);assert.ok(Math.hypot(s.centre.x-e.centre.x,s.centre.y-e.centre.y)<=tol*2,`${post} ${units} #${i} centre`);}
    });
  }
});

test('Heidenhain Klartext follows the same path, with helices as lines',()=>{
  const base=machined().design,partId=base.parts[0].id;
  for(const units of ['mm','inch']) {
    const text=partProgram(D.updateCam(base,{post:'heidenhain',units}),partId,'A'),{segments,lines}=readHeidenhain(text);
    assert.ok(lines.every((line,i)=>line.startsWith(i+' ')),'numbered 0, 1, 2 …');
    assert.match(lines[0],new RegExp(`^0 BEGIN PGM PLATTE_D10_A ${units==='mm'?'MM':'INCH'}$`));
    assert.equal(lines.at(-1).replace(/^\d+ /,''),`END PGM PLATTE_D10_A ${units==='mm'?'MM':'INCH'}`);
    assert.ok(lines.some(l=>/ BLK FORM 0\.1 Z X\+0 Y\+0 Z-/.test(l))&&lines.some(l=>/ BLK FORM 0\.2 X\+/.test(l)));
    assert.ok(lines.some(l=>/ TOOL CALL 1 Z S12000$/.test(l)),'tool call with spindle speed');
    assert.ok(lines.some(l=>/ CYCL DEF 9\.1 DWELL 2\.0$/.test(l)),'spindle start wait');
    assert.ok(!lines.some(l=>/\bG\d|\bM6\b/.test(l)),'no G-code words');
    const got=segments.filter(s=>s.kind!=='rapid'),want=reference(base,partId).filter(s=>s.kind!=='rapid');
    const tol=units==='inch'?.02:.012,there=farthest(sample(want,1),got),back=farthest(sample(got,1),want);
    assert.ok(there<=tol&&back<=tol,`${units}: same path (${there.toFixed(4)}, ${back.toFixed(4)})`);
    // Flat full circles stay circles in the original direction (climb: anticlockwise inside holes).
    const arcs=segments.filter(s=>s.kind==='arc');
    assert.ok(arcs.length>0&&arcs.every(s=>s.dir===1),'DR+ for climb bores');
    assert.ok(arcs.every(s=>same(s.from,s.to,.002)),'full circles end where they start');
  }
  const conventional=readHeidenhain(partProgram(D.updateCam(base,{post:'heidenhain',direction:'conventional'}),partId,'A')).segments.filter(s=>s.kind==='arc');
  assert.ok(conventional.length&&conventional.every(s=>s.dir===-1),'DR- for conventional');
});

test('each dialect has its own program frame, tool change and dwell',()=>{
  const {design,partId}=machined(),program=post=>partProgram(D.updateCam(design,{post}),partId,'A');
  const grbl=program('grbl');
  assert.match(grbl,/^M0 \(Fit T1: 5 mm tool for Drilling drill and set Z zero\)$/m);
  assert.doesNotMatch(grbl,/M6|G43/);assert.match(grbl,/^G4 P2\.0$/m);assert.doesNotMatch(grbl,/\r/);
  const linux=program('linuxcnc');
  assert.match(linux,/^T1 M6 \(T1: 5 mm tool for Drilling drill\)\nG43 H1\nS12000 M3\nG4 P2\.0$/m);
  assert.match(program('mach'),/\r\nG4 P2\.0\r\n/);
  assert.match(partProgram(D.updateCam(design,{post:'mach',dwellMs:true}),partId,'A'),/\r\nG4 P2000\r\n/,'Mach with “G04 dwell in milliseconds”');
  const fanuc=program('fanuc'),lines=fanuc.split('\r\n');
  assert.equal(lines[0],'%');assert.equal(lines[1],'O1000 (TISCH FUR CAFE)');assert.equal(lines.at(-2),'%');assert.equal(lines.at(-1),'');
  assert.match(fanuc,/\r\nT1 M6\r\n\(T1: 5 MM TOOL FOR DRILLING DRILL\)\r\nG43 H1 Z5\.000\r\nS12000 M3\r\nG4 P2000\r\n/);
  assert.match(fanuc,/\r\nG49\r\nM30\r\n%\r\n$/);
  assert.ok(lines.filter(l=>l.startsWith('(')).every(l=>l===l.toUpperCase()),'upper-case comments');
  assert.equal(partProgram(D.updateCam(design,{post:'fanuc',programNumber:42}),partId,'A').split('\r\n')[1].slice(0,5),'O0042');
  const siemens=program('siemens');
  assert.doesNotMatch(siemens,/[()]/,'Sinumerik comments use ;');
  assert.match(siemens,/\r\nG17 G71 G40 G90 G94\r\nG54\r\n/);
  assert.match(siemens,/\r\nT1 ; T1: 5 mm tool for Drilling drill\r\nM6\r\nD1\r\nS12000 M3\r\nG4 F2\.0\r\n/);
  assert.match(partProgram(D.updateCam(design,{post:'siemens',units:'inch'}),partId,'A'),/G17 G70 G40/);
  for(const post of CAM_POSTS) {
    const text=program(post);
    assert.ok(/^[\x20-\x7e\r\n]*$/.test(text),`${post}: ASCII only (“Tisch für Café”, “Ø”)`);
    assert.doesNotMatch(text,/NaN|undefined|Infinity/);
    assert.ok(!/P0(\.0)?\b|DWELL 0/.test(partProgram(D.updateCam(design,{post,spinUpS:0}),partId,'A')),`${post}: no dwell when the wait is 0`);
  }
});

test('line numbers, inches and arcs as lines are options for every ISO post',()=>{
  const {design,partId}=machined();
  for(const post of ['grbl','linuxcnc','mach','fanuc','siemens']) {
    const text=partProgram(D.updateCam(design,{post,lineNumbers:true}),partId,'A'),numbers=[...text.matchAll(/^N(\d+) /gm)].map(m=>Number(m[1]));
    assert.ok(numbers.length>50&&numbers.every((n,i)=>n===(i+1)*10),`${post}: N10, N20 …`);
    assert.ok(text.split(/\r?\n/).filter(l=>l&&!/^N\d+ /.test(l)).every(l=>/^[(;%O]/.test(l)),`${post}: only comments and the program frame are unnumbered`);
    const inch=partProgram(D.updateCam(design,{post,units:'inch'}),partId,'A');
    assert.match(inch,post==='siemens'?/G70/:/G20/);assert.match(inch,/X3\.9370 Y3\.9370/,'100 mm = 3.9370 in');
    assert.match(inch,/F23\.6\b/,'600 mm/min = 23.6 in/min');
    const lines=partProgram(D.updateCam(design,{post,arcs:'lines'}),partId,'A');
    assert.doesNotMatch(lines,/^(N\d+ )?G[23] /m,`${post}: no arc words`);
    const a=readIso(lines).segments.filter(s=>s.kind!=='rapid'),b=reference(design,partId).filter(s=>s.kind!=='rapid');
    const there=farthest(sample(b,1),a),back=farthest(sample(a,1),b);
    assert.ok(there<=.012&&back<=.012,`${post}: lines stay within 0.01 mm of the arcs (${there}, ${back})`);
  }
  for(const p of arcPoints({x:10,y:0,z:0},{g:3,x:10,y:0,z:-2,i:-10,j:0})) assert.ok(Math.abs(Math.hypot(p.x,p.y)-10)<=1e-9);
});

test('all panels in one program stop to load each sheet',()=>{
  const design=box(),count=camPlan(design).programs.length;
  for(const post of CAM_POSTS) {
    const text=designProgram(D.updateCam(design,{post}));
    const stops=post==='heidenhain'?text.match(/ STOP\r\n/g):text.match(/^(N\d+ )?M0 [(;] ?Load /gim);
    assert.equal(stops?.length,count-1,post);
  }
  assert.equal(ascii('Tür · 18×600 mm – “Ø8”'),'Tur - 18x600 mm - "D8"');
});

test('holding tabs keep a through-cut part in the sheet',()=>{
  let {design,partId}=machined();
  const path=cam=>readIso(partProgram(D.updateCam(design,{post:'linuxcnc',...cam}),partId,'A')).segments;
  const outside=p=>p.x<-1||p.x>601||p.y<-1||p.y>401;
  // Tool-centre points on the outline loop at the final depth.
  const deepest=segments=>{const pts=sample(segments.filter(s=>s.kind!=='rapid'),.25).filter(outside);return Math.min(...pts.map(p=>p.z));};
  const withTabs=path({}),without=path({tabs:0});
  assert.equal(deepest(withTabs),-18.3);assert.equal(deepest(without),-18.3);
  // Along the outline, the tool stays at the tab top (−15) over 4 spans of 8 + 6 mm.
  const loop=sample(withTabs.filter(s=>s.kind!=='rapid'),.25).filter(outside);
  const raised=loop.filter(p=>Math.abs(p.z+15)<1e-6&&Math.abs(p.x-Math.round(p.x*4)/4)<1e-9);
  assert.ok(raised.length>0,'tool lifts to the tab top');
  const lowest=new Map();
  for(const p of loop) {const key=Math.round(p.x)+','+Math.round(p.y);lowest.set(key,Math.min(lowest.get(key)??0,p.z));}
  const bridged=[...lowest.values()].filter(z=>Math.abs(z+15)<1e-6).length,free=[...lowest.values()].filter(z=>Math.abs(z+18.3)<1e-6).length;
  assert.ok(bridged>=4*13&&bridged<=4*15+4,`about 4 × 14 mm of outline keeps 3 mm of material (${bridged})`);
  assert.ok(free>1900,'the rest is cut through');
  const plan=camPlan(design).programs[0];assert.equal(plan.tabs,4);
  assert.match(partProgram(D.updateCam(design,{post:'grbl'}),partId,'A'),/\(4 holding tabs 8 mm wide, 3 mm high: cut them after removing the part\)/);
  // Tabs only on the passes below their top, clear of corners.
  const spans=placeTabs([[-3,-3],[-3,403],[603,403],[603,-3],[-3,-3]],4,14,6);
  assert.equal(spans.length,4);
  const corners=[0,406,1012,1418,1824];
  for(const [from,to] of spans) assert.ok(corners.every(c=>from>=c+6-1e-9||to<=c-6+1e-9),`span ${from}–${to} clear of corners`);
  // Too many or too tall tabs are reported.
  const tall=camPlan(D.updateCam(design,{tabHeight:18}));
  assert.match(tall.issues.find(i=>i.code==='TABS').message,/not lower than the 18 mm panel; no tabs/);assert.equal(tall.ready,true,'a warning, not an error');
  const small=D.updatePart(design,partId,{length:60,width:40,features:[]});
  assert.match(camPlan(D.updateCam(small,{tabs:8})).issues.find(i=>i.code==='TABS').message,/only \d of 8 tabs fit the outline/);
  assert.throws(()=>D.updateCam(design,{tabs:20}),/0 to 16/);assert.throws(()=>D.updateCam(design,{units:'cm'}),/mm or inch/);
});
