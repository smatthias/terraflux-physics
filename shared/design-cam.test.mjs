import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {camPlan,camSettings,layerTool,CAM_DEFAULTS} from './design-cam.mjs';
import {partProgram as partGcode,designProgram as designGcode,designDxf,dxfLayerName} from './cam-export.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids('b')});
const byName=(design,name)=>design.parts.find(p=>p.name===name);

// Minimal G-code interpreter: returns sampled tool-centre points of every move.
function simulate(gcode) {
  let pos={x:0,y:0,z:0};const points=[],rapids=[],words=[];
  for(const raw of gcode.split('\n')) {
    const line=raw.replace(/\([^)]*\)/g,'').trim();if(!line) continue;
    words.push(line);
    const value=letter=>{const m=line.match(new RegExp(letter+'(-?\\d+(?:\\.\\d+)?)'));return m?Number(m[1]):undefined;};
    const g=[...line.matchAll(/G(\d+)/g)].map(m=>Number(m[1])).find(n=>n<=3);
    if(g===undefined) continue;
    const end={x:value('X')??pos.x,y:value('Y')??pos.y,z:value('Z')??pos.z};
    if(g===0) {rapids.push(end);pos=end;continue;}
    if(g===1) {const n=Math.max(20,Math.ceil(Math.hypot(end.x-pos.x,end.y-pos.y,end.z-pos.z)));for(let k=1;k<=n;k++) {const t=k/n;points.push({x:pos.x+(end.x-pos.x)*t,y:pos.y+(end.y-pos.y)*t,z:pos.z+(end.z-pos.z)*t,f:value('F')});}pos=end;continue;}
    const c={x:pos.x+value('I'),y:pos.y+value('J')},R=Math.hypot(pos.x-c.x,pos.y-c.y);
    assert.ok(Math.abs(Math.hypot(end.x-c.x,end.y-c.y)-R)<1e-3,'arc end on the circle');
    let a0=Math.atan2(pos.y-c.y,pos.x-c.x),a1=Math.atan2(end.y-c.y,end.x-c.x),sweep=g===3?a1-a0:a0-a1;
    while(sweep<=1e-9) sweep+=Math.PI*2;
    for(let k=1;k<=64;k++) {const t=k/64,a=a0+(g===3?1:-1)*sweep*t;points.push({x:c.x+R*Math.cos(a),y:c.y+R*Math.sin(a),z:pos.z+(end.z-pos.z)*t,f:value('F'),arc:g});}
    pos=end;
  }
  return {points,rapids,words};
}
const cutting=sim=>sim.points.filter(p=>p.z<-1e-6);
// A round tool reaches every point within its radius of the tool-centre region
// (a rectangle, possibly a line); inside corners keep the tool radius.
const distanceToRect=(p,x0,y0,x1,y1)=>Math.hypot(Math.max(x0-p.u,0,p.u-x1),Math.max(y0-p.v,0,p.v-y1));
const near=(a,b,tol=1e-3)=>Math.abs(a-b)<=tol;

test('the left side program drills, grooves and cuts free without gouging',()=>{
  const design=box(),left=byName(design,'Left side'),sim=simulate(partGcode(design,left.id,'A'));
  // Allowed tool-centre regions and their deepest Z.
  const allowed=[
    ...left.features.filter(f=>f.type==='drill').map(f=>({inside:p=>near(p.x,f.u)&&near(p.y,f.v),z:-12})),
    {inside:p=>near(p.x,13)&&p.y>=21-1e-3&&p.y<=699+1e-3,z:-10},                       // groove: 6 mm tool in a 6 mm slot
    {inside:p=>(near(p.x,-3)||near(p.x,563))&&p.y>=-3-1e-3&&p.y<=723+1e-3||(near(p.y,-3)||near(p.y,723))&&p.x>=-3-1e-3&&p.x<=563+1e-3,z:-18.3},
  ];
  for(const p of cutting(sim)) assert.ok(allowed.some(a=>a.inside(p)&&p.z>=a.z-1e-6),`gouge at ${JSON.stringify(p)}`);
  assert.ok(sim.rapids.every(p=>p.z>=1-1e-9),'rapids stay above the clearance plane');
  assert.equal(Math.min(...sim.points.map(p=>p.z)),-18.3,'through cut 0.3 mm past the 18 mm panel');
  for(const f of left.features.filter(f=>f.type==='drill')) assert.equal(Math.min(...sim.points.filter(p=>near(p.x,f.u)&&near(p.y,f.v)).map(p=>p.z)),-12);
  let checked=0;
  for(let v=18;v<=702;v+=7) for(let u=10;u<=16;u+=1) {
    if(distanceToRect({u,v},13,21,13,699)>3+1e-6) continue;checked++;
    assert.ok(cutting(sim).some(p=>p.z<=-10+1e-6&&Math.hypot(p.x-u,p.y-v)<=3+1e-6),`groove covered at ${u},${v}`);
  }
  assert.ok(checked>500);
  assert.deepEqual(sim.words.filter(w=>/^M0/.test(w)).length,3,'three tools: drill, groove, profile');
  assert.match(partGcode(design,left.id,'A'),/T1: 5 mm tool for Drilling drill[\s\S]*T2: 6 mm tool for Grooves groove[\s\S]*T4: 6 mm tool for Parts profile/);
});

