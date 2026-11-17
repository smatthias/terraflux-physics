import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validatePricingPresets,savePricingPreset,exportPricingPresets,importPricingPresets} from './pricing-presets.mjs';
const pricing={currency:'EUR',sheetRates:{oak:10000,birch:20000,white:30000},handleUnitCents:250,labourHourlyCents:6000,labourMinutesPerPanel:5,markupBps:1250,edging:{materialCentsPerMetre:200,labourCentsPerMetre:300,wasteBps:1000}};
test('pricing presets validate all rates, replace names and detach nested data',()=>{
 const first=savePricingPreset([],{id:'a',name:' Workshop ',pricing});assert.equal(first[0].name,'Workshop');first[0].pricing.sheetRates.oak=1;assert.equal(pricing.sheetRates.oak,10000);
 const updated=savePricingPreset(first,{id:'new',name:'workshop',pricing});assert.equal(updated.length,1);assert.equal(updated[0].id,'a');assert.deepEqual(updated[0].pricing.edging,pricing.edging);
 assert.throws(()=>validatePricingPresets([...updated,...updated]));assert.throws(()=>savePricingPreset([],{id:'a',name:'',pricing}));assert.throws(()=>savePricingPreset([],{id:'a',name:'Bad',pricing:{...pricing,markupBps:-1}}));
});
test('pricing backups merge atomically, rename conflicts and reject invalid or oversized files',()=>{
 const existing=savePricingPreset([],{id:'a',name:'Workshop',pricing}),text=exportPricingPresets(existing),before=JSON.stringify(existing);
 const result=importPricingPresets(existing,text,()=> 'b');assert.equal(result.length,2);assert.equal(result[1].name,'Workshop (import 1)');assert.equal(result[1].id,'b');assert.equal(JSON.stringify(existing),before);
 assert.throws(()=>importPricingPresets(existing,'x'.repeat(131073)));assert.throws(()=>importPricingPresets(existing,'{}'));assert.throws(()=>importPricingPresets(existing,JSON.stringify({format:'terratec-pricing-presets',version:1,presets:[{id:'b',name:'Bad',pricing:{}}]})));assert.equal(JSON.stringify(existing),before);
 const noEdge={...pricing};delete noEdge.edging;assert.equal('edging' in savePricingPreset([],{id:'a',name:'Legacy',pricing:noEdge})[0].pricing,false);
});
test('stock prices survive preset backup and remain detached from input',()=>{
 const value={...pricing,stockRates:[{material:'oak',thicknessMm:18,stockLengthMm:2440,stockWidthMm:1220,unitCents:7777}]};
 const saved=savePricingPreset([],{id:'stock',name:'Stock sizes',pricing:value}),restored=importPricingPresets([],exportPricingPresets(saved),()=> 'restored');assert.deepEqual(restored[0].pricing.stockRates,value.stockRates);restored[0].pricing.stockRates[0].unitCents=1;assert.equal(saved[0].pricing.stockRates[0].unitCents,7777);
});
