import {test} from 'node:test';
import assert from 'node:assert/strict';
import {cabinetElevationSvg} from './cabinet-elevation.mjs';
const item={id:'a',type:'cabinet',name:'<A & B>',width:1.2,height:1.2,depth:.5,thickness:.018,shelves:1,columns:3,columnWeights:[1,2,1],columnShelves:[0,2,4],columnFronts:['open','single','double'],x:2,z:1,rotation:45};
test('elevation contains both views, dimension registers and applied panel geometry',()=>{
 const svg=cabinetElevationSvg(item);assert.match(svg,/data-view="interior"/);assert.match(svg,/data-view="front"/);assert.match(svg,/&lt;A &amp; B&gt;/);
 assert.match(svg,/1200.0 mm/);assert.match(svg,/282.0/);assert.match(svg,/564.0/);assert.match(svg,/depth 500.0/);assert.match(svg,/shelf centres above cabinet bottom/);
 assert.match(svg,/data-part="front-column-2"/);assert.equal((svg.match(/data-part="front-column-3"/g)||[]).length,2);assert.equal((svg.match(/data-bay-register=/g)||[]).length,3);
 assert.doesNotMatch(svg,/NaN|undefined|<script/);assert.match(svg,/no physical print scale/);
 assert.equal(svg,cabinetElevationSvg({...item,x:-4,z:-3,rotation:0}));
});
test('elevations support legacy and empty bays and reject invalid designs',()=>{
 const legacy={...item,columns:undefined,columnWeights:undefined,columnShelves:undefined,columnFronts:undefined};assert.equal((cabinetElevationSvg(legacy).match(/data-part="front"/g)||[]).length,2);
 assert.match(cabinetElevationSvg({...item,columnShelves:[0,0,0]}),/centres above cabinet bottom \(mm\): none/);
 assert.throws(()=>cabinetElevationSvg({...item,columnWeights:[0,1,1]}));
});
test('per-bay side cutaways show depth intervals and respect open fronts and shelf counts',()=>{
 const svg=cabinetElevationSvg(item);
 assert.equal((svg.match(/data-view="side"/g)||[]).length,3);
 assert.equal((svg.match(/data-side-part="front"/g)||[]).length,2);
 assert.equal((svg.match(/data-side-part="shelf"/g)||[]).length,6);
 assert.match(svg,/back panel 0–18.0 mm/);assert.match(svg,/shelf\/divider 18.0–482.0 mm/);assert.match(svg,/closed front 482.0–500.0 mm/);assert.match(svg,/Shelf\/divider depth 464.0 mm/);
 assert.match(svg,/sides\/dividers and handles omitted/);
 const changed=cabinetElevationSvg({...item,depth:.8,thickness:.02});assert.match(changed,/Shelf\/divider depth 760.0 mm/);assert.match(changed,/closed front 780.0–800.0 mm/);
 const open=cabinetElevationSvg({...item,columnFronts:['open','open','open'],columnShelves:[0,0,0]});assert.doesNotMatch(open,/data-side-part="front"|data-side-part="shelf"/);
});
