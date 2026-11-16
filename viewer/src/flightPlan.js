// Flight planning between two aerodromes of the run (airports.js) through the simulated weather: the route at a cruise
// level and true airspeed, flown through the timeline - every leg meets the winds aloft, clouds, icing and storms of
// the frame valid when the aircraft gets there, and the destination is judged at the ETA. Pure functions.
//
// Method (docs/weather/flight-sailing-reports.md, "Flight planning"):
//   - route: the straight line between the aerodromes on the grid's azimuthal-equidistant map (close to the great
//     circle over regional grids); courses and winds both in the grid's frame (true at its anchor);
//   - vertical profile: climb and descent at 300 ft per nautical mile (the "3 to 1" rule, 3 NM per 1 000 ft), cruise
//     level capped where the route is too short to reach it;
//   - each segment (about half a grid cell): the wind at its altitude (windsAloft.js; the surface wind where the run
//     has no winds aloft) from the frame nearest in time, the wind triangle (wind correction angle, heading, ground
//     speed), and the hazards at that point and altitude;
//   - fuel: trip fuel at the burn rate set, plus 45 minutes at that rate (the IFR reserve of 14 CFR 91.167).
// A planning aid on simulated weather: no airspace, no performance model beyond constant TAS, not for navigation.
import { cellLocation, surfaceWind, elevationAt, validTime, angleDiff, norm360, FT_PER_M, KT_PER_MS, M_PER_NM } from './station.js'
import { cellOf, project, distanceNm, initialCourse, finalCourse, intermediatePoint } from './geo.js'
import { flightReport, flightConditions, icingBand } from './aviation.js'
import { cloudLayer, presentWeather } from './station.js'
import { FB_LEVELS, CAT_SHEAR_KT_PER_1000FT, LAPSE_K_PER_M, TROPOPAUSE_M } from './windsAloft.js'
import { weatherSymbol } from './weatherLayers.js'
import { vfrAt, GAFOR } from './gafor.js'

export const CLIMB_FT_PER_NM = 300
export const RESERVE_HOURS = 0.75
export const DEFAULT_PLAN = { fromId: null, toId: null, cruiseFt: 9000, tasKt: 140, fuelPerHour: 35, crosswindLimitKt: 15 }
export const HAZARD_INFO = {
  thunderstorm: { label: 'Thunderstorm / CB', level: 'severe', colour: '#ff5252' },
  cyclone: { label: 'Tropical cyclone', level: 'severe', colour: '#e040fb' },
  terrain: { label: 'Below terrain clearance', level: 'severe', colour: '#ff8fd0' },
  icing: { label: 'Icing in cloud', level: 'warning', colour: '#6fb8ff' },
  turbulence: { label: 'Clear-air turbulence (shear)', level: 'caution', colour: '#ffb15c' },
  cloud: { label: 'In cloud (IMC)', level: 'info', colour: '#9fb3bf' },
  outside: { label: 'Outside the simulated area', level: 'warning', colour: '#c9a6ff' },
}

// The wind triangle: course tc and TAS against a wind from windFrom at windKt. headKt > 0 is a headwind, crossKt > 0
// from the right; wcaDeg > 0 turns the heading right (into a wind from the right). possible: false when the crosswind
// exceeds the airspeed or the ground speed is not positive.
export function windTriangle(tcDeg, tasKt, windFromDeg, windKt) {
  const rel = angleDiff(windFromDeg, tcDeg) * Math.PI / 180
  const headKt = windKt * Math.cos(rel), crossKt = windKt * Math.sin(rel), s = crossKt / tasKt
  if (!(tasKt > 0) || Math.abs(s) > 1) return { possible: false, headKt, crossKt, wcaDeg: NaN, headingDeg: NaN, gsKt: 0 }
  const wca = Math.asin(s), gsKt = tasKt * Math.cos(wca) - headKt
  return { possible: gsKt > 0, headKt, crossKt, wcaDeg: wca * 180 / Math.PI, headingDeg: norm360(tcDeg + wca * 180 / Math.PI), gsKt }
}

// The frame valid nearest to a time (ms), and whether the time is past the end of the run.
export function frameAt(frames, ms) {
  let best = 0, bestD = Infinity
  for (let k = 0; k < frames.length; k++) { const d = Math.abs(validTime(frames[k]) - ms); if (d < bestD) { bestD = d; best = k } }
  return { index: best, frame: frames[best], beyond: ms > validTime(frames.at(-1)) + 1 }
}

// The altitude (ft MSL) of the profile at distance dNm of a route of lengthNm between fields at depFt and arrFt.
export const profileAltitude = (dNm, lengthNm, cruiseFt, depFt, arrFt) => Math.max(Math.min(depFt, arrFt), Math.min(cruiseFt, depFt + CLIMB_FT_PER_NM * dNm, arrFt + CLIMB_FT_PER_NM * (lengthNm - dNm)))

// The runway end in use at an aerodrome: the end with the most headwind (as a tower would choose), heading in degrees.
export function activeRunway(frame, airport) {
  const w = surfaceWind(frame, airport.i), head = d => w.kt * Math.cos(angleDiff(w.fromDeg, d) * Math.PI / 180)
  return head(airport.runwayDeg) >= head(airport.runwayDeg + 180) ? airport.runwayDeg : norm360(airport.runwayDeg + 180) || 360
}

// Clearance over the highest terrain in the corridor: 1 000 ft, 2 000 ft over terrain from 3 000 ft (a simplified
// mountainous-area rule, not a published minimum altitude).
export const terrainClearanceFt = terrainFt => (terrainFt >= 3000 ? 2000 : 1000)

