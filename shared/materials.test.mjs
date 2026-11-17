import {test} from 'node:test';
import assert from 'node:assert/strict';
import {MATERIALS,WOOD_IDS,METAL_IDS,MATERIAL_GROUPS,materialInfo,isWood,isMetal} from './materials.mjs';
import {validateFurniture} from './furniture.mjs';
import {cabinetParts,cabinetBom,cabinetBomCsv,cabinetMaterials,cabinetHardware,cabinetMass,SCREW_SPEC} from './cabinet-parts.mjs';
import {manufacturingBatches} from './manufacturing.mjs';
import {validatePricing,quoteCosting} from './quote.mjs';
import {validateStockRates} from './stock-pricing.mjs';
import {createJobManifest} from './job-manifest.mjs';
import {rectangle} from './geometry.mjs';
const item={id:'c',type:'cabinet',name:'Unit',width:.8,depth:.5,height:1,thickness:.018,shelves:1,x:0,z:0,rotation:0};
const valid=extra=>validateFurniture([{...item,...extra}],rectangle(6,4),3)[0];
const close=(a,b)=>assert.ok(Math.abs(a-b)<1e-9,`${a} != ${b}`);

test('the catalogue has woods for panels and metals for hardware with stable legacy entries',()=>{
  assert.deepEqual(WOOD_IDS,['oak','birch','white','beech','walnut','maple','pine']);
  assert.deepEqual(METAL_IDS,['steel','stainless','brass','aluminium']);
  assert.deepEqual(cabinetMaterials.oak,{label:'Oak board',color:0xb88c59});
  assert.deepEqual(cabinetMaterials.birch,{label:'Birch plywood',color:0xdec49a});
  assert.deepEqual(cabinetMaterials.white,{label:'White board',color:0xe8e8e2});
  for(const m of Object.values(MATERIALS)){
    assert.ok(Object.isFrozen(m));assert.ok(m.densityKgM3>0&&Number.isInteger(m.color));
    assert.ok(m.category==='metal'?m.densityKgM3>2000:m.densityKgM3<1000,m.id);
  }
  assert.deepEqual(MATERIAL_GROUPS.flatMap(g=>g.ids).sort(),[...WOOD_IDS].sort());
  assert.equal(isWood('steel'),false);assert.equal(isMetal('steel'),true);
  assert.throws(()=>materialInfo('gold'),/Unknown material/);
});

test('cabinets accept every wood and metal; hardware fields are optional and validated',()=>{
  for(const material of WOOD_IDS) assert.equal(valid({material}).material,material);
  for(const hardwareMaterial of METAL_IDS) assert.equal(valid({hardwareMaterial}).hardwareMaterial,hardwareMaterial);
  const plain=valid({});
  assert.equal('hardwareMaterial' in plain,false);assert.equal('screwsPerJoint' in plain,false);
  assert.equal(valid({screwsPerJoint:0}).screwsPerJoint,0);
  assert.throws(()=>valid({material:'steel'}),/supported material/,'metal is not a panel material');
  assert.throws(()=>valid({hardwareMaterial:'oak'}),/hardware metal/);
  for(const screwsPerJoint of [-1,1.5,21,'4']) assert.throws(()=>valid({screwsPerJoint}),/whole number/);
});

test('panels take the wood colour and handles the hardware metal colour',()=>{
  const parts=cabinetParts({...item,material:'walnut',hardwareMaterial:'brass'});
  assert.ok(parts.filter(p=>p.cut).every(p=>p.color===MATERIALS.walnut.color));
  assert.ok(parts.filter(p=>!p.cut).every(p=>p.color===MATERIALS.brass.color));
  assert.ok(cabinetParts(item).filter(p=>!p.cut).every(p=>p.color===MATERIALS.steel.color),'steel by default');
});

test('screws are estimated from structural joints and listed with their metal',()=>{
  assert.deepEqual(cabinetHardware(item,2),{material:'steel',materialLabel:'Zinc-plated steel',handles:2,joints:4,screwsPerJoint:4,screws:16,screwSpec:SCREW_SPEC.label});
  const columns={...item,width:1.6,columns:3,screwsPerJoint:3,hardwareMaterial:'stainless'};
  assert.equal(cabinetBom(columns).hardware.joints,8,'four carcass corners plus two per divider');
  assert.equal(cabinetBom(columns).hardware.screws,24);
  assert.equal(cabinetBom({...item,screwsPerJoint:0}).hardware.screws,0);
  const csv=cabinetBomCsv({...item,hardwareMaterial:'brass'}).trim().split('\r\n').map(row=>row.split('","'));
  const header=csv[0].length;
  assert.ok(csv.every(row=>row.length===header),'every row keeps the column count');
  const screw=csv.find(row=>row[1].startsWith('Screw'));
  assert.equal(screw[2],'16');assert.equal(screw[8],'Brass');
  assert.equal(csv.find(row=>row[1]==='Handle')[8],'Brass');
});

