import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as T from './timeline.mjs';
import {validateProject} from './project.mjs';
import {rectangle,moveCorner} from './geometry.mjs';
import {moveFurniture} from './furniture.mjs';
import {deleteFurniture,duplicateFurniture} from './furniture-clipboard.mjs';
import * as D from './furniture-design-edit.mjs';
const cabinet=(id,x,z=0)=>({id,type:'cabinet',name:'Unit '+id,width:.8,depth:.5,height:1,thickness:.018,shelves:1,x,z,rotation:0});
const base=()=>validateProject({schemaVersion:7,units:'m',name:'Studio',presets:[],furnitureDesigns:[D.createDesign({id:'box',name:'Box',template:'box',makeId:(()=>{let n=0;return ()=>'b'+(++n);})()})],
  room:{height:2.8,outline:rectangle(8,6),openings:[{id:'door',type:'door',wall:'north',offset:1,width:.9,height:2,sill:0}],furniture:[cabinet('a',-2),cabinet('b',0)]}});
const normal=p=>validateProject({...p,schemaVersion:7});
const MIN=60000,START=Date.parse('2024-05-01T08:00:00Z');

// Random but valid edits, as the app would make them.
function randomEdits(count,seed=11) {
  let s=seed;const random=()=>(s=(s*1103515245+12345)%2147483648)/2147483648;let n=0;
  const states=[base()];
  for(let i=0;i<count;i++) {
    const p=states.at(-1),room=p.room,pick=list=>list[Math.floor(random()*list.length)];
    const edits=[
      ()=>({...p,room:moveFurniture(room,pick(room.furniture).id,{x:random()*6-3,z:random()*4-2},.25,true)}),
      ()=>({...p,room:duplicateFurniture(room,[pick(room.furniture).id],{makeId:()=>'n'+(++n)}).room}),
      ()=>room.furniture.length>1?{...p,room:deleteFurniture(room,[pick(room.furniture).id])}:p,
      ()=>({...p,room:{...room,outline:moveCorner(room.outline,'north-west',{x:-4-random(),z:-3-random()},.25,true)}}),
      ()=>({...p,name:'Studio '+Math.floor(random()*100)}),
      ()=>{const d=p.furnitureDesigns[0];return {...p,furnitureDesigns:[D.moveParts(d,[pick(d.parts).id],{x:Math.round(random()*20)})]};},
      ()=>{const d=p.furnitureDesigns[0];return {...p,furnitureDesigns:[D.updateLayer(d,pick(d.layers).id,{color:'#'+Math.floor(random()*0xffffff).toString(16).padStart(6,'0')})]};},
      ()=>({...p,room:{...room,openings:room.openings.map(o=>({...o,offset:Math.round((.5+random()*2)*100)/100}))}}),
    ];
    try {states.push(normal(pick(edits)()));} catch {states.push(p);}
  }
  return states;
}

test('patches rebuild exactly and stay local to the changed element',()=>{
  const states=randomEdits(150);
  for(let i=1;i<states.length;i++) {
    const d=T.diff(states[i-1],states[i]);
    if(!d) {assert.ok(T.deepEqual(states[i-1],states[i]));continue;}
    assert.ok(T.deepEqual(T.applyPatch(states[i-1],d),states[i]),'step '+i);
  }
  const many=normal({...base(),room:{...base().room,furniture:Array.from({length:30},(_,i)=>cabinet('c'+i,-3.5+(i%8)*.9,-2.5+Math.floor(i/8)*1.2))}});
  const moved=normal({...many,room:moveFurniture(many.room,'c7',{x:3.4,z:2.5},.25,true)});
  const patch=T.diff(many,moved);
  assert.deepEqual(Object.keys(patch.$obj.room.$obj.furniture.$items),['c7'],'only the moved cabinet is in the patch');
  assert.ok(JSON.stringify(patch).length<500,'a move stores a few hundred bytes, not the project');
});