// Everything the plan needs at one point of the route: the cell and the weather at an altitude at a time.
function weatherAt(ctx, i, altFt, ms, { hazards = true } = {}) {
  const { frames, aloftFor } = ctx, at = frameAt(frames, ms), frame = at.frame
  const field = aloftFor(frame), elevFt = Math.max(0, elevationAt(frame, i) ?? 0) * FT_PER_M
  let wind, tempC
  if (field?.available) { const w = field.at(i, altFt); wind = w; tempC = w.temperatureK - 273.15 }
  else { wind = surfaceWind(frame, i); const z = Math.max(0, Math.min(altFt / FT_PER_M, TROPOPAUSE_M) - elevFt / FT_PER_M); tempC = frame.forecast.fields.air_temperature.values[i] - 273.15 - LAPSE_K_PER_M * z }
  const gafor = (() => { try { return vfrAt(frame, i).category.key } catch { return '?' } })()
  const out = { gafor, frameIndex: at.index, beyond: at.beyond, wind: { fromDeg: wind.fromDeg, kt: wind.kt }, tempC, isaDevC: tempC - (15 - 1.98 * Math.min(altFt, 36089) / 1000), elevFt, aloft: !!field?.available, hazards: [] }
  if (!hazards) return out
  const cloud = cloudLayer(frame, i).layer, wx = presentWeather(frame, i), surfaceC = frame.forecast.fields.air_temperature.values[i] - 273.15
  if (cloud) {
    const base = elevFt + cloud.base * FT_PER_M, top = cloud.top === null ? base : elevFt + cloud.top * FT_PER_M
    out.cloud = { baseFt: base, topFt: top, genus: cloud.genus, abbreviation: cloud.abbreviation, cover: cloud.cover, convective: cloud.convective }
    if (altFt >= base && altFt <= top) out.hazards.push('cloud')
    const band = icingBand(cloud, surfaceC, wx)
    if (band) { out.icing = { fromFt: elevFt + band.fromFt, toFt: elevFt + band.toFt, severity: band.severity }; if (altFt >= out.icing.fromFt && altFt <= out.icing.toFt) out.hazards.push('icing') }
  }
  if (cloud?.convective === 'CB' || wx.thunder) out.hazards.push('thunderstorm')
  const sw = surfaceWind(frame, i); if (sw.cyclone && sw.kt >= 34) out.hazards.push('cyclone')
  // Vertical wind shear at the aircraft's altitude: the centred difference over 2 000 ft, in kt per 1 000 ft.
  if (field?.available && altFt - 1000 > elevFt + 500) {
    const up = field.at(i, altFt + 1000), down = field.at(i, altFt - 1000)
    out.shearKtPer1000ft = Math.hypot(up.u - down.u, up.v - down.v) * KT_PER_MS / 2
    if (out.shearKtPer1000ft >= CAT_SHEAR_KT_PER_1000FT) out.hazards.push('turbulence')
  }
  if (Number.isFinite(wx.freezingLevel)) out.freezingFt = elevFt + wx.freezingLevel * FT_PER_M
  else { const z0 = (surfaceC) / (LAPSE_K_PER_M * 1000) * 1000; out.freezingFt = surfaceC > 0 ? elevFt + z0 * FT_PER_M : elevFt }
  return out
}

// The worst GAFOR category of a list of keys (X worst; unknown '?' only when nothing else is known).
export const worstGafor = keys => keys.reduce((w, k) => ((GAFOR[k]?.rank ?? -1) > (GAFOR[w]?.rank ?? -1) ? k : w), '?')

// Contiguous stretches of each hazard along the segments: [{ kind, fromNm, toNm }].
function hazardRanges(segments) {
  const out = [], open = new Map()
  segments.forEach((s, k) => {
    for (const kind of Object.keys(HAZARD_INFO)) {
      const on = s.hazards.includes(kind)
      if (on && !open.has(kind)) open.set(kind, { kind, fromNm: s.fromNm, toNm: s.toNm })
      else if (on) open.get(kind).toNm = s.toNm
      if ((!on || k === segments.length - 1) && open.has(kind)) { out.push(open.get(kind)); open.delete(kind) }
    }
  })
  return out.sort((a, b) => a.fromNm - b.fromNm)
}

// Fly the route at one cruise level: the segments with their wind triangle and times. The wind triangle works in the
// grid's frame (the engine's winds are grid east/north), so each segment's course there is used; the relative angle,
// hence the wind correction, is the same in the geographic frame, where courses and headings are reported (true).
function fly(ctx, cruiseFt, { hazards = true } = {}) {
  const { route, tasKt, t0 } = ctx, segments = []
  let t = 0, possible = true
  for (const seg of route.segments) {
    const { fromNm, toNm, i } = seg, mid = (fromNm + toNm) / 2
    const altFt = profileAltitude(mid, route.lengthNm, cruiseFt, route.depFt, route.arrFt)
    const w = weatherAt(ctx, i, altFt, t0 + t * 1000, { hazards })
    const tri = windTriangle(seg.gridCourseDeg, tasKt, w.wind.fromDeg, w.wind.kt)
    if (!tri.possible) possible = false
    const hours = tri.possible ? (toNm - fromNm) / tri.gsKt : Infinity
    const cruise = Math.abs(altFt - cruiseFt) < 1 || altFt >= cruiseFt
    const terrainFt = route.corridorFt(i)
    if (hazards && cruise && altFt < terrainFt + terrainClearanceFt(terrainFt)) w.hazards.push('terrain')
    if (hazards && seg.outside) w.hazards.push('outside')
    segments.push({ k: segments.length, fromNm, toNm, i, leg: seg.leg, lat: seg.lat, lon: seg.lon, altFt, cruise, terrainFt, startMs: t0 + t * 1000, ...w, ...tri,
      gridHeadingDeg: tri.headingDeg, trueCourseDeg: seg.trueCourseDeg, gridCourseDeg: seg.gridCourseDeg, headingDeg: norm360(seg.trueCourseDeg + (tri.wcaDeg || 0)), hours })
    t += hours * 3600
  }
  const hours = t / 3600
  return { segments, hours, possible, etaMs: t0 + t * 1000, reachedFt: Math.max(...segments.map(s => s.altFt)) }
}

