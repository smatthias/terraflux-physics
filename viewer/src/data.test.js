import { test } from 'node:test'
import assert from 'node:assert/strict'
import { demo, validate } from './data.js'
test('demo and bare forecast load; missing rainfall remains null',()=>{const input=demo();assert.equal(validate(input).rain.values[18*32],null);assert.equal(validate(input.forecast).rain,null)})
test('reject malformed and misaligned fields',()=>{for(const mutate of [d=>d.forecast.fields.air_temperature.values.pop(),d=>d.forecast.grid.nx=100000,d=>d.precipitation_accumulation.values[0]=null,d=>d.precipitation_accumulation.valid_time_utc='2026-11-02T00:00:00Z',d=>d.forecast.fields.wind_east.values[0]=Infinity]){const d=demo();mutate(d);assert.throws(()=>validate(d))}})

test('bundled engine output loads with valid coverage and prescribed wind',async()=>{
 const {readFile}=await import('node:fs/promises')
 const input=JSON.parse(await readFile(new URL('../public/examples/engine-weather.json',import.meta.url),'utf8'))
 const {forecast,rain}=validate(input)
 assert.equal(forecast.grid.nx*forecast.grid.ny,120)
 assert.equal(forecast.elapsed_seconds,3600)
 assert.equal(rain.quality.filter(q=>q==='no_data').length,4)
 assert.equal(rain.quality.filter(q=>q==='decoded').length,116)
 assert.ok(forecast.fields.wind_east.values.every(v=>v===1.5))
 assert.ok(rain.values.filter(v=>v!==null).every(v=>v>0))
})

test('timeline has seven increasing solver snapshots and final-only rainfall',async()=>{
 const {readFile}=await import('node:fs/promises')
 const {validateSequence}=await import('./data.js')
 const input=JSON.parse(await readFile(new URL('../public/examples/engine-timeline.json',import.meta.url),'utf8'))
 const frames=validateSequence(input)
 assert.equal(frames.length,7)
 assert.deepEqual(frames.map(f=>f.forecast.elapsed_seconds),[0,600,1200,1800,2400,3000,3600])
 assert.ok(frames.slice(0,-1).every(f=>f.rain===null))
 assert.ok(frames.at(-1).rain)
 assert.notDeepEqual(frames[0].forecast.fields.air_temperature.values,frames.at(-1).forecast.fields.air_temperature.values)
 const total=f=>f.forecast.fields.air_temperature.values.reduce((a,b)=>a+b,0)
 for(const frame of frames)assert.ok(Math.abs(total(frame)-total(frames[0]))<1e-7,'periodic transport conserves scalar sum')
 for(const mutate of [d=>d.frames.reverse(),d=>d.frames[1].grid.spacing_east_m=2000,d=>d.frames[1].start_time_utc='2026-11-02T12:00:00Z',d=>d.frames[1].elapsed_seconds=0]){
  const invalid=structuredClone(input);mutate(invalid);assert.throws(()=>validateSequence(invalid))
 }
})
