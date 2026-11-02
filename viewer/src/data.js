const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }
export function validate(input) {
  const envelope = input.schema_version === 'terraflux.weather.mobile_response/v1'
  const forecast = envelope ? input.forecast : input
  check(forecast?.schema_version === 'terraflux.weather.forecast/v1', 'Expected a temperature forecast or mobile response v1.')
  const g = forecast.grid
  check(g && Number.isInteger(g.nx) && Number.isInteger(g.ny) && g.nx >= 2 && g.ny >= 2 && g.nx * g.ny <= 65536, 'Grid must contain 2–65,536 cells, with dimensions at least 2.')
  check(g.coordinate_system === 'local_azimuthal_equidistant_m' && g.array_order === 'row_major_y_then_x', 'Unsupported coordinate system or array order.')
  for (const key of ['origin_east_m','origin_north_m','spacing_east_m','spacing_north_m','geographic_anchor_longitude_deg','geographic_anchor_latitude_deg']) check(finite(g[key]), `Invalid grid ${key}.`)
  check(g.spacing_east_m > 0 && g.spacing_north_m > 0, 'Grid spacing must be positive.')
  check(Math.abs(g.geographic_anchor_longitude_deg) <= 180 && Math.abs(g.geographic_anchor_latitude_deg) <= 90, 'Invalid geographic anchor.')
  check(typeof forecast.start_time_utc === 'string' && forecast.start_time_utc.endsWith('Z') && Number.isFinite(Date.parse(forecast.start_time_utc)) && finite(forecast.elapsed_seconds) && forecast.elapsed_seconds >= 0, 'Invalid forecast time.')
  const count = g.nx * g.ny
  for (const [name, units] of [['air_temperature','K'],['wind_east','m/s'],['wind_north','m/s']]) {
    const field = forecast.fields?.[name]
    check(field?.units === units && Array.isArray(field.values) && field.values.length === count && field.values.every(finite), `Invalid ${name} array or units.`)
  }
  check(forecast.fields.air_temperature.values.every(v => v > 0), 'Temperature must be positive kelvin.')
  const rain = envelope ? input.precipitation_accumulation : null
  if (envelope) {
    check(rain?.units === 'mm' && rain.method === 'trapezoidal_rate_integration', 'Invalid rainfall units or method.')
    check(['values','quality'].every(k => Array.isArray(rain[k]) && rain[k].length === count), 'Rainfall arrays do not match the grid.')
    check(rain.quality.every((q,i) => q === 'decoded' ? finite(rain.values[i]) && rain.values[i] >= 0 : ['no_data','ambiguous'].includes(q) && rain.values[i] === null), 'Rainfall quality and values disagree.')
    check(rain.period_start_utc === forecast.start_time_utc && typeof rain.valid_time_utc === 'string' && rain.valid_time_utc.endsWith('Z') && Date.parse(rain.valid_time_utc) === Date.parse(forecast.start_time_utc) + forecast.elapsed_seconds * 1000, 'Rainfall interval does not match the forecast.')
  }
  return { forecast, rain }
}
export function demo() {
  const nx=32, ny=24, t=[], u=[], v=[], rain=[], quality=[]
  for(let y=0;y<ny;y++) for(let x=0;x<nx;x++) {
    const bump=Math.exp(-((x-19)**2+(y-12)**2)/75)
    t.push(285 + 13*bump + y*.13); u.push(3 + 2*Math.sin(y/5)); v.push(1.5*Math.cos(x/7))
    const missing=x<4 && y>17
    rain.push(missing?null:Math.max(0,9*Math.exp(-((x-9)**2+(y-15)**2)/50)-.2)); quality.push(missing?'no_data':'decoded')
  }
  return {schema_version:'terraflux.weather.mobile_response/v1', forecast:{schema_version:'terraflux.weather.forecast/v1', start_time_utc:'2026-11-01T12:00:00Z', elapsed_seconds:3600, observation_updates:[], grid:{nx,ny,origin_east_m:0,origin_north_m:0,spacing_east_m:1000,spacing_north_m:1000,geographic_anchor_longitude_deg:-100,geographic_anchor_latitude_deg:38,coordinate_system:'local_azimuthal_equidistant_m',array_order:'row_major_y_then_x'},fields:{air_temperature:{units:'K',values:t},wind_east:{units:'m/s',values:u},wind_north:{units:'m/s',values:v}}}, precipitation_accumulation:{period_start_utc:'2026-11-01T12:00:00Z',valid_time_utc:'2026-11-01T13:00:00Z',units:'mm',method:'trapezoidal_rate_integration',values:rain,quality}}
}

