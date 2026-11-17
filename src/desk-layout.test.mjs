import {test} from 'node:test';
import assert from 'node:assert/strict';
import * as L from './desk-layout.js';
const desk={width:1200,height:800};
const defs=[{id:'chest',rect:()=>({x:12,y:12,w:260,h:700})},{id:'tool',rect:d=>({x:d.width-352,y:12,w:340,h:500})},{id:'help',rect:()=>({x:300,y:500,w:400,h:200}),minimized:true}];

test('windows open where their defaults say, stacked in order',()=>{
  const w=L.createDesk(defs,desk);
  assert.deepEqual(w.map(x=>[x.id,x.x,x.y,x.w,x.h,x.z,x.minimized]),[['chest',12,12,260,700,1,false],['tool',848,12,340,500,2,false],['help',300,500,400,200,3,true]]);
  assert.equal(L.topWindow(w),'tool','the top visible window');
  // Whether a window starts minimised may depend on the desk (small screens).
  const small=d=>d.width<760,narrow=[{...defs[0],minimized:small}];
  assert.equal(L.createDesk(narrow,desk)[0].minimized,false);assert.equal(L.createDesk(narrow,{width:700,height:800})[0].minimized,true);
});

test('layers: focus brings to the front, send to back, minimise and restore',()=>{
  let w=L.createDesk(defs,desk);
  w=L.focusWindow(w,'chest');assert.deepEqual(w.map(x=>x.z),[3,1,2]);assert.equal(L.topWindow(w),'chest');
  w=L.sendToBack(w,'chest');assert.equal(w.find(x=>x.id==='chest').z,1);
  w=L.minimizeWindow(w,'tool');assert.equal(L.topWindow(w),'chest');
  w=L.focusWindow(w,'help');assert.equal(w.find(x=>x.id==='help').minimized,false,'focusing restores a minimised window');
});

test('moving and resizing keep the title bar on the desk and respect the minimum size',()=>{
  let w=L.createDesk(defs,desk);
  w=L.moveWindow(w,'tool',{x:5000,y:-300},desk);const t=w.find(x=>x.id==='tool');
  assert.deepEqual([t.x,t.y],[1140,0],'60 px of the title bar stays visible');
  w=L.moveWindow(w,'tool',{x:100,y:5000},desk);assert.equal(w.find(x=>x.id==='tool').y,800-L.TITLE);
  assert.deepEqual(L.resizeBy({x:100,y:100,w:300,h:300},'nw',50,40),{x:150,y:140,w:250,h:260});
  assert.deepEqual(L.resizeBy({x:100,y:100,w:300,h:300},'w',200,0),{x:180,y:100,w:220,h:300},'the minimum width stops the left edge');
  assert.deepEqual(L.resizeBy({x:100,y:100,w:300,h:300},'se',-500,20),{x:100,y:100,w:220,h:320});
  w=L.resizeWindow(w,'chest',{x:0,y:0,w:100,h:5000},desk);const c=w.find(x=>x.id==='chest');assert.deepEqual([c.w,c.h],[220,800]);
});

test('windows snap to halves, quarters and the whole desk, and come back to their size',()=>{
  assert.equal(L.snapZone({x:5,y:300},desk),'left');assert.equal(L.snapZone({x:1199,y:300},desk),'right');
  assert.equal(L.snapZone({x:600,y:3},desk),'max');assert.equal(L.snapZone({x:3,y:797},desk),'bottom-left');
  assert.equal(L.snapZone({x:600,y:400},desk),null);
  let w=L.createDesk(defs,desk);
  w=L.snapWindow(w,'tool','left',desk);let t=w.find(x=>x.id==='tool');
  assert.deepEqual([t.x,t.y,t.w,t.h,t.snap],[8,8,588,784,'left']);assert.equal(L.topWindow(w),'tool','a snapped window comes to the front');
  w=L.snapWindow(w,'tool','top-right',desk);t=w.find(x=>x.id==='tool');assert.deepEqual([t.x,t.y,t.w,t.h],[604,8,588,388]);
  assert.deepEqual(t.restore,{x:848,y:12,w:340,h:500},'the size before the first snap is kept');
  w=L.moveWindow(w,'tool',{x:300,y:200},desk);t=w.find(x=>x.id==='tool');assert.deepEqual([t.x,t.y,t.w,t.h,t.snap],[300,200,340,500,null],'dragging out of a snap restores the size');
  w=L.toggleMaximize(w,'chest',desk);assert.equal(w.find(x=>x.id==='chest').snap,'max');
  w=L.toggleMaximize(w,'chest',desk);assert.deepEqual(['x','y','w','h'].map(k=>w.find(x=>x.id==='chest')[k]),[12,12,260,700]);
});

test('when the desk is resized, snapped windows follow their zone and the others stay reachable',()=>{
  let w=L.snapWindow(L.createDesk(defs,desk),'tool','right',desk);
  const small={width:700,height:500};w=L.fitDesk(w,small);
  const t=w.find(x=>x.id==='tool'),c=w.find(x=>x.id==='chest');
  assert.deepEqual([t.x,t.w,t.h],[354,338,484]);assert.equal(c.h,500,'no taller than the desk');assert.ok(c.y+L.TITLE<=500);
});

test('layouts are kept as JSON and checked when read back',()=>{
  const w=L.snapWindow(L.createDesk(defs,desk),'tool','left',desk);
  const back=L.readLayout(L.writeLayout(w),['chest','tool','help']);
  assert.deepEqual(back,w);
  assert.equal(L.readLayout('{oops',['chest']),null);assert.equal(L.readLayout(JSON.stringify({version:2,windows:[]}),['chest']),null);
  assert.deepEqual(L.readLayout(L.writeLayout([{...w[0],x:'a'},w[1]]),['chest','tool']).map(x=>x.id),['tool'],'bad entries are dropped');
  assert.deepEqual(L.createDesk(defs,desk,back).map(x=>x.snap),[null,'left',null],'a kept layout opens as it was');
});
test('windows against the left or right edge dock: the canvas keeps the free middle',()=>{
  const desk={width:1200,height:700},win=(id,x,y,w,h,extra={})=>({id,x,y,w,h,z:1,minimized:false,snap:null,restore:null,...extra});
  assert.deepEqual(L.dockInsets([],desk),{left:0,right:0});
  // A tall panel on each side; two stacked on the right take the wider one.
  const docked=[win('a',8,8,330,684),win('b',862,8,330,338),win('c',900,354,292,338)];
  assert.deepEqual(L.dockInsets(docked,desk),{left:346,right:346});
  // Short, wide, minimised or free-floating windows float over the canvas.
  assert.deepEqual(L.dockInsets([win('a',8,8,330,200),win('b',8,8,600,684),win('c',8,8,330,684,{minimized:true}),win('d',400,8,330,684)],desk),{left:0,right:0});
  // Exactly at the limits: 40 % of the height and width, 16 px from the edge.
  assert.deepEqual(L.dockInsets([win('a',16,0,480,280)],desk),{left:504,right:0});
  assert.deepEqual(L.dockInsets([win('a',17,0,300,600)],desk),{left:0,right:0});
  // Never squeeze the canvas below its minimum width.
  assert.deepEqual(L.dockInsets([win('a',8,8,330,684),win('b',562,8,330,684)],{width:900,height:700}),{left:0,right:0});
});
