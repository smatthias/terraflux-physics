// Browser copy of project timelines (version history), keyed like recovery
// drafts ('draft-…' or the saved project ID). Uses IndexedDB, which has room for
// histories of several MiB; without it (private modes, tests) the history is
// kept for this session only. Writes are batched and the oldest histories
// beyond `keep` are removed.
const STORE='timelines';
export function indexedDbBackend(name='terratec-timelines') {
  let db;
  const open=()=>db??=new Promise((resolve,reject)=>{
    const request=indexedDB.open(name,1);
    request.onupgradeneeded=()=>request.result.createObjectStore(STORE,{keyPath:'key'}).createIndex('updatedAt','updatedAt');
    request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);
  });
  const run=async(mode,work)=>{
    const transaction=(await open()).transaction(STORE,mode),request=work(transaction.objectStore(STORE));
    return new Promise((resolve,reject)=>{transaction.oncomplete=()=>resolve(request.result);transaction.onerror=transaction.onabort=()=>reject(transaction.error);});
  };
  return {
    get:key=>run('readonly',store=>store.get(key)),
    put:record=>run('readwrite',store=>store.put(record)),
    delete:key=>run('readwrite',store=>store.delete(key)),
    // Keys, oldest first.
    keys:()=>run('readonly',store=>store.index('updatedAt').getAllKeys()),
  };
}
export function memoryBackend() {
  const records=new Map();
  return {
    get:async key=>records.get(key),
    put:async record=>{records.set(record.key,structuredClone(record));},
    delete:async key=>{records.delete(key);},
    keys:async()=>[...records.values()].sort((a,b)=>a.updatedAt.localeCompare(b.updatedAt)).map(r=>r.key),
  };
}
export function createTimelineStore({backend=globalThis.indexedDB?indexedDbBackend():memoryBackend(),delay=1000,keep=20,now=()=>new Date(),onError=()=>{}}={}) {
  const pending=new Map();let timer=null,writing=Promise.resolve();
  const write=async()=>{
    timer=null;
    const batch=[...pending];pending.clear();
    for(const [key,timeline] of batch) await backend.put({key,updatedAt:now().toISOString(),timeline});
    if(batch.length) {const keys=await backend.keys();for(const key of keys.slice(0,Math.max(0,keys.length-keep))) await backend.delete(key);}
  };
  const flush=()=>{clearTimeout(timer);timer=null;return writing=writing.then(write,write);};
  return {
    // Saves after `delay` ms; later saves of the same key replace earlier ones.
    save(key,timeline) {pending.set(key,timeline);if(!timer) timer=setTimeout(()=>flush().catch(onError),delay);},
    flush,
    async load(key) {await flush();return (await backend.get(key))?.timeline??null;},
    async remove(key) {pending.delete(key);await flush();await backend.delete(key);},
  };
}
