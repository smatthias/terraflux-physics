import {test} from 'node:test';
import assert from 'node:assert/strict';
import {cabinetDrawingSheets} from './cabinet-sheets.mjs';
const item={id:'a',type:'cabinet',name:'<Unit & A>',width:1.2,height:1.2,depth:.5,thickness:.018,shelves:1,columns:3,columnWeights:[1,2,1],columnShelves:[0,2,4],columnFronts:['open','single','double'],x:0,z:0,rotation:0};
test('cabinet sheets paginate shared views with fixed paper dimensions and escaped metadata',()=>{
 const html=cabinetDrawingSheets(item);assert.equal((html.match(/class="page"/g)||[]).length,5);assert.match(html,/@page\{size:A4 landscape/);assert.match(html,/width:273mm;height:186mm/);assert.match(html,/&lt;Unit &amp; A&gt;/);assert.match(html,/5 \/ 5/);assert.match(html,/C3 · side cutaway/);assert.match(html,/Shelf centres above cabinet bottom/);assert.doesNotMatch(html,/<script|<\?xml|NaN/);
 assert.match(cabinetDrawingSheets(item,{paper:'A3',orientation:'portrait'}),/width:273mm;height:396mm/);
 assert.match(cabinetDrawingSheets({...item,columns:1,columnWeights:undefined,columnShelves:undefined,columnFronts:undefined}),/3 \/ 3/);
});
test('sheet options and invalid applied geometry fail explicitly',()=>{
 assert.throws(()=>cabinetDrawingSheets(item,{paper:'Letter'}));assert.throws(()=>cabinetDrawingSheets(item,{orientation:'diagonal'}));assert.throws(()=>cabinetDrawingSheets({...item,columnWeights:[0,1,1]}));
});