test('face B programs are for the panel turned over about its length',()=>{
  const design=box(),right=byName(design,'Right side');
  const plan=camPlan(design);
  assert.deepEqual(plan.programs.filter(p=>p.partId===right.id).map(p=>p.face),['A','B']);
  const sim=simulate(partGcode(design,right.id,'B'));
  for(const u of [60,500]) assert.ok(sim.points.some(p=>near(p.x,u)&&near(p.y,720-346)&&near(p.z,-12)),'hole at Y = width − v');
  assert.ok(cutting(sim).every(p=>p.z>=-12-1e-6),'no through cut on face B');
  assert.throws(()=>partGcode(design,byName(design,'Bottom').id,'B'),/no machining/);
});

test('larger holes are bored helically; climb and conventional set the direction',()=>{
  let {design,partId}=D.addPart(D.createDesign({id:'d',name:'D',makeId:ids('l')}),'panel',{makeId:ids('p')});
  const hole=D.addFeature(design,partId,'drill',ids('f'));
  design=D.updateFeature(hole.design,hole.featureId,{diameter:10,depth:9});
  const gcode=partGcode(design,partId,'A'),sim=simulate(gcode);
  const arcs=sim.points.filter(p=>p.arc);
  assert.ok(arcs.length&&arcs.every(p=>p.arc===3),'climb bores anticlockwise');
  assert.ok(arcs.every(p=>near(Math.hypot(p.x-300,p.y-200),2.5)),'helix radius (10 − 5) / 2');
  assert.equal(Math.min(...arcs.map(p=>p.z)),-9);
  // Profile direction: signed area of the outline loop.
  const signedArea=sim2=>{const loop=cutting(sim2).filter(p=>p.x<0||p.x>600||p.y<0||p.y>400);let a=0;for(let i=0;i<loop.length-1;i++) a+=loop[i].x*loop[i+1].y-loop[i+1].x*loop[i].y;return a;};
  assert.ok(signedArea(sim)<0,'climb: outside profile clockwise');
  const conventional=D.updateCam(design,{direction:'conventional'}),sim2=simulate(partGcode(conventional,partId,'A'));
  assert.ok(sim2.points.filter(p=>p.arc).every(p=>p.arc===2));assert.ok(signedArea(sim2)>0);
});

test('pockets are cleared completely, inside the wall, at every step-down',()=>{
  let {design,partId}=D.addPart(D.createDesign({id:'d',name:'D',makeId:ids('l')}),'panel',{makeId:ids('p')});
  const added=D.addFeature(design,partId,'pocket',ids('f'));
  design=D.updateFeature(added.design,added.featureId,{u:100,v:50,length:80,width:37,depth:7});
  const sim=simulate(partGcode(design,partId,'A')),r=3;
  const inPocket=cutting(sim).filter(p=>p.x>-2&&p.x<600&&p.y>-2&&p.y<400);
  assert.ok(inPocket.every(p=>p.x>=103-1e-6&&p.x<=177+1e-6&&p.y>=53-1e-6&&p.y<=84+1e-6&&p.z>=-7-1e-6),'tool stays off the walls');
  let checked=0,corners=0;
  for(let u=100;u<=180;u+=2) for(let v=50;v<=87;v+=2) {
    if(distanceToRect({u,v},103,53,177,84)>r+1e-6) {corners++;continue;}checked++;
    assert.ok(inPocket.some(p=>near(p.z,-7)&&Math.hypot(p.x-u,p.y-v)<=r+1e-6),`covered at ${u},${v}`);
  }
  assert.ok(checked>700&&corners>0,'everything a 6 mm tool can reach is cleared');
  for(let u=100;u<=180;u+=2) for(let v=50;v<=87;v+=2) if(distanceToRect({u,v},103,53,177,84)>r+1e-6)
    assert.ok([[100,50],[180,50],[100,87],[180,87]].some(([cu,cv])=>Math.hypot(u-cu,v-cv)<r),`only corner points stay uncut (${u},${v})`);
  assert.deepEqual([...new Set(inPocket.map(p=>p.z).filter(z=>[-3,-6,-7].includes(z)))].sort((a,b)=>a-b),[-7,-6,-3],'3 mm step-downs to 7 mm');
});

