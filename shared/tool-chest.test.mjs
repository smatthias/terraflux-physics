import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as D from './furniture-design-edit.mjs';
import {TOOL_CHEST,CHEST_GROUPS,chestTool,planeSide,gaugeMarks,ploughGroove,housing,widenFeature} from './tool-chest.mjs';
import {updateMachining,addMachining,planeSides} from './bench-edits.mjs';
const ids=p=>{let n=0;return ()=>p+(++n);};
const box=()=>D.createDesign({id:'box',name:'Box',template:'box',makeId:ids('b')});
const byName=(design,name)=>design.parts.find(p=>p.name===name);
const ACTIONS=['tape','mark','saw','plane','chisel','rebate','plough','router','shoulder','bore','brush'];

test('the tool chest covers the essential hand tools, each usable or explained',()=>{
  assert.ok(TOOL_CHEST.length>=35);
  assert.equal(new Set(TOOL_CHEST.map(t=>t.id)).size,TOOL_CHEST.length,'unique IDs');
  for(const group of CHEST_GROUPS) assert.ok(TOOL_CHEST.some(t=>t.group===group),group);
  for(const t of TOOL_CHEST) {
    assert.ok(t.action===null?t.note.length>10:ACTIONS.includes(t.action),`${t.name}: an action or a reason`);
    assert.ok(t.name&&t.use,t.id);
  }
  for(const name of ['Jack plane (No. 5)','Smoothing plane (No. 4)','Block plane','Plough plane','Router plane','Dovetail saw','Marking gauge','Mortise gauge','Brace and auger bits','Bench chisels','Try square'])
    assert.ok(TOOL_CHEST.some(t=>t.name===name),name);
  assert.equal(chestTool('dovetail-saw').preset.kerf,.6);assert.equal(chestTool('nope'),null);
});

test('plane presets find the right side for each piece',()=>{
  const design=box(),side=byName(design,'Left side');
  assert.deepEqual(['up','edge','end'].map(p=>planeSide(side,'B',p)),['B','v1','u1']);
  let frame=D.createDesign({id:'f',name:'F',makeId:ids('l')});
  ({design:frame}=D.addPart(frame,'bar',{makeId:ids('p')}));({design:frame}=D.addPart(frame,'tube',{makeId:ids('q')}));
  assert.deepEqual(['up','edge','end'].map(p=>planeSide(frame.parts[0],'top',p)),['height1','width1','length1']);
  assert.deepEqual(['up','edge','end'].map(p=>planeSide(frame.parts[1],'top',p)),['length1','length1','length1']);
  for(const part of [side,...frame.parts]) for(const p of ['up','edge','end']) assert.ok(planeSides(part).includes(planeSide(part,'A',p)));
});

test('gauges, dividers, plough, housings, router and shoulder planes',()=>{
  const design=box(),side=byName(design,'Left side');   // 560 × 720
  assert.deepEqual(gaugeMarks(side,'A',{mode:'gauge',direction:'along',distance:9}),[{direction:'along',at:9}]);
  assert.deepEqual(gaugeMarks(side,'A',{mode:'gauge',direction:'across',from:'far',distance:37}),[{direction:'across',at:523}]);
  assert.deepEqual(gaugeMarks(side,'A',{mode:'mortise',distance:20,width:8}).map(m=>m.at),[20,28]);
  assert.deepEqual(gaugeMarks(side,'A',{mode:'divide',direction:'across',parts:4}).map(m=>m.at),[140,280,420]);
  assert.throws(()=>gaugeMarks(side,'A',{mode:'gauge',distance:800}),/on the piece/);
  const groove=ploughGroove(side,'A',{distance:10,width:6,depth:8,from:'far'});
  assert.deepEqual(groove,{type:'groove',face:'A',u:0,v:704,length:560,width:6,depth:8});
  assert.deepEqual(housing(side,'A',{at:200,width:18,depth:6}),{type:'groove',face:'A',u:200,v:0,length:18,width:720,depth:6});
  const added=addMachining(design,side.id,[housing(side,'A',{at:200,width:18,depth:6})],ids('h')),id=added.featureIds[0];
  const deeper=updateMachining(added.design,side.id,id,{depth:9}).parts.find(p=>p.id===side.id).features.find(f=>f.id===id);
  assert.equal(deeper.depth,9,'router plane');
  assert.deepEqual(widenFeature(deeper,{toward:'low',amount:.5}),{length:18.5,u:199.5},'a housing across the face widens along it');
  assert.deepEqual(widenFeature(groove,{toward:'high',amount:1}),{width:7});
  assert.throws(()=>updateMachining(added.design,side.id,id,{depth:30}),/depth/);
  assert.throws(()=>updateMachining(added.design,side.id,'nope',{depth:3}),/Choose a groove or pocket/);
});

import {drillHoles,showLayer} from './bench-edits.mjs';
test('bench machining can go on a chosen layer; layers can be shown and hidden',()=>{
  const design=box(),side=byName(design,'Left side'),reference=design.layers.find(l=>l.operation==='none');
  const {design:d,featureIds}=drillHoles(design,side.id,[{face:'A',u:200,v:200,diameter:5,depth:5}],ids('h'),reference.id);
  assert.equal(byName(d,'Left side').features.find(f=>f.id===featureIds[0]).layer,reference.id);
  assert.throws(()=>drillHoles(design,side.id,[{face:'A',u:200,v:200,diameter:5,depth:5}],ids('h'),'nope'),/Choose a layer/);
  const locked=D.updateLayer(design,reference.id,{locked:true});
  assert.throws(()=>drillHoles(locked,side.id,[{face:'A',u:200,v:200,diameter:5,depth:5}],ids('h'),reference.id),/locked/);
  assert.equal(showLayer(design,reference.id,false).layers.find(l=>l.id===reference.id).visible,false);
});
