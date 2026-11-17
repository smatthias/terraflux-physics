// Project timeline: every committed change to a project becomes a timestamped
// version, so any earlier state can be rebuilt, previewed and restored.
//
// Versions store what changed, not whole projects: a structural diff in which
// arrays of objects with stable IDs (walls, openings, furniture, designs, their
// layers and parts, presets…) are compared by ID. Every `keyframeEvery`-th
// version, and the first, stores the full project instead, so rebuilding a
// state applies at most that many diffs. Each version also lists the project
// elements it changed (element keys such as 'furniture:<id>' or 'design:<id>').
// Timelines are plain JSON, limited in versions and size; the oldest versions
// are dropped first, keeping a full project at the new start.
export const TIMELINE_FORMAT='terratec-timeline';
export const TIMELINE_LIMITS=Object.freeze({versions:2000,bytes:2*1024*1024,keyframeEvery:50,coalesceMs:3000});

// ---- Structural diff and patch
const isObject=v=>v!==null&&typeof v==='object'&&!Array.isArray(v);
const isKeyed=a=>Array.isArray(a)&&a.every(x=>isObject(x)&&typeof x.id==='string')&&new Set(a.map(x=>x.id)).size===a.length;
const clone=v=>v===undefined?undefined:JSON.parse(JSON.stringify(v));
export function deepEqual(a,b) {
  if(a===b) return true;
  if(Array.isArray(a)) return Array.isArray(b)&&a.length===b.length&&a.every((x,i)=>deepEqual(x,b[i]));
  if(isObject(a)) {
    if(!isObject(b)) return false;
    const ka=Object.keys(a),kb=Object.keys(b);
    return ka.length===kb.length&&ka.every(k=>Object.hasOwn(b,k)&&deepEqual(a[k],b[k]));
  }
  return false;
}
// Patch forms: {$set: value}, {$del: 1}, {$obj: {key: patch}} and
// {$keyed: order, $items: {id: patch or {$new: value}}} for arrays keyed by ID.
export function diff(a,b) {
  if(deepEqual(a,b)) return undefined;
  if(isKeyed(a)&&isKeyed(b)&&(a.length||b.length)) {
    const before=new Map(a.map(x=>[x.id,x])),items=Object.create(null);
    for(const item of b) {
      const old=before.get(item.id);
      if(!old) items[item.id]={$new:clone(item)};
      else {const d=diff(old,item);if(d) items[item.id]=d;}
    }
    return {$keyed:b.map(x=>x.id),$items:items};
  }
  if(isObject(a)&&isObject(b)) {
    const out=Object.create(null);
    for(const key of new Set([...Object.keys(a),...Object.keys(b)])) {
      if(!Object.hasOwn(b,key)) out[key]={$del:1};
      else {const d=diff(a[key],b[key]);if(d) out[key]=d;}
    }
    return {$obj:out};
  }
  return {$set:clone(b)};
}
// Own-property write, so keys such as '__proto__' stay plain data.
const put=(object,key,value)=>Object.defineProperty(object,key,{value,enumerable:true,writable:true,configurable:true});
export function applyPatch(a,patch) {
  if(Object.hasOwn(patch,'$set')) return clone(patch.$set);
  if(patch.$keyed) {
    const before=new Map((Array.isArray(a)?a:[]).map(x=>[x.id,x]));
    return patch.$keyed.map(id=>{
      const p=Object.hasOwn(patch.$items,id)?patch.$items[id]:undefined;
      if(!p) {if(!before.has(id)) throw Error('Timeline patch refers to a missing item');return clone(before.get(id));}
      return Object.hasOwn(p,'$new')?clone(p.$new):applyPatch(before.get(id),p);
    });
  }
  if(patch.$obj) {
    const out={...(isObject(a)?a:{})};
    for(const [key,p] of Object.entries(patch.$obj)) {if(p.$del) delete out[key];else put(out,key,applyPatch(Object.hasOwn(out,key)?out[key]:undefined,p));}
    return out;
  }
  throw Error('Invalid timeline patch');
}

