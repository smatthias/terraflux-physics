// Sailing report: a small-craft marine report at one sea cell of the current engine frame. It covers wind (Beaufort,
// warnings), the boat's true and apparent wind and point of sail, the sea (the viewer's wind-sea estimate, waves.js),
// currents and water temperature from the ocean model, visibility and weather, and the barometer and its tendency.
// Pure functions: no Vue, no three.js. Requirements, sources and limits: docs/weather/flight-sailing-reports.md.
//
// Simulated, not observed: not for navigation. Waves are the viewer's parametric wind-sea estimate (no swell, no tides);
// visibility is estimated (station.js); the warning thresholds and the sail-plan limits are user settings.
import {
  KT_PER_MS, M_PER_NM, HPA, finiteOr, angleDiff, norm360, cellLocation, formatLatLon, validTime, surfaceWind, beaufort, compass16,
  relativeHumidity, pressures, cloudLayer, presentWeather, estimateVisibility, weatherWords, pressureTendency, cycloneHazard,
} from './station.js'
import { seaState, G } from './waves.js'
import { distanceNm, initialCourse } from './geo.js'
import { weatherSymbol } from './weatherLayers.js'

// ---- Where: the nearest sea cell ------------------------------------------------------------------------------------
// The sea cell nearest to cell i (Euclidean distance in metres on the grid). sea: mask (1 = sea) or null (unknown).
// Returns { i, snapped, distanceM } or null when the grid has no sea at all.
export function nearestSea(sea, grid, i) {
  if (!sea) return { i, snapped: false, distanceM: 0, known: false }
  if (sea[i]) return { i, snapped: false, distanceM: 0, known: true }
  const { nx, ny, spacing_east_m: dx, spacing_north_m: dy } = grid, x0 = i % nx, y0 = Math.floor(i / nx)
  let best = -1, bestD = Infinity
  for (let j = 0; j < nx * ny; j++) {
    if (!sea[j]) continue
    const d = Math.hypot((j % nx - x0) * dx, (Math.floor(j / nx) - y0) * dy)
    if (d < bestD) { bestD = d; best = j }
  }
  return best < 0 ? null : { i: best, snapped: true, distanceM: bestD, known: true }
}

// ---- Wind words and warnings ----------------------------------------------------------------------------------------
// US NWS marine wind warnings on the sustained wind (gale 34-47 kt, storm 48-63 kt, hurricane force from 64 kt). The
// small-craft advisory has regional definitions: its wind and sea thresholds are settings.
export const WARNINGS = {
  none: { key: 'none', label: 'No warning', colour: '#3ddc84' },
  sca: { key: 'sca', label: 'Small-craft advisory', colour: '#ffd166' },
  gale: { key: 'gale', label: 'Gale warning', colour: '#ff9f43' },
  storm: { key: 'storm', label: 'Storm warning', colour: '#ff5252' },
  hurricane: { key: 'hurricane', label: 'Hurricane-force wind warning', colour: '#e040fb' },
}
export function marineWarning(kt, hs, { scaKt = 22, scaSeasM = 2 } = {}) {
  if (kt >= 64) return WARNINGS.hurricane
  if (kt >= 48) return WARNINGS.storm
  if (kt >= 34) return WARNINGS.gale
  if (kt >= scaKt || (Number.isFinite(hs) && hs >= scaSeasM)) return WARNINGS.sca
  return WARNINGS.none
}

// Shipping-forecast visibility words (UK Met Office): good > 5 NM, moderate 2-5 NM, poor 1 000 m - 2 NM, very poor
// below 1 000 m (fog).
export function marineVisibility(metres) {
  if (metres === null || metres === undefined || !Number.isFinite(metres)) return null
  const nm = metres / M_PER_NM
  return { nm, word: metres < 1000 ? 'very poor' : nm < 2 ? 'poor' : nm <= 5 ? 'moderate' : 'good' }
}

// Barometric tendency in shipping-forecast words by the change over 3 h (hPa): steady below 0.1, slowly to 1.5,
// (plain) to 3.5, quickly to 6.0, very rapidly above.
export function tendencyWord(hPaPer3h) {
  if (hPaPer3h === null || hPaPer3h === undefined || !Number.isFinite(hPaPer3h)) return null
  const a = Math.abs(hPaPer3h)
  if (a < 0.1) return 'steady'
  const dir = hPaPer3h > 0 ? 'rising' : 'falling'
  return a <= 1.5 ? `${dir} slowly` : a <= 3.5 ? dir : a <= 6 ? `${dir} quickly` : `${dir} very rapidly`
}

