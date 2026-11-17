import {test} from 'node:test';
import assert from 'node:assert/strict';
import {workshopHandoff} from './workshop-handoff.mjs';
const item={id:'a',name:'Cabinet',width:.9,height:1.2,depth:.5,thickness:.018,shelves:1};
const project={name:'Workshop',room:{furniture:[item]}};
test('handoff includes stock context, source references, placed register and stable pagination',()=>{
  const html=workshopHandoff(project,'2026-11-06T12:00:00Z');
  assert.match(html,/A4 landscape/);assert.match(html,/kerf 3.0 mm/);assert.match(html,/cabinet references/);assert.match(html,/panel register/);assert.match(html,/C1:side-1/);assert.match(html,/not to scale/);
  const count=(html.match(/<section class="page">/g)||[]).length;assert.ok(count>=4);
  assert.equal((html.match(new RegExp('of '+count+' · Snapshot','g'))||[]).length,count);
  assert.equal(workshopHandoff(project,'2026-11-06T12:00:00Z'),html);
});
test('handoff escapes hostile names and lists oversize panels instead of losing them',()=>{
  const html=workshopHandoff({name:'<script>alert(1)</script>',room:{furniture:[{...item,name:'<img src=x onerror=alert(1)>',stockLengthMm:100,stockWidthMm:100}]}},'fixed');
  assert.ok(!html.includes('<script>'));assert.ok(!html.includes('<img '));assert.match(html,/&lt;script&gt;/);assert.match(html,/&lt;img/);assert.match(html,/8 unplaced panels/);assert.match(html,/unplaced register/);assert.match(html,/default-src 'none'/);
  const empty=workshopHandoff({name:'Empty',room:{furniture:[]}},'fixed');assert.match(empty,/0 batches · 0 sheets · 0 unplaced/);
});