// ---- Project elements: the units whose versions are tracked and restorable.
export const ELEMENT_KINDS=Object.freeze({project:'Project',room:'Room shell',opening:'Opening',furniture:'Furniture',design:'Design',preset:'Opening preset',furniturePreset:'Furniture preset',workshopProfile:'Workshop profile'});
const SHELL_KEYS=['width','depth','height','outline','walls','locked'];
export function projectElements(project) {
  const elements=new Map(),add=(kind,id,name,value)=>elements.set(id===null?kind:kind+':'+id,{kind,id,name,value});
  add('project',null,project.name,{name:project.name});
  add('room',null,'Room shell',Object.fromEntries(SHELL_KEYS.filter(k=>project.room[k]!==undefined).map(k=>[k,project.room[k]])));
  for(const o of project.room.openings??[]) add('opening',o.id,`${o.type==='door'?'Door':'Window'} on ${o.wall}`,o);
  for(const f of project.room.furniture??[]) add('furniture',f.id,f.name,f);
  for(const d of project.furnitureDesigns??[]) add('design',d.id,d.name,d);
  for(const p of project.presets??[]) add('preset',p.id,p.name,p);
  for(const p of project.furniturePresets??[]) add('furniturePreset',p.id,p.name,p);
  for(const p of project.workshopProfiles??[]) add('workshopProfile',p.id,p.name,p);
  return elements;
}
const KIND_ORDER=Object.keys(ELEMENT_KINDS);
// Elements added, changed or removed between two project states.
export function changedElements(before,after) {
  const a=projectElements(before),b=projectElements(after),changes=[];
  for(const [key,el] of b) {
    if(!a.has(key)) changes.push({key,kind:el.kind,name:el.name,action:'added'});
    else if(!deepEqual(a.get(key).value,el.value)) changes.push({key,kind:el.kind,name:el.name,action:'changed'});
  }
  for(const [key,el] of a) if(!b.has(key)) changes.push({key,kind:el.kind,name:el.name,action:'removed'});
  return changes.sort((x,y)=>KIND_ORDER.indexOf(x.kind)-KIND_ORDER.indexOf(y.kind)||x.name.localeCompare(y.name));
}
// A short label for versions recorded without one.
export function describeChanges(changes) {
  if(changes.length!==1) return changes.length+' elements changed';
  const [{kind,name,action}]=changes;
  return kind==='project'?'Project renamed':kind==='room'?'Room shell changed':`${ELEMENT_KINDS[kind]} “${name}” ${action}`;
}
// A project with one element replaced, restored or removed (value undefined).
export function withElement(project,key,value) {
  const [kind,...rest]=key.split(':'),id=rest.join(':'),next=clone(project);
  const setIn=(list,item)=>{const index=list.findIndex(x=>x.id===id);if(item===undefined) {if(index>=0) list.splice(index,1);} else if(index>=0) list[index]=clone(item);else list.push(clone(item));return list;};
  if(kind==='project') {if(value) next.name=value.name;}
  else if(kind==='room') {for(const k of SHELL_KEYS) delete next.room[k];Object.assign(next.room,clone(value));}
  else if(kind==='opening') setIn(next.room.openings,value);
  else if(kind==='furniture') setIn(next.room.furniture,value);
  else if(kind==='design') next.furnitureDesigns=setIn(next.furnitureDesigns??[],value);
  else if(kind==='preset') setIn(next.presets,value);
  else if(kind==='furniturePreset') next.furniturePresets=setIn(next.furniturePresets??[],value);
  else if(kind==='workshopProfile') setIn(next.workshopProfiles,value);
  else throw Error('Unknown project element');
  return next;
}

