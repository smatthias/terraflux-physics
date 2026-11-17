import {MATERIALS} from './materials.mjs';
export const stockRateKey=value=>[value.material,value.thicknessMm,value.stockLengthMm,value.stockWidthMm].map((v,i)=>i?Math.round(v*1000):v).join(':');
export function validateStockRates(value){
 if(!Array.isArray(value)||value.length>128)throw Error('Expected at most 128 stock price overrides');
 const keys=new Set(),dimension=(v,min,max)=>typeof v==='number'&&Number.isFinite(v)&&v>=min&&v<=max&&Math.abs(v*1000-Math.round(v*1000))<1e-6;
 return value.map(row=>{
  if(!row||!Object.hasOwn(MATERIALS,row.material??'')||!dimension(row.thicknessMm,1,200)||!dimension(row.stockLengthMm,100,10000)||!dimension(row.stockWidthMm,100,10000)||!Number.isSafeInteger(row.unitCents)||row.unitCents<0||row.unitCents>1000000)throw Error('Invalid stock price: catalogue material, thickness 1–200 mm, stock 100–10000 mm, up to three dimension decimals and integer cents 0–1000000');
  const result={material:row.material,thicknessMm:Math.round(row.thicknessMm*1000)/1000,stockLengthMm:Math.round(row.stockLengthMm*1000)/1000,stockWidthMm:Math.round(row.stockWidthMm*1000)/1000,unitCents:row.unitCents},key=stockRateKey(result);
  if(keys.has(key))throw Error('Duplicate stock price for material, thickness and ordered sheet dimensions');keys.add(key);return result;
 });
}

export function stockPricingCoverage(lines,overrides=[]){
 const configured=validateStockRates(overrides),keys=new Set(configured.map(stockRateKey)),used=new Set(),groups=new Map();
 for(const line of lines){
  const key=stockRateKey(line),source=keys.has(key)?'stock':'material';used.add(key);
  if(!groups.has(key))groups.set(key,{material:line.material,thicknessMm:line.thicknessMm,stockLengthMm:line.stockLengthMm,stockWidthMm:line.stockWidthMm,source,unitCents:line.unitCents,batches:[],sheets:0});
  const group=groups.get(key);group.batches.push(line.batch);group.sheets+=line.quantity;
 }
 const stocks=[...groups.values()];
 return {stocks,totalBatches:lines.length,totalSheets:lines.reduce((n,l)=>n+l.quantity,0),overrideSheets:stocks.filter(s=>s.source==='stock').reduce((n,s)=>n+s.sheets,0),fallbackSheets:stocks.filter(s=>s.source==='material').reduce((n,s)=>n+s.sheets,0),zeroRateSheets:stocks.filter(s=>s.unitCents===0).reduce((n,s)=>n+s.sheets,0),unusedOverrides:configured.filter(row=>!used.has(stockRateKey(row)))};
}
export function addCoverageOverrides(existing,stocks){
 const result=validateStockRates(existing),keys=new Set(result.map(stockRateKey));
 for(const stock of stocks){const key=stockRateKey(stock);if(keys.has(key))continue;
  result.push({material:stock.material,thicknessMm:Math.round(stock.thicknessMm*1000)/1000,stockLengthMm:Math.round(stock.stockLengthMm*1000)/1000,stockWidthMm:Math.round(stock.stockWidthMm*1000)/1000,unitCents:stock.unitCents});keys.add(key);
 }
 return validateStockRates(result);
}
