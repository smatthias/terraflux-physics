// Flight weather report: a METAR-style report and a decoded pilot briefing at one station cell of the current engine
// frame. Code form after WMO FM 15 / ICAO Annex 3, flight categories after the FAA (AIM 7-1). Pure functions: no Vue,
// no three.js. Requirements, sources and limits: docs/weather/flight-sailing-reports.md.
//
// Simulated, not observed: the station is a synthetic aerodrome (airports.js; ZZZZ, ICAO's "no location indicator",
// for a bare grid cell), every report carries
// RMK SIMULATED, visibility is an estimate (the engine has no visibility) and gusts are not modelled. Elements the engine
// does not provide are printed as solidi, never invented.
import {
  KT_PER_MS, FT_PER_M, M_PER_SM, HPA, finiteOr, angleDiff, norm360, cellLocation, formatLatLon, validTime, surfaceWind, beaufort,
  dewPoint, relativeHumidity, elevationAt, pressures, cloudLayer, presentWeather, estimateVisibility, weatherWords, cycloneHazard,
} from './station.js'

const pad = (v, n) => String(Math.max(0, Math.trunc(v))).padStart(n, '0')

// ---- METAR groups ----------------------------------------------------------------------------------------------------
export function metarTime(ms) {
  const d = new Date(ms)
  return `${pad(d.getUTCDate(), 2)}${pad(d.getUTCHours(), 2)}${pad(d.getUTCMinutes(), 2)}Z`
}

// dddffKT: direction true to the nearest 10 degrees (north is 360), speed in whole knots (three digits from 100 kt);
// calm (below 1 kt) is 00000KT.
export function metarWind(fromDeg, kt) {
  const speed = Math.round(kt)
  if (speed < 1) return '00000KT'
  const dir = Math.round(norm360(fromDeg) / 10) * 10 || 360
  return `${pad(dir, 3)}${speed < 100 ? pad(speed, 2) : String(speed)}KT`
}

// Prevailing visibility in metres, rounded down to the ICAO reporting step: 50 m below 800 m, 100 m below 5 km,
// 1 km below 10 km; 9999 from 10 km. //// when unknown.
export function metarVisibility(metres) {
  if (metres === null || metres === undefined || !Number.isFinite(metres)) return '////'
  if (metres >= 10000) return '9999'
  const step = metres < 800 ? 50 : metres < 5000 ? 100 : 1000
  return pad(Math.floor(metres / step) * step, 4)
}

// Cloud base in hundreds of feet, rounded down to the reporting step (100 ft to 10 000 ft, 1 000 ft above).
export function metarHeight(metres) {
  const ft = Math.max(0, metres * FT_PER_M)
  return pad(ft < 10000 ? Math.floor(ft / 100) : Math.floor(ft / 1000) * 10, 3)
}

// Present weather groups from the neutral description (station.js) and the visibility estimate.
//   intensity: '-' light, '' moderate, '+' heavy/violent; descriptor TS, SH or FZ; precipitation RA, DZ, SN, RASN
//   (the engine's sleet is rain and snow mixed); hail GR (>= 5 mm) or small hail GS; obscuration FG or BR.
const PRECIP_CODE = { rain: 'RA', drizzle: 'DZ', snow: 'SN', sleet: 'RASN' }
export function metarWeather(wx, visibility) {
  const groups = []
  if (wx?.kind) {
    const prefix = wx.intensity === 'light' ? '-' : wx.intensity === 'heavy' || wx.intensity === 'violent' ? '+' : ''
    const descriptor = wx.thunder ? 'TS' : wx.showers ? 'SH' : wx.freezing ? 'FZ' : ''
    const hail = wx.hail >= 0.5 ? 'GR' : wx.hail > 0 ? 'GS' : ''
    groups.push(`${prefix}${descriptor}${PRECIP_CODE[wx.kind] ?? 'UP'}${hail}`)
  } else if (wx?.thunder) groups.push('TS')
  if (visibility?.obscuration === 'fog') groups.push('FG')
  else if (visibility?.obscuration === 'mist') groups.push('BR')
  return groups
}