// ---- Timelines
const timeOf=entry=>Date.parse(entry.at);
export function createTimeline(project,{at,label='Start of history'}) {
  return {format:TIMELINE_FORMAT,version:1,entries:[{seq:1,at:new Date(at).toISOString(),label,changes:[],keyframe:clone(project)}]};
}
// Rebuilds the project at a version (by sequence number).
export function stateAt(timeline,seq) {
  const end=timeline.entries.findIndex(e=>e.seq===seq);
  if(end<0) throw Error('Version not found');
  let start=end;while(!timeline.entries[start].keyframe) start--;
  let state=clone(timeline.entries[start].keyframe);
  for(let i=start+1;i<=end;i++) state=applyPatch(state,timeline.entries[i].patch);
  return state;
}
export const headState=timeline=>stateAt(timeline,timeline.entries.at(-1).seq);
// The latest version at or before a time (or the first version).
export function versionAtTime(timeline,time) {
  const t=typeof time==='number'?time:Date.parse(time);
  let found=timeline.entries[0];
  for(const entry of timeline.entries) if(timeOf(entry)<=t) found=entry;
  return found;
}
// Adds a version for the change from `before` to `after`. Quick repeats of the
// same edit to the same single element (typing a name, dragging a colour) within
// `coalesceMs` update the previous version instead of adding one.
export function appendVersion(timeline,before,after,{at,label}) {
  const changes=changedElements(before,after);
  if(!changes.length) return timeline;
  const entries=[...timeline.entries],last=entries.at(-1),time=new Date(at).toISOString();
  label=String(label||describeChanges(changes)).slice(0,120);
  const sameEdit=entries.length>1&&!last.keyframe&&last.label===label&&last.changes.length===1&&changes.length===1&&last.changes[0].key===changes[0].key&&
    Date.parse(time)-timeOf(last)<=TIMELINE_LIMITS.coalesceMs;
  if(sameEdit) {
    const base=stateAt({...timeline,entries},entries.at(-2).seq),merged=changedElements(base,after);
    if(!merged.length) entries.pop();
    else entries[entries.length-1]={...last,at:time,changes:merged,patch:diff(base,after)};
    return compactTimeline({...timeline,entries});
  }
  let sinceKeyframe=0;for(let i=entries.length-1;i>=0&&!entries[i].keyframe;i--) sinceKeyframe++;
  const entry={seq:last.seq+1,at:time,label,changes};
  if(sinceKeyframe+1>=TIMELINE_LIMITS.keyframeEvery) entry.keyframe=clone(after);else entry.patch=diff(before,after);
  entries.push(entry);
  return compactTimeline({...timeline,entries});
}
// Drops the oldest versions beyond the limits; the new first version becomes a
// full project so every remaining version can still be rebuilt.
// Each version's size is measured once, so checking the limit stays cheap.
const sizes=new WeakMap();
const entrySize=entry=>{let n=sizes.get(entry);if(n===undefined) {n=new TextEncoder().encode(JSON.stringify(entry)).length+1;sizes.set(entry,n);}return n;};
export function compactTimeline(timeline,limits=TIMELINE_LIMITS) {
  let entries=timeline.entries;
  const size=list=>list.reduce((n,e)=>n+entrySize(e),64);
  const rebase=(list,from)=>{const first=list[from];const state=stateAt({...timeline,entries:list},first.seq);const {patch,...rest}=first;return [{...rest,keyframe:state},...list.slice(from+1)];};
  if(entries.length>limits.versions) entries=rebase(entries,entries.length-limits.versions);
  while(entries.length>1&&size(entries)>limits.bytes) entries=rebase(entries,Math.max(1,Math.ceil(entries.length/10)));
  return entries===timeline.entries?timeline:{...timeline,entries};
}
// Versions that changed one element, newest first, with that element's value then.
export function elementHistory(timeline,key) {
  const out=[];
  for(const entry of timeline.entries) {
    const change=entry.changes.find(c=>c.key===key);
    if(change) out.push({seq:entry.seq,at:entry.at,label:entry.label,action:change.action});
  }
  return out.reverse().map((item,i,list)=>({...item,version:list.length-i}));
}
export const elementAt=(timeline,seq,key)=>projectElements(stateAt(timeline,seq)).get(key)?.value;

// Structure check for stored timelines; `validateState` (e.g. validateProject)
// checks every full project and the final state.
export function validateTimeline(value,validateState) {
  if(value?.format!==TIMELINE_FORMAT||value.version!==1||!Array.isArray(value.entries)||!value.entries.length||value.entries.length>TIMELINE_LIMITS.versions) throw Error('Invalid project timeline');
  let previous=0,state;
  for(const [i,entry] of value.entries.entries()) {
    if(!Number.isSafeInteger(entry?.seq)||entry.seq<=previous||typeof entry.at!=='string'||!Number.isFinite(Date.parse(entry.at))||typeof entry.label!=='string'||entry.label.length>120||
      !Array.isArray(entry.changes)||entry.changes.length>500||(i===0&&!entry.keyframe)||(entry.keyframe===undefined)===(entry.patch===undefined)||
      !entry.changes.every(c=>typeof c?.key==='string'&&Object.hasOwn(ELEMENT_KINDS,c.kind)&&typeof c.name==='string'&&['added','changed','removed'].includes(c.action)))
      throw Error('Invalid project timeline version');
    previous=entry.seq;
    // Rebuild every version once, so a damaged diff anywhere is found now.
    if(entry.keyframe) {if(!isObject(entry.keyframe)) throw Error('Invalid project timeline version');state=clone(entry.keyframe);if(validateState) validateState(state);}
    else {if(!isObject(entry.patch)) throw Error('Invalid project timeline version');state=applyPatch(state,entry.patch);}
  }
  if(validateState) validateState(state);
  return value;
}
