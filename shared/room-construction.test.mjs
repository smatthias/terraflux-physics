import {test} from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {moveWall,removeWall,replaceWall,roomFromWalls,wallLines} from './room-construction.mjs';
import {wallIds,cornerIds,cornerPoint,wallSegment,roomWalls} from './room-topology.mjs';
import {validateProject} from './project.mjs';
import {rectangle} from './geometry.mjs';
import {createJobManifest,verifyJobManifest} from './job-manifest.mjs';
const close=(a,b,message)=>assert.ok(Math.abs(a-b)<1e-9,`${message??''} ${a} != ${b}`);
const closePoint=(p,q,message)=>{close(p.x,q.x,message);close(p.z,q.z,message);};
const cabinet=(id,x,z)=>({id,type:'cabinet',name:id,width:.8,depth:.5,height:1,thickness:.018,shelves:1,x,z,rotation:0});
const door=(id,wall,offset)=>({id,type:'door',wall,offset,width:.9,height:2,sill:0,hinge:'A',swing:'in'});
const project=room=>validateProject({schemaVersion:7,units:'m',name:'Walls',presets:[],room:{height:3,openings:[],furniture:[],...room}});
const box=()=>project({outline:rectangle(6,4),openings:[door('n','north',1),door('e','east',1),door('s','south',2)],furniture:[cabinet('c',0,0)]}).room;
const trapezoid=()=>project({outline:[{x:-3,z:-2},{x:3,z:-2},{x:2,z:2},{x:-2,z:2}],openings:[door('s','south',1)]}).room;
const centre=(room,opening)=>{const w=wallSegment(room,opening.wall),along=opening.offset+opening.width/2;return {x:w.a.x+w.tx*along,z:w.a.z+w.tz*along};};
const area=outline=>outline.reduce((n,a,i)=>{const b=outline[(i+1)%outline.length];return n+(a.x*b.z-b.x*a.z)/2;},0);
const parallelDistance=(p,q)=>(q.point.x-p.point.x)*-p.direction.z+(q.point.z-p.point.z)*p.direction.x;

test('moving a wall translates its line; neighbours keep their lines and corners are recomputed',()=>{
  const room=box(),moved=moveWall(room,'north',.5);
  assert.deepEqual(moved.outline,[{x:-3,z:-2.5},{x:3,z:-2.5},{x:3,z:2},{x:-3,z:2}]);
  assert.equal(moved.depth,4.5);assert.equal(moved.width,6);
  assert.equal('walls' in moved,false,'the original four walls are not stored');
  assert.deepEqual(moveWall(room,'north',-.5).outline[0],{x:-3,z:-1.5});
  // Openings on other walls keep their world position, even when measured from a moved corner;
  // openings on the moved wall travel with it and keep their position along it.
  for(const opening of room.openings) {
    const old=centre(room,opening),shift=opening.wall==='north'?-.5:0;
    closePoint(centre(moved,moved.openings.find(o=>o.id===opening.id)),{x:old.x,z:old.z+shift},opening.id);
  }
  assert.equal(moved.openings.find(o=>o.id==='e').offset,1.5,'east wall A corner moved 0.5 m');
  assert.deepEqual(moved.openings.find(o=>o.id==='s'),room.openings.find(o=>o.id==='s'),'unaffected openings are unchanged');
  assert.deepEqual(moved.furniture,room.furniture);
});

test('moving an angled wall keeps every wall direction and moves exactly the requested distance',()=>{
  const room=project({outline:[{x:-3,z:-2},{x:2.5,z:-2.5},{x:3.5,z:2},{x:-2,z:1.5}]}).room;
  for(const wall of wallIds(room)) for(const distance of [.4,-.3]) {
    const before=wallLines(room),after=wallLines(moveWall(room,wall,distance));
    after.forEach((line,i)=>{
      close(line.direction.x,before[i].direction.x);close(line.direction.z,before[i].direction.z);
      close(parallelDistance(before[i],line),line.id===wall?-distance:0,line.id);
    });
  }
});

test('invalid wall moves are rejected with context and leave the room untouched',()=>{
  const room=box(),copy=structuredClone(room);
  assert.throws(()=>moveWall(room,'north',-4.5),/^Error: Cannot move North wall: .*(inverted|convex)/);
  const furnished=project({outline:rectangle(6,4),furniture:[cabinet('c',0,0)]}).room;
  assert.throws(()=>moveWall(furnished,'north',-1.8),/^Error: Cannot move North wall: Cabinet must fit inside the room/);
  assert.throws(()=>moveWall(room,'west',-4.8),/^Error: Cannot move West wall: Opening must fit/);
  assert.throws(()=>moveWall(room,'north',NaN),/move distance/);
  assert.throws(()=>moveWall(room,'ceiling',1),/Unknown wall/);
  assert.deepEqual(room,copy);
});

