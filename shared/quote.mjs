import {stockRateKey,validateStockRates} from './stock-pricing.mjs';
import {cabinetBom} from './cabinet-parts.mjs';
import {WOOD_IDS,METAL_IDS,MATERIALS} from './materials.mjs';
// Oak, birch and white rates are always required; other woods are optional so saved quotes stay unchanged.
const BASE_RATES=['oak','birch','white'];
export function validatePricing(input){
  if(!input||!['EUR','USD','GBP'].includes(input.currency))throw Error('Choose EUR, USD or GBP');
  const integer=(v,max)=>Number.isSafeInteger(v)&&v>=0&&v<=max;
  // Metal sheet rates are optional too (metal panels of placed designs).
  const extraRates=[...WOOD_IDS,...METAL_IDS].filter(k=>!BASE_RATES.includes(k)&&input.sheetRates?.[k]!==undefined);
  if(!BASE_RATES.every(k=>integer(input.sheetRates?.[k],1000000))||!extraRates.every(k=>integer(input.sheetRates[k],1000000))||!integer(input.handleUnitCents,1000000)||!integer(input.labourHourlyCents,1000000)||!integer(input.labourMinutesPerPanel,600)||!integer(input.markupBps,10000))throw Error('Invalid pricing: use non-negative integer cents, minutes (0–600) and markup basis points (0–10000)');
  let edging;
  if(input.edging!==undefined){
    const value=input.edging;
    if(!value||typeof value!=='object'||Array.isArray(value)||!integer(value.materialCentsPerMetre,1000000)||!integer(value.labourCentsPerMetre,1000000)||!integer(value.wasteBps,10000))throw Error('Invalid edging rates: use non-negative integer cents and waste basis points from 0 to 10000');
    edging={materialCentsPerMetre:value.materialCentsPerMetre,labourCentsPerMetre:value.labourCentsPerMetre,wasteBps:value.wasteBps};
  }
  // Prices per metre of profile stock (bars, rods, tubes, angles of placed designs), by profile key.
  let profileRates;
  if(input.profileRates!==undefined){
    const value=input.profileRates;
    if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).length>200||!Object.entries(value).every(([k,v])=>/^[a-z]+\|[a-z-]+\|[0-9.x d]{1,40}$/.test(k)&&integer(v,1000000)))throw Error('Invalid profile prices: use non-negative integer cents per metre');
    profileRates=Object.fromEntries(Object.keys(value).sort().map(k=>[k,value[k]]));
  }
  return {...(input.stockRates!==undefined?{stockRates:validateStockRates(input.stockRates)}:{}),...(edging?{edging}:{}),...(profileRates?{profileRates}:{}),currency:input.currency,sheetRates:Object.fromEntries([...BASE_RATES,...extraRates].map(k=>[k,input.sheetRates[k]])),handleUnitCents:input.handleUnitCents,labourHourlyCents:input.labourHourlyCents,labourMinutesPerPanel:input.labourMinutesPerPanel,markupBps:input.markupBps};
}
export function quoteCosting(manifest,revision,input){
  const pricing=validatePricing(input);
  if(manifest.validation.status==='blocked')throw Error('Resolve handoff blockers before costing a quote');
  const lines=manifest.batches.map(b=>{
    const dimensions={material:b.material,thicknessMm:b.thickness*1000,stockLengthMm:b.layout.stock.stockLengthMm,stockWidthMm:b.layout.stock.stockWidthMm};
    const override=pricing.stockRates?.find(row=>stockRateKey(row)===stockRateKey(dimensions)),unitCents=override?.unitCents??pricing.sheetRates[b.material];
    if(unitCents===undefined)throw Error(`Enter a sheet price for ${MATERIALS[b.material].label}`);
    return {batch:b.number,...dimensions,quantity:b.layout.sheets.length,unitCents,amountCents:b.layout.sheets.length*unitCents,...(pricing.stockRates!==undefined?{rateSource:override?'stock':'material'}:{})};
  });
  const materialCents=lines.reduce((n,l)=>n+l.amountCents,0);
  const handles=manifest.source.room.furniture.filter(c=>c.type!=='design').reduce((n,c)=>n+cabinetBom(c).handles,0),hardwareCents=handles*pricing.handleUnitCents;
  // Profile pieces of placed designs count like panels for labour.
  const labourMinutes=(manifest.summary.panels+(manifest.summary.profilePieces??0))*pricing.labourMinutesPerPanel,labourCents=Math.round(labourMinutes*pricing.labourHourlyCents/60);
  let profiles;
  if(manifest.profiles?.length){
    const lines=manifest.profiles.map(g=>{
      const centsPerMetre=pricing.profileRates?.[g.key];
      if(centsPerMetre===undefined)throw Error(`Enter a price per metre for ${g.label}`);
      return {key:g.key,label:g.label,pieces:g.count,lengthMm:g.totalLengthMm,centsPerMetre,amountCents:Math.round(g.totalLengthMm*centsPerMetre/1000)};
    });
    profiles={lines,totalCents:lines.reduce((n,l)=>n+l.amountCents,0)};
  }
  let edging;
  if(pricing.edging){
    const netLengthMm=Math.round(manifest.source.room.furniture.filter(item=>item.type!=='design').reduce((sum,item)=>sum+cabinetBom(item).rows.reduce((n,row)=>n+row.bandLengthMetres,0),0)*1000);
    const purchaseLengthMm=Math.ceil(netLengthMm*(10000+pricing.edging.wasteBps)/10000);
    const materialCents=Math.round(purchaseLengthMm*pricing.edging.materialCentsPerMetre/1000),labourCents=Math.round(netLengthMm*pricing.edging.labourCentsPerMetre/1000);
    edging={netLengthMm,purchaseLengthMm,materialCents,labourCents,totalCents:materialCents+labourCents};
  }
  const subtotalCents=materialCents+hardwareCents+labourCents+(edging?.totalCents??0)+(profiles?.totalCents??0),markupCents=Math.round(subtotalCents*pricing.markupBps/10000);
  const totalCents=subtotalCents+markupCents;
  if(!Number.isSafeInteger(totalCents))throw Error('Quote total exceeds supported precision');
  return {...(edging?{edging}:{}),...(profiles?{profiles}:{}),version:1,sourceSha256:manifest.sourceSha256,revision,pricing,lines,handles,labourMinutes,materialCents,hardwareCents,labourCents,subtotalCents,markupCents,totalCents,taxIncluded:false};
}
export function moneyToCents(value){
  if(typeof value!=='string'||!/^\d{1,5}(\.\d{1,2})?$/.test(value))throw Error('Enter prices with up to two decimals');
  const [major,minor='']=value.split('.');return Number(major)*100+Number(minor.padEnd(2,'0'));
}
export function validateQuoteDetails(input={}){
  if(!input||typeof input!=='object'||Array.isArray(input))throw Error('Invalid quote details');
  const result={};
  for(const [field,max] of [['customer',120],['reference',80],['notes',1000],['customerContact',1000],['customerId',64]]){
    const value=input[field]??'';
    if(typeof value!=='string'||value.length>max)throw Error('Invalid quote '+field);
    result[field]=value.trim();
  }
  if(result.customerId&&!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(result.customerId))throw Error('Invalid quote customer ID');
  return result;
}