// A route point: an aerodrome (airports.js) or a coordinate. { name, lat, lon, airport? }.
// The route through the grid: great-circle legs sampled about every half cell (at most ~160 segments in all), each
// sample projected onto the grid (geo.js, the engine's projection) to find its cell and its grid-frame course.
function buildRoute(grid, frame, points) {
  const step = Math.max(0.5, Math.min(grid.spacing_east_m, grid.spacing_north_m) / M_PER_NM / 2)
  const legsNm = points.slice(1).map((p, k) => distanceNm(points[k], p)), total = legsNm.reduce((a, b) => a + b, 0)
  const scale = Math.max(1, total / step / 160), segs = [], path = []
  let acc = 0
  const sample = (a, b, f) => intermediatePoint(a, b, f)
  const located = q => { const c = cellOf(grid, q); if (c) return { ...c, outside: false }; const { east, north } = project(grid, q), cx = Math.min(grid.nx - 1, Math.max(0, Math.floor((east - grid.origin_east_m) / grid.spacing_east_m))), cy = Math.min(grid.ny - 1, Math.max(0, Math.floor((north - grid.origin_north_m) / grid.spacing_north_m))); return { i: cy * grid.nx + cx, x: cx, y: cy, east, north, outside: true } }
  points.forEach((p, k) => {
    if (k === points.length - 1) return
    const q = points[k + 1], L = legsNm[k], n = Math.max(2, Math.ceil(L / (step * scale)))
    for (let j = 0; j < n; j++) {
      const a = sample(p, q, j / n), b = sample(p, q, (j + 1) / n), m = sample(p, q, (j + 0.5) / n)
      const pa = project(grid, a), pb = project(grid, b), cell = located(m)
      segs.push({ leg: k, fromNm: acc + L * j / n, toNm: acc + L * (j + 1) / n, i: cell.i, lat: m.lat, lon: m.lon, outside: cell.outside,
        trueCourseDeg: initialCourse(m, b), gridCourseDeg: norm360(Math.atan2(pb.east - pa.east, pb.north - pa.north) * 180 / Math.PI) })
      path.push({ dNm: acc + L * j / n, lat: a.lat, lon: a.lon, east: pa.east, north: pa.north })
    }
    acc += L
  })
  const last = points.at(-1), pl = project(grid, last)
  path.push({ dNm: total, lat: last.lat, lon: last.lon, east: pl.east, north: pl.north })
  return { segs, path, legsNm, lengthNm: total }
}

// The point of the route at a distance (NM): position, grid position and cell (interpolated along the path).
export function routePoint(plan, dNm) {
  const path = plan.path, d = Math.max(0, Math.min(plan.lengthNm, dNm))
  let k = 0; while (k < path.length - 2 && path[k + 1].dNm < d) k++
  const a = path[k], b = path[k + 1], f = (d - a.dNm) / ((b.dNm - a.dNm) || 1)
  return { dNm: d, lat: a.lat + f * (b.lat - a.lat), lon: a.lon + f * (b.lon - a.lon), east: a.east + f * (b.east - a.east), north: a.north + f * (b.north - a.north) }
}

// Turn aerodromes and coordinates into route points, with their cells and elevations. Throws when a point lies
// outside the simulated area (there is no weather there).
export function routePoints(frame, items) {
  const g = frame.forecast.grid
  return items.map((it, k) => {
    const airport = it.airport ?? null
    const pos = Number.isFinite(it.lat) && Number.isFinite(it.lon) ? { lat: it.lat, lon: it.lon } : airport ? cellLocation(g, airport.i) : null
    if (!pos) throw new Error(`Point ${k + 1} has no position.`)
    const cell = cellOf(g, pos)
    if (!cell) throw new Error(`${it.name || `Point ${k + 1}`} (${pos.lat.toFixed(3)}, ${pos.lon.toFixed(3)}) lies outside the simulated area: there is no weather there.`)
    const elevationFt = airport ? airport.elevationFt : Math.max(0, elevationAt(frame, cell.i) ?? 0) * FT_PER_M
    return { name: it.name || airport?.id || `WP${k}`, id: airport?.id ?? (it.name || `WP${k}`), lat: pos.lat, lon: pos.lon, airport, i: cell.i, x: cell.x, y: cell.y, elevationFt, isAirport: !!airport, runwayDeg: airport?.runwayDeg ?? null }
  })
}

