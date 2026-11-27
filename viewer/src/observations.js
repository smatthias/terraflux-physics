// Canonical radar observations: retain times and quality; never write into simulation fields.
export function parseObservations(records) {
  if (!Array.isArray(records) || !records.length || records.length > 100000) throw new Error('Expected 1–100,000 canonical observation records')
  const frames = new Map(), seen = new Set()
  let sourceKey = null, source = null
  for (const r of records) {
    if (r?.schema_version !== 'terraflux.weather.observation/v1' || r.variable !== 'radar_reflectivity' || r.units !== 'dBZ') throw new Error('Expected radar_reflectivity observations in dBZ')
    const s = r.source
    if (!s || typeof s.source_id !== 'string' || !s.source_id.trim() || !['public_dataset', 'synthetic_fixture', 'user_supplied', 'licensed_feed'].includes(s.source_kind) || typeof s.interpretation !== 'string' || !s.interpretation || typeof r.vertical_reference !== 'string' || !r.vertical_reference) throw new Error('Observation source and vertical reference are required')
    const key = JSON.stringify([s.source_id, s.source_kind, s.product_id ?? '', s.interpretation, r.vertical_reference])
    if (sourceKey && sourceKey !== key) throw new Error('Load one source, product and vertical reference at a time')
    sourceKey = key; source = { ...s, vertical_reference: r.vertical_reference }
    const time = r.valid_time_utc
    const ms = typeof time === 'string' ? Date.parse(time) : NaN
    if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(time) || !Number.isFinite(ms) || new Date(ms).toISOString() !== time.replace('Z', '.000Z')) throw new Error('Valid time must be a real UTC timestamp (YYYY-MM-DDTHH:mm:ssZ)')
    const xy = r.location?.coordinates
    if (r.location?.crs !== 'EPSG:4326' || !Array.isArray(xy) || xy.length !== 2 || !xy.every(Number.isFinite) || Math.abs(xy[0]) > 180 || Math.abs(xy[1]) > 90) throw new Error('Expected valid EPSG:4326 longitude/latitude')
    const status = r.quality?.status
    if (!['decoded', 'no_data', 'ambiguous'].includes(status) || (status === 'decoded' ? !Number.isFinite(r.value) : r.value !== null)) throw new Error('Quality and value disagree; missing/ambiguous observations must be null')
    const id = JSON.stringify([ms, ...xy])
    if (seen.has(id)) throw new Error('Duplicate observation location at one valid time')
    seen.add(id)
    if (!frames.has(ms)) frames.set(ms, { ms, time, points: [] })
    frames.get(ms).points.push({ lon: xy[0], lat: xy[1], value: r.value, status, reason: r.quality.reason ?? '' })
  }
  return { source, frames: [...frames.values()].sort((a, b) => a.ms - b.ms), count: records.length }
}

export function observationSummary(frame) {
  const counts = { decoded: 0, no_data: 0, ambiguous: 0 }
  let min = Infinity, max = -Infinity
  for (const p of frame.points) {
    counts[p.status]++
    if (p.status === 'decoded') { min = Math.min(min, p.value); max = Math.max(max, p.value) }
  }
  return { ...counts, total: frame.points.length, min: counts.decoded ? min : null, max: counts.decoded ? max : null }
}

export function observationAge(ms, now = Date.now()) {
  const minutes = (now - ms) / 60000
  return { minutes, label: minutes < -1 ? 'Future valid time' : minutes > 15 ? 'Older than 15 minutes' : 'Within 15 minutes' }
}

// Longitude/latitude scatter preview, no interpolation and no invented coverage between points.
export function observationPlot(frame, limit = 5000) {
  let west = Infinity, east = -Infinity, south = Infinity, north = -Infinity
  for (const p of frame.points) { west = Math.min(west, p.lon); east = Math.max(east, p.lon); south = Math.min(south, p.lat); north = Math.max(north, p.lat) }
  const dx = east - west || 0.01, dy = north - south || 0.01
  const stride = Math.max(1, Math.ceil(frame.points.length / limit))
  const points = frame.points.filter((_, i) => i % stride === 0).map(p => ({ ...p, x: 20 + (p.lon - west) / dx * 960, y: 480 - (p.lat - south) / dy * 460 }))
  return { west, east, south, north, points, sampled: points.length < frame.points.length }
}
export function reflectivityColour(value) {
  if (value === null) return '#6e8490'
  return value < 0 ? '#426984' : value < 20 ? '#4fa6ca' : value < 35 ? '#50bf7a' : value < 50 ? '#e9c14d' : value < 65 ? '#ec7656' : '#d876cf'
}
