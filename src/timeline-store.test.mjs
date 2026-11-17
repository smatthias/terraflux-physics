import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createTimelineStore,memoryBackend} from './timeline-store.js';

const timeline=n=>({format:'terratec-timeline',version:1,entries:[{seq:n}]});
function counting() {
  const backend=memoryBackend(),puts=[];
  return {puts,backend:{...backend,put:async record=>{puts.push(record.key);await backend.put(record);}}};
}
test('saves are batched; the latest history of each project is written once',async()=>{
  const {backend,puts}=counting();let clock=0;
  const store=createTimelineStore({backend,delay:20,now:()=>new Date(++clock*1000)});
  store.save('a',timeline(1));store.save('a',timeline(2));store.save('b',timeline(3));
  assert.deepEqual(puts,[]);
  await new Promise(r=>setTimeout(r,60));
  assert.deepEqual(puts,['a','b']);
  assert.deepEqual(await store.load('a'),timeline(2));
  assert.equal(await store.load('missing'),null);
});
test('loading or removing writes pending saves first',async()=>{
  const store=createTimelineStore({backend:memoryBackend(),delay:10000});
  store.save('a',timeline(1));
  assert.deepEqual(await store.load('a'),timeline(1));
  store.save('b',timeline(2));await store.remove('b');
  assert.equal(await store.load('b'),null);
  store.save('c',timeline(3));await store.flush();
  assert.deepEqual(await store.load('c'),timeline(3));
});
test('only the most recently used histories are kept',async()=>{
  let clock=0;const backend=memoryBackend();
  const store=createTimelineStore({backend,keep:3,now:()=>new Date(++clock*1000)});
  for(const key of ['a','b','c','d']) {store.save(key,timeline(1));await store.flush();}
  store.save('b',timeline(2));await store.flush();
  store.save('e',timeline(1));await store.flush();
  assert.deepEqual(await backend.keys(),['d','b','e']);
});
test('write failures are reported, and later saves still work',async()=>{
  const backend=memoryBackend(),errors=[];let fail=true;
  const store=createTimelineStore({backend:{...backend,put:async r=>{if(fail) throw Error('Quota exceeded');await backend.put(r);}},delay:5,onError:e=>errors.push(e.message)});
  store.save('a',timeline(1));await new Promise(r=>setTimeout(r,30));
  assert.deepEqual(errors,['Quota exceeded']);
  fail=false;store.save('a',timeline(2));await store.flush();
  assert.deepEqual(await store.load('a'),timeline(2));
});
