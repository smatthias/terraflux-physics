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
 const rows=[['Surface storage','stored_volume_m3'],['Soil storage','soil_volume_m3'],['Evaporated','evaporated_volume_m3'],['Drained','drained_volume_m3'],['Outlet discharge','discharged_volume_m3'],['Rain input','cumulative_rainfall_volume_m3']]
 return rows.map(([label,key])=>({label,baseline:a.surfaceWater[key],variant:b.surfaceWater[key],delta:b.surfaceWater[key]-a.surfaceWater[key]}))
}

// Export validated comparison aggregates, one row per snapshot. Numeric values
// retain JS precision; scenario labels are escaped and spreadsheet-formula-safe.
export function comparisonCsv(baseline,variant,baselineName='Baseline',variantName='Variant'){
 if(!baseline.length||baseline.length!==variant.length)throw Error('CSV requires aligned nonempty scenario frames.')
 const metrics=['stored_volume_m3','soil_volume_m3','evaporated_volume_m3','drained_volume_m3','cumulative_rainfall_volume_m3','discharged_volume_m3','balance_error_m3']
 const parameters=['timestep_seconds','routing_speed_m_s','infiltration_rate_m_s','evaporation_rate_m_s','drainage_rate_m_s']
 const header=['valid_time_utc','elapsed_seconds','baseline_name','variant_name','known_cells','masked_cells',...metrics.flatMap(k=>[`baseline_${k}`,`variant_${k}`,`delta_${k}`]),...parameters.flatMap(k=>[`baseline_${k}`,`variant_${k}`]),'baseline_soil_capacity_m','variant_soil_capacity_m']
 const escape=value=>{
  if(typeof value==='number'){if(!Number.isFinite(value))throw Error('CSV cannot export nonfinite metrics.');return String(value)}
  let text=String(value??'')
  if(/^[\s]*[=+@-]/.test(text)||/^[\t\r]/.test(text))text="'"+text
  return /[",\r\n]/.test(text)?`"${text.replaceAll('"','""')}"`:text
 }
 const rows=baseline.map((a,i)=>{
  const b=variant[i]
  if(!a.surfaceWater||!b.surfaceWater||a.forecast.start_time_utc!==b.forecast.start_time_utc||a.forecast.elapsed_seconds!==b.forecast.elapsed_seconds)throw Error('CSV requires synchronized water output.')
  const valid=new Date(Date.parse(a.forecast.start_time_utc)+a.forecast.elapsed_seconds*1000).toISOString()
  const known=a.surfaceWater.values.filter(v=>v!==null).length
  return [valid,a.forecast.elapsed_seconds,baselineName,variantName,known,a.surfaceWater.values.length-known,
   ...metrics.flatMap(k=>[a.surfaceWater[k],b.surfaceWater[k],b.surfaceWater[k]-a.surfaceWater[k]]),
   ...parameters.flatMap(k=>[a.surfaceWater.parameters[k],b.surfaceWater.parameters[k]]),a.surfaceWater.soil_capacity_m,b.surfaceWater.soil_capacity_m]
 })
 return '\uFEFF'+[header,...rows].map(row=>row.map(escape).join(',')).join('\r\n')+'\r\n'
}
