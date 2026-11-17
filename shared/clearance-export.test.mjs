import {test} from 'node:test';
import assert from 'node:assert/strict';
import {clearanceSnapshot,clearanceCsv} from './clearance-export.mjs';
import {rectangle} from './geometry.mjs';
const project={schemaVersion:6,presets:[],units:'m',name:'=Workshop, "North"',room:{width:6,depth:4,height:3,outline:rectangle(6,4),openings:[],furniture:[{id:'cabinet',type:'cabinet',name:'=Unit, "A"',width:1,depth:.4,height:1.2,x:0,z:-1.78,rotation:0,thickness:.018,shelves:1}]}};
test('clearance snapshot captures reproducible settings, issue identities and detached geometry',()=>{
 const before=JSON.stringify(project),snapshot=clearanceSnapshot(project,{minWall:.05},'2026-11-08T09:00:00Z');
 assert.equal(snapshot.kind,'terratec-clearance-review');assert.equal(snapshot.review.issues[0].cabinetId,'cabinet');assert.equal(snapshot.review.issues[0].code,'WALL_GAP');
 assert.equal(snapshot.settings.doorDepth,.6);assert.equal(snapshot.units,'m');assert.match(snapshot.scope,/Advisory/);
 snapshot.room.furniture[0].x=99;assert.equal(JSON.stringify(project),before);
 assert.equal(clearanceSnapshot(project,{minWall:0}).review.issues.length,0);
 assert.throws(()=>clearanceSnapshot(project,{minWall:NaN}));assert.throws(()=>clearanceSnapshot(project,{},'bad'));
});
test('CSV includes metadata, escapes names and neutralizes spreadsheet formulas',()=>{
 const csv=clearanceCsv(project,{});
 assert.ok(csv.includes('"\'=Workshop, ""North"""'));assert.ok(csv.includes('"\'=Unit, ""A"""'));assert.match(csv,/WALL_GAP/);assert.match(csv,/Minimum wall gap/);
 const empty=clearanceCsv({...project,room:{...project.room,furniture:[]}},{});assert.match(empty,/"Issues","0"/);assert.match(empty,/"Cabinet ID","Cabinet name"/);
});
test('service conflicts and related cabinet identities survive exports',()=>{
 const a={...project.room.furniture[0],name:'A',x:0,z:0},b={...a,id:'b',name:'B',z:.7};
 const input={...project,room:{...project.room,furniture:[a,b]}};
 const snapshot=clearanceSnapshot(input,{serviceDepth:.6});assert.equal(snapshot.settings.serviceDepth,.6);assert.equal(snapshot.review.affectedCount,2);assert.equal(snapshot.review.issues[0].otherCabinetId,'b');
 assert.match(clearanceCsv(input,{serviceDepth:.6}),/Other cabinet ID/);assert.match(clearanceCsv(input,{serviceDepth:.6}),/SERVICE_ZONE/);
});