// ---- The boat ------------------------------------------------------------------------------------------------------
// True wind angle (signed: positive = wind over the starboard side) and the apparent wind: the true air motion minus
// the boat's own motion. Angles in degrees, speeds in knots.
export function apparentWind(twdFrom, twsKt, headingDeg, boatKt) {
  const f = twdFrom * Math.PI / 180, h = headingDeg * Math.PI / 180
  // Air motion (towards) of the true wind, and relative to the moving boat.
  const ax = -twsKt * Math.sin(f) - boatKt * Math.sin(h), ay = -twsKt * Math.cos(f) - boatKt * Math.cos(h)
  const aws = Math.hypot(ax, ay), awdFrom = aws > 1e-9 ? norm360(Math.atan2(-ax, -ay) * 180 / Math.PI) : norm360(headingDeg)
  return { twa: angleDiff(twdFrom, headingDeg), aws, awa: angleDiff(awdFrom, headingDeg), awdFrom }
}

// Customary points of sail by the absolute true wind angle.
export function pointOfSail(twaAbs) {
  if (twaAbs < 40) return 'in irons (no-go zone)'
  if (twaAbs < 55) return 'close-hauled'
  if (twaAbs < 80) return 'close reach'
  if (twaAbs < 100) return 'beam reach'
  if (twaAbs < 160) return 'broad reach'
  return 'running'
}
export const tackOf = twa => (Math.abs(twa) < 2 ? 'head to wind' : Math.abs(twa) > 178 ? 'dead run' : twa > 0 ? 'starboard tack' : 'port tack')

// Wave steepness s = 2 pi Hs / (g Tp^2): gentle (swell-like) below 0.025, a typical wind sea to 0.04 (the fully developed
// Pierson-Moskowitz sea is about 0.03), steep and short above.
export function steepness(hs, tp) {
  if (!(hs > 0.05) || !(tp > 0)) return null
  const s = 2 * Math.PI * hs / (G * tp * tp)
  return { value: s, label: s < 0.025 ? 'long and gentle' : s <= 0.04 ? 'typical wind sea' : 'steep and short' }
}

// ---- The report ---------------------------------------------------------------------------------------------------
export const DEFAULT_SAILING_SETTINGS = { headingDeg: 45, boatKt: 5, reefKt: 18, scaKt: 22, scaSeasM: 2 }

