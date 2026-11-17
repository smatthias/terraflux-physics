import {test} from 'node:test';
import assert from 'node:assert/strict';
import {printableQuote} from './quote-print.mjs';
import {validateQuoteDetails} from './quote.mjs';
const quote={id:'id',createdAt:'2026-11-06T12:00:00Z',details:{customer:'<script>Customer</script>',reference:'Q-123',notes:'Line 1\nLine 2'},costing:{revision:2,sourceSha256:'abc',pricing:{currency:'EUR',markupBps:1000,handleUnitCents:250,labourHourlyCents:6000,labourMinutesPerPanel:5},materialCents:20000,hardwareCents:500,labourCents:4000,subtotalCents:24500,markupCents:2450,totalCents:26950,handles:2,labourMinutes:40,lines:[{batch:1,material:'oak',thicknessMm:18,stockLengthMm:2440,stockWidthMm:1220,quantity:2,unitCents:10000,amountCents:20000}]}};
test('printable quotes use saved values and escaped customer metadata with bounded sections',()=>{
 const html=printableQuote(quote,'Job');assert.match(html,/269.50 EUR/);assert.match(html,/Q-123/);assert.match(html,/A4 portrait/);assert.match(html,/&lt;script&gt;Customer/);assert.ok(!html.includes('<script>'));assert.match(html,/Source SHA-256: abc/);assert.match(html,/Page 3 of 3/);
 assert.match(printableQuote({...quote,details:undefined},'Old quote'),/Not specified/);
 const long=printableQuote({...quote,costing:{...quote.costing,lines:Array(25).fill(quote.costing.lines[0])}},'Job');assert.match(long,/Page 5 of 5/);
 assert.throws(()=>validateQuoteDetails({customer:'x'.repeat(121)}));assert.throws(()=>validateQuoteDetails({notes:7}));
});