test('a timeline rebuilds every recorded state, across full snapshots',()=>{
  const states=randomEdits(180,5);
  let timeline=T.createTimeline(states[0],{at:START}),recorded=[{seq:1,state:states[0]}];
  for(let i=1;i<states.length;i++) {
    const next=T.appendVersion(timeline,states[i-1],states[i],{at:START+i*MIN,label:'Edit '+i});
    if(next!==timeline&&next.entries.at(-1).seq!==timeline.entries.at(-1).seq) recorded.push({seq:next.entries.at(-1).seq,state:states[i]});
    timeline=next;
  }
  assert.ok(timeline.entries.length>100);
  assert.ok(timeline.entries.filter(e=>e.keyframe).length>=3,'a full project every 50 versions');
  for(const {seq,state} of recorded) assert.ok(T.deepEqual(normal(T.stateAt(timeline,seq)),state),'version '+seq);
  assert.ok(T.deepEqual(normal(T.headState(timeline)),states.at(-1)));
  assert.doesNotThrow(()=>T.validateTimeline(JSON.parse(JSON.stringify(timeline)),normal));
});

test('versions list the elements they changed',()=>{
  const a=base(),b=normal({...a,room:{...moveFurniture(a.room,'a',{x:-1,z:1},.25),furniture:[...moveFurniture(a.room,'a',{x:-1,z:1},.25).furniture.filter(f=>f.id!=='b'),cabinet('c',2)]},furnitureDesigns:[D.renameDesign(a.furnitureDesigns[0],'Hall box')]});
  assert.deepEqual(T.changedElements(a,b).map(c=>[c.key,c.action,c.name]),[
    ['furniture:a','changed','Unit a'],['furniture:b','removed','Unit b'],['furniture:c','added','Unit c'],['design:box','changed','Hall box']]);
  const walls=normal({...a,room:{...a.room,outline:moveCorner(a.room.outline,'north-west',{x:-4.5,z:-3},.25,true)}});
  assert.deepEqual(T.changedElements(a,walls).map(c=>c.key),['room']);
  assert.deepEqual(T.changedElements(a,a),[]);
});

test('the version at a time: scroll back an hour and continue',()=>{
  let timeline=T.createTimeline(base(),{at:START}),state=base();
  for(let minute=1;minute<=120;minute++) {
    const next=normal({...state,room:moveFurniture(state.room,'a',{x:-3+(minute%8)*.25,z:0},.25,true)}); // stays clear of cabinet b at x = 0
    timeline=T.appendVersion(timeline,state,next,{at:START+minute*MIN,label:'Cabinet moved'});state=next;
  }
  const hourAgo=T.versionAtTime(timeline,START+120*MIN-60*MIN);
  assert.equal(hourAgo.at,new Date(START+60*MIN).toISOString());
  assert.equal(T.versionAtTime(timeline,START-5*MIN).seq,1,'before the start: the first version');
  assert.equal(T.versionAtTime(timeline,START+999*MIN).seq,timeline.entries.at(-1).seq);
  // Continue from there: the restored state is appended as a new version.
  const past=normal(T.stateAt(timeline,hourAgo.seq)),length=timeline.entries.length;
  timeline=T.appendVersion(timeline,state,past,{at:START+121*MIN,label:'Restored version from 09:00'});
  assert.equal(timeline.entries.length,length+1,'nothing is lost: history grows');
  assert.ok(T.deepEqual(normal(T.headState(timeline)),past));
  assert.ok(T.deepEqual(normal(T.stateAt(timeline,timeline.entries.at(-2).seq)),state),'the pre-restore state is still a version');
});

