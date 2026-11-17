import {test} from 'node:test';
import assert from 'node:assert/strict';
import {cabinetCompartments} from './cabinet-compartments.mjs';
import {cabinetParts,cabinetBom} from './cabinet-parts.mjs';
import {validateFurniture} from './furniture.mjs';
import {rectangle} from './geometry.mjs';
import {validateProject} from './project.mjs';
import {duplicateCabinet,saveFurniturePreset,instantiateFurniturePreset} from './furniture-library.mjs';
import {manufacturingBatches} from './manufacturing.mjs';
const item={id:'a',type:'cabinet',name:'A',width:1.2,height:1.2,depth:.5,thickness:.018,shelves:2,x:0,z:0,rotation:0};
const room={outline:rectangle(10,6),height:3,openings:[],furniture:[{...item,columns:3}]};
test('compartments partition internal width and generate matching shelves, dividers and BOM',()=>{
 const cabinet=room.furniture[0],layout=cabinetCompartments(cabinet),parts=cabinetParts(cabinet),bom=cabinetBom(cabinet);
 assert.ok(Math.abs(layout.bayWidth*3+2*cabinet.thickness-(item.width-2*item.thickness))<1e-12);
 assert.equal(parts.filter(p=>p.part==='divider').length,2);assert.equal(parts.filter(p=>p.part==='shelf').length,6);assert.equal(bom.panelCount,15);
 assert.ok(Math.abs(bom.volume-parts.filter(p=>p.cut).reduce((n,p)=>n+p.size[0]*p.size[1]*p.size[2],0))<1e-12);
 for(const shelf of parts.filter(p=>p.part==='shelf'))for(const divider of parts.filter(p=>p.part==='divider'))assert.ok(Math.abs(shelf.position[0]-divider.position[0])+1e-9>=(shelf.size[0]+divider.size[0])/2);
 assert.equal(manufacturingBatches([cabinet])[0].panelCount,bom.panelCount);
});
test('legacy cabinets preserve their representation and invalid columns or narrow bays fail',()=>{
 const [legacy]=validateFurniture([item],room.outline,3);assert.equal('columns' in legacy,false);assert.equal(cabinetParts(item).filter(p=>p.part==='shelf').length,2);
 for(const columns of [0,9,1.5,NaN,'2',null])assert.throws(()=>validateFurniture([{...item,columns}],room.outline,3));
 assert.throws(()=>validateFurniture([{...item,width:.2,thickness:.018,columns:8}],room.outline,3),/clear width/);
});
test('compartments survive project roundtrip, copies and saved presets',()=>{
 const project=validateProject({schemaVersion:6,presets:[],name:'Test',units:'m',room});
 assert.equal(validateProject(JSON.parse(JSON.stringify(project))).room.furniture[0].columns,3);
 assert.equal(duplicateCabinet(room,'a','b',{x:2,z:0},.1).furniture[1].columns,3);
 const [preset]=saveFurniturePreset([],{...room.furniture[0],id:'preset',name:'Three bays'});
 assert.equal(instantiateFurniturePreset(room,preset,'c',{x:-2,z:0},.1).furniture[1].columns,3);
});
test('independent shelf counts drive parts, BOM, manufacturing and saved designs',()=>{
 const cabinet={...item,columns:3,columnShelves:[0,2,4]},layout=cabinetCompartments(cabinet);
 assert.deepEqual(layout.bays.map(b=>b.shelves),[0,2,4]);
 const shelves=cabinetParts(cabinet).filter(p=>p.part==='shelf');assert.equal(shelves.length,6);
 for(const bay of layout.bays)assert.equal(shelves.filter(p=>p.position[0]===bay.x).length,bay.shelves);
 assert.equal(cabinetBom(cabinet).rows.find(r=>r.part==='shelf').quantity,6);assert.equal(manufacturingBatches([cabinet])[0].panelCount,15);
 const space={...room,furniture:[cabinet]};
 const project=validateProject({schemaVersion:6,presets:[],name:'Test',units:'m',room:space});
 assert.deepEqual(validateProject(JSON.parse(JSON.stringify(project))).room.furniture[0].columnShelves,[0,2,4]);
 const copy=duplicateCabinet(space,'a','b',{x:2,z:0},.1).furniture[1];assert.deepEqual(copy.columnShelves,[0,2,4]);copy.columnShelves[0]=7;assert.equal(cabinet.columnShelves[0],0);
 const [preset]=saveFurniturePreset([],{...cabinet,id:'preset',name:'Mixed shelves'});assert.deepEqual(instantiateFurniturePreset(space,preset,'c',{x:-2,z:0},.1).furniture[1].columnShelves,[0,2,4]);
});
test('column shelf overrides reject invalid arrays and physically crowded shelves',()=>{
 for(const columnShelves of [[],[1],[0,1,11],[0,1,-1],[0,1,1.5],[0,1,null],null,'1,2,3',Array(3)])assert.throws(()=>validateFurniture([{...item,columns:3,columnShelves}],room.outline,3));
 assert.throws(()=>validateFurniture([{...item,height:.2,thickness:.018,columns:3,columnShelves:[0,10,0]}],room.outline,3),/height/);
 const [valid]=validateFurniture([{...item,height:.2,thickness:.018,shelves:10,columns:3,columnShelves:[0,0,0]}],room.outline,3);assert.equal(cabinetParts(valid).filter(p=>p.part==='shelf').length,0);
});
test('weighted bays use available width and yield unique manufacturing shelf identities',()=>{
 const cabinet={...item,columns:3,columnWeights:[1,2,1],columnShelves:[1,2,3]},layout=cabinetCompartments(cabinet);
 assert.ok(Math.abs(layout.bays[1].width-2*layout.bays[0].width)<1e-12);
 assert.ok(Math.abs(layout.bays.reduce((n,b)=>n+b.width,0)+4*item.thickness-item.width)<1e-12);
 for(let n=0;n<2;n++)assert.ok(Math.abs(layout.dividers[n]-(layout.bays[n].x+layout.bays[n].width/2+item.thickness/2))<1e-12);
 const bom=cabinetBom(cabinet),shelves=bom.rows.filter(r=>r.part.startsWith('shelf-column-'));assert.equal(shelves.length,3);assert.deepEqual(shelves.map(r=>r.quantity),[1,2,3]);
 assert.ok(Math.abs(shelves[1].length-2*shelves[0].length)<1e-12);
 const batches=manufacturingBatches([cabinet]);const panels=batches[0].layout.sheets.flatMap(s=>s.panels);assert.equal(new Set(panels.map(p=>p.id)).size,panels.length);
 const source={...room,furniture:[cabinet]},copy=duplicateCabinet(source,'a','b',{x:2,z:0},.1).furniture[1];assert.deepEqual(copy.columnWeights,[1,2,1]);copy.columnWeights[0]=9;assert.equal(cabinet.columnWeights[0],1);
 const [preset]=saveFurniturePreset([],{...cabinet,id:'weighted',name:'Weighted'});assert.deepEqual(instantiateFurniturePreset(source,preset,'c',{x:-2,z:0},.1).furniture[1].columnWeights,[1,2,1]);
 const project=validateProject({schemaVersion:6,presets:[],name:'Test',units:'m',room:source});assert.deepEqual(validateProject(JSON.parse(JSON.stringify(project))).room.furniture[0].columnWeights,[1,2,1]);
});
test('invalid width weights and undersized weighted bays are rejected',()=>{
 for(const columnWeights of [null,[],[1,2],[1,0,1],[1,-1,1],[1,NaN,1],[1,'2',1],[1,1001,1],Array(3)])assert.throws(()=>validateFurniture([{...item,columns:3,columnWeights}],room.outline,3));
 assert.throws(()=>validateFurniture([{...item,columns:3,columnWeights:[.01,100,1]}],room.outline,3),/clear width/);
});
test('per-bay fronts generate open/single/double panels and matching hardware without duplicate IDs',()=>{
 const cabinet={...item,columns:3,columnWeights:[1,2,1],columnFronts:['open','single','double']},parts=cabinetParts(cabinet),layout=cabinetCompartments(cabinet),bom=cabinetBom(cabinet);
 const fronts=parts.filter(p=>p.part.startsWith('front'));assert.equal(fronts.length,3);assert.equal(parts.filter(p=>p.part==='handle').length,3);assert.equal(bom.handles,3);
 assert.equal(bom.rows.find(r=>r.part==='front-column-2').quantity,1);assert.equal(bom.rows.find(r=>r.part==='front-column-3').quantity,2);
 for(const front of fronts){const bay=layout.bays[Number(front.part.split('-').at(-1))-1];assert.ok(front.position[0]-front.size[0]/2>bay.x-bay.width/2);assert.ok(front.position[0]+front.size[0]/2<bay.x+bay.width/2);}
 const panels=manufacturingBatches([cabinet])[0].layout.sheets.flatMap(s=>s.panels);assert.equal(new Set(panels.map(p=>p.id)).size,panels.length);
 assert.equal(cabinetBom({...cabinet,columnFronts:['open','open','open']}).handles,0);
 assert.equal(cabinetParts(item).filter(p=>p.part==='front').length,2);
});
test('front configurations validate and survive project copies and presets',()=>{
 for(const columnFronts of [null,[],['open'],['open','bad','single'],Array(3)])assert.throws(()=>validateFurniture([{...item,columns:3,columnFronts}],room.outline,3));
 const cabinet={...item,columns:3,columnFronts:['open','single','double']},source={...room,furniture:[cabinet]};
 const project=validateProject({schemaVersion:6,presets:[],name:'Test',units:'m',room:source});assert.deepEqual(validateProject(JSON.parse(JSON.stringify(project))).room.furniture[0].columnFronts,cabinet.columnFronts);
 const copy=duplicateCabinet(source,'a','b',{x:2,z:0},.1).furniture[1];copy.columnFronts[0]='double';assert.equal(cabinet.columnFronts[0],'open');
 const [preset]=saveFurniturePreset([],{...cabinet,id:'fronts',name:'Mixed fronts'});assert.deepEqual(instantiateFurniturePreset(source,preset,'c',{x:-2,z:0},.1).furniture[1].columnFronts,cabinet.columnFronts);
});
