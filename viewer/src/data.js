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