// frame: the current frame; requested: the picked cell; sea: the run's sea mask (or null); waves: the frame's wave field
// (waves.js waveField result, or null); frames/index: the timeline (for the barometer tendency).
export function sailingReport(frame, requested, { sea = null, waves = null, frames = null, index = 0, settings = {} } = {}) {
  const opts = { ...DEFAULT_SAILING_SETTINGS, ...settings }
  const g = frame.forecast.grid, near = nearestSea(sea, g, requested)
  if (!near) return { available: false, reason: 'This run has no sea: there is nowhere to sail.', settings: opts }
  const i = near.i, loc = cellLocation(g, i), ms = validTime(frame)
  const wind = surfaceWind(frame, i), bf = beaufort(wind.speed)

  // Sea: the wind sea at the cell.
  let seaInfo = null
  if (waves && near.known) {
    const hs = waves.hs[i], tp = waves.tp[i], towards = norm360(Math.atan2(waves.dirEast[i], waves.dirNorth[i]) * 180 / Math.PI)
    seaInfo = { hs, tp, length: waves.length[i], speedKt: waves.speed[i] * KT_PER_MS, fromDeg: norm360(towards + 180), towardsDeg: towards, state: seaState(hs), regime: waves.regime[i] ?? 'calm', fetchKm: waves.fetch[i] / 1000, steepness: steepness(hs, tp) }
  }
  const warning = marineWarning(wind.kt, seaInfo?.hs, opts)

  // The boat.
  const aw = apparentWind(wind.fromDeg, wind.kt, opts.headingDeg, opts.boatKt)
  const boat = { headingDeg: norm360(opts.headingDeg), speedKt: opts.boatKt, ...aw, tack: tackOf(aw.twa), pointOfSail: pointOfSail(Math.abs(aw.twa)), vmgKt: opts.boatKt * Math.cos(aw.twa * Math.PI / 180) }

  // Current and water temperature (ocean runs; SST also from a storm run's surface).
  const o = frame.ocean
  let current = null
  if (o && o.mask[i]) {
    const u = o.u[i], v = o.v[i], drift = Math.hypot(u, v) * KT_PER_MS
    const against = seaInfo && seaInfo.hs > 0.05 ? -(u * Math.sin(seaInfo.towardsDeg * Math.PI / 180) + v * Math.cos(seaInfo.towardsDeg * Math.PI / 180)) * KT_PER_MS : 0
    current = { setDeg: norm360(Math.atan2(u, v) * 180 / Math.PI), driftKt: drift, againstWavesKt: against, windAgainstCurrent: against > 0.5 }
  }
  const sstK = finiteOr(o?.thermal?.sst?.[i], null) ?? (o?.mask[i] ? finiteOr(o?.temperature?.[i], null) : null) ?? finiteOr(frame.surface?.sst?.[i], null)
  const water = sstK === null ? null : { c: sstK - 273.15, coldShock: sstK - 273.15 < 15 }

  // Weather, visibility and the barometer.
  const rh = relativeHumidity(frame, i), cloud = cloudLayer(frame, i), wx = presentWeather(frame, i)
  const vis = estimateVisibility({ rh, weather: wx, cloudBase: cloud.layer?.base ?? null })
  const p = pressures(frame, i), tendency = frames ? pressureTendency(frames, index, i) : null
  const barometer = p.qnh === null ? null : { qnhHpa: p.qnh / HPA, stormAnomalyHpa: p.stormAnomaly / HPA, tendency: tendency && !tendency.static ? { ...tendency, word: tendencyWord(tendency.hPaPer3h) } : null, staticField: !!tendency?.static }

  // Hazards, most serious first.
  const hazards = [], add = (level, text) => hazards.push({ level, text })
  const tc = cycloneHazard(wind)
  if (tc) add(tc.level, tc.text)
  if (warning.key === 'hurricane' || warning.key === 'storm' || warning.key === 'gale') add('severe', `${warning.label}: ${Math.round(wind.kt)} kt, force ${bf.force} (${bf.label})`)
  if (wx.thunder) add('severe', 'Thunderstorms: squalls, sudden wind shifts and lightning; reef or drop sail early')
  const surge = finiteOr(frame.storms?.env?.storm_surge_m?.[i], 0)
  if (surge > 0.3) add('warning', `Storm surge about ${surge.toFixed(1)} m: harbours and low quays may flood`)
  if (barometer?.tendency && barometer.tendency.hPaPer3h <= -3.6) add('warning', `Barometer ${barometer.tendency.word} (${barometer.tendency.hPaPer3h.toFixed(1)} hPa in 3 h): weather likely to deteriorate`)
  if (current?.windAgainstCurrent) add('warning', `Wind against current (${current.againstWavesKt.toFixed(1)} kt against the waves): steeper, breaking seas`)
  if (seaInfo?.steepness?.label === 'steep and short' && seaInfo.hs >= 1) add('warning', `Steep, short seas: ${seaInfo.hs.toFixed(1)} m every ${seaInfo.tp.toFixed(1)} s`)
  if (seaInfo?.regime === 'depth-limited') add('caution', 'Depth-limited waves: seas break in the shallows near the coast')
  if (seaInfo?.regime === 'damped by sea ice') add('warning', 'Sea ice: waves damped, navigation restricted')
  const visWords = vis ? marineVisibility(vis.metres) : null
  if (visWords && (visWords.word === 'poor' || visWords.word === 'very poor')) add('warning', `Visibility ${visWords.word} (${vis.metres < 1000 ? `${Math.round(vis.metres / 50) * 50} m` : `${visWords.nm.toFixed(1)} NM`}): sound signals, radar reflector, reduce speed`)
  if (water?.coldShock) add('caution', `Water ${water.c.toFixed(1)} °C: cold-water shock risk; wear a lifejacket and dress for immersion`)

  // Sail-plan hint against the user's own limits.
  let plan
  if (['gale', 'storm', 'hurricane'].includes(warning.key)) plan = { level: 'severe', text: 'Gale or worse: stay in harbour or seek shelter.' }
  else if (wind.kt >= opts.scaKt || (seaInfo && seaInfo.hs >= opts.scaSeasM)) plan = { level: 'warning', text: `Beyond your small-craft limits (${opts.scaKt} kt / ${opts.scaSeasM} m): stay in harbour unless crew and boat are equipped for it.` }
  else if (wind.kt >= opts.reefKt) plan = { level: 'caution', text: `Above your reefing wind (${opts.reefKt} kt): reef before you leave.` }
  else if (wind.kt < 4) plan = { level: 'info', text: 'Very light wind: expect to drift or motor.' }
  else plan = { level: 'ok', text: `Within the limits set: full sail (${boat.pointOfSail}).` }

  const notes = ['Simulated from engine output (synthetic inputs): not for navigation.']
  if (!near.known) notes.push('This run has no land/sea mask: the point is assumed to be on water and waves are not estimated.')
  else notes.push('Waves are the viewer\'s wind-sea estimate (JONSWAP growth, fetch to land): no swell, tides or refraction.')
  if (near.snapped) notes.push(`Moved to the nearest sea cell, ${(near.distanceM / 1000).toFixed(0)} km from the picked point.`)
  if (vis) notes.push('Visibility is estimated (the engine has no visibility).')
  if (!current && o) notes.push('No ocean current at this cell.')
  if (barometer?.staticField) notes.push('The pressure field of this run is prescribed and constant in time: no barometer tendency.')
  notes.push('Gusts are not modelled. Directions are true.')

  return {
    available: true,
    station: { i, requested, snapped: near.snapped, distanceKm: near.distanceM / 1000, x: loc.x, y: loc.y, lat: loc.lat, lon: loc.lon, where: formatLatLon(loc), onSeaKnown: near.known },
    time: { ms, iso: new Date(ms).toISOString().replace('.000Z', 'Z') },
    wind: { ...wind, beaufort: bf, from16: compass16(wind.fromDeg) }, warning, boat, sea: seaInfo, current, water,
    weather: { words: weatherWords(wx, vis), thunder: wx.thunder, modelled: wx.modelled },
    visibility: vis ? { ...vis, ...visWords } : null, barometer, hazards, plan, notes, settings: opts,
  }
}