// Cloud of operational significance (ICAO): layers below 5 000 ft, and any CB or TCU. NSC when there is none; ////// when
// the run has no cloud model (the sky is unknown, so neither NSC nor CAVOK may be claimed).
export function metarCloud(cloud) {
  if (!cloud.modelled) return { group: '//////', significant: false }
  const l = cloud.layer
  if (!l) return { group: 'NSC', significant: false }
  const significant = l.base * FT_PER_M < 5000 || !!l.convective
  return { group: significant ? `${l.cover}${metarHeight(l.base)}${l.convective ?? ''}` : 'NSC', significant }
}

// Temperature group element: whole degrees (halves rounded up), M for negative values (M00 for -0.5 < t < 0).
export function metarTemperature(c) {
  if (c === null || c === undefined || !Number.isFinite(c)) return '//'
  const r = Math.round(c)
  return r < 0 || (r === 0 && c < 0) ? `M${pad(Math.abs(r), 2)}` : pad(r, 2)
}

// QNH in whole hectopascals, rounded down (ICAO).
export const metarQnh = hPa => (hPa === null || hPa === undefined || !Number.isFinite(hPa) ? 'Q////' : `Q${pad(Math.floor(hPa), 4)}`)

// ---- Flight category (FAA) ---------------------------------------------------------------------------------------
// VFR: ceiling > 3 000 ft and visibility > 5 SM; MVFR: 1 000-3 000 ft and/or 3-5 SM; IFR: 500 to below 1 000 ft and/or
// 1 to below 3 SM; LIFR: below 500 ft and/or below 1 SM. The worse element decides. ceilingFt: Infinity for no ceiling,
// null when unknown; visibilitySm null when unknown.
export const CATEGORIES = {
  VFR: { name: 'VFR', colour: '#3ddc84', label: 'visual flight rules' },
  MVFR: { name: 'MVFR', colour: '#4a9dff', label: 'marginal VFR' },
  IFR: { name: 'IFR', colour: '#ff5252', label: 'instrument flight rules' },
  LIFR: { name: 'LIFR', colour: '#e040fb', label: 'low IFR' },
  UNKNOWN: { name: '—', colour: '#7e98a5', label: 'unknown: neither ceiling nor visibility available' },
}
const RANK = ['LIFR', 'IFR', 'MVFR', 'VFR']
const ceilingCategory = ft => (ft < 500 ? 'LIFR' : ft < 1000 ? 'IFR' : ft <= 3000 ? 'MVFR' : 'VFR')
const visibilityCategory = sm => (sm < 1 ? 'LIFR' : sm < 3 ? 'IFR' : sm <= 5 ? 'MVFR' : 'VFR')
export function flightCategory(ceilingFt, visibilitySm) {
  const byCeiling = ceilingFt === null || ceilingFt === undefined ? null : ceilingCategory(ceilingFt)
  const byVisibility = visibilitySm === null || visibilitySm === undefined ? null : visibilityCategory(visibilitySm)
  const known = [byCeiling, byVisibility].filter(Boolean)
  if (!known.length) return { ...CATEGORIES.UNKNOWN, byCeiling, byVisibility, partial: true, basis: 'no ceiling or visibility data' }
  const worst = known.reduce((a, b) => (RANK.indexOf(b) < RANK.indexOf(a) ? b : a))
  const partial = known.length < 2
  return { ...CATEGORIES[worst], byCeiling, byVisibility, partial, basis: partial ? (byCeiling ? 'ceiling only (visibility unknown)' : 'visibility only (cloud not modelled)') : 'ceiling and visibility' }
}

// ---- Runway wind ---------------------------------------------------------------------------------------------------
// Components of the wind on a runway of heading runwayDeg (true): headwind positive (negative: tailwind), crosswind
// positive from the right.
export function runwayComponents(fromDeg, kt, runwayDeg) {
  const theta = angleDiff(fromDeg, runwayDeg) * Math.PI / 180
  return { head: kt * Math.cos(theta), cross: kt * Math.sin(theta) }
}
// Runway designator of a heading: heading / 10 rounded, 01-36.
export const runwayDesignator = deg => pad(Math.round(norm360(deg) / 10) || 36, 2)

