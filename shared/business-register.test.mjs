import {test} from 'node:test';
import assert from 'node:assert/strict';
import {filterCustomers,exportCustomerBackup,parseCustomerBackup} from './customers.mjs';
import {quoteRegister,filterQuotes} from './quote-register.mjs';
test('customer search matches all terms and backups validate without importing old identities',()=>{
 const customers=[{id:'old',name:'Berlin Studio',email:'hello@example.com',phone:'123',address:'Mitte',notes:''}];
 assert.equal(filterCustomers(customers,' BERLIN mitte ').length,1);assert.equal(filterCustomers(customers,'missing').length,0);
 const restored=parseCustomerBackup(JSON.parse(exportCustomerBackup(customers[0])));assert.equal(restored.id,undefined);assert.equal(restored.email,'hello@example.com');
 assert.throws(()=>parseCustomerBackup({format:'bad'}),/format/);
 assert.throws(()=>parseCustomerBackup({format:'terratec-customer-backup',version:1,customer:{name:'A',email:'bad'}}),/email/);
});
test('quote register preserves revision/source identities and filters saved customer/status data',()=>{
 const quote={id:'q',createdAt:'2026-11-07T10:00:00Z',details:{customer:'Saved Customer',reference:'Q-1'},costing:{pricing:{currency:'EUR'},totalCents:1234,sourceSha256:'hash'}};
 const records=[{id:'a',name:'Kitchen',revisions:[{number:1,quotes:[quote]},{number:2,quotes:[{...quote,id:'q2',lifecycle:{status:'accepted'},costing:{...quote.costing,pricing:{currency:'USD'}}}]}]}];
 const rows=quoteRegister(records);assert.equal(rows.length,2);assert.equal(filterQuotes(rows,'customer kitchen','accepted').length,1);assert.equal(filterQuotes(rows,'none').length,0);assert.deepEqual(rows.map(q=>q.currency),['EUR','USD']);assert.equal(rows[0].revision,1);assert.equal(rows[0].sourceSha256,'hash');
});