// The plan. Either waypoints ([{ name, lat, lon } | { airport }], departure first, destination last) or from/to
// aerodromes. frames: the run; startIndex: departure frame; aloftFor(frame): its winds-aloft field or null; airports:
// all aerodromes (for alternates).
// detail: false skips the best-level comparison, the cross-section and the alternate (for scanning departure times).
export function planFlight({ frames, startIndex, from = null, to = null, waypoints = null, airports = [], cruiseFt = 9000, tasKt = 140, fuelPerHour = 35, crosswindLimitKt = 15, aloftFor = () => null, detail = true }) {
  const items = waypoints ?? (from && to ? [{ airport: from, name: from.id }, { airport: to, name: to.id }] : null)
  if (!items || items.length < 2) return { ok: false, reason: 'Choose a departure and a destination.' }
  if (!(tasKt > 0)) return { ok: false, reason: 'Set a true airspeed above zero.' }
  const f0 = frames[startIndex], g = f0.forecast.grid
  let points
  try { points = routePoints(f0, items) } catch (err) { return { ok: false, reason: err.message } }
  for (let k = 1; k < points.length; k++) if (distanceNm(points[k - 1], points[k]) < 0.1) return { ok: false, reason: `${points[k - 1].name} and ${points[k].name} are the same place.` }
  const depart = points[0], dest = points.at(-1)
  const built = buildRoute(g, f0, points), lengthNm = built.lengthNm
  // The highest terrain in the corridor: the cell and its eight neighbours.
  const corridorFt = i => { let m = 0; const x = i % g.nx, y = Math.floor(i / g.nx); for (let yy = Math.max(0, y - 1); yy <= Math.min(g.ny - 1, y + 1); yy++) for (let xx = Math.max(0, x - 1); xx <= Math.min(g.nx - 1, x + 1); xx++) m = Math.max(m, elevationAt(f0, yy * g.nx + xx) ?? 0); return Math.max(0, m) * FT_PER_M }
  const route = { lengthNm, segments: built.segs, corridorFt, depFt: depart.elevationFt, arrFt: dest.elevationFt }
  const t0 = validTime(f0), ctx = { frames, aloftFor, route, tasKt, t0 }
  const flight = fly(ctx, cruiseFt)
  if (!flight.possible) return { ok: false, reason: `At ${tasKt} kt the aircraft cannot make good the course against the wind on this route (crosswind above the airspeed or no ground speed).` }
  const arrival = frameAt(frames, flight.etaMs)
  // Weather at the ends: aerodromes on their runway in use; coordinates without a runway (the report's runway into wind).
  const reportAt = (frame, pt) => { const ap = pt.airport, rwy = ap ? activeRunway(frame, ap) : Math.round(surfaceWind(frame, pt.i).fromDeg / 10) * 10 || 360; return flightReport(frame, pt.i, { runwayDeg: rwy, crosswindLimitKt, station: { id: pt.id, name: ap?.name ?? 'coordinate' } }) }
  const departure = reportAt(f0, depart), destination = reportAt(arrival.frame, dest)
  const symbols = { departure: weatherSymbol(f0, depart.i), destination: weatherSymbol(arrival.frame, dest.i) }

  // Navigation log rows between checkpoints: the route points, TOC and TOD; long rows are split until there are about
  // eight (at most one split below 5 NM).
  const toc = (cruiseFt - depart.elevationFt) / CLIMB_FT_PER_NM, tod = lengthNm - (cruiseFt - dest.elevationFt) / CLIMB_FT_PER_NM
  const cum = [0]; built.legsNm.forEach(L => cum.push(cum.at(-1) + L))
  let checks = points.map((p, k) => ({ dNm: cum[k], name: p.id }))
  if (toc > 0.5 && toc < tod - 0.5) checks.push({ dNm: toc, name: 'TOC' }, { dNm: tod, name: 'TOD' })
  checks.sort((a, b) => a.dNm - b.dNm)
  while (checks.length < 9) {
    let best = -1, len = 0
    for (let k = 0; k < checks.length - 1; k++) { const l = checks[k + 1].dNm - checks[k].dNm; if (l > len) { len = l; best = k } }
    if (best < 0 || len < 10) break
    checks.splice(best + 1, 0, { dNm: (checks[best].dNm + checks[best + 1].dNm) / 2, name: '' })
  }
  const legs = []
  for (let c = 0; c < checks.length - 1; c++) {
    const a = checks[c].dNm, b = checks[c + 1].dNm
    const seg = flight.segments.filter(x => x.toNm > a + 1e-9 && x.fromNm < b - 1e-9)
    if (!seg.length || b - a < 1e-6) continue
    // Time of the row: each segment's time in proportion to its overlap with the row.
    const hours = seg.reduce((s, x) => s + x.hours * (Math.min(b, x.toNm) - Math.max(a, x.fromNm)) / (x.toNm - x.fromNm), 0)
    const midSeg = seg[Math.floor(seg.length / 2)], startPt = routePoint({ path: built.path, lengthNm }, a), endPt = routePoint({ path: built.path, lengthNm }, b)
    const altAt = d => profileAltitude(d, lengthNm, cruiseFt, depart.elevationFt, dest.elevationFt)
    legs.push({ fromName: checks[c].name, toName: checks[c + 1].name, fromNm: a, toNm: b, distNm: b - a, altFromFt: altAt(a), altToFt: altAt(b), altFt: altAt((a + b) / 2),
      tcDeg: initialCourse(startPt, endPt), wind: midSeg.wind, headKt: midSeg.headKt, crossKt: midSeg.crossKt, wcaDeg: midSeg.wcaDeg, headingDeg: norm360(initialCourse(startPt, endPt) + midSeg.wcaDeg),
      gsKt: (b - a) / hours, hours, etaMs: t0 + (hoursAtDistance({ segments: flight.segments }, b)) * 3.6e6, tempC: midSeg.tempC, isaDevC: midSeg.isaDevC, frameIndex: midSeg.frameIndex, hazards: [...new Set(seg.flatMap(x => x.hazards))], gafor: worstGafor(seg.map(x => x.gafor)) })
  }
  const ranges = hazardRanges(flight.segments)
  const fuel = { trip: flight.hours * fuelPerHour, reserve: RESERVE_HOURS * fuelPerHour, perHour: fuelPerHour }
  const avgGsKt = lengthNm / flight.hours

  // Alternate: needed when the destination is below marginal VFR at the ETA or (an aerodrome's) crosswind exceeds the
  // limit; the nearest other aerodrome that is VFR/MVFR with an acceptable crosswind when the aircraft would get there.
  const destinationBad = ['IFR', 'LIFR', '—'].includes(destination.category.name) || (dest.isAirport && Math.abs(destination.runway.cross) > crosswindLimitKt)
  let alternate = null
  if (destinationBad && detail) {
    const options = airports.filter(x => x.id !== depart.id && x.id !== dest.id).map(x => {
      const d = distanceNm(dest, cellLocation(g, x.i)), eta = flight.etaMs + d / tasKt * 3.6e6
      const r = flightReport(frameAt(frames, eta).frame, x.i, { runwayDeg: activeRunway(frameAt(frames, eta).frame, x), crosswindLimitKt, station: x })
      return { airport: x, distanceNm: d, etaMs: eta, category: r.category, crossKt: r.runway.cross, metar: r.metar, symbol: weatherSymbol(frameAt(frames, eta).frame, x.i) }
    }).filter(o => ['VFR', 'MVFR'].includes(o.category.name) && Math.abs(o.crossKt) <= crosswindLimitKt).sort((p, q) => p.distanceNm - q.distanceNm)
    alternate = options[0] ?? null
  }

  // Go / no-go checks, most serious first.
  const checkList = [], add = (level, text) => checkList.push({ level, text })
  const nm = r => `${Math.round(r.fromNm)}–${Math.round(r.toNm)} NM`
  for (const r of ranges.filter(r => r.kind === 'thunderstorm')) add('severe', `Thunderstorms (CB) on the route at ${nm(r)}: deviate or delay`)
  for (const r of ranges.filter(r => r.kind === 'cyclone')) add('severe', `Tropical-storm winds on the route at ${nm(r)}: do not fly through the cyclone`)
  for (const r of ranges.filter(r => r.kind === 'terrain')) add('severe', `Cruise below terrain clearance at ${nm(r)}: choose a higher level`)
  for (const r of ranges.filter(r => r.kind === 'outside')) add('warning', `The route leaves the simulated area at ${nm(r)}: no weather there (the nearest edge cell is used)`)
  if (['IFR', 'LIFR'].includes(destination.category.name)) add('warning', `${dest.id} is ${destination.category.name} at the ETA: an instrument flight plan and an alternate are needed${alternate ? ` (alternate ${alternate.airport.id}, ${alternate.category.name})` : ' and no aerodrome nearby is VFR/MVFR then'}`)
  if (['IFR', 'LIFR'].includes(departure.category.name)) add('warning', `${depart.id} is ${departure.category.name} at departure`)
  if (dest.isAirport && Math.abs(destination.runway.cross) > crosswindLimitKt) add('warning', `Crosswind ${Math.round(Math.abs(destination.runway.cross))} kt at ${dest.id} on arrival (runway ${destination.runway.designator}) exceeds the ${crosswindLimitKt} kt limit`)
  if (depart.isAirport && Math.abs(departure.runway.cross) > crosswindLimitKt) add('warning', `Crosswind ${Math.round(Math.abs(departure.runway.cross))} kt at ${depart.id} on departure (runway ${departure.runway.designator}) exceeds the ${crosswindLimitKt} kt limit`)
  // GAFOR along the route at the time flown: X-ray (VFR practically impossible) and Mike (marginal) stretches.
  const gaforRanges = kind => { const out = []; let cur = null; for (const x of flight.segments) { if (x.gafor === kind) { if (cur) cur.toNm = x.toNm; else cur = { fromNm: x.fromNm, toNm: x.toNm } } else if (cur) { out.push(cur); cur = null } } if (cur) out.push(cur); return out }
  for (const r of gaforRanges('X')) add('warning', `GAFOR X-ray (closed: VFR practically impossible) on the route at ${nm(r)}`)
  for (const r of gaforRanges('M')) add('caution', `GAFOR Mike (marginal) on the route at ${nm(r)}`)
  for (const r of ranges.filter(r => r.kind === 'icing')) add('warning', `Icing in cloud at your altitude at ${nm(r)}`)
  for (const r of ranges.filter(r => r.kind === 'turbulence')) add('caution', `Possible clear-air turbulence (wind shear) at ${nm(r)}`)
  if (flight.reachedFt < cruiseFt - 1) add('info', `The route is too short to reach ${cruiseFt.toLocaleString('en-US')} ft: the profile tops out at ${Math.round(flight.reachedFt / 100) * 100} ft`)
  if (arrival.beyond) add('info', 'The flight ends after the last frame of the run: the weather after it is held at the last frame')
  if (!flight.segments[0].aloft) add('info', 'This run has no winds aloft (no pressure field): the surface wind is used at every altitude')
  if (!depart.isAirport || !dest.isAirport) add('info', 'A route end is a coordinate, not an aerodrome: its weather is reported for the point, without a runway')

  // Best level: the same route at every winds-aloft level; the quickest one without terrain conflict or icing.
  const levels = !detail ? [] : FB_LEVELS.map(level => {
    const f = fly(ctx, level.ft), cruiseSegs = f.segments.filter(x => x.cruise)
    const kinds = new Set(cruiseSegs.flatMap(x => x.hazards).filter(k => k === 'terrain' || k === 'icing' || k === 'turbulence' || k === 'cloud'))
    return { level, possible: f.possible, hours: f.hours, fuel: f.hours * fuelPerHour, avgWindKt: lengthNm / f.hours - tasKt, reachedFt: f.reachedFt, reached: f.reachedFt >= level.ft - 1, hazards: [...kinds] }
  })
  const usable = levels.filter(l => l.possible && l.reached && !l.hazards.includes('terrain'))
  const clean = usable.filter(l => !l.hazards.includes('icing'))
  const best = (clean.length ? clean : usable).reduce((p, q) => (p === null || q.hours < p.hours ? q : p), null)

  // The cross-section: weather at every level at 3-9 points, each when the aircraft passes it.
  const stationsCount = Math.min(9, Math.max(3, Math.round(lengthNm / 25) + 1)), section = []
  for (let j = 0; detail && j < stationsCount; j++) {
    const d = lengthNm * j / (stationsCount - 1), seg = flight.segments.find(x => d <= x.toNm + 1e-9) ?? flight.segments.at(-1)
    const ms = seg.startMs, i = seg.i, base = weatherAt(ctx, i, seg.altFt, ms)
    const winds = FB_LEVELS.map(level => { const w = weatherAt(ctx, i, level.ft, ms, { hazards: false }); return { level, ...w.wind, tempC: w.tempC, below: level.ft < base.elevFt + 1500 } })
    section.push({ distNm: d, i, ms, frameIndex: base.frameIndex, terrainFt: corridorFt(i), elevFt: base.elevFt, cloud: base.cloud ?? null, icing: base.icing ?? null, freezingFt: base.freezingFt, thunder: base.hazards.includes('thunderstorm'), winds })
  }

  return {
    ok: true, departFrameIndex: startIndex, from: depart, to: dest, points, cumNm: cum, legsNm: built.legsNm, path: built.path, cruiseFt, tasKt, lengthNm,
    tcDeg: initialCourse(points[0], points[1]), finalCourseDeg: finalCourse(points.at(-2), points.at(-1)), departMs: t0, etaMs: flight.etaMs, hours: flight.hours, avgGsKt, avgWindKt: avgGsKt - tasKt, reachedFt: flight.reachedFt,
    fuel, departure, destination, symbols, arrivalFrameIndex: arrival.index, gaforRoute: worstGafor(flight.segments.map(x => x.gafor)), alternate, checks: checkList, ranges, legs, segments: flight.segments, levels, best, section,
    profile: flight.segments.map(x => ({ distNm: (x.fromNm + x.toNm) / 2, altFt: x.altFt, terrainFt: x.terrainFt, headKt: x.headKt })),
    aloft: flight.segments[0].aloft,
  }
}