// Colours of the Beaufort forces 0-12 (map ground of the sailing view, trend strip): calm blues to violet storms.
export const BEAUFORT_COLOURS = ['#20404f', '#22506a', '#246685', '#2a7fa0', '#3aa3a0', '#5fbf7f', '#a8cf55', '#e0c040', '#f0a030', '#f07a30', '#e8503a', '#d03c6a', '#b040c0']

// ---- Marine forecast wording (shipping-forecast style) from the coming frames of the run --------------------------
const DIRECTION_WORDS = ['north', 'northeast', 'east', 'southeast', 'south', 'southwest', 'west', 'northwest']
export const directionWord = deg => DIRECTION_WORDS[Math.round(norm360(deg) / 45) % 8]
// Force with its name from gale upward (Met Office usage): "6", "gale 8", "severe gale 9", "storm 10", ...
export const forceWords = force => (force >= 12 ? 'hurricane force 12' : force === 11 ? 'violent storm 11' : force === 10 ? 'storm 10' : force === 9 ? 'severe gale 9' : force === 8 ? 'gale 8' : String(force))
// Timing: imminent within 6 h, soon 6-12 h, later after 12 h.
export const timingWord = hours => (hours <= 6 ? 'imminent' : hours <= 12 ? 'soon' : 'later')
const cap = s => s.charAt(0).toUpperCase() + s.slice(1)

