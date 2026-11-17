import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {placedDesignItem} from './design-placement.mjs';
import {validateProject} from './project.mjs';
import {rectangle} from './geometry.mjs';
import {manufacturingBatches,manufacturingCsv,designProfiles,profileKey} from './manufacturing.mjs';
import {createJobManifest,verifyJobManifest} from './job-manifest.mjs';
import {quoteCosting,validatePricing} from './quote.mjs';
import {workshopHandoff} from './workshop-handoff.mjs';
import {printableQuote} from './quote-print.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids('b')});
// A steel frame: four Ø25 × 2 tube legs, two 40 × 40 × 2 rails and a 2 mm steel sheet top.
function frame() {
  let design=D.createDesign({id:'frame',name:'Frame',makeId:ids('l')});
  const add=(shape,overrides)=>({design}=D.addPart(design,shape,{makeId:ids('f'+design.parts.length+'-'),overrides}));
  for(const [x,z] of [[0,0],[475,0],[0,375],[475,375]]) add('tube',{axis:'y',diameter:25,wall:2,length:700,x,y:0,z,material:'steel'});
  for(const z of [0,360]) add('box-tube',{axis:'x',width:40,height:40,wall:2,length:450,x:25,y:660,z,material:'steel'});
  add('panel',{orientation:'horizontal',length:500,width:400,thickness:2,x:0,y:700,z:0,material:'steel'});
  return design;
}
const cabinet={id:'cab',type:'cabinet',name:'Cabinet',width:.8,depth:.5,height:1,thickness:.018,shelves:1,x:-2,z:0,rotation:0,material:'birch'};
const project=(designs=[box()],placed=[placedDesignItem(box(),{id:'p1',x:1,z:-1})])=>validateProject({schemaVersion:7,units:'m',name:'Shop',presets:[],furnitureDesigns:designs,
  room:{height:2.8,outline:rectangle(8,6),openings:[],furniture:[cabinet,...placed]}});
const pricing={currency:'EUR',sheetRates:{oak:100,birch:200,white:300},handleUnitCents:50,labourHourlyCents:6000,labourMinutesPerPanel:10,markupBps:0};

test('placed design panels are nested with cabinet panels of the same stock',()=>{
  const p=project(),batches=manufacturingBatches(p.room.furniture,p.furnitureDesigns);
  const birch18=batches.find(b=>b.material==='birch'&&b.thickness===.018),birch6=batches.find(b=>b.material==='birch'&&b.thickness===.006);
  assert.deepEqual(birch18.cabinets,[{id:'cab',name:'Cabinet',code:'C1'},{id:'p1',name:'Box',code:'D1',kind:'design',designId:'box'}],'one batch: cabinet and design share 18 mm birch sheets');
  const panels=birch18.layout.sheets.flatMap(s=>s.panels).filter(p=>p.cabinetId==='p1');
  assert.deepEqual(panels.map(p=>p.label).sort(),['Bottom','Left side','Right side','Shelf','Top']);
  const side=panels.find(p=>p.label==='Left side');assert.deepEqual([side.length,side.width],[560,720]);assert.match(side.shortId,/^D1:\d$/);
  assert.deepEqual(birch6.cabinets.map(c=>c.code),['D1'],'the 6 mm back has its own batch');
  // Without placed designs nothing changes.
  const cabinetsOnly=validateProject({...p,room:{...p.room,furniture:[cabinet]}});
  assert.deepEqual(manufacturingBatches(cabinetsOnly.room.furniture,cabinetsOnly.furnitureDesigns),manufacturingBatches([cabinet]));
  // Placed twice: twice the panels; the design's sheet stock applies to its panels.
  const twice=project([box()],[placedDesignItem(box(),{id:'p1',x:1,z:-1}),placedDesignItem(box(),{id:'p2',x:1,z:1.5})]);
  assert.equal(manufacturingBatches(twice.room.furniture,twice.furnitureDesigns).reduce((n,b)=>n+b.panelCount,0),cabinetPanels()+12);
  const ownStock=D.updateStock(box(),{stockLengthMm:2800,stockWidthMm:2070,stockKerfMm:4});
  assert.deepEqual(ownStock.stock,{stockLengthMm:2800,stockWidthMm:2070,stockTrimMm:0,stockAllowRotation:true,stockKerfMm:4});
  const split=project([ownStock],[placedDesignItem(ownStock,{id:'p1',x:1,z:-1})]);
  assert.equal(manufacturingBatches(split.room.furniture,split.furnitureDesigns).filter(b=>b.material==='birch'&&b.thickness===.018).length,2,'other stock, other batch');
  assert.equal('stock' in D.updateStock(ownStock,{stockLengthMm:null,stockWidthMm:null,stockTrimMm:null,stockAllowRotation:null,stockKerfMm:null}),false);
  assert.throws(()=>D.updateStock(box(),{stockKerfMm:50}),/kerf/);
});
const cabinetPanels=()=>manufacturingBatches([cabinet]).reduce((n,b)=>n+b.panelCount,0);

