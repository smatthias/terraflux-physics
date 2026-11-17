import {test} from 'node:test';
import assert from 'node:assert/strict';
import {cabinetBom,cabinetParts,cabinetBomCsv} from './cabinet-parts.mjs';
import {validateFurniture} from './furniture.mjs';
import {rectangle} from './geometry.mjs';
import {duplicateCabinet,saveFurniturePreset,instantiateFurniturePreset} from './furniture-library.mjs';
import {manufacturingBatches} from './manufacturing.mjs';
import {workshopHandoff} from './workshop-handoff.mjs';
const base={id:'a',type:'cabinet',name:'Cabinet',width:1.2,height:1.2,depth:.5,thickness:.018,shelves:2,columns:3,columnWeights:[1,2,1],columnFronts:['open','single','double'],x:0,z:0,rotation:0};
const item={...base,shelfJoinery:{type:'dado',engagementMm:3},edgeBanding:{thicknessMm:2,fronts:true,shelves:true},lengthAllowanceMm:4,widthAllowanceMm:3};
const room={height:3,outline:rectangle(10,6),openings:[],furniture:[item]};
test('finished, core and blank sizes combine engagement and selected bands in correct order',()=>{
 const bom=cabinetBom(item);assert.deepEqual(cabinetParts(item),cabinetParts(base));
 for(const row of bom.rows){const front=row.part.startsWith('front'),shelf=row.part.startsWith('shelf');assert.ok(Math.abs(row.coreLength-row.length-(shelf ? .006 : front ? -.004 : 0))<1e-10);assert.ok(Math.abs(row.coreWidth-row.width-(front?-.004:shelf?-.002:0))<1e-10);assert.ok(Math.abs(row.blankLength-row.coreLength-.004)<1e-10);assert.ok(Math.abs(row.blankWidth-row.coreWidth-.003)<1e-10);}
 const front=bom.rows.find(r=>r.part==='front-column-2');assert.equal(front.edgeLengthEdges,2);assert.equal(front.edgeWidthEdges,2);assert.ok(Math.abs(front.bandLengthMetres-2*(front.length+front.width))<1e-10);
 const shelf=bom.rows.find(r=>r.part==='shelf-column-1');assert.ok(Math.abs(shelf.bandLengthMetres-shelf.quantity*shelf.length)<1e-10);
 const placed=manufacturingBatches([item])[0].layout.sheets.flatMap(s=>s.panels);assert.ok(placed.some(p=>p.part==='shelf-column-1-1'&&Math.abs(p.length-shelf.blankLength*1000)<1e-8));
 assert.match(cabinetBomCsv(item),/core_length_mm/);assert.match(workshopHandoff({name:'Test',room}),/Edge-banding planning register/);
});
test('banding validates, preserves optional legacy state and survives copies and presets',()=>{
 for(const edgeBanding of [null,{thicknessMm:-1,fronts:true,shelves:true},{thicknessMm:6,fronts:true,shelves:true},{thicknessMm:NaN,fronts:true,shelves:true},{thicknessMm:1,fronts:'yes',shelves:true}])assert.throws(()=>validateFurniture([{...base,edgeBanding}],room.outline,3));
 assert.equal('edgeBanding' in validateFurniture([base],room.outline,3)[0],false);
 const copy=duplicateCabinet(room,'a','b',{x:2,z:0},.1).furniture[1];copy.edgeBanding.thicknessMm=3;assert.equal(item.edgeBanding.thicknessMm,2);
 const [preset]=saveFurniturePreset([],{...item,id:'preset',name:'Banded'});assert.deepEqual(instantiateFurniturePreset(room,preset,'c',{x:-2,z:0},.1).furniture[1].edgeBanding,item.edgeBanding);
 const unbanded=cabinetBom({...item,edgeBanding:{thicknessMm:0,fronts:true,shelves:true}});assert.ok(unbanded.rows.every(r=>r.bandLengthMetres===0));
});