// The wind now and its change over the next 24 h of the run at cell i: "Southwest 5, veering west gale 8 later.",
// "Calm, becoming south 6 soon."
// Veering is a clockwise shift, backing anticlockwise; a shift counts from 45 degrees, a force change from one force.
// { text, now: { dirWord, force }, change: null | { hoursAhead, veer, force, ... }, horizonHours }
export function marineWindForecast(frames, index, i, { windowHours = 24 } = {}) {
  const now = frames[index], t0 = now.forecast.elapsed_seconds, w0 = surfaceWind(now, i), f0 = beaufort(w0.speed).force
  const ahead = []
  for (let k = index + 1; k < frames.length; k++) { const h = (frames[k].forecast.elapsed_seconds - t0) / 3600; if (h > windowHours) break; ahead.push({ h, w: surfaceWind(frames[k], i) }) }
  // A calm has no direction: "Calm"; a change out of it "becoming <direction> <force>", into it "becoming calm".
  const windWords = (w, f) => (f === 0 ? 'calm' : `${directionWord(w.fromDeg)} ${forceWords(f)}`)
  const head = cap(windWords(w0, f0))
  if (!ahead.length) return { text: `${head}.`, now: { dirWord: directionWord(w0.fromDeg), force: f0 }, change: null, horizonHours: 0 }
  const last = ahead.at(-1), f1 = beaufort(last.w.speed).force, turn = angleDiff(last.w.fromDeg, w0.fromDeg)
  const shifted = Math.abs(turn) >= 45 && last.w.speed >= 1.6 && w0.speed >= 1.6, changed = f1 !== f0
  if (!shifted && !changed) return { text: `${head}.`, now: { dirWord: directionWord(w0.fromDeg), force: f0 }, change: null, horizonHours: last.h }
  // When the change first shows: the first frame past half of it.
  const first = ahead.find(a => (shifted && Math.abs(angleDiff(a.w.fromDeg, w0.fromDeg)) >= Math.abs(turn) / 2) || (changed && Math.abs(beaufort(a.w.speed).force - f0) >= Math.abs(f1 - f0) / 2)) ?? last
  const when = timingWord(first.h)
  const text = changed && (f0 === 0 || f1 === 0)
    ? `${head}, becoming ${windWords(last.w, f1)} ${when}.`
    : shifted
    ? `${head}, ${turn > 0 ? 'veering' : 'backing'} ${directionWord(last.w.fromDeg)}${changed ? ` ${forceWords(f1)}` : ''} ${when}.`
    : `${head}, ${f1 > f0 ? 'increasing' : 'decreasing'} ${forceWords(f1)} ${when}.`
  return { text, now: { dirWord: directionWord(w0.fromDeg), force: f0 }, change: { hoursAhead: first.h, veer: shifted ? (turn > 0 ? 'veering' : 'backing') : null, force: f1, when }, horizonHours: last.h }
}

// Beaufort force at the station for every frame of the run (the trend strip).
export const beaufortSeries = (frames, i) => frames.map(f => beaufort(surfaceWind(f, i).speed).force)

// ---- A data point on the chart: what the sailing view shows where the user clicks ----------------------------------
// Wind (Beaufort, warning), the wind sea there (waves.js field) when the point is at sea, current and water temperature
// (ocean runs), weather symbol and words, visibility and barometer, and the distance and true bearing from the boat
// (great circle). sea: the run's sea mask or null; waves: the frame's wave field; boat: { lat, lon } or null.
export function pointReadout(frame, i, { sea = null, waves = null, boat = null, settings = {} } = {}) {
  const opts = { ...DEFAULT_SAILING_SETTINGS, ...settings }
  const g = frame.forecast.grid, loc = cellLocation(g, i), wind = surfaceWind(frame, i), bf = beaufort(wind.speed)
  const onSea = sea ? !!sea[i] : null
  let seaInfo = null
  if (onSea && waves) {
    const towards = norm360(Math.atan2(waves.dirEast[i], waves.dirNorth[i]) * 180 / Math.PI)
    seaInfo = { hs: waves.hs[i], tp: waves.tp[i], length: waves.length[i], fromDeg: norm360(towards + 180), state: seaState(waves.hs[i]), regime: waves.regime[i] ?? 'calm' }
  }
  const o = frame.ocean
  const current = o && o.mask[i] ? { setDeg: norm360(Math.atan2(o.u[i], o.v[i]) * 180 / Math.PI), driftKt: Math.hypot(o.u[i], o.v[i]) * KT_PER_MS } : null
  const sstK = finiteOr(o?.thermal?.sst?.[i], null) ?? (o?.mask[i] ? finiteOr(o?.temperature?.[i], null) : null) ?? finiteOr(frame.surface?.sst?.[i], null)
  const rh = relativeHumidity(frame, i), cloud = cloudLayer(frame, i), wx = presentWeather(frame, i)
  const vis = estimateVisibility({ rh, weather: wx, cloudBase: cloud.layer?.base ?? null }), p = pressures(frame, i)
  const fromBoat = boat ? { nm: distanceNm(boat, loc), bearingDeg: initialCourse(boat, loc) } : null
  return {
    i, x: loc.x, y: loc.y, lat: loc.lat, lon: loc.lon, where: formatLatLon(loc), onSea,
    wind: { ...wind, beaufort: bf, from16: compass16(wind.fromDeg) }, warning: marineWarning(wind.kt, seaInfo?.hs, opts), sea: seaInfo, current,
    water: sstK === null ? null : { c: sstK - 273.15 }, symbol: weatherSymbol(frame, i), weather: { words: weatherWords(wx, vis), thunder: wx.thunder, modelled: wx.modelled },
    visibility: vis ? { metres: vis.metres, ...marineVisibility(vis.metres) } : null, qnhHpa: p.qnh === null ? null : p.qnh / HPA, fromBoat,
  }
}
