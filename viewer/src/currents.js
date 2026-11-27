// Currents for the sailing view: the run's ocean model where it has one; otherwise current tables loaded by the user
// from public sources (tidal stream atlases and chart "tidal diamonds", NOAA CO-OPS current predictions, or any point
// or gridded series such as a Copernicus Marine export as CSV); and a wind-drift estimate where nothing else is known.
// Pure functions. Requirements, sources and formats: docs/weather/sailing-currents.md.
//
// Conventions: u, v in m/s towards east and north (grid frame, like the engine's winds); "set" is the direction the
// current flows TOWARDS (degrees true) and "drift" its speed (kt), as on charts and in tidal stream atlases.
import { KT_PER_MS, norm360, angleDiff, validTime, cellLocation, surfaceWind } from './station.js'
import { project } from './geo.js'

const RAD = Math.PI / 180, MS_PER_KT = 1 / KT_PER_MS
// Wind drift: the surface current a sustained wind drives, about 2.5 % of the 10 m wind, turned 20 degrees to the right
// of the wind in the northern hemisphere (to the left in the southern), built up over about 6 hours.
export const DRIFT = { factor: 0.025, deflectionDeg: 20, tauHours: 6 }
export const DEFAULT_CURRENT_SETTINGS = { drift: true, tables: true, radiusKm: 30 }
export const SOURCES = {
  ocean: { key: 'ocean', label: 'ocean model (engine)', short: 'ocean model' },
  table: { key: 'table', label: 'current table', short: 'table' },
  drift: { key: 'drift', label: 'wind drift (estimated, 2.5 % of the wind)', short: 'wind drift' },
}
export const setOf = (u, v) => norm360(Math.atan2(u, v) / RAD)
export const fromSetDrift = (setDeg, driftKt) => [driftKt * MS_PER_KT * Math.sin(setDeg * RAD), driftKt * MS_PER_KT * Math.cos(setDeg * RAD)]

// ---- Tables ----------------------------------------------------------------------------------------------------------
// A table: { name, format, kind: 'total' | 'tidal', stations: [{ id, name, lat, lon }], span: [ms, ms] | null, note,
// sample(stationIndex, ms) -> { u, v } | null }. 'tidal' tables hold only the tidal stream: the wind drift is added to
// them; 'total' tables (model or observed currents) replace it.
const parseTime = text => {
  if (typeof text === 'number') return text
  const t = String(text).trim(), iso = /^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}(:\d{2})?$/.test(t) ? `${t.replace(' ', 'T')}Z` : t
  const ms = Date.parse(iso)
  if (!Number.isFinite(ms)) throw new Error(`Unreadable time "${text}" (use ISO 8601 UTC, e.g. 2026-06-05T06:00Z).`)
  return ms
}
// Linear interpolation of u, v in a time series (null outside it, and across gaps of more than maxGapMs).
function seriesSample(times, us, vs, ms, maxGapMs = 6 * 3.6e6) {
  if (!times.length || ms < times[0] || ms > times.at(-1)) return null
  let k = 0; while (k < times.length - 1 && times[k + 1] < ms) k++
  if (k === times.length - 1) return { u: us[k], v: vs[k] }
  const dt = times[k + 1] - times[k]; if (dt > maxGapMs) return null
  const f = dt > 0 ? (ms - times[k]) / dt : 0
  return { u: us[k] + (us[k + 1] - us[k]) * f, v: vs[k] + (vs[k + 1] - vs[k]) * f }
}

