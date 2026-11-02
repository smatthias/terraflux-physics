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
