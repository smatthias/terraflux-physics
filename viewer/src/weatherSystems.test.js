import { test } from 'node:test'
import assert from 'node:assert/strict'
import { regionalWeatherDemo,summarizeFrame } from './weatherSystems.js'
import { validateSequence } from './data.js'
test('regional systems move with bounded synthetic cloud and humidity fields',()=>{
 const input=regionalWeatherDemo(),frames=validateSequence(input)
 assert.equal(frames.length,14)
 assert.equal(summarizeFrame(frames[0]).area_km2,1080000)
 assert.ok(frames[13].atmosphere.systems[0].u>frames[0].atmosphere.systems[0].u)
 assert.notDeepEqual(frames[0].atmosphere.cloud,frames[13].atmosphere.cloud)
 const m=summarizeFrame(frames[0]);assert.ok(m.cloud_pct>0&&m.cloud_pct<100)
 assert.ok(m.pressure_min_hpa<1000&&m.pressure_max_hpa>1020)
 assert.ok(m.humidity_mean_pct>0&&m.humidity_mean_pct<100)
 for(const mutate of [d=>d.atmosphere.cloud_fraction_by_frame[0][0]=2,d=>d.atmosphere.relative_humidity_pct_by_frame[0][0]=-1,d=>d.atmosphere.sea_level_pressure_hpa_by_frame[0].pop(),d=>d.atmosphere.systems_by_frame[0][0].u=2,d=>d.atmosphere.source_kind='observed']){const d=structuredClone(input);mutate(d);assert.throws(()=>validateSequence(d))}
})
test('metrics average known rainfall only and count wet area with explicit threshold',()=>{
 const f={forecast:{grid:{nx:2,ny:2,spacing_east_m:1000,spacing_north_m:1000},fields:{air_temperature:{values:[273.15,283.15,293.15,303.15]},wind_east:{values:[3,0,0,0]},wind_north:{values:[4,0,0,0]}}},rain:{values:[0,2,null,null]},surfaceWater:{values:[0,.001,null,null],soil_values:[0,.001,null,null],soil_capacity_m:.002}}
 const m=summarizeFrame(f)
 assert.equal(m.rain_mean_mm,1);assert.equal(m.rain_coverage_pct,50);assert.equal(m.wet_area_km2,1);assert.equal(m.water_max_mm,1);assert.equal(m.soil_fill_pct,25);assert.equal(m.wind_max_m_s,5);assert.equal(m.temperature_mean_c,15)
})