test('tools and CAM settings come from the layers and the design',()=>{
  let design=box();const drill=design.layers.find(l=>l.operation==='drill');
  assert.deepEqual(layerTool(drill),{diameter:5,feed:600,plunge:300,rpm:12000,stepdown:4});
  assert.equal(layerTool(design.layers.find(l=>l.operation==='none')),null);
  design=D.updateLayerTool(design,drill.id,{rpm:9000,stepdown:6});
  design=D.updateCam(design,{safeZ:12,post:'linuxcnc'});
  assert.deepEqual(camSettings(design),{...CAM_DEFAULTS,post:'linuxcnc',safeZ:12});
  const gcode=partGcode(design,byName(design,'Left side').id,'A');
  assert.match(gcode,/T1 M6 \(T1: 5 mm tool for Drilling drill\)\nG43 H1\nS9000 M3\nG4 P2\.0/);
  assert.match(gcode,/G0 Z12\.000/);
  assert.equal(Math.max(...simulate(gcode).rapids.map(p=>p.z)),12);
  assert.deepEqual(D.updateLayerTool(design,drill.id,{rpm:null,stepdown:null}).layers[0].tool,undefined,'clearing every value removes the tool');
  assert.throws(()=>D.updateLayerTool(design,drill.id,{diameter:0}),/Tool diameter/);
  assert.throws(()=>D.updateCam(design,{post:'biesse'}),/Choose a listed post-processor/);
});

test('a tool too large for a feature blocks export with the reason',()=>{
  let {design,partId}=D.addPart(D.createDesign({id:'d',name:'D',makeId:ids('l')}),'panel',{makeId:ids('p')});
  const added=D.addFeature(design,partId,'pocket',ids('f'));
  design=D.updateFeature(added.design,added.featureId,{length:5,width:20});
  const plan=camPlan(design);
  assert.equal(plan.ready,false);assert.match(plan.issues[0].message,/6 mm tool does not fit the 5 × 20 mm pocket/);
  assert.throws(()=>partGcode(design,partId,'A'),/Resolve CAM errors before exporting/);
  assert.throws(()=>designGcode(design),/Resolve CAM errors/);
  const drilled=D.addFeature(D.addPart(D.createDesign({id:'e',name:'E',makeId:ids('m')}),'panel',{makeId:ids('q')}).design,'q1','drill',ids('h'));
  assert.match(camPlan(D.updateFeature(drilled.design,drilled.featureId,{diameter:3})).issues[0].message,/5 mm tool is larger than the 3 mm hole/);
});

test('one file for all panels pauses to load each sheet; edge holes are noted',()=>{
  const gcode=designGcode(box());
  assert.equal((gcode.match(/^M0 \(Load /gm)??[]).length,camPlan(box()).programs.length-1);
  assert.ok(gcode.trim().endsWith('M30'));assert.doesNotMatch(gcode,/NaN|undefined/);
  assert.ok(gcode.split('\n').every(line=>!/\(.*\(|\).*\)/.test(line.replace(/\([^)]*\)/g,''))),'no nested comments');
});

// Minimal DXF reader: pairs of group code and value.
function readDxf(text) {
  const lines=text.trimEnd().split('\n'),pairs=[];
  for(let i=0;i<lines.length;i+=2) pairs.push([Number(lines[i]),lines[i+1]]);
  const entities=[];let current=null;
  const start=pairs.findIndex(([c,v],i)=>c===2&&v==='ENTITIES');
  for(const [code,value] of pairs.slice(start+1)) {
    if(code===0) {current={type:value};entities.push(current);} else if(current) (current[code]??=[]).push(value);
  }
  const layers=pairs.filter(([c,v],i)=>c===2&&pairs[i-1][1]==='LAYER'&&pairs[i-1][0]===0).map(([,v])=>v);
  return {pairs,entities,layers};
}
test('the DXF has one drawing per panel face on CAM-style layers',()=>{
  const design=box(),dxf=readDxf(designDxf(design));
  assert.ok(dxf.pairs.some(([c,v])=>c===1&&v==='AC1009'));
  const units=dxf.pairs.findIndex(([c,v])=>c===9&&v==='$INSUNITS');assert.deepEqual(dxf.pairs[units+1],[70,'4'],'millimetres');
  assert.deepEqual(dxf.layers.filter(l=>!l.startsWith('TT_')).sort(),['Drilling_DRILL_Z12','Grooves_GROOVE_Z10','Parts_PROFILE_Z18_THRU','Parts_PROFILE_Z6_THRU'].sort(),'the 6 mm back has its own profile depth');
  assert.ok(dxf.layers.every(l=>/^[A-Za-z0-9_$-]+$/.test(l)),'R12-safe layer names');
  const circles=dxf.entities.filter(e=>e.type==='CIRCLE').map(e=>[e[8][0],Number(e[10][0]),Number(e[20][0]),Number(e[40][0])]);
  assert.deepEqual(circles.filter(c=>c[1]<560),[['Drilling_DRILL_Z12',60,346,2.5],['Drilling_DRILL_Z12',500,346,2.5]],'left side, face A');
  const rowB=-(720+200);
  assert.deepEqual(circles.filter(c=>c[2]<0).map(c=>[c[1],c[2]]),[[660+60,rowB+720-346],[660+500,rowB+720-346]],'right side face B, turned over');
  const outlines=dxf.entities.filter(e=>e.type==='POLYLINE'&&e[8][0].startsWith('Parts_PROFILE_'));
  assert.equal(outlines.length,6,'each panel outline once');
  assert.ok(dxf.entities.filter(e=>e.type==='POLYLINE').every(e=>e[70][0]==='1'),'closed polylines');
  assert.equal(dxfLayerName('Back groove','groove',12.5,false),'Back_groove_GROOVE_Z12p5');
});
