// The data stream of the fullscreen scene: what is being rendered right now (dataset, view, layer, frame, overlays), the
// statistics of the rendered field, which data blocks of the frame feed it, and an event log with one line per scene
// rebuild that says what changed. Pure functions; the component and the app only wire them up.

export const VIEW_LABELS = {
  field: 'Field explorer', map: 'Weather map', severe: 'Storm watch', water: 'Catchment', hazards: 'Hazards', ocean: 'Ocean streams', polar: 'Polar melt',
  coast: 'Sea level & coasts', carbon: 'Carbon & ocean life', solar: 'Sun & solar cycle', dust: 'Cosmic dust', solarwind: 'Solar wind & poles',
}

// Readable names of the frame's data blocks (the keys validateSequence attaches to a frame).
export const CHANNEL_LABELS = {
  forecast: 'Forecast grid', rain: 'Radar rainfall', terrain: 'Terrain', terrainDerivatives: 'Slope & aspect', terrainTemperature: 'Lapse-rate temperature',
  atmosphere: 'Regional atmosphere', surfaceWater: 'Surface water', coupled: 'Coupled atmosphere', clouds: 'Cloud formation', precipitation: 'Precipitation',
  storms: 'Tropical cyclones', severe: 'Severe convection', surface: 'Land / sea surface', orography: 'Orography', hydrology: 'Catchment hydrology',
  ocean: 'Ocean circulation', polar: 'Polar melt', coast: 'Coastal flooding', carbon: 'Ocean carbon', solar: 'Solar cycle', dust: 'Cosmic dust', solarWind: 'Solar wind',
}

export function channelLabel(key) {
  if (CHANNEL_LABELS[key]) return CHANNEL_LABELS[key]
  const words = String(key).replace(/([a-z0-9])([A-Z])/g, '$1 $2').replace(/[_-]+/g, ' ').trim().toLowerCase()
  return words.charAt(0).toUpperCase() + words.slice(1)
}

const isBlock = v => v !== null && v !== undefined && typeof v === 'object'

// The data blocks of a frame. A block is "active" when the selected layer cannot be drawn without it: the layer's own
// `needs` test fails once the block is removed. The forecast grid carries the base fields and is always marked as base.
export function frameChannels(frame, layerDef) {
  if (!frame) return []
  const keys = Object.keys(frame).filter(k => isBlock(frame[k]) && !k.startsWith('_'))
  const needs = typeof layerDef?.needs === 'function' ? layerDef.needs : null
  const usable = needs ? safeNeeds(needs, frame) : false
  return keys.map(key => {
    let active = false
    if (needs && usable) {
      const without = { ...frame, [key]: undefined }
      active = !safeNeeds(needs, without)
    }
    return { key, label: channelLabel(key), active, base: key === 'forecast' }
  })
}

function safeNeeds(needs, frame) {
  try { return !!needs(frame) } catch { return false }
}

// Count, valid cells, range and mean of a field with missing values (null, NaN).
export function fieldStats(values) {
  const list = values ?? []
  let valid = 0, sum = 0, min = Infinity, max = -Infinity
  for (const v of list) {
    if (v === null || v === undefined || !Number.isFinite(v)) continue
    valid++; sum += v
    if (v < min) min = v
    if (v > max) max = v
  }
  return valid ? { cells: list.length, valid, min, max, mean: sum / valid } : { cells: list.length, valid: 0, min: null, max: null, mean: null }
}

// Distribution of the valid values over [lo, hi] in `bins` bins, as fractions of the fullest bin (0..1). Values outside
// the range go to the end bins, so that a fixed colour scale still shows everything.
export function histogram(values, lo, hi, bins = 24) {
  const counts = new Array(bins).fill(0)
  if (!(bins > 0)) return counts
  const span = hi - lo
  for (const v of values ?? []) {
    if (v === null || v === undefined || !Number.isFinite(v)) continue
    const k = span > 0 ? Math.floor((v - lo) / span * bins) : Math.floor(bins / 2)
    counts[Math.min(bins - 1, Math.max(0, k))]++
  }
  const top = Math.max(...counts)
  return top > 0 ? counts.map(c => c / top) : counts
}

// The values of one grid row (row 0 is the southern edge), missing values as null.
export function rowSignal(values, nx, row) {
  if (!(nx > 0)) return []
  const start = row * nx
  return Array.from({ length: nx }, (_, x) => {
    const v = values?.[start + x]
    return v === null || v === undefined || !Number.isFinite(v) ? null : v
  })
}

// SVG polyline segments of a signal in a width x height box, broken at missing values.
export function signalPaths(signal, lo, hi, width = 100, height = 40, pad = 3) {
  const n = signal.length, span = hi - lo, paths = []
  let current = []
  signal.forEach((v, i) => {
    if (v === null) { if (current.length) paths.push(current); current = []; return }
    const x = n > 1 ? i / (n - 1) * width : width / 2
    const u = span > 0 ? (v - lo) / span : 0.5
    const y = height - pad - Math.min(1, Math.max(0, u)) * (height - 2 * pad)
    current.push(`${x.toFixed(2)},${y.toFixed(2)}`)
  })
  if (current.length) paths.push(current)
  return paths.map(p => (p.length === 1 ? [p[0], p[0]] : p).join(' '))
}

