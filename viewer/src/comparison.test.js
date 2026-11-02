import {test} from 'node:test'
import assert from 'node:assert/strict'
import {readFile} from 'node:fs/promises'
import {validateSequence} from './data.js'
import {validateComparison,waterComparison} from './comparison.js'
const read=async path=>JSON.parse(await readFile(new URL(path,import.meta.url),'utf8'))
test('matched deterministic scenarios isolate infiltration and expose storage deltas',async()=>{
 const a=await read('../public/examples/engine-timeline.json'),b=await read('../public/comparison/engine-timeline.json')
 const baseline=validateSequence(a),variant=validateComparison(baseline,b)
 const rows=waterComparison(baseline.at(-1),variant.at(-1))
 assert.equal(rows.find(r=>r.label==='Rain input').delta,0)
 assert.ok(rows.find(r=>r.label==='Surface storage').delta>0)
 assert.ok(rows.find(r=>r.label==='Soil storage').delta<0)
 assert.equal(b.water_scenario.infiltration_rate_m_s,a.water_scenario.infiltration_rate_m_s/10)
 assert.ok(waterComparison(baseline[0],variant[0]).every(r=>r.delta===0))
 assert.ok(waterComparison(baseline[7],validateComparison(baseline,a)[7]).every(r=>r.delta===0))
 for(const mutate of [x=>x.frames.pop(),x=>x.frames[1].forecast.fields.wind_east.values[0]+=1,x=>x.terrain.values[0]+=1]){
  const invalid=structuredClone(b);mutate(invalid);assert.throws(()=>validateComparison(baseline,invalid))
 }
})

test('CSV exports precise budgets, timestamps and applied parameters for every frame',async()=>{
 const {comparisonCsv}=await import('./comparison.js')
 const baseline=validateSequence(await read('../public/examples/engine-timeline.json'))
 const variant=validateComparison(baseline,await read('../public/comparison/engine-timeline.json'))
 const csv=comparisonCsv(baseline,variant,'Baseline','Lower infiltration')
 assert.ok(csv.startsWith('\uFEFFvalid_time_utc,'))
 const rows=csv.slice(1).trimEnd().split('\r\n').map(row=>row.split(',')),header=rows.shift()
 assert.equal(rows.length,14)
 assert.ok(rows.every(row=>row.length===header.length))
 const last=rows.at(-1),column=k=>last[header.indexOf(k)]
 assert.equal(column('valid_time_utc'),'2026-11-01T14:10:00.000Z')
 assert.equal(Number(column('known_cells')),116);assert.equal(Number(column('masked_cells')),4)
 assert.equal(Number(column('delta_stored_volume_m3')),variant.at(-1).surfaceWater.stored_volume_m3-baseline.at(-1).surfaceWater.stored_volume_m3)
 assert.equal(Number(column('variant_infiltration_rate_m_s')),1e-7)
 assert.ok(comparisonCsv(baseline,variant,'A, "quoted"',' =HYPERLINK("x")').includes('"A, ""quoted"""'))
 assert.ok(comparisonCsv(baseline,variant,'Baseline','=1+1').includes(",'=1+1,"))
 assert.throws(()=>comparisonCsv([],[]))
})