// ---- Flying the plan: where the aircraft is, and what it meets, at a time into the flight -------------------------
// Elapsed hours at a distance along the route (from the segments' times).
export function hoursAtDistance(plan, dNm) {
  let acc = 0
  for (const s of plan.segments) {
    if (dNm <= s.toNm) return acc + s.hours * Math.max(0, Math.min(1, (dNm - s.fromNm) / ((s.toNm - s.fromNm) || 1)))
    acc += s.hours
  }
  return acc
}

// Top of climb and top of descent: where the profile reaches and leaves the cruise level (null when it never does).
export function climbDescentPoints(plan) {
  const L = plan.lengthNm, toc = (plan.cruiseFt - plan.from.elevationFt) / CLIMB_FT_PER_NM, tod = L - (plan.cruiseFt - plan.to.elevationFt) / CLIMB_FT_PER_NM
  if (!(toc < tod)) return { toc: null, tod: null }
  const at = d => ({ distNm: d, hours: hoursAtDistance(plan, d), ms: plan.departMs + hoursAtDistance(plan, d) * 3.6e6 })
  return { toc: at(toc), tod: at(tod) }
}

// The aircraft at `hours` into the flight: position along the route, altitude, vertical speed, heading and track,
// speeds, wind and temperature of the segment flown, the frame of the weather met, the hazards around it and the next
// hazard ahead. Pure: the 3D fly-through and its display read it every animation frame.
export function planStateAt(plan, hours) {
  const t = Math.max(0, Math.min(plan.hours, hours)), segs = plan.segments
  let acc = 0, k = 0
  while (k < segs.length - 1 && acc + segs[k].hours < t) { acc += segs[k].hours; k++ }
  const s = segs[k], f = s.hours > 0 ? Math.max(0, Math.min(1, (t - acc) / s.hours)) : 0
  const distNm = s.fromNm + f * (s.toNm - s.fromNm)
  const altFt = profileAltitude(distNm, plan.lengthNm, plan.cruiseFt, plan.from.elevationFt, plan.to.elevationFt)
  const ahead = profileAltitude(Math.min(plan.lengthNm, distNm + 0.5), plan.lengthNm, plan.cruiseFt, plan.from.elevationFt, plan.to.elevationFt)
  const behind = profileAltitude(Math.max(0, distNm - 0.5), plan.lengthNm, plan.cruiseFt, plan.from.elevationFt, plan.to.elevationFt)
  const gradientFtPerNm = (ahead - behind) / ((Math.min(plan.lengthNm, distNm + 0.5) - Math.max(0, distNm - 0.5)) || 1)
  const verticalFpm = gradientFtPerNm * s.gsKt / 60
  const phase = verticalFpm > 50 ? 'climb' : verticalFpm < -50 ? 'descent' : 'cruise'
  const hazardsNow = plan.ranges.filter(r => r.kind !== 'cloud' && r.fromNm <= distNm && r.toNm >= distNm).map(r => r.kind)
  const next = plan.ranges.filter(r => r.kind !== 'cloud' && r.fromNm > distNm).sort((a, b) => a.fromNm - b.fromNm)[0] ?? null
  const legIndex = plan.legs.findIndex(l => distNm <= l.toNm + 1e-9)
  return {
    hours: t, fraction: plan.hours > 0 ? t / plan.hours : 0, done: t >= plan.hours, distNm, remainingNm: plan.lengthNm - distNm, remainingHours: plan.hours - t,
    altFt, verticalFpm, phase, pitchDeg: Math.atan2(gradientFtPerNm, 6076.12) * 180 / Math.PI,
    tcDeg: s.trueCourseDeg, headingDeg: s.headingDeg, gridTrackDeg: s.gridCourseDeg, gridHeadingDeg: s.gridHeadingDeg, wcaDeg: s.wcaDeg, leg: s.leg, gsKt: s.gsKt, tasKt: plan.tasKt, wind: s.wind, headKt: s.headKt, crossKt: s.crossKt,
    tempC: s.tempC, isaDevC: s.isaDevC, inCloud: s.hazards.includes('cloud'), gafor: s.gafor, hazards: hazardsNow, segment: k, legIndex: legIndex < 0 ? plan.legs.length - 1 : legIndex,
    ms: plan.departMs + t * 3.6e6, frameIndex: s.frameIndex,
    ahead: next ? { kind: next.kind, inNm: next.fromNm - distNm, inMinutes: (hoursAtDistance(plan, next.fromNm) - t) * 60, toNm: next.toNm } : null,
  }
}

