import {test} from 'node:test';
import assert from 'node:assert/strict';
import {clearancePrint} from './clearance-print.mjs';
import {rectangle} from './geometry.mjs';
const cabinet={id:'a',type:'cabinet',name:'<Unit & A>',width:1,depth:.4,height:1.2,x:0,z:-1.78,rotation:0,thickness:.018,shelves:1};
const project={schemaVersion:6,presets:[],units:'m',name:'<Workshop>',room:{height:3,outline:rectangle(6,4),openings:[],furniture:[cabinet]}};
test('print handoff links numbered issues to cabinet plan markers and escapes user text',()=>{
 const html=clearancePrint(project,{},'2026-11-08T09:00:00Z');
 assert.match(html,/data-cabinet-ref="C1"/);assert.match(html,/<td>I1<\/td>/);assert.match(html,/C1 · &lt;Unit &amp; A&gt;/);assert.match(html,/&lt;Workshop&gt;/);assert.match(html,/2026-11-08T09:00:00Z/);assert.match(html,/not to scale/);assert.doesNotMatch(html,/<Unit|<\?xml/);
 assert.match(html,/@page\{size:A4 landscape/);assert.match(html,/Minimum wall gap 0.050/);
});
test('print review paginates issue registers and handles clean and empty projects',()=>{
 const many={...project,room:{...project.room,outline:rectangle(30,4),furniture:Array.from({length:13},(_,i)=>({...cabinet,id:'c'+i,x:i*1.1-7}))}};
 const html=clearancePrint(many,{});assert.equal((html.match(/<section class="page">/g)||[]).length,4);assert.match(html,/<td>I13<\/td>/);assert.match(html,/data-cabinet-ref="C13"/);
 assert.match(clearancePrint(project,{minWall:0}),/No clearance issues/);
 assert.match(clearancePrint({...project,room:{...project.room,furniture:[]}},{}),/0 of 0 cabinets/);
 assert.throws(()=>clearancePrint(project,{minWall:NaN}));
});