test('quick repeats of the same edit share one version',()=>{
  let timeline=T.createTimeline(base(),{at:START}),state=base();
  for(const [i,name] of ['S','St','Stu','Studio 2'].entries()) {
    const next=normal({...state,name});timeline=T.appendVersion(timeline,state,next,{at:START+i*1000,label:'Renamed'});state=next;
  }
  assert.equal(timeline.entries.length,2,'typing one name is one version');
  assert.equal(T.headState(timeline).name,'Studio 2');
  const later=normal({...state,name:'Studio 3'});
  assert.equal(T.appendVersion(timeline,state,later,{at:START+10000,label:'Renamed'}).entries.length,3,'after a pause: a new version');
  const back=T.appendVersion(timeline,state,base(),{at:START+4000,label:'Renamed'});
  assert.equal(back.entries.length,1,'typing back to the original removes the version');
  assert.equal(T.appendVersion(timeline,state,state,{at:START+5000,label:'x'}),timeline,'no change, no version');
});

test('one element can be restored as it was, keeping everything else',()=>{
  let timeline=T.createTimeline(base(),{at:START}),state=base();
  const step=(next,label)=>{next=normal(next);timeline=T.appendVersion(timeline,state,next,{at:START+timeline.entries.length*MIN,label});state=next;};
  step({...state,room:moveFurniture(state.room,'a',{x:-3,z:2},.25)},'Cabinet moved');
  step({...state,room:deleteFurniture(state.room,['b'])},'Deleted 1 item');
  step({...state,furnitureDesigns:[D.renameDesign(state.furnitureDesigns[0],'Renamed box')]},'Design renamed');
  step({...state,room:moveFurniture(state.room,'a',{x:3,z:2},.25)},'Cabinet moved');
  const history=T.elementHistory(timeline,'furniture:a');
  assert.deepEqual(history.map(h=>[h.version,h.label]),[[2,'Cabinet moved'],[1,'Cabinet moved']]);
  const first=history.at(-1),old=T.elementAt(timeline,first.seq,'furniture:a');
  assert.deepEqual([old.x,old.z],[-3,2]);
  const restored=normal(T.withElement(state,'furniture:a',old));
  assert.deepEqual([restored.room.furniture[0].x,restored.furniture],[ -3,undefined]);
  assert.equal(restored.furnitureDesigns[0].name,'Renamed box','other elements keep their current state');
  const b=normal(T.withElement(state,'furniture:b',T.elementAt(timeline,1,'furniture:b')));
  assert.deepEqual(b.room.furniture.map(f=>f.id),['a','b'],'a deleted cabinet comes back');
  const clash=T.withElement(state,'furniture:b',{...T.elementAt(timeline,1,'furniture:b'),x:3,z:2});
  assert.throws(()=>normal(clash),/Cabinets cannot overlap/,'a restore that no longer fits is refused');
  assert.equal(normal(T.withElement(state,'design:box',T.elementAt(timeline,1,'design:box'))).furnitureDesigns[0].name,'Box');
  const shell=T.elementAt(timeline,1,'room');assert.deepEqual(Object.keys(shell),['width','depth','height','outline']);
});

test('old versions are dropped at the limits; the rest still rebuild',()=>{
  const states=randomEdits(120,9);
  let timeline=T.createTimeline(states[0],{at:START}),seqs=[[1,states[0]]];
  for(let i=1;i<states.length;i++) {const next=T.appendVersion(timeline,states[i-1],states[i],{at:START+i*MIN,label:'E'+i});if(next.entries.at(-1).seq!==timeline.entries.at(-1).seq) seqs.push([next.entries.at(-1).seq,states[i]]);timeline=next;}
  const small=T.compactTimeline(timeline,{...T.TIMELINE_LIMITS,versions:30});
  assert.equal(small.entries.length,30);assert.ok(small.entries[0].keyframe,'the new first version is a full project');
  for(const [seq,state] of seqs.filter(([seq])=>seq>=small.entries[0].seq)) assert.ok(T.deepEqual(normal(T.stateAt(small,seq)),state),'version '+seq);
  const bytes=new TextEncoder().encode(JSON.stringify(timeline)).length,tight=T.compactTimeline(timeline,{...T.TIMELINE_LIMITS,bytes:Math.round(bytes/3)});
  assert.ok(new TextEncoder().encode(JSON.stringify(tight)).length<=bytes/3);
  assert.ok(T.deepEqual(normal(T.headState(tight)),states.at(-1)),'the current state always survives');
});

