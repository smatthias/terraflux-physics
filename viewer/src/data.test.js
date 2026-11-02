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

test('terrain has explicit datum and exact grid alignment',async()=>{
 const {readFile}=await import('node:fs/promises')
 const {validateSequence}=await import('./data.js')
 const input=JSON.parse(await readFile(new URL('../public/examples/engine-timeline.json',import.meta.url),'utf8'))
 const frames=validateSequence(input)
 assert.equal(frames[0].terrain,frames[6].terrain)
 assert.equal(frames[0].terrain.values.length,120)
 assert.ok(frames[0].terrain.values.every(v=>v>=80&&v<=780))
 for(const mutate of [d=>d.terrain.grid.nx++,d=>d.terrain.values.pop(),d=>d.terrain.values[0]=null,d=>d.terrain.units='ft',d=>d.terrain.vertical_reference='',d=>d.terrain.source_id='']){
  const invalid=structuredClone(input);mutate(invalid);assert.throws(()=>validateSequence(invalid))
 }
 const noTerrain=structuredClone(input);delete noTerrain.terrain;delete noTerrain.terrain_temperature;delete noTerrain.terrain_derivatives;delete noTerrain.surface_water
 assert.equal(validateSequence(noTerrain)[0].terrain,undefined)
})

test('terrain temperature diagnostic matches the explicit lapse-rate relation',async()=>{
 const {readFile}=await import('node:fs/promises')
 const {validateSequence}=await import('./data.js')
 const input=JSON.parse(await readFile(new URL('../public/examples/engine-timeline.json',import.meta.url),'utf8'))
 const frames=validateSequence(input)
 for(const f of frames)for(let i=0;i<120;i++){
  const expected=f.forecast.fields.air_temperature.values[i]-.0065*(f.terrain.values[i]-80)
  assert.ok(Math.abs(f.terrainTemperature.values[i]-expected)<1e-10)
 }
 for(const mutate of [d=>d.terrain_temperature.vertical_reference='other',d=>d.terrain_temperature.values_by_frame.pop(),d=>d.terrain_temperature.values_by_frame[0][0]+=1,d=>d.terrain_temperature.lapse_rate_k_per_m=null,d=>delete d.terrain]){
  const invalid=structuredClone(input);mutate(invalid);assert.throws(()=>validateSequence(invalid))
 }
 const disabled=structuredClone(input);delete disabled.terrain_temperature
 assert.equal(validateSequence(disabled)[0].terrainTemperature,undefined)
})


test('terrain derivatives preserve undefined flat aspect and reject malformed arrays',async()=>{
 const {readFile}=await import('node:fs/promises')
 const {validateSequence}=await import('./data.js')
 const input=JSON.parse(await readFile(new URL('../public/examples/engine-timeline.json',import.meta.url),'utf8'))
 const d=validateSequence(input)[0].terrainDerivatives
 assert.equal(d.slope_deg.length,120)
 assert.equal(d.slope_deg[5*12+7],0)
 assert.equal(d.aspect_deg[5*12+7],null)
 assert.ok(d.slope_deg.some(v=>v>0))
 for(const mutate of [x=>x.terrain_derivatives.slope_deg.pop(),x=>x.terrain_derivatives.slope_deg[0]=-1,x=>x.terrain_derivatives.aspect_deg[0]=360,x=>x.terrain_derivatives.aspect_deg[5*12+7]=0,x=>x.terrain_derivatives.method='unknown']){
  const invalid=structuredClone(input);mutate(invalid);assert.throws(()=>validateSequence(invalid))
 }
})


test('surface water preserves masked cells and balances all covered rainfall',async()=>{
 const {readFile}=await import('node:fs/promises')
 const {validateSequence}=await import('./data.js')
 const input=JSON.parse(await readFile(new URL('../public/examples/engine-timeline.json',import.meta.url),'utf8'))
 const frames=validateSequence(input)
 for(const f of frames){
  assert.equal(f.surfaceWater.values.filter(v=>v===null).length,4)
  assert.ok(Math.abs(f.surfaceWater.balance_error_m3)<1e-7)
 }
 assert.ok(frames.at(-1).surfaceWater.stored_volume_m3>0)
 assert.ok(frames.at(-1).surfaceWater.values.some((v,i)=>v!==null&&Math.abs(v-frames.at(-1).rain.values[i]/1000)>1e-6),'routing redistributes rainfall')
 for(const mutate of [d=>d.surface_water.values_by_frame[1][0]=-1,d=>d.surface_water.stored_volume_m3[1]+=10,d=>d.surface_water.cumulative_rainfall_volume_m3[1]+=10,d=>d.surface_water.boundary='open',d=>d.surface_water.values_by_frame[1][0]=null]){
  const invalid=structuredClone(input);mutate(invalid);assert.throws(()=>validateSequence(invalid))
 }
})

test('soil water stays within capacity and participates in combined mass balance',async()=>{
 const {readFile}=await import('node:fs/promises')
 const {validateSequence}=await import('./data.js')
 const input=JSON.parse(await readFile(new URL('../public/examples/engine-timeline.json',import.meta.url),'utf8'))
 const frames=validateSequence(input)
 const final=frames.at(-1).surfaceWater
 assert.ok(final.soil_volume_m3>0)
 assert.ok(final.soil_values.some(v=>v===.0015),'some cells reach capacity')
 assert.equal(final.soil_values.filter(v=>v===null).length,4)
 assert.ok(Math.abs(final.stored_volume_m3+final.soil_volume_m3-final.cumulative_rainfall_volume_m3)<1e-7)
 for(const mutate of [d=>d.surface_water.soil_values_by_frame[1][0]=1,d=>d.surface_water.soil_volume_m3[1]+=1,d=>d.surface_water.soil_values_by_frame[1][0]=null,d=>delete d.surface_water.infiltration_rate_m_s]){
  const invalid=structuredClone(input);mutate(invalid);assert.throws(()=>validateSequence(invalid))
 }
})