test('removing a wall extends its neighbours until they meet at a redefined corner',()=>{
  assert.throws(()=>removeWall(box(),'north'),/^Error: Cannot remove North wall: West wall and East wall are parallel and cannot meet$/);
  const room=trapezoid(),before=structuredClone(room),triangle=removeWall(room,'south');
  assert.deepEqual(room,before);
  assert.deepEqual(wallIds(triangle),['north','east','west']);
  assert.deepEqual(cornerIds(triangle),['north-west','north-east','east+west']);
  assert.deepEqual(cornerPoint(triangle,'east+west'),{x:0,z:10},'east and west walls extended to their intersection');
  assert.deepEqual(triangle.openings,[],'openings on the removed wall are removed with it');
  assert.ok(area(triangle.outline)>area(room.outline),'removing a wall of a convex room only enlarges it');
  const stored=validateProject({schemaVersion:7,units:'m',name:'T',presets:[],room:triangle});
  assert.equal(stored.schemaVersion,7);assert.deepEqual(stored.room.walls,[{id:'north'},{id:'east'},{id:'west'}]);
  assert.throws(()=>removeWall(triangle,'north'),/at least 3 walls/);
});

test('removing a wall keeps openings on the extended neighbours in place',()=>{
  const room=project({outline:[{x:-3,z:-2},{x:3,z:-2},{x:2,z:2},{x:-2,z:2}],openings:[door('e','east',.5),door('w','west',1)]}).room;
  const next=removeWall(room,'south');
  for(const opening of room.openings) closePoint(centre(next,next.openings.find(o=>o.id===opening.id)),centre(room,opening),opening.id);
  assert.ok(next.openings.find(o=>o.id==='w').offset>1,'west wall A corner moved away');
});

test('exchanging a wall for one line keeps its ID and re-projects its openings',()=>{
  const room=box(),angled=replaceWall(room,'north',[{a:{x:-3,z:-2.5},b:{x:3,z:-1.5}}]);
  assert.deepEqual(wallIds(angled),['north','east','south','west']);
  assert.deepEqual(angled.outline.slice(0,2),[{x:-3,z:-2.5},{x:3,z:-1.5}]);
  const opening=angled.openings.find(o=>o.id==='n'),wall=wallSegment(angled,'north'),old=centre(room,room.openings[0]),now=centre(angled,opening);
  close((old.x-now.x)*wall.tx+(old.z-now.z)*wall.tz,0,'opening centre is the projection of its old centre');
});

test('exchanging a wall for several walls adds redefined corners and rehosts openings',()=>{
  const room=box(),bay=replaceWall(room,'north',[{a:{x:-3,z:-2},b:{x:0,z:-3}},{a:{x:0,z:-3},b:{x:3,z:-2}}]);
  assert.deepEqual(wallIds(bay),['north-1','north-2','east','south','west']);
  assert.deepEqual(cornerIds(bay),['west+north-1','north-1+north-2','north-2+east','south-east','south-west']);
  assert.deepEqual(cornerPoint(bay,'north-1+north-2'),{x:0,z:-3});
  assert.equal(bay.openings.find(o=>o.id==='n').wall,'north-1','door moves to the nearest replacement wall');
  const stored=validateProject({schemaVersion:7,units:'m',name:'Bay',presets:[],room:bay});
  assert.equal(stored.schemaVersion,7);assert.deepEqual(stored.room,bay);
  // Replacing a replacement allocates unused IDs; explicit IDs are honoured.
  assert.deepEqual(wallIds(replaceWall(bay,'north-2',[{a:{x:0,z:-3},b:{x:1.5,z:-2.8}},{a:{x:1.5,z:-2.8},b:{x:3,z:-2}}])).slice(0,3),['north-1','north-3','north-4']);
  assert.deepEqual(wallIds(replaceWall(room,'north',[{id:'bay-left',a:{x:-3,z:-2},b:{x:0,z:-3}},{id:'bay-right',a:{x:0,z:-3},b:{x:3,z:-2}}])).slice(0,2),['bay-left','bay-right']);
});

