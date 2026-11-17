import {test} from 'node:test';
import assert from 'node:assert/strict';
import {manufacturingBatches,manufacturingCsv} from './manufacturing.mjs';
const item={id:'a',name:'Cabinet',width:.9,height:1.2,depth:.5,thickness:.018,shelves:1};
test('compatible cabinets share a batch with unique panel provenance and stable ordering',()=>{
  const cabinets=[item,{...item,id:'b',name:'Second',lengthAllowanceMm:8}];
  const [batch]=manufacturingBatches(cabinets);assert.equal(batch.cabinets.length,2);assert.equal(batch.panelCount,16);
  const pieces=[...batch.layout.sheets.flatMap(s=>s.panels),...batch.layout.unplaced];
  assert.equal(pieces.length,16);assert.equal(new Set(pieces.map(p=>p.id)).size,16);
  assert.equal(pieces.filter(p=>p.cabinetId==='a').length,8);assert.equal(pieces.filter(p=>p.cabinetName==='Second').length,8);
  assert.deepEqual(manufacturingBatches([...cabinets].reverse()),[batch]);
  assert.equal(manufacturingBatches([item,{...item,id:'b',material:'oak',stockLengthMm:2440,stockWidthMm:1220,stockTrimMm:0,stockKerfMm:3,stockAllowRotation:true}]).length,1);
});
test('every incompatible manufacturing setting creates a separate batch',()=>{
  for(const change of [{material:'birch'},{thickness:.02},{stockLengthMm:2500},{stockWidthMm:1250},{stockTrimMm:5},{stockKerfMm:4},{stockAllowRotation:false}])assert.equal(manufacturingBatches([item,{...item,id:'b',...change}]).length,2);
});
test('project export includes placed and oversize panels, source IDs and stock context',()=>{
  const furniture=[{...item,name:'=formula'},{...item,id:'b',stockLengthMm:100,stockWidthMm:100}];
  const csv=manufacturingCsv(furniture);assert.match(csv,/"'=formula"/);assert.match(csv,/"oversize"/);assert.match(csv,/"placed"/);assert.match(csv,/cabinet_id/);
  assert.equal(csv.trim().split('\r\n').length,17);assert.equal(new Set(csv.trim().split('\r\n').map(r=>r.split(',').length)).size,1);
  assert.deepEqual(manufacturingBatches([]),[]);assert.equal(manufacturingCsv([]).trim().split('\r\n').length,1);
});
