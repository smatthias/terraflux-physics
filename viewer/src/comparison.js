import { validateSequence } from './data.js'
export function validateComparison(baseline, input){
 const variant=validateSequence(input)
 const same=(a,b)=>{
  if(typeof a==='number'&&typeof b==='number')return Math.abs(a-b)<=1e-9*Math.max(1,Math.abs(a),Math.abs(b))
  if(Array.isArray(a)&&Array.isArray(b))return a.length===b.length&&a.every((v,i)=>same(v,b[i]))
  if(a&&b&&typeof a==='object'&&typeof b==='object')return Object.keys(a).length===Object.keys(b).length&&Object.keys(a).every(k=>same(a[k],b[k]))
  return a===b
 }
 if(variant.length!==baseline.length)throw Error('Comparison requires the same number of snapshots.')
 baseline.forEach((a,i)=>{
  const b=variant[i]
  if(!a.surfaceWater||!b.surfaceWater)throw Error('Both scenarios must contain water output at every frame.')
  if(!same(a.forecast.grid,b.forecast.grid)||a.forecast.start_time_utc!==b.forecast.start_time_utc||a.forecast.elapsed_seconds!==b.forecast.elapsed_seconds)throw Error('Comparison grids and timestamps must match.')
  if(!same(a.terrain,b.terrain)||!same(a.rain,b.rain)||!same(a.forecast.fields,b.forecast.fields))throw Error('Comparison requires matching terrain, rainfall and atmospheric forcing.')
  if(!same(a.surfaceWater.values.map(v=>v===null),b.surfaceWater.values.map(v=>v===null)))throw Error('Comparison coverage masks must match.')
 })
 return variant
}
export function waterComparison(a,b){
 const rows=[['Surface storage','stored_volume_m3'],['Soil storage','soil_volume_m3'],['Evaporated','evaporated_volume_m3'],['Drained','drained_volume_m3'],['Rain input','cumulative_rainfall_volume_m3']]
 return rows.map(([label,key])=>({label,baseline:a.surfaceWater[key],variant:b.surfaceWater[key],delta:b.surfaceWater[key]-a.surfaceWater[key]}))
}
