// Analytic illustration of regional fields; no forecast skill or cloud dynamics.
export function regionalWeatherDemo() {
 const nx=48,ny=36,spacing=25000,count=nx*ny,frames=[],clouds=[],pressure=[],humidity=[],systems=[]
 const grid={nx,ny,origin_east_m:-600000,origin_north_m:-450000,spacing_east_m:spacing,spacing_north_m:spacing,geographic_anchor_longitude_deg:10,geographic_anchor_latitude_deg:50,coordinate_system:'local_azimuthal_equidistant_m',array_order:'row_major_y_then_x'}
 for(let frame=0;frame<14;frame++){
  const t=frame/13,lx=.35+.09*t,ly=.57+.025*t,hx=.78-.03*t,hy=.34
  const temp=[],east=[],north=[],cloud=[],p=[],rh=[]
  for(let y=0;y<ny;y++)for(let x=0;x<nx;x++){
   const u=(x+.5)/nx,v=(y+.5)/ny,dx=u-lx,dy=v-ly,r=Math.hypot(dx,dy)
   const low=Math.exp(-(dx*dx+dy*dy)/.045),high=Math.exp(-((u-hx)**2+(v-hy)**2)/.07)
   const frontX=lx+.12+.15*Math.sin((v-.2)*3),band=Math.exp(-(((u-frontX)/.038)**2))
   const spiral=Math.exp(-(((r-.17-.025*Math.sin(Math.atan2(dy,dx)*3+frame*.08))/.055)**2))
   const fraction=Math.min(1,Math.max(0,.12+.65*band+.6*spiral-.18*high))
   cloud.push(fraction);p.push(1015-27*low+15*high);rh.push(Math.min(100,Math.max(15,42+48*fraction+10*low)))
   temp.push(280+9*u-4*v-3*band);east.push(6-23*dy*low+12*(v-hy)*high);north.push(2+23*dx*low-12*(u-hx)*high)
  }
  frames.push({schema_version:'terraflux.weather.forecast/v1',start_time_utc:'2026-11-01T12:00:00Z',elapsed_seconds:frame*600,grid:{...grid},observation_updates:[],fields:{air_temperature:{units:'K',values:temp},wind_east:{units:'m/s',values:east},wind_north:{units:'m/s',values:north}}})
  clouds.push(cloud);pressure.push(p);humidity.push(rh)
  systems.push([{kind:'low',u:lx,v:ly,label:'LOW'},{kind:'high',u:hx,v:hy,label:'HIGH'}, {kind:'cold_front',points:Array.from({length:12},(_,i)=>{const v=.08+i*.075;return [lx+.12+.15*Math.sin((v-.2)*3),v]})}])
 }
 return {schema_version:'terraflux.weather.timeline/v1',frames,atmosphere:{schema_version:'terraflux.weather.atmosphere_display/v1',source_kind:'synthetic',description:'Analytic regional illustration; no cloud microphysics, pressure dynamics or forecast skill.',cloud_fraction_by_frame:clouds,sea_level_pressure_hpa_by_frame:pressure,relative_humidity_pct_by_frame:humidity,systems_by_frame:systems}}
}
export function summarizeFrame(frame, temperatures=frame.forecast.fields.air_temperature.values){
 const f=frame.forecast,g=f.grid,n=g.nx*g.ny
 const stats=a=>a.length?a.reduce((s,v)=>({min:Math.min(s.min,v),max:Math.max(s.max,v),sum:s.sum+v}),{min:Infinity,max:-Infinity,sum:0}):null
 const temp=stats(temperatures),wind=stats(f.fields.wind_east.values.map((u,i)=>Math.hypot(u,f.fields.wind_north.values[i])))
 const result={area_km2:n*g.spacing_east_m*g.spacing_north_m/1e6,temperature_mean_c:temp.sum/n-273.15,temperature_min_c:temp.min-273.15,temperature_max_c:temp.max-273.15,wind_mean_m_s:wind.sum/n,wind_max_m_s:wind.max}
 if(frame.atmosphere){const a=frame.atmosphere,p=stats(a.pressure);result.cloud_pct=stats(a.cloud).sum/n*100;result.pressure_min_hpa=p.min;result.pressure_max_hpa=p.max;result.humidity_mean_pct=stats(a.humidity).sum/n}
 if(frame.rain){const a=frame.rain.values.filter(v=>v!==null);result.rain_coverage_pct=a.length/n*100;result.rain_mean_mm=a.length?stats(a).sum/a.length:null}
 if(frame.surfaceWater){const a=frame.surfaceWater.values.filter(v=>v!==null);result.wet_area_km2=a.filter(v=>v>.0001).length*g.spacing_east_m*g.spacing_north_m/1e6;result.water_max_mm=a.length?stats(a).max*1000:null;const soil=frame.surfaceWater.soil_values?.filter(v=>v!==null);if(soil?.length&&frame.surfaceWater.soil_capacity_m>0)result.soil_fill_pct=stats(soil).sum/soil.length/frame.surfaceWater.soil_capacity_m*100}
 return result
}
