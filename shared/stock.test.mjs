import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validateStock,panelStockFit} from './stock.mjs';
import {cabinetBom,cabinetBomCsv} from './cabinet-parts.mjs';
import {validateFurniture} from './furniture.mjs';
import {rectangle} from './geometry.mjs';
const item={id:'a',name:'Cabinet',type:'cabinet',width:1.1,depth:.5,height:1.2,thickness:.018,shelves:3,x:0,z:0,rotation:0};
test('stock fit considers trim, exact boundaries and optional rotation',()=>{
  const stock={stockLengthMm:1000,stockWidthMm:600,stockTrimMm:10};
  assert.equal(panelStockFit(.98,.58,stock),'direct');
  assert.equal(panelStockFit(.58,.98,stock),'rotated');
  assert.equal(panelStockFit(.58,.98,{...stock,stockAllowRotation:false}),'oversize');
  assert.equal(panelStockFit(.981,.58,stock),'oversize');
  for(const change of [{stockLengthMm:0},{stockWidthMm:'1220'},{stockTrimMm:-1},{stockLengthMm:100,stockTrimMm:50},{stockAllowRotation:'true'},{stockTrimMm:NaN}]) assert.throws(()=>validateStock(change),/Stock/);
});
test('BOM checks blank allowances, counts oversize quantities and persists stock settings',()=>{
  const stock={...item,stockLengthMm:1200,stockWidthMm:1100,stockTrimMm:0,stockAllowRotation:true};
  assert.equal(cabinetBom(stock).allFit,true);
  const configured={...stock,lengthAllowanceMm:1,stockTrimMm:1};
  const bom=cabinetBom(configured);assert.equal(bom.allFit,false);assert.equal(bom.oversizeCount,2);
  const saved=validateFurniture([configured],rectangle(6,4),3)[0];
  assert.equal(saved.stockTrimMm,1);assert.equal(saved.stockLengthMm,1200);
  assert.equal(validateFurniture([item],rectangle(6,4),3)[0].stockLengthMm,2440);
  const csv=cabinetBomCsv(saved);assert.match(csv,/stock_fit/);assert.match(csv,/oversize/);
  assert.equal(new Set(csv.trim().split('\r\n').map(r=>r.split(',').length)).size,1);
});