test('mass estimates use design panel volume and material densities',()=>{
  const oak=cabinetMass(item),pine=cabinetMass({...item,material:'pine'});
  const bom=cabinetBom(item);
  close(oak.panelsKg,bom.volume*700);close(pine.panelsKg,bom.volume*520);
  close(oak.screwsKg,16*Math.PI*.002**2*.04*7850);
  close(oak.handlesKg,2*.015*.12*.025*7850);
  close(oak.totalKg,oak.panelsKg+oak.screwsKg+oak.handlesKg);
  assert.ok(cabinetMass({...item,hardwareMaterial:'aluminium'}).handlesKg<oak.handlesKg);
  // The model uses 18 mm panels throughout, including back and doors: about 3.6 m² of
  // oak at roughly 12.6 kg/m², so the carcass should weigh around 45 kg.
  // sides 2×1×0.5 + top/bottom 2×0.764×0.5 + back 0.964×0.764 + shelf 0.764×0.464 + doors 2×0.964×0.379
  close(bom.area,1+.764+.736496+.354496+.730712);
  assert.ok(oak.totalKg>40&&oak.totalKg<50,'plausible oak carcass weight: '+oak.totalKg);
});

test('new woods batch and price separately; legacy pricing is unchanged',()=>{
  const batches=manufacturingBatches([{...valid({}),id:'a'},{...valid({material:'walnut'}),id:'b'}]);
  assert.deepEqual(batches.map(b=>b.materialLabel).sort(),['Oak board','Walnut solid wood']);
  const legacy={currency:'EUR',sheetRates:{oak:100,birch:200,white:300},handleUnitCents:50,labourHourlyCents:0,labourMinutesPerPanel:0,markupBps:0};
  assert.deepEqual(validatePricing(legacy),legacy,'saved quotes re-validate identically');
  assert.deepEqual(validatePricing({...legacy,sheetRates:{...legacy.sheetRates,walnut:900,gold:1}}).sheetRates,{oak:100,birch:200,white:300,walnut:900});
  assert.throws(()=>validatePricing({...legacy,sheetRates:{...legacy.sheetRates,walnut:-1}}),/Invalid pricing/);
  assert.throws(()=>validatePricing({...legacy,sheetRates:{oak:1,birch:1}}),/Invalid pricing/,'base rates stay required');
  assert.equal(validateStockRates([{material:'walnut',thicknessMm:18,stockLengthMm:2440,stockWidthMm:1220,unitCents:5}])[0].material,'walnut');
  // Metal sheets (panels of placed designs) can have stock prices too; unknown materials cannot.
  assert.equal(validateStockRates([{material:'steel',thicknessMm:2,stockLengthMm:2000,stockWidthMm:1000,unitCents:5}])[0].material,'steel');
  assert.throws(()=>validateStockRates([{material:'gold',thicknessMm:18,stockLengthMm:2440,stockWidthMm:1220,unitCents:5}]));
  assert.throws(()=>validateStockRates([{material:'oak',thicknessMm:201,stockLengthMm:2440,stockWidthMm:1220,unitCents:5}]));
});

test('quoting a wood without a rate fails clearly instead of producing NaN',async()=>{
  const project={schemaVersion:6,units:'m',name:'Walnut',presets:[],room:{height:3,outline:rectangle(6,4),openings:[],furniture:[{...item,material:'walnut'}]}};
  const manifest=await createJobManifest(project,'2024-01-01T00:00:00.000Z');
  const pricing={currency:'EUR',sheetRates:{oak:100,birch:200,white:300},handleUnitCents:50,labourHourlyCents:0,labourMinutesPerPanel:0,markupBps:0};
  assert.throws(()=>quoteCosting(manifest,1,pricing),/Enter a sheet price for Walnut solid wood/);
  const cost=quoteCosting(manifest,1,{...pricing,sheetRates:{...pricing.sheetRates,walnut:900}});
  assert.equal(cost.lines[0].unitCents,900);assert.ok(Number.isSafeInteger(cost.totalCents));
});
