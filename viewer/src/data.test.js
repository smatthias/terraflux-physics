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

test('timeline has seven solver snapshots with cumulative rainfall after time zero',async()=>{
 const {readFile}=await import('node:fs/promises')
 const {validateSequence}=await import('./data.js')
 const input=JSON.parse(await readFile(new URL('../public/examples/engine-timeline.json',import.meta.url),'utf8'))
 const frames=validateSequence(input)
 assert.equal(frames.length,7)
 assert.deepEqual(frames.map(f=>f.forecast.elapsed_seconds),[0,600,1200,1800,2400,3000,3600])
 assert.equal(frames[0].rain,null)
 assert.ok(frames.slice(1).every(f=>f.rain))
 for(let i=1;i<frames.length;i++){
  const rain=frames[i].rain
  assert.equal(rain.quality.filter(q=>q==='no_data').length,4)
  assert.equal(Date.parse(rain.valid_time_utc)-Date.parse(rain.period_start_utc),i*600000)
  // Independent Z-R + trapezoidal reference at cell 0 for all prefix intervals.
  const baseDbz=15+25*Math.exp(-(7*7+4*4)/10)
  const rate=k=>(10**((baseDbz+k/3)/10)/300)**(1/1.4)
  let expected=0
  for(let k=1;k<=i;k++)expected+=(rate(k-1)+rate(k))/2/6
  assert.ok(Math.abs(rain.values[0]-expected)<1e-10)
  if(i>1)for(let cell=0;cell<120;cell++){
   if(rain.values[cell]!==null)assert.ok(rain.values[cell]>frames[i-1].rain.values[cell])
   else assert.equal(frames[i-1].rain.values[cell],null)
  }
 }
 assert.ok(frames.at(-1).rain)
 assert.notDeepEqual(frames[0].forecast.fields.air_temperature.values,frames.at(-1).forecast.fields.air_temperature.values)
 const total=f=>f.forecast.fields.air_temperature.values.reduce((a,b)=>a+b,0)
 for(const frame of frames)assert.ok(Math.abs(total(frame)-total(frames[0]))<1e-7,'periodic transport conserves scalar sum')
 for(const mutate of [d=>d.frames.reverse(),d=>d.frames[1].forecast.grid.spacing_east_m=2000,d=>d.frames[1].forecast.start_time_utc='2026-11-02T12:00:00Z',d=>d.frames[1].forecast.elapsed_seconds=0]){
  const invalid=structuredClone(input);mutate(invalid);assert.throws(()=>validateSequence(invalid))
 }
})
