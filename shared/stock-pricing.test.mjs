import {test} from 'node:test';
import assert from 'node:assert/strict';
import {stockPricingCoverage,addCoverageOverrides} from './stock-pricing.mjs';
const line={batch:1,material:'oak',thicknessMm:18,stockLengthMm:2440,stockWidthMm:1220,quantity:2,unitCents:10000};
const override={material:'oak',thicknessMm:18,stockLengthMm:2440,stockWidthMm:1220,unitCents:10000};
test('coverage aggregates identical stocks and distinguishes fallback, zero and unused prices',()=>{
 const lines=[line,{...line,batch:2,quantity:1},{...line,batch:3,material:'birch',quantity:4,unitCents:0}],unused={...override,thicknessMm:20};
 const review=stockPricingCoverage(lines,[override,unused]);assert.equal(review.stocks.length,2);assert.equal(review.totalBatches,3);assert.equal(review.totalSheets,7);assert.equal(review.overrideSheets,3);assert.equal(review.fallbackSheets,4);assert.equal(review.zeroRateSheets,4);assert.deepEqual(review.stocks[0].batches,[1,2]);assert.deepEqual(review.unusedOverrides,[unused]);
 assert.equal(stockPricingCoverage([],[override]).unusedOverrides.length,1);
});
test('creating missing overrides copies effective rates without duplicates, mutation or partial overflow',()=>{
 const review=stockPricingCoverage([line,{...line,batch:2,material:'birch',unitCents:0}]),before=JSON.stringify(review);
 const result=addCoverageOverrides([],review.stocks);assert.equal(result.length,2);assert.equal(result[1].unitCents,0);assert.equal(JSON.stringify(review),before);assert.equal(addCoverageOverrides(result,review.stocks).length,2);
 const existing=Array.from({length:128},(_,i)=>({...override,stockLengthMm:100+i})),snapshot=JSON.stringify(existing);assert.throws(()=>addCoverageOverrides(existing,review.stocks),/128/);assert.equal(JSON.stringify(existing),snapshot);
});