test('stored timelines are validated',()=>{
  const timeline=T.createTimeline(base(),{at:START});
  assert.doesNotThrow(()=>T.validateTimeline(timeline,normal));
  for(const bad of [{},{...timeline,format:'x'},{...timeline,entries:[]},{...timeline,entries:[{...timeline.entries[0],keyframe:undefined,patch:{$set:1}}]},
    {...timeline,entries:[timeline.entries[0],{seq:1,at:timeline.entries[0].at,label:'x',changes:[],patch:{$obj:{}}}]},
    {...timeline,entries:[{...timeline.entries[0],changes:[{key:'x',kind:'spaceship',name:'',action:'added'}]}]}])
    assert.throws(()=>T.validateTimeline(bad,normal),/Invalid project timeline/);
  assert.throws(()=>T.validateTimeline({...timeline,entries:[{...timeline.entries[0],keyframe:{...base(),name:''}}]},normal),/Project name/);
});

test('a damaged diff in the middle of a stored timeline is found',()=>{
  const states=randomEdits(60,5);let timeline=T.createTimeline(states[0],{at:START});
  states.slice(1).forEach((s,i)=>{timeline=T.appendVersion(timeline,states[i],s,{at:START+(i+1)*MIN,label:'Edit '+i});});
  const stored=JSON.parse(JSON.stringify(timeline));
  assert.doesNotThrow(()=>T.validateTimeline(stored,normal));
  // Break a diff that is followed by a full snapshot, so the newest state alone would still rebuild.
  const index=stored.entries.findIndex((e,i)=>i>0&&e.patch&&stored.entries.slice(i+1).some(n=>n.keyframe));
  assert.ok(index>0);
  const damaged=structuredClone(stored);damaged.entries[index].patch={$keyed:'broken'};
  assert.doesNotThrow(()=>T.headState(damaged));
  assert.throws(()=>T.validateTimeline(damaged,normal));
});

test('IDs that look like object internals are ordinary data',()=>{
  const p=base(),q=normal({...p,room:{...p.room,furniture:[...p.room.furniture,cabinet('constructor',2),cabinet('__proto__',2.9)]}});
  const r=normal({...q,room:{...q.room,furniture:q.room.furniture.map(f=>f.id==='__proto__'?{...f,x:3.1}:f)}});
  let timeline=T.createTimeline(p,{at:START});
  timeline=T.appendVersion(timeline,p,q,{at:START+MIN,label:'Add'});
  timeline=T.appendVersion(timeline,q,r,{at:START+2*MIN,label:'Move'});
  const stored=JSON.parse(JSON.stringify(timeline));
  assert.deepEqual(T.stateAt(stored,2),q);assert.deepEqual(T.stateAt(stored,3),r);
  assert.equal(Object.getPrototypeOf(T.stateAt(stored,3).room.furniture.at(-1)),Object.prototype);
  assert.deepEqual(T.changedElements(q,r).map(c=>c.key),['furniture:__proto__']);
});

test('versions recorded without a label describe their change',()=>{
  const p=base(),q=normal({...p,room:{...p.room,furniture:p.room.furniture.map(f=>f.id==='a'?{...f,x:-2.5}:f)}});
  let timeline=T.appendVersion(T.createTimeline(p,{at:START}),p,q,{at:START+MIN});
  assert.equal(timeline.entries.at(-1).label,'Furniture “Unit a” changed');
  const r=normal({...q,name:'Studio 2'});
  timeline=T.appendVersion(timeline,q,r,{at:START+5*MIN,label:''});
  assert.equal(timeline.entries.at(-1).label,'Project renamed');
  const s=normal({...r,name:'Studio 3',room:{...r.room,height:3}});
  assert.equal(T.appendVersion(timeline,r,s,{at:START+9*MIN}).entries.at(-1).label,'2 elements changed');
  assert.equal(T.appendVersion(timeline,r,s,{at:START+9*MIN,label:'x'.repeat(300)}).entries.at(-1).label.length,120);
});