// CSV: a header row, one row per time and place. Columns (case-insensitive; the first match is used):
//   time: time, time_utc, datetime, date_time, t       position: lat / latitude, lon / lng / longitude
//   station (optional): station, id, name
//   the current, one of: u and v (m/s, or u_kt and v_kt), uo and vo (Copernicus), set (deg, towards) with drift (kt)
//   or speed_ms / speed_cms
function parseCsv(text, { name = 'current table', kind = 'total' } = {}) {
  const all = text.split(/\r?\n/).map(l => l.trim()), comments = all.filter(l => l.startsWith('#')).join(' ')
  const lines = all.filter(l => l && !l.startsWith('#'))
  if (lines.length < 2) throw new Error('The CSV needs a header row and at least one data row.')
  const sep = lines[0].includes(';') && !lines[0].includes(',') ? ';' : lines[0].includes('\t') ? '\t' : ','
  const head = lines[0].split(sep).map(h => h.trim().toLowerCase().replace(/^"|"$/g, ''))
  const col = (...names) => { for (const n of names) { const k = head.indexOf(n); if (k >= 0) return k } return -1 }
  const cT = col('time', 'time_utc', 'datetime', 'date_time', 't'), cLat = col('lat', 'latitude'), cLon = col('lon', 'lng', 'longitude'), cId = col('station', 'id', 'name')
  const cU = col('u', 'u_ms', 'uo', 'east_ms', 'u_m_s'), cV = col('v', 'v_ms', 'vo', 'north_ms', 'v_m_s'), cUk = col('u_kt'), cVk = col('v_kt')
  const cSet = col('set', 'set_deg', 'direction', 'dir', 'direction_deg'), cDrift = col('drift', 'drift_kt', 'speed_kt', 'rate_kt', 'rate'), cSms = col('speed_ms', 'speed_m_s'), cScm = col('speed_cms', 'speed_cm_s')
  if (cT < 0) throw new Error('No time column (time, time_utc, datetime).')
  if (cLat < 0 || cLon < 0) throw new Error('No position columns (lat, lon).')
  const vector = cU >= 0 && cV >= 0 ? 'uv' : cUk >= 0 && cVk >= 0 ? 'uvkt' : cSet >= 0 && (cDrift >= 0 || cSms >= 0 || cScm >= 0) ? 'setdrift' : null
  if (!vector) throw new Error('No current columns: u and v (m/s), u_kt and v_kt, or set (degrees towards) with drift (kt), speed_ms or speed_cms.')
  const stations = new Map()
  lines.slice(1).forEach((line, r) => {
    const f = line.split(sep).map(x => x.trim().replace(/^"|"$/g, ''))
    const num = k => { const v = Number(f[k]); if (!Number.isFinite(v)) throw new Error(`Row ${r + 2}: "${f[k]}" is not a number (column ${head[k]}).`); return v }
    const lat = num(cLat), lon = num(cLon)
    if (Math.abs(lat) > 90 || Math.abs(lon) > 180) throw new Error(`Row ${r + 2}: position out of range.`)
    let u, v
    if (vector === 'uv') { u = num(cU); v = num(cV) } else if (vector === 'uvkt') { u = num(cUk) * MS_PER_KT; v = num(cVk) * MS_PER_KT }
    else { const speedKt = cDrift >= 0 ? num(cDrift) : cSms >= 0 ? num(cSms) * KT_PER_MS : num(cScm) / 100 * KT_PER_MS;[u, v] = fromSetDrift(num(cSet), speedKt) }
    const id = cId >= 0 && f[cId] ? f[cId] : `${lat.toFixed(4)},${lon.toFixed(4)}`
    if (!stations.has(id)) stations.set(id, { id, name: id, lat, lon, rows: [] })
    stations.get(id).rows.push([parseTime(f[cT]), u, v])
  })
  const table = seriesTable(name, 'csv', kind, [...stations.values()])
  // Comment lines may say what the table is: "# kind: tidal", "# synthetic: true", "# source: …".
  if (/kind:\s*tidal/i.test(comments)) table.kind = 'tidal'
  table.synthetic = /synthetic:\s*true/i.test(comments); table.source = comments.match(/source:\s*([^#]+)/i)?.[1]?.trim() ?? null
  return table
}
function seriesTable(name, format, kind, list, note = null) {
  const stations = list.map(s => { const rows = [...s.rows].sort((a, b) => a[0] - b[0]); return { id: s.id, name: s.name, lat: s.lat, lon: s.lon, times: rows.map(r => r[0]), u: rows.map(r => r[1]), v: rows.map(r => r[2]), speeds: s.speeds ?? null, events: s.events ?? null } })
  const all = stations.flatMap(s => s.times)
  return { name, format, kind, note, stations: stations.map(({ id, name: n, lat, lon }) => ({ id, name: n, lat, lon })), span: all.length ? [Math.min(...all), Math.max(...all)] : null,
    sample: (k, ms) => { const s = stations[k]; return s.events ? slackSample(s.events, ms) : seriesSample(s.times, s.u, s.v, ms) } }
}

// NOAA CO-OPS current predictions (public domain): the JSON of
//   https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=currents_predictions&station=…&time_zone=gmt&units=english&format=json
// { current_predictions: { units?, cp: [{ Time: "2026-06-05 00:00", Velocity_Major, meanFloodDir, meanEbbDir, Bin, Depth, Type? }] } }.
// Velocity_Major: flood positive (knots with units=english, cm/s with metric). interval=MAX_SLACK gives the slack,
// maximum flood and maximum ebb events (Type), between which the stream follows a sine curve. The response has no
// position: it is taken from the file (lat, lng at the top level, as the station metadata gives them) or given.
function parseNoaa(doc, { name = 'NOAA current predictions', position = null } = {}) {
  const cp = doc.current_predictions?.cp
  if (!Array.isArray(cp) || !cp.length) throw new Error('NOAA file without current predictions (current_predictions.cp).')
  const lat = Number(doc.lat ?? doc.latitude ?? doc.station?.lat ?? position?.lat), lon = Number(doc.lng ?? doc.lon ?? doc.longitude ?? doc.station?.lng ?? position?.lon)
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) throw new Error('The NOAA predictions carry no station position: add "lat" and "lng" to the file (from the station metadata) or enter the position.')
  const unitsText = String(doc.current_predictions.units ?? doc.units ?? 'knots').toLowerCase(), toKt = unitsText.includes('cm') ? KT_PER_MS / 100 : 1
  const events = cp.some(r => r.Type) ? cp.map(r => ({ ms: parseTime(r.Time), type: String(r.Type).toLowerCase(), kt: Math.abs(Number(r.Velocity_Major)) * toKt, setDeg: Number(r.Velocity_Major) >= 0 ? Number(r.meanFloodDir) : Number(r.meanEbbDir) })).sort((a, b) => a.ms - b.ms) : null
  const rows = cp.map(r => { const vmaj = Number(r.Velocity_Major) * toKt, dir = vmaj >= 0 ? Number(r.meanFloodDir) : Number(r.meanEbbDir), [u, v] = fromSetDrift(dir, Math.abs(vmaj)); return [parseTime(r.Time), u, v] })
  if (rows.some(r => !Number.isFinite(r[0]) || !Number.isFinite(r[1]) || !Number.isFinite(r[2]))) throw new Error('NOAA predictions with missing values (Time, Velocity_Major, meanFloodDir, meanEbbDir).')
  const id = String(doc.station?.id ?? doc.id ?? doc.stationId ?? 'NOAA')
  return seriesTable(name, 'noaa', 'tidal', [{ id, name: doc.station?.name ?? doc.name ?? id, lat, lon, rows, events }], events ? 'slack and maximum events, sine curve between them' : null)
}
// Between slack and maximum events: a sine curve (0 at slack, the maximum at flood or ebb).
function slackSample(events, ms) {
  if (ms < events[0].ms || ms > events.at(-1).ms) return null
  let k = 0; while (k < events.length - 1 && events[k + 1].ms < ms) k++
  const a = events[k], b = events[Math.min(events.length - 1, k + 1)], f = b.ms > a.ms ? (ms - a.ms) / (b.ms - a.ms) : 0
  const peak = a.type === 'slack' ? b : a, rising = a.type === 'slack'
  const kt = peak.type === 'slack' ? 0 : peak.kt * Math.sin((rising ? f : 1 - f) * Math.PI / 2)
  const [u, v] = fromSetDrift(peak.setDeg, kt)
  return { u, v }
}

// A tidal stream atlas (the hourly tables of an atlas or the "tidal diamonds" of a chart):
// { schema: "terraflux.tidal-stream/v1", name, source, reference_port: { name, spring_range_m, neap_range_m,
//   high_waters: [{ time: "2026-06-05T03:12Z", range_m: 3.4 }] },
//   diamonds: [{ id: "A", lat, lon, hours: [{ hour: -6, set_deg, spring_kt, neap_kt }, … { hour: 6, … }] }] }.
// The stream at a time: the hours from the nearest high water at the reference port (within 6.5 h), the set and rates
// interpolated between the hours, the rate between neaps and springs by the day's range (the Admiralty "computation of
// rates": rate = neap + (spring - neap) (range - neap range) / (spring range - neap range), extrapolated a little).
function parseTidalStream(doc, { name } = {}) {
  const port = doc.reference_port, hws = port?.high_waters
  if (!port || !Array.isArray(hws) || !hws.length) throw new Error('A tidal stream table needs reference_port.high_waters (the times of high water at the reference port).')
  const spring = Number(port.spring_range_m), neap = Number(port.neap_range_m)
  if (!(spring > neap && neap > 0)) throw new Error('reference_port needs spring_range_m > neap_range_m > 0.')
  if (!Array.isArray(doc.diamonds) || !doc.diamonds.length) throw new Error('No diamonds (points with hourly set and rates).')
  const highWaters = hws.map(h => ({ ms: parseTime(h.time ?? h), range: Number.isFinite(Number(h.range_m)) ? Number(h.range_m) : (spring + neap) / 2 })).sort((a, b) => a.ms - b.ms)
  const diamonds = doc.diamonds.map((d, k) => {
    const lat = Number(d.lat), lon = Number(d.lon)
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) throw new Error(`Diamond ${d.id ?? k + 1}: no position.`)
    const hours = [...(d.hours ?? [])].map(h => ({ hour: Number(h.hour), set: Number(h.set_deg), spring: Number(h.spring_kt), neap: Number(h.neap_kt) })).sort((a, b) => a.hour - b.hour)
    if (hours.length < 2 || hours.some(h => ![h.hour, h.set, h.spring, h.neap].every(Number.isFinite))) throw new Error(`Diamond ${d.id ?? k + 1}: hours need hour, set_deg, spring_kt and neap_kt.`)
    return { id: String(d.id ?? String.fromCharCode(65 + k)), name: d.name ?? `◆${d.id ?? String.fromCharCode(65 + k)}`, lat, lon, hours }
  })
  const sample = (k, ms) => {
    let hw = null, best = Infinity
    for (const h of highWaters) { const dh = (ms - h.ms) / 3.6e6; if (Math.abs(dh) < Math.abs(best)) { best = dh; hw = h } }
    if (!hw || Math.abs(best) > 6.5) return null
    const hs = diamonds[k].hours, x = Math.max(hs[0].hour, Math.min(hs.at(-1).hour, best))
    let j = 0; while (j < hs.length - 2 && hs[j + 1].hour < x) j++
    const a = hs[j], b = hs[j + 1], f = (x - a.hour) / ((b.hour - a.hour) || 1)
    const set = norm360(a.set + angleDiff(b.set, a.set) * f), sp = a.spring + (b.spring - a.spring) * f, np = a.neap + (b.neap - a.neap) * f
    const factor = Math.max(-0.2, Math.min(1.2, (hw.range - neap) / (spring - neap))), kt = Math.max(0, np + (sp - np) * factor)
    const [u, v] = fromSetDrift(set, kt)
    return { u, v }
  }
  return { name: name ?? doc.name ?? 'tidal stream atlas', format: 'tidal-stream', kind: 'tidal', note: `hours from high water at ${port.name ?? 'the reference port'}`, stations: diamonds.map(({ id, name: n, lat, lon }) => ({ id, name: n, lat, lon })), span: [highWaters[0].ms - 6.5 * 3.6e6, highWaters.at(-1).ms + 6.5 * 3.6e6], sample, source: doc.source ?? null, synthetic: !!doc.synthetic }
}

// Read a file's text into a table. options: { name, kind ('total' | 'tidal', for CSV), position ({ lat, lon }, for a
// NOAA file without one) }. Throws with the reason when the file cannot be read.
export function parseCurrentTable(text, options = {}) {
  const t = String(text).trim()
  if (t.startsWith('{') || t.startsWith('[')) {
    let doc; try { doc = JSON.parse(t) } catch (err) { throw new Error(`Not valid JSON: ${err.message}`) }
    if (doc.current_predictions) return parseNoaa(doc, options)
    if (doc.diamonds || doc.schema === 'terraflux.tidal-stream/v1') return parseTidalStream(doc, options)
    throw new Error('Unknown JSON: expected NOAA current predictions (current_predictions.cp) or a tidal stream table (diamonds).')
  }
  return parseCsv(t, options)
}

// ---- The current field ----------------------------------------------------------------------------------------------
// The wind drift of every frame: the 10 m wind relaxed towards over tauHours (a first-order lag through the frames, from
// rest), times the factor, turned to the right (NH) or left (SH). { u, v } per frame (Float32Array per cell).
export function driftFields(frames, { sea = null, drift = DRIFT } = {}) {
  const g = frames[0].forecast.grid, n = g.nx * g.ny, turn = (g.geographic_anchor_latitude_deg ?? 1) >= 0 ? -drift.deflectionDeg : drift.deflectionDeg
  const c = Math.cos(turn * RAD), s = Math.sin(turn * RAD)
  // The lag integrates the wind over each interval (its mean of the two frames); the first frame starts at its wind.
  const lu = new Float64Array(n), lv = new Float64Array(n), pu = new Float64Array(n), pv = new Float64Array(n)
  let prev = null
  return frames.map(f => {
    const t = validTime(f), first = prev === null, a = first ? 1 : 1 - Math.exp(-(t - prev) / (drift.tauHours * 3.6e6)); prev = t
    const u = new Float32Array(n), v = new Float32Array(n)
    for (let i = 0; i < n; i++) {
      const w = surfaceWind(f, i), mu = first ? w.u : (pu[i] + w.u) / 2, mv = first ? w.v : (pv[i] + w.v) / 2
      pu[i] = w.u; pv[i] = w.v; lu[i] += (mu - lu[i]) * a; lv[i] += (mv - lv[i]) * a
      if (sea && !sea[i]) continue
      // Rotate by `turn` (negative: clockwise, to the right of the wind) and scale.
      u[i] = drift.factor * (lu[i] * c - lv[i] * s); v[i] = drift.factor * (lu[i] * s + lv[i] * c)
    }
    return { u, v }
  })
}

// The current model of a run: currents at any place and time, and a field per frame for the map.
// frames: the run; sea: land/sea mask; tables: parsed tables; settings: { drift, tables, radiusKm }.
export function currentModel(frames, { sea = null, tables = [], settings = {} } = {}) {
  const opts = { ...DEFAULT_CURRENT_SETTINGS, ...settings }, g = frames[0].forecast.grid, n = g.nx * g.ny, times = frames.map(validTime)
  const drifts = opts.drift ? driftFields(frames, { sea }) : null
  const R = opts.radiusKm * 1000
  // Table stations in grid metres.
  const points = opts.tables ? tables.flatMap((t, ti) => t.stations.map((s, k) => { const q = project(g, s); return { ti, k, east: q.east, north: q.north } })) : []
  const bracket = ms => { if (ms <= times[0]) return [0, 0, 0]; if (ms >= times.at(-1)) return [times.length - 1, times.length - 1, 0]; let k = 0; while (times[k + 1] < ms) k++; return [k, k + 1, (ms - times[k]) / (times[k + 1] - times[k])] }
  const cellAt = (east, north) => { const cx = Math.floor((east - g.origin_east_m) / g.spacing_east_m), cy = Math.floor((north - g.origin_north_m) / g.spacing_north_m); return cx >= 0 && cy >= 0 && cx < g.nx && cy < g.ny ? cy * g.nx + cx : -1 }
  // Tables at a point: inverse-distance weighting of the stations within R that have a value then; the coverage w
  // (1 at a station, 0 at R) says how much the table replaces the rest.
  function tableAt(east, north, ms) {
    let su = 0, sv = 0, sw = 0, cover = 0, kind = 'tidal', names = new Set()
    for (const p of points) {
      const d = Math.hypot(east - p.east, north - p.north); if (d > R) continue
      const val = tables[p.ti].sample(p.k, ms); if (!val) continue
      const w = 1 / Math.max(d, 200) ** 2; su += val.u * w; sv += val.v * w; sw += w; cover = Math.max(cover, 1 - d / R)
      if (tables[p.ti].kind === 'total') kind = 'total'; names.add(tables[p.ti].name)
    }
    return sw > 0 ? { u: su / sw, v: sv / sw, cover, kind, names: [...names] } : null
  }
  const fromFrames = (arrays, i, a, b, f) => ({ u: arrays(a).u[i] * (1 - f) + arrays(b).u[i] * f, v: arrays(a).v[i] * (1 - f) + arrays(b).v[i] * f })
  // The current at a place and time: { u, v, source: 'ocean' | 'table' | 'drift', parts, names } or null.
  function at(east, north, ms) {
    const i = cellAt(east, north); if (i < 0 || (sea && !sea[i])) return null
    const [a, b, f] = bracket(ms)
    const oa = frames[a].ocean, ob = frames[b].ocean
    if (oa?.mask?.[i] && ob?.mask?.[i] && Number.isFinite(oa.u?.[i])) {
      const o = fromFrames(k => frames[k].ocean, i, a, b, f); return { ...o, source: 'ocean', parts: { ocean: o } }
    }
    const d = drifts ? fromFrames(k => drifts[k], i, a, b, f) : null, tb = points.length ? tableAt(east, north, ms) : null
    if (tb) {
      const rest = tb.kind === 'total' ? (d ? { u: d.u * (1 - tb.cover), v: d.v * (1 - tb.cover) } : { u: 0, v: 0 }) : (d ?? { u: 0, v: 0 })
      const base = tb.kind === 'total' ? tb.cover : 1
      return { u: tb.u * base + rest.u, v: tb.v * base + rest.v, source: 'table', parts: { table: { u: tb.u, v: tb.v, cover: tb.cover, kind: tb.kind }, drift: d }, names: tb.names }
    }
    return d ? { ...d, source: 'drift', parts: { drift: d } } : null
  }
  // The field of a frame (its valid time) for the map and the reports: per cell u, v and the source (0 none, 1 ocean,
  // 2 table, 3 drift).
  const cache = new Map()
  function field(k) {
    if (cache.has(k)) return cache.get(k)
    const u = new Float32Array(n), v = new Float32Array(n), source = new Uint8Array(n), ms = times[k]
    for (let i = 0; i < n; i++) { const c = cellLocation(g, i), x = at(c.east, c.north, ms); if (!x) continue; u[i] = x.u; v[i] = x.v; source[i] = x.source === 'ocean' ? 1 : x.source === 'table' ? 2 : 3 }
    const out = { u, v, source }; cache.set(k, out); return out
  }
  // A reading for the reports: set (towards, degrees in the grid frame) and drift (kt), with its source in words.
  function reading(i, ms) {
    const c = cellLocation(g, i), x = at(c.east, c.north, ms); if (!x) return null
    const speed = Math.hypot(x.u, x.v)
    return { u: x.u, v: x.v, setDeg: setOf(x.u, x.v), driftKt: speed * KT_PER_MS, source: x.source, sourceLabel: x.source === 'table' ? `${x.names.join(', ')}${x.parts.table.kind === 'tidal' && x.parts.drift ? ' + wind drift' : ''}` : SOURCES[x.source].label }
  }
  return { at, field, reading, stations: points.map(p => ({ ...tables[p.ti].stations[p.k], table: tables[p.ti].name, kind: tables[p.ti].kind, east: p.east, north: p.north })), hasDrift: !!drifts, tableCount: tables.length }
}

// How well a table covers a run: its stations inside the grid and its time span against the run's.
export function tableCoverage(table, frames) {
  const g = frames[0].forecast.grid, inside = table.stations.filter(s => { const q = project(g, s), cx = (q.east - g.origin_east_m) / g.spacing_east_m, cy = (q.north - g.origin_north_m) / g.spacing_north_m; return cx >= 0 && cy >= 0 && cx < g.nx && cy < g.ny }).length
  const t0 = validTime(frames[0]), t1 = validTime(frames.at(-1)), span = table.span
  const overlapH = span ? Math.max(0, Math.min(t1, span[1]) - Math.max(t0, span[0])) / 3.6e6 : 0
  return { inside, total: table.stations.length, overlapHours: overlapH, runHours: (t1 - t0) / 3.6e6 }
}