// Snapshot playback only: no interpolation and no browser-side simulation.
export function validateSequence(input) {
  if(input?.schema_version !== 'terraflux.weather.timeline/v1') return [validate(input)]
  check(Array.isArray(input.frames) && input.frames.length >= 2 && input.frames.length <= 240, 'Timeline requires 2–240 frames.')
  const frames=input.frames.map(validate), first=frames[0].forecast
  if(input.terrain !== undefined){
    const terrain=validateTerrain(input.terrain,first.grid)
    frames.forEach(frame=>{frame.terrain=terrain})
  }
  const keys=Object.keys(first.grid)
  check(frames.length*first.grid.nx*first.grid.ny <= 2000000, 'Timeline exceeds two million cell snapshots.')
  frames.forEach((frame,i)=>{
    const f=frame.forecast
    check(keys.every(key=>f.grid[key]===first.grid[key]) && Object.keys(f.grid).length===keys.length, 'Timeline grids must match exactly.')
    check(f.start_time_utc===first.start_time_utc, 'Timeline start times must match.')
    check(i===0 || f.elapsed_seconds>frames[i-1].forecast.elapsed_seconds, 'Timeline frames must have strictly increasing elapsed times.')
  })
  if(input.atmosphere !== undefined){
    const a=input.atmosphere,count=first.grid.nx*first.grid.ny
    check(a?.schema_version==='terraflux.weather.atmosphere_display/v1'&&a.source_kind==='synthetic','Only explicitly synthetic atmosphere displays are currently supported.')
    for(const key of ['cloud_fraction_by_frame','sea_level_pressure_hpa_by_frame','relative_humidity_pct_by_frame','systems_by_frame'])check(Array.isArray(a[key])&&a[key].length===frames.length,'Atmosphere frame count mismatch.')
    frames.forEach((frame,i)=>{
      const cloud=a.cloud_fraction_by_frame[i],pressure=a.sea_level_pressure_hpa_by_frame[i],humidity=a.relative_humidity_pct_by_frame[i],systems=a.systems_by_frame[i]
      check(Array.isArray(cloud)&&cloud.length===count&&cloud.every(v=>finite(v)&&v>=0&&v<=1),'Invalid cloud fraction.')
      check(Array.isArray(pressure)&&pressure.length===count&&pressure.every(v=>finite(v)&&v>0),'Invalid pressure field.')
      check(Array.isArray(humidity)&&humidity.length===count&&humidity.every(v=>finite(v)&&v>=0&&v<=100),'Invalid relative humidity.')
      const point=p=>Array.isArray(p)&&p.length===2&&p.every(v=>finite(v)&&v>=0&&v<=1)
      check(Array.isArray(systems)&&systems.length<=20&&systems.every(s=>s&&(['low','high'].includes(s.kind)?finite(s.u)&&finite(s.v)&&s.u>=0&&s.u<=1&&s.v>=0&&s.v<=1:s.kind==='cold_front'&&Array.isArray(s.points)&&s.points.length>=2&&s.points.length<=100&&s.points.every(point))),'Invalid weather-system positions.')
      frame.atmosphere={cloud,pressure,humidity,systems,synthetic:true}
    })
  }
  if(input.surface_water !== undefined){
    const w=input.surface_water,count=first.grid.nx*first.grid.ny,area=first.grid.spacing_east_m*first.grid.spacing_north_m
    check(input.terrain && w?.schema_version==='terraflux.water.timeline/v1' && w.units==='m' && w.method==='head_gradient_donor_limited' && w.boundary==='closed' && w.missing_rainfall_policy==='impermeable_mask', 'Unsupported surface water contract.')
    check(finite(w.routing_speed_m_s)&&w.routing_speed_m_s>=0&&finite(w.timestep_seconds)&&w.timestep_seconds>0, 'Invalid routing parameters.')
    for(const key of ['values_by_frame','stored_volume_m3','cumulative_rainfall_volume_m3','balance_error_m3'])check(Array.isArray(w[key])&&w[key].length===frames.length,'Water frame count mismatch.')
    const soilKeys=['soil_values_by_frame','soil_volume_m3','soil_storage_capacity_m','infiltration_rate_m_s','process_order']
    const hasSoil=soilKeys.some(k=>w[k]!==undefined)
    const lossKeys=['evaporation_rate_m_s','drainage_rate_m_s','cumulative_evaporation_volume_m3','cumulative_drainage_volume_m3']
    const hasLosses=lossKeys.some(k=>w[k]!==undefined)
    if(hasLosses){
      check(hasSoil&&lossKeys.every(k=>w[k]!==undefined)&&w.process_order==='rain_route_infiltrate_evaporate_drain','Invalid water loss configuration.')
      check(finite(w.evaporation_rate_m_s)&&w.evaporation_rate_m_s>=0&&finite(w.drainage_rate_m_s)&&w.drainage_rate_m_s>=0,'Invalid water loss rates.')
      for(const key of lossKeys.slice(2))check(Array.isArray(w[key])&&w[key].length===frames.length,'Water loss frame count mismatch.')
    }
    if(hasSoil){
      check(soilKeys.every(k=>w[k]!==undefined)&&(hasLosses?w.process_order==='rain_route_infiltrate_evaporate_drain':w.process_order==='rain_route_infiltrate')&&finite(w.soil_storage_capacity_m)&&w.soil_storage_capacity_m>=0&&finite(w.infiltration_rate_m_s)&&w.infiltration_rate_m_s>=0,'Invalid infiltration configuration.')
      check(Array.isArray(w.soil_values_by_frame)&&w.soil_values_by_frame.length===frames.length&&Array.isArray(w.soil_volume_m3)&&w.soil_volume_m3.length===frames.length,'Soil frame count mismatch.')
    }
    frames.forEach((frame,i)=>{
      const depths=w.values_by_frame[i]
      check(Array.isArray(depths)&&depths.length===count&&depths.every(v=>v===null||(finite(v)&&v>=0)), 'Invalid water depths.')
      check(i===0||depths.every((v,j)=>(v===null)===(w.values_by_frame[0][j]===null)), 'Water mask must remain fixed.')
      const volume=depths.reduce((a,v)=>a+(v??0)*area,0),stored=w.stored_volume_m3[i],rain=w.cumulative_rainfall_volume_m3[i],error=w.balance_error_m3[i]
      const evaporated=hasLosses?w.cumulative_evaporation_volume_m3[i]:0,drained=hasLosses?w.cumulative_drainage_volume_m3[i]:0
      check([evaporated,drained].every(v=>finite(v)&&v>=0),'Invalid water loss budget.')
      if(hasLosses){
        check(i===0?evaporated===0&&drained===0:evaporated>=w.cumulative_evaporation_volume_m3[i-1]&&drained>=w.cumulative_drainage_volume_m3[i-1],'Cumulative water losses must be nondecreasing.')
        const seconds=i===0?0:frame.forecast.elapsed_seconds-frames[i-1].forecast.elapsed_seconds
        const activeCount=depths.filter(v=>v!==null).length
        const previousEvap=i===0?0:w.cumulative_evaporation_volume_m3[i-1],previousDrain=i===0?0:w.cumulative_drainage_volume_m3[i-1]
        check(evaporated-previousEvap<=w.evaporation_rate_m_s*seconds*area*activeCount+1e-7&&drained-previousDrain<=w.drainage_rate_m_s*seconds*area*activeCount+1e-7,'Water losses exceed prescribed rate limits.')
      }
      const soil=hasSoil?w.soil_values_by_frame[i]:null,soilVolume=hasSoil?w.soil_volume_m3[i]:0
      if(hasSoil){
        check(Array.isArray(soil)&&soil.length===count&&soil.every((v,j)=>(v===null)===(depths[j]===null)&&(v===null||(finite(v)&&v>=0&&v<=w.soil_storage_capacity_m+1e-12))), 'Invalid soil storage or coverage.')
        check(hasLosses||i===0||soil.every((v,j)=>v===null||v+1e-12>=w.soil_values_by_frame[i-1][j]), 'Soil storage cannot decrease without a sink process.')
      }
      check(finite(soilVolume)&&soilVolume>=0,'Invalid soil volume.')
      check([stored,rain,error].every(finite)&&stored>=0&&rain>=0, 'Invalid water volume budget.')
      const tol=1e-8*Math.max(1,rain)
      check(Math.abs(volume-stored)<=tol&&Math.abs(stored+soilVolume+evaporated+drained-rain-error)<=tol&&Math.abs(error)<=tol, 'Water mass balance mismatch.')
      if(hasSoil)check(Math.abs(soil.reduce((a,v)=>a+(v??0)*area,0)-soilVolume)<=tol,'Soil volume mismatch.')
      check(i===0?stored===0&&soilVolume===0&&rain===0:rain>=w.cumulative_rainfall_volume_m3[i-1], 'Water requires zero initial storage and nondecreasing rain.')
      if(frame.rain){
        check(depths.every((v,j)=>(v===null)===(frame.rain.values[j]===null)), 'Water and rainfall coverage mismatch.')
        const rainVolume=frame.rain.values.reduce((a,v)=>a+(v??0)/1000*area,0)
        check(Math.abs(rainVolume-rain)<=tol,'Applied water input disagrees with accumulated rain.')
      }
      frame.surfaceWater={values:depths,stored_volume_m3:stored,cumulative_rainfall_volume_m3:rain,balance_error_m3:error,soil_values:soil,soil_volume_m3:soilVolume,soil_capacity_m:hasSoil?w.soil_storage_capacity_m:null,evaporated_volume_m3:evaporated,drained_volume_m3:drained}
    })
  }
  if(input.terrain_derivatives !== undefined){
    const d=input.terrain_derivatives, count=first.grid.nx*first.grid.ny
    check(input.terrain && d?.schema_version==='terraflux.terrain.derivatives/v1' && d.method==='central_interior_one_sided_edges', 'Terrain derivatives require terrain and a supported method.')
    check(Array.isArray(d.slope_deg) && d.slope_deg.length===count && d.slope_deg.every(v=>finite(v)&&v>=0&&v<=90), 'Invalid slope array.')
    check(Array.isArray(d.aspect_deg) && d.aspect_deg.length===count && d.aspect_deg.every((v,i)=>d.slope_deg[i]===0?v===null:finite(v)&&v>=0&&v<360), 'Invalid downhill aspect; flat cells require null.')
    frames.forEach(frame=>{frame.terrainDerivatives=d})
  }
  if(input.terrain_temperature !== undefined){
    const d=input.terrain_temperature
    check(input.terrain && d?.schema_version==='terraflux.weather.terrain_temperature/v1' && d.units==='K', 'Terrain temperature diagnostic requires terrain and supported units/schema.')
    check(finite(d.reference_elevation_m) && finite(d.lapse_rate_k_per_m) && d.vertical_reference===input.terrain.vertical_reference, 'Invalid lapse-rate configuration or vertical datum.')
    check(Array.isArray(d.values_by_frame) && d.values_by_frame.length===frames.length, 'Terrain temperature frame count mismatch.')
    frames.forEach((frame,i)=>{
      const values=d.values_by_frame[i], baseline=frame.forecast.fields.air_temperature.values
      check(Array.isArray(values) && values.length===baseline.length && values.every(v=>finite(v)&&v>0), 'Invalid adjusted temperature array.')
      check(values.every((v,j)=>Math.abs(v-(baseline[j]-d.lapse_rate_k_per_m*(input.terrain.values[j]-d.reference_elevation_m)))<=1e-8), 'Adjusted temperatures disagree with lapse-rate metadata.')
      frame.terrainTemperature={...d,values,values_by_frame:undefined}
    })
  }
  return frames
}

export function validateTerrain(terrain, grid) {
  check(terrain?.schema_version==='terraflux.terrain.grid/v1', 'Unsupported terrain schema.')
  check(terrain.grid && Object.keys(grid).length===Object.keys(terrain.grid).length && Object.keys(grid).every(k=>terrain.grid[k]===grid[k]), 'Terrain must match the forecast grid exactly.')
  check(terrain.units==='m', 'Terrain elevation units must be metres.')
  check(typeof terrain.vertical_reference==='string' && terrain.vertical_reference.trim().length>0, 'Terrain vertical reference is required.')
  check(typeof terrain.source_id==='string' && terrain.source_id.trim().length>0, 'Terrain source identity is required.')
  check(Array.isArray(terrain.values) && terrain.values.length===grid.nx*grid.ny && terrain.values.every(finite), 'Terrain requires a finite elevation for every cell.')
  return terrain
}