// ---- Performance ----------------------------------------------------------------------------------------------------
// Pressure altitude (ft) from a station pressure (Pa); density altitude (ft): the ISA height with the same air density.
export const pressureAltitudeFt = pa => 145366.45 * (1 - (pa / 101325) ** 0.190284)
export const densityAltitudeFt = rho => 44330.8 * (1 - (rho / 1.225) ** 0.234969) * FT_PER_M
export const isaTemperatureC = elevationFt => 15 - 1.98 * elevationFt / 1000
// Moist-air density with the engine's relation rho = p / (R_d T (1 + 0.608 q)).
export const moistDensity = (pa, tK, q = 0) => pa / (287.05 * tK * (1 + 0.608 * q))

// ---- Icing ------------------------------------------------------------------------------------------------------------
// Inside the cloud between the 0 degC and -20 degC levels (supercooled water). Level temperatures: the precipitation
// kernel's cloud base/top temperatures and freezing level where present, otherwise the surface temperature cooled at
// the ISA lapse rate (6.5 K/km; marked estimated).
export function icingBand(layer, surfaceC, wx) {
  if (!layer || layer.top === null) return null
  const estimated = !(Number.isFinite(wx?.baseTemperature) && Number.isFinite(wx?.topTemperature))
  const tBase = estimated ? surfaceC - 0.0065 * layer.base : wx.baseTemperature - 273.15
  const tTop = estimated ? surfaceC - 0.0065 * layer.top : wx.topTemperature - 273.15
  const levelOf = t => (tBase === tTop ? (t <= tBase ? layer.base : layer.top) : layer.base + (tBase - t) / (tBase - tTop) * (layer.top - layer.base))
  if (tTop > 0 || tBase < -20) return null
  let lo = tBase <= 0 ? layer.base : Number.isFinite(wx?.freezingLevel) && !estimated ? wx.freezingLevel : levelOf(0)
  const hi = tTop >= -20 ? layer.top : levelOf(-20)
  lo = Math.max(layer.base, Math.min(lo, layer.top))
  if (!(hi > lo)) return null
  const severity = layer.convective ? 'moderate to severe (convective cloud)' : ['nimbostratus', 'stratus', 'stratocumulus', 'altostratus', 'altocumulus'].includes(layer.genus) ? 'light to moderate (layer cloud)' : 'light'
  return { fromFt: lo * FT_PER_M, toFt: hi * FT_PER_M, severity, estimated }
}

// ---- Ceiling, visibility and category at a cell (the report's core; also the category map) ----------------------
// Ceiling: the base of the lowest broken or overcast layer; none (Infinity) under a clear or scattered sky; unknown
// (null) without a cloud model.
export function flightConditions(frame, i) {
  const rh = relativeHumidity(frame, i), cloud = cloudLayer(frame, i), wx = presentWeather(frame, i)
  const vis = estimateVisibility({ rh, weather: wx, cloudBase: cloud.layer?.base ?? null })
  const visSm = vis ? vis.metres / M_PER_SM : null, layer = cloud.layer
  const ceilingFt = !cloud.modelled ? null : layer && (layer.cover === 'BKN' || layer.cover === 'OVC') ? layer.base * FT_PER_M : Infinity
  return { rh, cloud, wx, vis, visSm, ceilingFt, category: flightCategory(ceilingFt, visSm) }
}

// Share of the grid in each flight category (the map summary): { VFR, MVFR, IFR, LIFR, '—' } in percent.
export function categoryShares(frame) {
  const n = frame.forecast.grid.nx * frame.forecast.grid.ny, counts = { VFR: 0, MVFR: 0, IFR: 0, LIFR: 0, '—': 0 }
  for (let i = 0; i < n; i++) counts[flightConditions(frame, i).category.name]++
  return Object.fromEntries(Object.entries(counts).map(([k, v]) => [k, v / n * 100]))
}

// ---- The report ---------------------------------------------------------------------------------------------------
// station: the aerodrome ({ id, name }) whose identifier heads the report; without one, ZZZZ (no location indicator).
export const DEFAULT_FLIGHT_SETTINGS = { runwayDeg: 360, crosswindLimitKt: 15, station: null }