// ---- Flight rules: can the flight be made visually, or is IFR needed? ---------------------------------------------
// From the plan, at the time each part is flown:
//   - along the route, the VFR band of every segment (gafor.js): from the higher of the VFR minimum height and 500 ft
//     over the corridor's highest terrain, up to the clear-of-cloud limit with the ground in sight;
//   - the GAFOR category of every segment, and of departure (at departure) and destination (at the ETA).
// VFR possible: a band everywhere and C/O/D at both ends and en route. VFR marginal: Mike somewhere, or a band thinner
// than 500 ft, or no data to judge. IFR required: X-ray anywhere or no VFR band somewhere. Not advisable: thunderstorms
// or tropical-storm winds on the route (neither VFR nor IFR should go through them).
export const RULES = {
  VFR: { key: 'VFR', label: 'VFR possible', short: 'VFR', colour: '#2ecc71', rank: 0 },
  MARGINAL: { key: 'MARGINAL', label: 'VFR marginal', short: 'VFR·M', colour: '#ff9f1c', rank: 1 },
  IFR: { key: 'IFR', label: 'IFR required', short: 'IFR', colour: '#b36bff', rank: 2 },
  NOGO: { key: 'NOGO', label: 'Not advisable', short: 'NO-GO', colour: '#ff3b30', rank: 3 },
}
export const THIN_BAND_FT = 500
export function flightRules(plan, frames) {
  if (!plan?.ok) return null
  const reasons = [], add = (rule, text) => reasons.push({ rule, text })
  const nm = (a, b) => `${Math.round(a)}–${Math.round(b)} NM`
  // The ends.
  const ends = [['departure', plan.from, frames[plan.departFrameIndex]], ['destination', plan.to, frames[plan.arrivalFrameIndex]]].map(([role, pt, frame]) => {
    let v = null; try { v = vfrAt(frame, pt.i) } catch { v = null }
    const key = v?.category.key ?? '?'
    if (key === 'X') add('IFR', `${pt.id} is X-ray at ${role === 'departure' ? 'departure' : 'the ETA'} (visibility ${v.visibilityM === null ? '—' : `${Math.round(v.visibilityM / 100) * 100} m`}, main cloud base ${Number.isFinite(v.hsFt) ? `${Math.round(v.hsFt / 100) * 100} ft` : 'none'}): below VFR minima`)
    else if (key === 'M') add('MARGINAL', `${pt.id} is Mike at ${role === 'departure' ? 'departure' : 'the ETA'}: marginal VFR, high workload`)
    else if (key === '?') add('MARGINAL', `${pt.id}: no visibility or cloud data at ${role === 'departure' ? 'departure' : 'the ETA'} to judge VFR`)
    return { role, id: pt.id, gafor: key, vfr: v }
  })
  // En route: the VFR band of every segment.
  const segs = plan.segments.map(seg => {
    let v = null; try { v = vfrAt(frames[seg.frameIndex], seg.i) } catch { v = null }
    const floorFt = Math.max(v ? v.space.bottomFt : 0, seg.terrainFt + 500), topFt = v?.space.ok ? v.space.topFt : -Infinity
    const band = topFt - floorFt, key = v?.category.key ?? '?'
    const kind = key === 'X' || band < 0 ? 'IFR' : key === 'M' || band < THIN_BAND_FT || key === '?' ? 'MARGINAL' : 'VFR'
    return { fromNm: seg.fromNm, toNm: seg.toNm, floorFt, topFt, band, key, kind, hsFt: v?.hsFt ?? null, visibilityM: v?.visibilityM ?? null }
  })
  const stretches = kind => { const out = []; let cur = null; for (const x of segs) { if (x.kind === kind) { if (cur) { cur.toNm = x.toNm; cur.items.push(x) } else cur = { fromNm: x.fromNm, toNm: x.toNm, items: [x] } } else if (cur) { out.push(cur); cur = null } } if (cur) out.push(cur); return out }
  for (const r of stretches('IFR')) {
    const lowBase = Math.min(...r.items.map(x => (Number.isFinite(x.hsFt) ? x.hsFt : Infinity))), lowVis = Math.min(...r.items.map(x => x.visibilityM ?? Infinity))
    add('IFR', `No VFR airspace at ${nm(r.fromNm, r.toNm)}${Number.isFinite(lowBase) ? ` (main cloud base down to ${Math.round(lowBase / 100) * 100} ft)` : ''}${Number.isFinite(lowVis) && lowVis < 5000 ? ` (visibility down to ${Math.round(lowVis / 100) * 100} m)` : ''}: IFR required`)
  }
  for (const r of stretches('MARGINAL')) add('MARGINAL', `Marginal VFR at ${nm(r.fromNm, r.toNm)}: ${r.items.some(x => x.key === 'M') ? 'GAFOR Mike' : 'a VFR band under 500 ft between terrain and cloud'}`)
  for (const r of plan.ranges.filter(x => x.kind === 'thunderstorm' || x.kind === 'cyclone')) add('NOGO', `${r.kind === 'thunderstorm' ? 'Thunderstorms' : 'Tropical-storm winds'} at ${nm(r.fromNm, r.toNm)}: neither VFR nor IFR through them`)
  // The band along the whole route: one VFR altitude works if the highest floor is below the lowest top.
  const usable = segs.filter(x => Number.isFinite(x.topFt))
  const floorFt = Math.max(...segs.map(x => x.floorFt)), ceilingFt = usable.length ? Math.min(...usable.map(x => x.topFt)) : null
  const continuous = ceilingFt !== null && usable.length === segs.length && floorFt <= ceilingFt
  const worst = reasons.reduce((w, r) => (RULES[r.rule].rank > w.rank ? RULES[r.rule] : w), RULES.VFR)
  if (worst.key === 'IFR' && plan.ranges.some(x => x.kind === 'icing')) add('IFR', 'IFR at the planned level meets icing in cloud: ice protection needed, or choose another level')
  if (worst.key === 'VFR' && !continuous) add('MARGINAL', 'No single VFR altitude works all the way: the altitude has to change along the route')
  const rule = reasons.reduce((w, r) => (RULES[r.rule].rank > w.rank ? RULES[r.rule] : w), RULES.VFR)
  return {
    rule, reasons: reasons.sort((a, b) => RULES[b.rule].rank - RULES[a.rule].rank), ends, segments: segs,
    band: { floorFt, ceilingFt, continuous }, routeGafor: plan.gaforRoute,
  }
}

