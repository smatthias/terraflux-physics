import {test} from 'node:test';
import assert from 'node:assert/strict';
import {duplicateProject} from './duplicate-project.mjs';
import {validateProject} from './project.mjs';
import {exportPlanSvg} from './plan-svg.mjs';
const project=validateProject({schemaVersion:1,units:'m',name:'Studio',room:{width:6,depth:4,height:3}});
test('duplication copies nested editable data, excludes server identity and bounds the name',()=>{
  const original={...project,id:'server-id',name:'x'.repeat(120),presets:[{id:'entry',name:'Entry',type:'door',width:.9,height:2.1,sill:0,hinge:'A',swing:'in'}]};
  const copy=duplicateProject(original);
  assert.equal(copy.name.length,120);assert.ok(copy.name.endsWith(' (copy)'));assert.equal(copy.id,undefined);
  copy.room.outline[0].x-=1;copy.presets[0].width=1.1;
  assert.equal(original.room.outline[0].x,-3);assert.equal(original.presets[0].width,.9);
});
test('scaled SVG physical dimensions encode the selected uniform scale and preserve measurements',()=>{
  const before=JSON.stringify(project);
  for(const scale of [20,50,100,200]) {
    const svg=exportPlanSvg(project,{scale});
    const viewBox=svg.match(/viewBox="([^"]+)"/)[1].split(' ').map(Number);
    const width=Number(svg.match(/width="([0-9.]+)mm"/)[1]),height=Number(svg.match(/height="([0-9.]+)mm"/)[1]);
    assert.ok(Math.abs(width/viewBox[2]-1000/scale)<1e-5);
    assert.ok(Math.abs(height/viewBox[3]-1000/scale)<1e-5);
    assert.match(svg,new RegExp('data-print-scale="1:'+scale+'"'));
    assert.match(svg,/6.00 m/);assert.match(svg,/print at 100%/);
  }
  assert.match(exportPlanSvg(project),/width="1200"/);
  assert.throws(()=>exportPlanSvg(project,{scale:0}));assert.throws(()=>exportPlanSvg(project,{scale:75}));
  assert.equal(JSON.stringify(project),before);
});
