import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validateProject} from './project.mjs';
import {exportPlanSvg,planSheetLayout} from './plan-svg.mjs';
const project=validateProject({schemaVersion:1,units:'m',name:'Sheet studio',room:{width:11,depth:4,height:3}});
test('A4/A3 portrait and landscape exports preserve scale and centre the complete drawing in usable space',()=>{
  for(const paper of ['A4','A3'])for(const orientation of ['portrait','landscape']) {
    const layout=planSheetLayout(project,{scale:100,paper,orientation});
    assert.equal(layout.fits,true);assert.equal(layout.scale,100);
    const short=paper==='A4'?210:297,long=paper==='A4'?297:420;
    assert.equal(layout.width,orientation==='portrait'?short:long);
    assert.equal(layout.height,orientation==='portrait'?long:short);
    const svg=exportPlanSvg(project,{scale:100,paper,orientation});
    assert.match(svg,new RegExp('width="'+layout.width+'mm"'));
    assert.match(svg,new RegExp('height="'+layout.height+'mm"'));
    const transform=svg.match(/data-plan="true" transform="translate\(([^)]+)\) scale\(([^)]+)\)"/);
    const [x,y]=transform[1].split(' ').map(Number),factor=Number(transform[2]);
    assert.equal(factor,10);
    const {drawing}=layout,left=x+drawing.minX*factor,top=y+drawing.minZ*factor;
    assert.ok(left>=10 && top>=10);
    assert.ok(left+layout.requiredWidth<=layout.width-10+1e-8);
    assert.ok(top+layout.requiredHeight<=layout.height-22+1e-8);
    assert.ok(Math.abs(left-(layout.width-left-layout.requiredWidth))<1e-8);
  }
});
test('oversized sheets are blocked, suggestions fit, and settings never silently change',()=>{
  const options={scale:20,paper:'A4',orientation:'landscape'};
  const before=JSON.stringify(project),layout=planSheetLayout(project,options);
  assert.equal(layout.fits,false);assert.equal(options.scale,20);assert.ok(layout.suggestedScale);
  assert.throws(()=>exportPlanSvg(project,options),/does not fit/);
  assert.equal(planSheetLayout(project,{...options,scale:layout.suggestedScale}).fits,true);
  assert.throws(()=>planSheetLayout(project,{paper:'A4'}),/Choose a print scale/);
  assert.throws(()=>planSheetLayout(project,{paper:'A5',scale:100}));
  assert.equal(JSON.stringify(project),before);
});