export function flightReport(frame, i, settings = {}) {
  const opts = { ...DEFAULT_FLIGHT_SETTINGS, ...settings }
  const g = frame.forecast.grid, loc = cellLocation(g, i), ms = validTime(frame), elevation = elevationAt(frame, i)
  const wind = surfaceWind(frame, i), bf = beaufort(wind.speed)
  const tK = frame.forecast.fields.air_temperature.values[i], tC = tK - 273.15
  const { rh, cloud, wx, vis, visSm, ceilingFt, category } = flightConditions(frame, i)
  const tdK = dewPoint(tK, rh), tdC = tdK === null ? null : tdK - 273.15
  const p = pressures(frame, i), qnhHpa = p.qnh === null ? null : p.qnh / HPA
  const weatherGroups = metarWeather(wx, vis)
  const cloudGroup = metarCloud(cloud)
  const layer = cloud.layer
  const cavok = cloud.modelled && !cloudGroup.significant && vis && vis.metres >= 10000 && !weatherGroups.length
  const remarks = ['SIMULATED']
  if (vis) remarks.push('VIS EST')
  if (wx.virga) remarks.push('VIRGA')
  if (wind.cyclone) remarks.push('TC WIND')
  const ident = opts.station?.id ?? 'ZZZZ'
  const metar = ['METAR', ident, metarTime(ms), metarWind(wind.fromDeg, wind.kt),
    ...(cavok ? ['CAVOK'] : [metarVisibility(vis?.metres), ...weatherGroups, cloudGroup.group]),
    `${metarTemperature(tC)}/${metarTemperature(tdC)}`, metarQnh(qnhHpa), 'RMK', ...remarks].join(' ')

  // Runway wind.
  const runway = { headingDeg: norm360(opts.runwayDeg) || 360, designator: runwayDesignator(opts.runwayDeg), ...runwayComponents(wind.fromDeg, wind.kt, opts.runwayDeg), best: runwayDesignator(wind.fromDeg) }
  // Performance: pressure and density altitude at the station.
  const elevationFt = elevation !== null ? Math.max(0, elevation) * FT_PER_M : 0
  let density = null
  if (p.station !== null) {
    const q = frame.coupled?.specificHumidity?.[i]
    density = p.stationSource === 'sea-level field' && Number.isFinite(frame.coupled?.density?.[i]) ? frame.coupled.density[i] : moistDensity(p.station, tK, Number.isFinite(q) ? q : 0)
  }
  const performance = p.station === null ? null : {
    elevationFt, pressureAltitudeFt: pressureAltitudeFt(p.station), densityAltitudeFt: densityAltitudeFt(density), density, isaDeviationC: tC - isaTemperatureC(elevationFt), stationHpa: p.station / HPA, stationSource: p.stationSource,
  }
  const icing = icingBand(layer, tC, wx)

  // Hazards, most serious first.
  const hazards = []
  const add = (level, text) => hazards.push({ level, text })
  const tc = cycloneHazard(wind)
  if (tc) add(tc.level, tc.level === 'severe' ? `${tc.text}; do not fly in or near the storm` : tc.text)
  if (wx.thunder || layer?.convective === 'CB') add('severe', `Thunderstorm${layer?.convective === 'CB' ? ' (CB)' : ''}: severe turbulence, lightning, hail and wind shear; keep well clear (the AIM advises at least 20 NM from severe storms)`)
  if (wx.hail > 0) add('severe', `Hail up to ${wx.hail.toFixed(1)} cm at the surface`)
  if (wx.freezing) add('severe', `Freezing ${wx.kind}: severe airframe icing on the ground and in the descent`)
  if (icing) add(layer?.convective ? 'severe' : 'warning', `Icing ${icing.severity} in cloud from ${Math.round(icing.fromFt / 100) * 100} to ${Math.round(icing.toFt / 100) * 100} ft AGL${icing.estimated ? ' (levels estimated from the ISA lapse rate)' : ''}`)
  if (frame.severe?.possible?.[i]) add('severe', 'Tornado possible (severe-convection kernel)')
  const shear = finiteOr(frame.severe?.shear1?.[i], null)
  if (shear !== null && shear >= 10) add('warning', `Strong low-level shear: ${Math.round(shear * KT_PER_MS)} kt over the lowest kilometre (possible LLWS on approach)`)
  if (layer?.convective === 'TCU') add('warning', 'Towering cumulus: moderate turbulence and showers nearby')
  if (Number.isFinite(wx.updraft) && wx.updraft >= 10 && !wx.thunder) add('warning', `Convective updrafts up to ${Math.round(wx.updraft)} m/s`)
  const cross = finiteOr(frame.orography?.cross?.[i], null)
  if (cross !== null && Math.abs(cross) * KT_PER_MS >= 25 && finiteOr(frame.orography?.obstacle?.[i], 0) > 0) add('warning', `Mountain waves possible: ${Math.round(Math.abs(cross) * KT_PER_MS)} kt across the ridge; expect lee turbulence and rotors`)
  if (frame.orography?.foehn?.[i]) add('caution', 'Foehn: gusty downslope wind and turbulence in the lee')
  if (Math.abs(runway.cross) > opts.crosswindLimitKt) add('warning', `Crosswind ${Math.round(Math.abs(runway.cross))} kt on runway ${runway.designator} exceeds the ${opts.crosswindLimitKt} kt limit set`)
  if (runway.head < -5) add('caution', `Tailwind ${Math.round(-runway.head)} kt on runway ${runway.designator}: runway ${runway.best} is into wind`)
  if (wind.kt >= 25 && !wind.cyclone) add('warning', `Strong surface wind ${Math.round(wind.kt)} kt (${bf.label})`)
  if (wx.virga) add('caution', 'Virga: rain evaporating below a high cloud base; possible dry microbursts')
  if (performance && performance.densityAltitudeFt - elevationFt >= 2000) add('caution', `High density altitude ${Math.round(performance.densityAltitudeFt / 100) * 100} ft: longer take-off run and reduced climb`)

  const notes = ['Simulated from engine output (synthetic inputs): not for flight planning.', 'Gusts are not modelled; no G group is reported.']
  if (vis) notes.push(`Visibility is estimated from ${vis.basis.join(', ')} (the engine has no visibility).`)
  else notes.push('No visibility: the run has neither humidity nor precipitation at this point.')
  if (rh !== null && rh > 100) notes.push(`The engine state is supersaturated here (RH ${Math.round(rh)} %: the coupled model does not condense); treated as saturated.`)
  if (!cloud.modelled) notes.push('No cloud model in this run: the sky is unknown (//////).')
  if (!wx.modelled) notes.push('No precipitation model in this run: present weather is not reported.')
  if (qnhHpa === null) notes.push('No pressure field in this run: QNH and altitudes are not available.')
  notes.push('Wind direction and runway heading are true, not magnetic.')

  return {
    station: { i, id: ident, name: opts.station?.name ?? null, x: loc.x, y: loc.y, lat: loc.lat, lon: loc.lon, where: formatLatLon(loc), elevationM: elevation },
    time: { ms, iso: new Date(ms).toISOString().replace('.000Z', 'Z'), group: metarTime(ms) },
    wind: { ...wind, beaufort: bf },
    visibility: vis ? { ...vis, sm: visSm, group: metarVisibility(vis.metres) } : null,
    weather: { ...wx, groups: weatherGroups, words: weatherWords(wx, vis) },
    cloud: { ...cloud, group: cloudGroup.group, significant: cloudGroup.significant, ceilingFt },
    temperature: { c: tC, dewC: tdC, spread: tdC === null ? null : tC - tdC, rh },
    pressure: { qnhHpa, inHg: qnhHpa === null ? null : qnhHpa * 0.0295300 },
    performance, runway, category, icing, hazards, cavok, metar, notes, settings: opts,
  }
}

// The flight category at the station for every frame of the run (the trend strip).
export const flightCategorySeries = (frames, i, settings) => frames.map(f => flightReport(f, i, settings).category.name)