// The round trip: the outbound plan, the return along the reversed route at its own departure time, their flight
// rules, and the verdict for the pair.
export function returnTrip({ frames, outbound, returnIndex, waypoints, ...options }) {
  if (!outbound?.ok) return null
  const back = planFlight({ ...options, frames, startIndex: returnIndex, waypoints: [...waypoints].reverse() })
  const out = { plan: outbound, rules: flightRules(outbound, frames) }, ret = { plan: back, rules: back.ok ? flightRules(back, frames) : null }
  const afterArrival = back.ok ? back.departMs >= outbound.etaMs : false
  const worst = [out.rules?.rule, ret.rules?.rule].filter(Boolean).reduce((w, r) => (r.rank > w.rank ? r : w), RULES.VFR)
  const verdict = !back.ok ? { rule: RULES.NOGO, text: `The return cannot be planned: ${back.reason}` }
    : !afterArrival ? { rule: RULES.NOGO, text: 'The return departs before the outbound flight arrives: choose a later return time' }
    : out.rules.rule.key === 'VFR' && ret.rules.rule.key === 'VFR' ? { rule: RULES.VFR, text: 'The return trip is possible under VFR both ways' }
    : worst.key === 'NOGO' ? { rule: RULES.NOGO, text: `Not advisable: ${out.rules.rule.key === 'NOGO' ? 'the outbound flight' : 'the return'} meets thunderstorms or tropical-storm winds` }
    : { rule: worst, text: `Outbound: ${out.rules.rule.label} · return: ${ret.rules.rule.label}` }
  return { outbound: out, back: ret, verdict, stayHours: back.ok ? (back.departMs - outbound.etaMs) / 3.6e6 : null }
}

// Flight rules for departures at each of the given frames (the VFR windows of a day): light plans, no detail.
export function ruleWindows({ frames, indices, waypoints, ...options }) {
  return indices.map(index => {
    const plan = planFlight({ ...options, frames, startIndex: index, waypoints, detail: false })
    const rules = plan.ok ? flightRules(plan, frames) : null
    return { index, ms: validTime(frames[index]), rule: rules?.rule ?? RULES.NOGO, reason: plan.ok ? (rules.reasons[0]?.text ?? 'VFR all the way') : plan.reason, etaMs: plan.ok ? plan.etaMs : null }
  })
}