// The legend ramp of the field explorer (blue, teal, green, orange) as RGB for a position 0..1.
const RAMP = [[0x28, 0x68, 0xcc], [0x26, 0xb1, 0xb4], [0xa3, 0xce, 0x30], [0xd0, 0x5b, 0x26]]
export function rampRgb(t) {
  const u = Math.min(1, Math.max(0, Number.isFinite(t) ? t : 0)) * (RAMP.length - 1)
  const k = Math.min(RAMP.length - 2, Math.floor(u)), f = u - k
  return RAMP[k].map((c, j) => Math.round(c + (RAMP[k + 1][j] - c) * f))
}

// Short labels of the overlays switched on in a view: true flags by name, choices as "name: value".
const OVERLAY_NAMES = {
  isobarIntervalHpa: 'isobar hPa', coldPools: 'cold pools', oroPrecip: 'orographic rain', landSea: 'land/sea', stormWind: 'storm wind', seaIce: 'sea ice',
  iceSheet: 'ice sheet', iceDrift: 'ice drift', sunwind: 'solar wind', vectors: 'wind vectors', frontOverlay: 'fronts', cloudVolumes: 'cloud volumes',
}
export function overlayChips(settings) {
  if (!settings) return []
  const chips = []
  for (const [key, value] of Object.entries(settings)) {
    const name = OVERLAY_NAMES[key] ?? channelLabel(key).toLowerCase()
    if (value === true) chips.push(name)
    else if (typeof value === 'string' && value !== '') chips.push(`${name}: ${value}`)
    else if (typeof value === 'number' && Number.isFinite(value)) chips.push(`${name}: ${value}`)
  }
  return chips
}

// Compact number for the stream: 3–4 significant figures, thousands separators for large values.
export function formatSignal(v, digits = 3) {
  if (v === null || v === undefined || !Number.isFinite(v)) return '—'
  const a = Math.abs(v)
  if (a === 0) return '0'
  if (a >= 1e6 || a < 1e-3) return v.toExponential(Math.max(0, digits - 1)).replace('e+', 'e')
  if (a >= 1000) return Math.round(v).toLocaleString('en-US')
  return Number(v.toPrecision(digits)).toString()
}

export function clockLabel(date = new Date()) {
  const p = (v, n = 2) => String(v).padStart(n, '0')
  return `${p(date.getHours())}:${p(date.getMinutes())}:${p(date.getSeconds())}.${p(date.getMilliseconds(), 3)}`
}

// What changed between two render snapshots, as log entries { tag, text }. The first snapshot starts the stream; a
// rebuild without a visible difference is still reported, so every scene rebuild shows up in the log.
export function describeChanges(prev, next) {
  if (!next) return []
  const gridLabel = s => `${s.nx}×${s.ny}`
  if (!prev) return [{ tag: 'INIT', text: `scene online · ${next.viewLabel} · ${next.layerTitle} · ${gridLabel(next)} cells` }]
  const out = []
  if (prev.source !== next.source || prev.frames !== next.frames || prev.nx !== next.nx || prev.ny !== next.ny) {
    out.push({ tag: 'DATA', text: `${next.source} · ${next.frames} frame${next.frames === 1 ? '' : 's'} · ${gridLabel(next)} cells` })
  }
  if (prev.variant !== next.variant) out.push({ tag: 'DATA', text: `showing ${next.variant}` })
  if (prev.view !== next.view) out.push({ tag: 'VIEW', text: `${prev.viewLabel} → ${next.viewLabel}` })
  if (prev.layer !== next.layer) out.push({ tag: 'LAYER', text: `${next.layerTitle}${next.unit ? ` [${next.unit}]` : ''}` })
  if (prev.frame !== next.frame && prev.frames === next.frames) out.push({ tag: 'FRAME', text: `${next.frame + 1}/${next.frames} · ${next.frameLabel}` })
  const before = new Set(prev.overlays), after = new Set(next.overlays)
  const added = next.overlays.filter(o => !before.has(o)), removed = prev.overlays.filter(o => !after.has(o))
  if (added.length || removed.length) out.push({ tag: 'OVERLAY', text: [...added.map(o => `+${o}`), ...removed.map(o => `−${o}`)].join(' ') })
  if (prev.selected !== next.selected) {
    out.push(next.selected === null
      ? { tag: 'PROBE', text: 'cell probe cleared' }
      : { tag: 'PROBE', text: `cell ${next.selectedLabel} → ${formatSignal(next.selectedValue, 4)}${next.unit ? ` ${next.unit}` : ''}` })
  }
  if (!out.length) out.push({ tag: 'RENDER', text: 'scene rebuilt' })
  return out
}

// Newest first, capped; every entry gets an id (for keyed rendering) and a clock label.
export function appendEntries(log, entries, { limit = 80, now = new Date(), startId = 0 } = {}) {
  if (!entries.length) return log
  let id = log.length ? log[0].id : startId
  const stamped = entries.map(e => ({ ...e, id: ++id, time: clockLabel(now) })).reverse()
  return [...stamped, ...log].slice(0, limit)
}