test('invalid exchanges are rejected with context',()=>{
  const room=box();
  assert.throws(()=>replaceWall(room,'north',[]),/1–8 walls/);
  assert.throws(()=>replaceWall(room,'north',[{a:{x:0,z:-2},b:{x:0,z:-2}}]),/^Error: Cannot replace North wall: Wall points must be at least 0.1 m apart/);
  assert.throws(()=>replaceWall(room,'north',[{a:{x:3,z:-2},b:{x:-3,z:-2}}]),/Cannot replace North wall: .*(inverted|convex)/,'reversed walls are rejected');
  assert.throws(()=>replaceWall(room,'north',[{a:{x:-3,z:-2},b:{x:0,z:-2}},{a:{x:0,z:-2},b:{x:3,z:-2}}]),/parallel and cannot meet/,'collinear walls cannot form a corner');
  assert.throws(()=>replaceWall(room,'north',[{a:{x:-3,z:-2},b:{x:0,z:-1}},{a:{x:0,z:-1},b:{x:3,z:-2}}]),/convex/,'inward bends are concave');
  assert.throws(()=>replaceWall(room,'north',[{id:'east',a:{x:-3,z:-2},b:{x:3,z:-2.5}}]),/already used/);
  assert.throws(()=>replaceWall(room,'north',[{id:'Bad ID',a:{x:-3,z:-2},b:{x:3,z:-2.5}}]),/Wall IDs/);
  assert.throws(()=>replaceWall(room,'north',[{a:{x:-3,z:-2},b:{x:3,z:-2}},{a:{x:3,z:-2},b:{x:3,z:-2}}]),/0.1 m apart/);
});

test('any sequence of wall edits yields a closed, valid room or leaves the input unchanged',()=>{
  let seed=7;const random=()=>(seed=(seed*1103515245+12345)%2147483648)/2147483648;
  let accepted=0,rejected=0;
  for(let run=0;run<40;run++){
    let room=project({outline:rectangle(4+random()*4,3+random()*3),openings:[door('d','east',.3)],furniture:[cabinet('c',0,0)]}).room;
    for(let step=0;step<25;step++){
      const walls=roomWalls(room),wall=walls[Math.floor(random()*walls.length)],before=structuredClone(room);
      const bend=(random()-.2)*.8,mid={x:(wall.a.x+wall.b.x)/2-wall.nx*bend,z:(wall.a.z+wall.b.z)/2-wall.nz*bend};
      const operations=[
        ()=>moveWall(room,wall.id,(random()-.5)*2),
        ()=>removeWall(room,wall.id),
        ()=>replaceWall(room,wall.id,[{a:wall.a,b:mid},{a:mid,b:wall.b}]),
        ()=>replaceWall(room,wall.id,[{a:{x:wall.a.x,z:wall.a.z+random()-.5},b:wall.b}]),
      ];
      let next;
      try {next=operations[Math.floor(random()*operations.length)]();}
      catch(error) {assert.match(error.message,/^Cannot |Unknown wall|at least|at most/);assert.deepEqual(room,before);rejected++;continue;}
      accepted++;
      const stored=validateProject({schemaVersion:7,units:'m',name:'Run',presets:[],room:next});
      assert.deepEqual(stored.room,next,'operation results are already normalised');
      assert.equal(cornerIds(next).length,wallIds(next).length);
      assert.equal(new Set(cornerIds(next)).size,wallIds(next).length,'corner IDs are unique');
      const rebuilt=roomFromWalls(wallLines(next));
      rebuilt.outline.forEach((p,i)=>closePoint(p,next.outline[i],'walls reproduce the corners'));
      room=next;
    }
  }
  assert.ok(accepted>200&&rejected>50,`exercise both paths (accepted ${accepted}, rejected ${rejected})`);
});

test('native quantities accept rooms with any supported number of walls',()=>{
  const binary=new URL('../build/terratec-core',import.meta.url).pathname;
  const bay=replaceWall(box(),'north',[{a:{x:-3,z:-2},b:{x:0,z:-3}},{a:{x:0,z:-3},b:{x:3,z:-2}}]);
  const triangle=removeWall(trapezoid(),'south');
  for(const room of [bay,triangle]){
    const result=JSON.parse(execFileSync(binary,['--polygon','3',...room.outline.flatMap(p=>[p.x,p.z])].map(String),{encoding:'utf8'}));
    close(result.area,area(room.outline));close(result.volume,area(room.outline)*3);
    close(result.perimeter,roomWalls(room).reduce((n,w)=>n+w.length,0));
  }
});

test('job manifests for rebuilt rooms verify, and original-wall rooms keep their source hash',async()=>{
  const base={schemaVersion:6,units:'m',name:'Hash',presets:[],room:{height:3,outline:rectangle(6,4),openings:[],furniture:[cabinet('c',0,0)]}};
  const classic=await createJobManifest(base,'2024-01-01T00:00:00.000Z');
  const movedBack=moveWall(moveWall(validateProject(base).room,'north',1),'north',-1);
  assert.equal((await createJobManifest({...base,room:movedBack},'2024-01-01T00:00:00.000Z')).sourceSha256,classic.sourceSha256);
  const bay={...base,schemaVersion:7,room:replaceWall(validateProject(base).room,'north',[{a:{x:-3,z:-2},b:{x:0,z:-3}},{a:{x:0,z:-3},b:{x:3,z:-2}}])};
  const result=await verifyJobManifest(JSON.stringify(await createJobManifest(bay)),bay);
  assert.equal(result.matchesCurrent,true);
});