test('bars, rods and tubes form a profile cut list; metal sheets batch on their own',()=>{
  const f=frame(),p=project([box(),f],[placedDesignItem(box(),{id:'p1',x:1,z:-1}),placedDesignItem(f,{id:'t1',x:-2,z:1.5})]);
  const profiles=designProfiles(p.room.furniture,p.furnitureDesigns);
  assert.deepEqual(profiles.map(g=>[g.key,g.count,g.totalLengthMm]),[['steel|box-tube|40x40x2',2,900],['steel|tube|d25x2',4,2800]]);
  assert.equal(profiles[1].label,'Zinc-plated steel round tube Ø25 × 2 mm');
  assert.equal(profileKey(f.parts[0]),'steel|tube|d25x2');
  const steel=manufacturingBatches(p.room.furniture,p.furnitureDesigns).find(b=>b.material==='steel');
  assert.equal(steel.materialLabel,'Zinc-plated steel');assert.equal(steel.thickness,.002);assert.equal(steel.panelCount,1);
  const csv=manufacturingCsv(p.room.furniture,p.furnitureDesigns);
  assert.equal((csv.match(/"profile"/g)??[]).length,6,'one row per profile piece');
  assert.match(csv,/"Zinc-plated steel round tube Ø25 × 2 mm","","t1","Frame","Tube \d","","","","700\.000","","","profile"/);
  assert.equal(manufacturingCsv([cabinet],[]),manufacturingCsv([cabinet]),'cabinet-only CSV unchanged');
});

test('job manifests and quotes include placed designs',async()=>{
  const f=frame(),p=project([box(),f],[placedDesignItem(box(),{id:'p1',x:1,z:-1}),placedDesignItem(f,{id:'t1',x:-2,z:1.5})]);
  const manifest=await createJobManifest(p,'2024-01-01T00:00:00.000Z');
  assert.deepEqual({designs:manifest.summary.designs,profilePieces:manifest.summary.profilePieces},{designs:2,profilePieces:6});
  assert.equal(manifest.summary.panels,cabinetPanels()+6+1);
  assert.equal(manifest.profiles.length,2);
  assert.ok(manifest.validation.issues.some(i=>i.code==='DESIGN_MACHINING'&&i.severity==='review'));
  assert.equal(manifest.validation.status,'review-required');
  assert.equal((await verifyJobManifest(JSON.stringify(manifest),p)).matchesCurrent,true,'the manifest verifies');
  assert.throws(()=>quoteCosting(manifest,1,pricing),/Enter a sheet price for Zinc-plated steel/);
  const metal={...pricing,sheetRates:{...pricing.sheetRates,steel:4000}};
  assert.throws(()=>quoteCosting(manifest,1,metal),/Enter a price per metre for Zinc-plated steel rectangular tube 40 × 40 × 2 mm/);
  const full={...metal,profileRates:{'steel|tube|d25x2':350,'steel|box-tube|40x40x2':900}};
  const quote=quoteCosting(manifest,1,full);
  assert.deepEqual(quote.profiles.lines.map(l=>[l.key,l.pieces,l.lengthMm,l.amountCents]),[['steel|box-tube|40x40x2',2,900,810],['steel|tube|d25x2',4,2800,980]]);
  assert.equal(quote.profiles.totalCents,1790);
  assert.equal(quote.labourMinutes,(manifest.summary.panels+6)*10,'profile pieces count for labour');
  assert.equal(quote.subtotalCents,quote.materialCents+quote.hardwareCents+quote.labourCents+1790);
  assert.deepEqual(validatePricing(full).profileRates,{'steel|box-tube|40x40x2':900,'steel|tube|d25x2':350});
  assert.throws(()=>validatePricing({...pricing,profileRates:{'steel|tube|d25x2':-1}}),/Invalid profile prices/);
  // Edging ignores placed designs instead of failing on them.
  const edged=quoteCosting(manifest,1,{...full,edging:{materialCentsPerMetre:100,labourCentsPerMetre:100,wasteBps:0}});
  assert.ok(edged.edging.netLengthMm>=0);
  const html=printableQuote({id:'q',createdAt:'2024-01-01T00:00:00.000Z',costing:quote,details:{}},'Job');
  assert.match(html,/Profile stock/);assert.match(html,/Zinc-plated steel round tube Ø25 × 2 mm/);
  const handoff=workshopHandoff(p,'2024-01-01T00:00:00.000Z');
  assert.match(handoff,/D1:1/);assert.match(handoff,/Profile cut list · Zinc-plated steel round tube Ø25 × 2 mm/);
  // A design of profiles only can still be handed off.
  const onlyFrame=validateProject({...p,furnitureDesigns:[f],room:{...p.room,furniture:[placedDesignItem(f,{id:'t1',x:-2,z:1.5})]}});
  const tubes=D.removeParts(f,[f.parts.at(-1).id]),frameOnly=validateProject({...onlyFrame,furnitureDesigns:[tubes]});
  const m2=await createJobManifest(frameOnly,'2024-01-01T00:00:00.000Z');
  assert.equal(m2.batches.length,0);assert.notEqual(m2.validation.status,'blocked','profiles alone are something to hand off');
});

test('cabinet-only manifests and quotes are unchanged',async()=>{
  const p=validateProject({schemaVersion:6,units:'m',name:'Shop',presets:[],room:{height:2.8,outline:rectangle(8,6),openings:[],furniture:[cabinet]}});
  const manifest=await createJobManifest(p,'2024-01-01T00:00:00.000Z');
  assert.equal('profiles' in manifest,false);assert.deepEqual(Object.keys(manifest.summary),['cabinets','panels','sheets','unplaced']);
  const quote=quoteCosting(manifest,1,pricing);
  assert.equal('profiles' in quote,false);assert.equal('profileRates' in quote.pricing,false);
  assert.equal(manifest.validation.issues.some(i=>i.code==='DESIGN_MACHINING'),false);
});
