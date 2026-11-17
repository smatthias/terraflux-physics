import {test} from 'node:test';
import assert from 'node:assert/strict';
import {exportPlanSvg} from './plan-svg.mjs';
import {validateProject} from './project.mjs';
test('SVG includes escaped title, measured geometry and symbols without mutating the project',()=>{
  const project=validateProject({schemaVersion:1,units:'m',name:'A <script> & "Studio"',room:{width:6,depth:4,height:3}});
  project.room.openings=[{id:'door',wall:'north',type:'door',offset:1,width:.83,height:2.03,sill:0,hinge:'B',swing:'out'}];
  const before=JSON.stringify(project),svg=exportPlanSvg(project);
  assert.match(svg,/xmlns="http:\/\/www.w3.org\/2000\/svg"/);
  assert.match(svg,/6.00 m/);assert.match(svg,/0.83 m/);assert.match(svg,/A &lt;script&gt; &amp; &quot;Studio&quot;/);
  assert.equal(svg.includes('<script>'),false);assert.equal(/NaN|Infinity|undefined/.test(svg),false);
  assert.match(svg,/model coordinates in metres/);assert.equal(JSON.stringify(project),before);
});
