import {test} from 'node:test';
import assert from 'node:assert/strict';
import {shelfJoinery} from './shelf-joinery.mjs';
import {cabinetParts,cabinetBom,cabinetBomCsv} from './cabinet-parts.mjs';
import {validateFurniture} from './furniture.mjs';
import {rectangle} from './geometry.mjs';
import {duplicateCabinet,saveFurniturePreset,instantiateFurniturePreset} from './furniture-library.mjs';
import {manufacturingBatches} from './manufacturing.mjs';
import {workshopHandoff} from './workshop-handoff.mjs';
import {validateProject} from './project.mjs';
const base={id:'a',type:'cabinet',name:'Cabinet',width:1.2,height:1.2,depth:.5,thickness:.018,shelves:2,columns:3,columnWeights:[1,2,1],x:0,z:0,rotation:0};
const dado={...base,shelfJoinery:{type:'dado',engagementMm:3},lengthAllowanceMm:4};
const room={height:3,outline:rectangle(10,6),openings:[],furniture:[dado]};
test('dado engagement extends only shelf blanks and reaches manufacturing without changing clear geometry',()=>{
 assert.deepEqual(cabinetParts(base),cabinetParts(dado));
 const bom=cabinetBom(dado),original=cabinetBom(base);assert.equal(bom.volume,original.volume);
 for(const row of bom.rows){const extra=row.part.startsWith('shelf')?6:0;assert.equal(row.joineryLengthMm,extra);assert.ok(Math.abs((row.blankLength-row.length)*1000-(4+extra))<1e-9);}
 const placed=manufacturingBatches([dado])[0].layout.sheets.flatMap(s=>s.panels);
 assert.ok(placed.some(p=>p.part.startsWith('shelf')&&Math.abs(p.length-(cabinetBom(base).rows.find(r=>r.part===p.part.replace(/-\d+$/,'')).length*1000+10))<1e-8));
 assert.match(cabinetBomCsv(dado),/joinery_length_mm/);assert.match(workshopHandoff({name:'Test',room}),/Shelf joinery planning register/);
});
test('joinery rejects invalid engagement and persists through validated copies, projects and presets',()=>{
 for(const joint of [null,{type:'dado',engagementMm:0},{type:'dado',engagementMm:9},{type:'dado',engagementMm:NaN},{type:'butt',engagementMm:1},{type:'bad',engagementMm:2}])assert.throws(()=>validateFurniture([{...base,shelfJoinery:joint}],room.outline,3));
 const [legacy]=validateFurniture([base],room.outline,3);assert.equal('shelfJoinery' in legacy,false);assert.deepEqual(shelfJoinery(base),{type:'butt',engagementMm:0});
 const copy=duplicateCabinet(room,'a','b',{x:2,z:0},.1).furniture[1];copy.shelfJoinery.engagementMm=4;assert.equal(dado.shelfJoinery.engagementMm,3);
 const [preset]=saveFurniturePreset([],{...dado,id:'preset',name:'Dado'});assert.equal(instantiateFurniturePreset(room,preset,'c',{x:-2,z:0},.1).furniture[1].shelfJoinery.engagementMm,3);
 assert.equal(validateProject({schemaVersion:6,units:'m',presets:[],name:'Test',room}).room.furniture[0].shelfJoinery.type,'dado');
});
