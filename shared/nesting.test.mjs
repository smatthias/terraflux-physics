import {test} from 'node:test';
import assert from 'node:assert/strict';
import {packPanels,nestCabinet,nestingCsv} from './nesting.mjs';
import {validateStock} from './stock.mjs';
const item={name:'Cabinet',width:1.1,height:1.2,depth:.5,thickness:.018,shelves:3};
function check(layout,expected){
  const all=layout.sheets.flatMap(s=>s.panels);assert.equal(all.length+layout.unplaced.length,expected);
  assert.equal(new Set([...all,...layout.unplaced].map(p=>p.id)).size,expected);
  const t=layout.stock.stockTrimMm,k=layout.stock.stockKerfMm;
  for(const s of layout.sheets)for(const p of s.panels){
    assert.ok(p.x>=t-1e-7&&p.y>=t-1e-7);
    assert.ok(p.x+p.width<=layout.stock.stockLengthMm-t+1e-7&&p.y+p.height<=layout.stock.stockWidthMm-t+1e-7);
    for(const q of s.panels)if(p!==q)assert.ok(p.x+p.width+k<=q.x+1e-7||q.x+q.width+k<=p.x+1e-7||p.y+p.height+k<=q.y+1e-7||q.y+q.height+k<=p.y+1e-7);
  }
  assert.ok(layout.utilisation>=0&&layout.utilisation<=1);
}
test('cabinet layout is deterministic, accounts for every panel and maintains kerf and trim',()=>{
  const settings={...item,stockTrimMm:8,stockKerfMm:4};
  const layout=nestCabinet(settings);check(layout,10);assert.deepEqual(layout,nestCabinet(settings));assert.equal(layout.unplaced.length,0);
  const csv=nestingCsv({...settings,name:'=unsafe'});assert.match(csv,/kerf_mm/);assert.match(csv,/"'=unsafe"/);assert.equal(csv.trim().split('\r\n').length,11);
});
test('packing handles exact fit, rotation, oversize and kerf-induced extra sheets',()=>{
  const parts=[{id:'a',length:500,width:500},{id:'b',length:500,width:500}];
  const stock={stockLengthMm:1000,stockWidthMm:500,stockKerfMm:0,stockAllowRotation:false};
  assert.equal(packPanels(parts,stock).sheets.length,1);
  const spaced=packPanels(parts,{...stock,stockKerfMm:3});assert.equal(spaced.sheets.length,2);check(spaced,2);
  const rotated=packPanels([{id:'r',length:400,width:900}],stock);assert.equal(rotated.unplaced.length,1);
  assert.equal(packPanels([{id:'r',length:400,width:900}],{...stock,stockAllowRotation:true}).sheets[0].panels[0].rotated,true);
  const oversized=packPanels([{id:'x',length:2000,width:2000}],stock);assert.equal(oversized.sheets.length,0);check(oversized,1);
  for(const value of [-1,21,NaN,'3'])assert.throws(()=>validateStock({stockKerfMm:value}),/kerf/);
});
test('varied deterministic packing cases preserve geometry invariants',()=>{
  for(let seed=1;seed<=24;seed++){
    const parts=Array.from({length:18},(_,i)=>({id:'p'+i,length:100+(seed*137+i*173)%1200,width:80+(seed*89+i*223)%900}));
    check(packPanels(parts,{stockLengthMm:1200,stockWidthMm:800,stockTrimMm:10,stockKerfMm:seed%7,stockAllowRotation:seed%2===0}),18);
  }
});
