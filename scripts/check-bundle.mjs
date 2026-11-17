import assert from 'node:assert/strict';
import {readFile,stat,mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {gzipSync} from 'node:zlib';
const root=resolve('dist'),manifest=JSON.parse(await readFile(resolve(root,'.vite/manifest.json'),'utf8'));
const entry=manifest['index.html'];assert.ok(entry?.isEntry,'Production entry missing');
const initial=new Set();
function visit(key){if(initial.has(key))return;const chunk=manifest[key];assert.ok(chunk,'Missing imported chunk '+key);initial.add(key);for(const imported of chunk.imports??[])visit(imported);}
visit('index.html');
const chunks=[];
for(const [key,chunk] of Object.entries(manifest)){
 const file=resolve(root,chunk.file);assert.ok(file.startsWith(root+'/'),'Asset outside dist');
 const bytes=await readFile(file);assert.ok(bytes.length<=400000,`${chunk.file} exceeds 400 kB chunk budget`);
 for(const css of chunk.css??[])assert.ok((await stat(resolve(root,css))).size<=15000,'CSS exceeds 15 kB budget');
 for(const imported of [...chunk.imports??[],...chunk.dynamicImports??[]])assert.ok(manifest[imported],'Missing manifest dependency '+imported);
 chunks.push({file:chunk.file,bytes:bytes.length,gzipBytes:gzipSync(bytes).length,initial:initial.has(key)});
}
// The furniture designer perspective is budgeted on its own: it must stay lazy and
// within 82 kB (80 kB until its panels became desk windows), and the rest of the
// application keeps the 1000 kB total (950 kB until CAM posts, holding tabs and design
// manufacturing, 975 kB until furniture snapping and auto-arrange, 990 kB until the desk).
const designerKey='src/DesignerWorkspace.vue',designer=manifest[designerKey];
assert.ok(designer?.isDynamicEntry&&!initial.has(designerKey),designerKey+' must stay lazy');
const designerBytes=chunks.find(c=>c.file===designer.file).bytes;
assert.ok(designerBytes<=82000,'Furniture designer exceeds 82 kB budget');
// The version-history panel likewise: lazy and within 12 kB (recording versions is part of the entry).
const historyKey='src/TimelinePanel.vue',historyPanel=manifest[historyKey];
assert.ok(historyPanel?.isDynamicEntry&&!initial.has(historyKey),historyKey+' must stay lazy');
const historyBytes=chunks.find(c=>c.file===historyPanel.file).bytes;
assert.ok(historyBytes<=12000,'Version history panel exceeds 12 kB budget');
// The workshop benches (designer tools) likewise: the drilling and cutting
// benches, the workbench and the chunks only they import must stay lazy and within 100 kB.
const benchKeys=new Set(['src/DrillingBench.vue','src/CuttingBench.vue','src/Workbench.vue']);
for(const key of benchKeys) assert.ok(manifest[key]?.isDynamicEntry&&!initial.has(key),key+' must stay lazy');
const importers=new Map();
for(const [key,chunk] of Object.entries(manifest)) for(const imported of chunk.imports??[]) importers.set(imported,[...(importers.get(imported)??[]),key]);
for(let grew=true;grew;) {grew=false;for(const [key,list] of importers) if(!benchKeys.has(key)&&list.every(k=>benchKeys.has(k))) {benchKeys.add(key);grew=true;}}
const benchFiles=new Set([...benchKeys].map(k=>manifest[k].file)),benchBytes=chunks.filter(c=>benchFiles.has(c.file)).reduce((n,c)=>n+c.bytes,0);
assert.ok(benchBytes<=100000,'Workshop benches exceed 100 kB budget');
// Initial JavaScript: 870 kB since the studio became a desk of windows (860 kB before);
// total 1000 kB (990 kB before): the desk moved from the benches' chunks into the entry.
const entryBytes=chunks.find(c=>c.file===entry.file).bytes,initialBytes=chunks.filter(c=>c.initial).reduce((n,c)=>n+c.bytes,0),totalBytes=chunks.reduce((n,c)=>n+c.bytes,0)-designerBytes-historyBytes-benchBytes;
assert.ok(entryBytes<=180000,'Entry exceeds 180 kB budget');assert.ok(initialBytes<=870000,'Initial JavaScript exceeds 870 kB budget');assert.ok(totalBytes<=1000000,'Total JavaScript exceeds 1000 kB budget');
for(const key of ['src/ManufacturingPanel.vue','src/WorkshopJobs.vue'])assert.ok(manifest[key]?.isDynamicEntry&&!initial.has(key),key+' must stay lazy');
await mkdir('test-results',{recursive:true});await writeFile('test-results/bundle-report.json',JSON.stringify({entryBytes,initialBytes,totalBytes,designerBytes,historyBytes,benchBytes,chunks},null,2)+'\n');
console.log(`Bundle budgets passed: entry ${(entryBytes/1000).toFixed(1)} kB, initial ${(initialBytes/1000).toFixed(1)} kB, total ${(totalBytes/1000).toFixed(1)} kB plus designer ${(designerBytes/1000).toFixed(1)} kB history panel ${(historyBytes/1000).toFixed(1)} kB and workshop benches ${(benchBytes/1000).toFixed(1)} kB; ${chunks.length} chunks.`);
