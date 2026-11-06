// Solar cycle support: validation of the documented terraflux.solar.cycle/v1 block (the monthly sunspot record,
// its minima and maxima, the backtest, the ensemble forecast, the irradiance, UV, flare and climate series, the
// butterfly diagram and the solar-disk frames), metrics of a frame, chart and table data, colours and the disk's
// geometry. No physics runs in the browser: every value comes from the engine output (ratios of engine numbers at
// most, such as the R-scale shares of the flare size distribution).
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const SOLAR_SCHEMA = 'terraflux.solar.cycle/v1'
export const SOLAR_METHOD = 'hathaway_cycles_onset_prior_fit_ar1_ensemble_proxy_irradiance'
export const SOLAR_BASES = [
  { key: 'brightness', label: 'Brightness temperature (true colour of the photosphere)' },
  { key: 'contrast', label: 'Contrast to the quiet Sun (spots dark, faculae bright)' },
  { key: 'cycle', label: 'Spot groups by cycle (old cycle blue, new cycle orange)' },
]
export const R_SCALE = [
  { level: 1, label: 'R1 minor', threshold: 'M1', key: 'm1' },
  { level: 2, label: 'R2 moderate', threshold: 'M5', key: 'm5' },
  { level: 3, label: 'R3 strong', threshold: 'X1', key: 'x1' },
  { level: 4, label: 'R4 severe', threshold: 'X10', key: 'x10' },
  { level: 5, label: 'R5 extreme', threshold: 'X20', key: 'x20' },
]
export const R_COLORS = ['#9aa5ab', '#ffe066', '#ffb347', '#ff7b4a', '#ff3b30', '#c724b1']

export const monthIndex = label => { const [y, m] = String(label).split('-').map(Number); return y * 12 + m - 1 }
export const monthYear = index => (index + 0.5) / 12
export const monthLabel = index => `${Math.floor(index / 12)}-${String(index % 12 + 1).padStart(2, '0')}`

const series = (v, n) => Array.isArray(v) && v.length === n && v.every(finite)
const nullable = (v, n) => Array.isArray(v) && v.length === n && v.every(x => x === null || finite(x))
const triple = (t, n) => t && ['p5', 'p50', 'p95'].every(k => series(t[k], n))

export function validateSolar(block, frames) {
  const b = block, grid = frames[0].forecast.grid
  check(b?.schema_version === SOLAR_SCHEMA && b.method === SOLAR_METHOD, 'Unsupported solar contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind) && ['cycle_table', 'monthly'].includes(b.record_kind) && typeof b.data_origin === 'string', 'Solar source must be engine_output or synthetic, with a record kind and a data origin.')
  const r = b.record, n = r?.months
  check(Number.isInteger(n) && n >= 480 && /^\d{4}-\d{2}$/.test(r.first_month), 'The solar record needs at least 40 years of months.')
  for (const key of ['sunspot_number', 'model', 'f107_sfu', 'tsi_w_m2', 'facular_index', 'ozone_change_pct', 'stratosphere_k', 'uv_index_change_pct', 'forcing_w_m2', 'surface_k']) check(series(r[key], n), `Invalid solar record ${key}.`)
  check(nullable(r.smoothed, n) && r.sunspot_number.every(v => v >= 0), 'Invalid sunspot record.')
  check(r.flares && ['m1_expected', 'x1_expected', 'm1_count', 'x1_count'].every(k => series(r.flares[k], n)) && nullable(r.flares.largest_w_m2, n), 'Invalid flare record.')
  const bands = b.irradiance?.bands
  check(Array.isArray(bands) && bands.length >= 1 && bands.every(x => typeof x.key === 'string' && finite(x.from_nm) && finite(x.variability) && series(r.uv_change_pct?.[x.key], n)), 'Invalid UV bands.')
  check(Array.isArray(b.cycles) && b.cycles.length >= 2 && b.cycles.every((c, k) => Number.isInteger(c.number) && finite(c.minimum_year) && (k === 0 || c.number === b.cycles[k - 1].number + 1)), 'Invalid solar cycles.')
  const f = b.forecast, m = f?.months
  check(Number.isInteger(m) && m >= 12 && monthIndex(f.first_month) === monthIndex(r.first_month) + n, 'The solar forecast must start where the record ends.')
  check(['p5', 'p25', 'p50', 'p75', 'p95', 'central'].every(k => series(f.sunspot?.[k], m)), 'Invalid sunspot forecast.')
  for (let k = 0; k < m; k++) check(f.sunspot.p5[k] <= f.sunspot.p25[k] && f.sunspot.p25[k] <= f.sunspot.p50[k] && f.sunspot.p50[k] <= f.sunspot.p75[k] && f.sunspot.p75[k] <= f.sunspot.p95[k], 'Sunspot forecast percentiles must be ordered.')
  check(['f107_sfu', 'tsi_w_m2', 'ozone_change_pct', 'stratosphere_k', 'uv_index_change_pct', 'surface_k'].every(k => triple(f[k], m)) && bands.every(x => triple(f.uv_change_pct?.[x.key], m)), 'Invalid irradiance forecast.')
  check(f.flares && ['m1_expected', 'x1_expected', 'x1_probability'].every(k => triple(f.flares[k], m)), 'Invalid flare forecast.')
  check(Array.isArray(f.turning_points) && f.turning_points.every(t => ['minimum', 'maximum'].includes(t.kind) && Number.isInteger(t.cycle)), 'Invalid turning points.')
  check(Array.isArray(b.backtest?.summary) && Array.isArray(b.backtest.entries), 'Invalid backtest.')
  const bf = b.butterfly
  check(bf && series(bf.years, bf.years.length) && series(bf.latitudes_deg, bf.latitudes_deg.length) && Array.isArray(bf.density_uhem_per_deg) && bf.density_uhem_per_deg.length === bf.years.length &&
    bf.density_uhem_per_deg.every(row => series(row, bf.latitudes_deg.length) && row.every(v => v >= 0)), 'Invalid butterfly diagram.')
  const d = b.disk, count = grid.nx * grid.ny
  check(d && d.cells === grid.nx && d.cells === grid.ny && nullable(d.quiet_brightness_k, count) && Array.isArray(d.frames) && d.frames.length === frames.length, 'Solar disk frames must match the timeline grid and frames.')
  check(Array.isArray(b.flares?.rates) && b.flares.rates.length > 0 && b.flares.rates.every(x => finite(x.x1_per_year) && x.x1_per_year > 0), 'Invalid flare rates.')
  if (b.long_term !== undefined) validateLongTerm(b.long_term)
  frames.forEach((frame, k) => {
    const x = d.frames[k]
    check(x.elapsed_seconds === frame.forecast.elapsed_seconds && /^\d{4}-\d{2}$/.test(x.month) && ['record', 'forecast'].includes(x.source), 'Solar disk frame times must match the timeline.')
    check(nullable(x.brightness_k, count) && x.brightness_k.every((v, i) => (v === null) === (d.quiet_brightness_k[i] === null)), 'Solar disk maps must match the grid and the disk.')
    check(Array.isArray(x.regions) && x.regions.every(g => finite(g.latitude_deg) && Math.abs(g.latitude_deg) <= 90 && finite(g.longitude_deg) && finite(g.area_uhem) && g.area_uhem > 0), 'Invalid active regions.')
    check(Array.isArray(x.flares) && x.flares.every(fl => finite(fl.peak_flux_w_m2) && Number.isInteger(fl.r_scale) && fl.region >= -1 && fl.region < x.regions.length), 'Invalid disk flares.')
    frame.solar = {
      index: k, block: b, disk: x, month: monthIndex(x.month), year: x.year, source: x.source, sunspot: x.sunspot_number, smoothed: x.smoothed,
      brightness: x.brightness_k, quiet: d.quiet_brightness_k, regions: x.regions, flares: x.flares, synthetic: b.source_kind === 'synthetic',
    }
  })
  return b
}

// Index of a month in the record (or -1) and in the forecast (or -1).
export function locate(block, month) {
  const r0 = monthIndex(block.record.first_month), f0 = monthIndex(block.forecast.first_month)
  if (month >= r0 && month < r0 + block.record.months) return { kind: 'record', k: month - r0 }
  if (month >= f0 && month < f0 + block.forecast.months) return { kind: 'forecast', k: month - f0 }
  return null
}

// The cycle a date belongs to and how far into it: the last observed minimum before it, or a predicted one.
export function cycleAt(block, year) {
  let cycle = block.cycles[0].number, start = block.cycles[0].minimum_year
  for (const c of block.cycles) if (c.minimum_year <= year) { cycle = c.number; start = c.minimum_year }
  for (const t of block.forecast.turning_points) if (t.kind === 'minimum' && t.year_median !== null && t.year_median <= year && t.cycle > cycle) { cycle = t.cycle; start = t.year_median }
  return { cycle, phase_years: year - start }
}

// Ratios of the R-scale thresholds to X1 in the engine's flare size distribution (the same at every sunspot number).
export function rScaleShares(block) {
  const x = block.flares.rates.find(v => v.x1_per_year > 0)
  return { m1: x.m1_per_year / x.x1_per_year, m5: x.m5_per_year / x.x1_per_year, x1: 1, x10: x.x10_per_year / x.x1_per_year, x20: x.x20_per_year / x.x1_per_year }
}

// Metrics of the frame's month: from the record before the issue date, the forecast (median and 90 % range) after.
export function summarizeSolar(frame) {
  const s = frame.solar, b = s.block, at = locate(b, s.month)
  // Bands by their upper wavelength: Lyman-alpha, 200-242 nm, UV-C, UV-B, UV-A (nested bands end where they end).
  const rec = b.record, fc = b.forecast, bands = [...b.irradiance.bands].sort((a, c) => a.to_nm - c.to_nm || a.from_nm - c.from_nm)
  const quietTsi = b.irradiance.proxies.tsi_quiet_w_m2
  const pick = (recordSeries, forecastTriple) => at.kind === 'record'
    ? { value: recordSeries[at.k], p5: null, p95: null }
    : { value: forecastTriple.p50[at.k], p5: forecastTriple.p5[at.k], p95: forecastTriple.p95[at.k] }
  const sunspot = at.kind === 'record' ? { value: rec.sunspot_number[at.k], p5: null, p95: null } : { value: fc.sunspot.p50[at.k], p5: fc.sunspot.p5[at.k], p95: fc.sunspot.p95[at.k] }
  const smoothed = at.kind === 'record' ? rec.smoothed[at.k] : fc.sunspot.p50[at.k]
  // Rising or falling: the smoothed (record) or median (forecast) value half a year later against half a year earlier.
  const smooth = month => { const x = locate(b, month); if (!x) return null; return x.kind === 'record' ? rec.smoothed[x.k] : fc.sunspot.p50[x.k] }
  const before = smooth(s.month - 6), after = smooth(s.month + 6)
  const trend = before === null || after === null ? 'unknown' : after - before > 8 ? 'rising' : before - after > 8 ? 'declining' : (smoothed ?? sunspot.value) < 40 ? 'near minimum' : 'near maximum'
  const tsi = pick(rec.tsi_w_m2, fc.tsi_w_m2), f107 = pick(rec.f107_sfu, fc.f107_sfu)
  const x1 = pick(rec.flares.x1_expected, fc.flares.x1_expected), m1 = pick(rec.flares.m1_expected, fc.flares.m1_expected)
  const shares = rScaleShares(b)
  const where = cycleAt(b, s.year)
  return {
    source: at.kind, month: monthLabel(s.month), year: s.year, ...where, trend,
    sunspot: sunspot.value, sunspot_p5: sunspot.p5, sunspot_p95: sunspot.p95, smoothed,
    f107_sfu: f107.value, f107_p5: f107.p5, f107_p95: f107.p95,
    tsi_w_m2: tsi.value, tsi_p5: tsi.p5, tsi_p95: tsi.p95, tsi_change_w_m2: tsi.value - quietTsi,
    uv: bands.map(x => ({ key: x.key, label: x.label, from_nm: x.from_nm, to_nm: x.to_nm, ...((v) => ({ change_pct: v.value, p5: v.p5, p95: v.p95 }))(pick(rec.uv_change_pct[x.key], fc.uv_change_pct[x.key])), reference_pct: 100 * x.variability })),
    ozone_pct: pick(rec.ozone_change_pct, fc.ozone_change_pct).value, stratosphere_k: pick(rec.stratosphere_k, fc.stratosphere_k).value,
    uv_index_change_pct: pick(rec.uv_index_change_pct, fc.uv_index_change_pct).value,
    surface_k: pick(rec.surface_k, fc.surface_k).value, surface_p5: pick(rec.surface_k, fc.surface_k).p5, surface_p95: pick(rec.surface_k, fc.surface_k).p95,
    flares: {
      m1: m1.value, x1: x1.value, x1_p5: x1.p5, x1_p95: x1.p95, p_x1: 1 - Math.exp(-x1.value),
      r_scale: R_SCALE.map(l => ({ ...l, expected: x1.value * shares[l.key], probability: 1 - Math.exp(-x1.value * shares[l.key]) })),
      m1_count: at.kind === 'record' ? rec.flares.m1_count[at.k] : null, x1_count: at.kind === 'record' ? rec.flares.x1_count[at.k] : null,
      largest_w_m2: at.kind === 'record' ? rec.flares.largest_w_m2[at.k] : null,
    },
    disk: { spot_ppm: s.disk.spot_ppm, facular_ppm: s.disk.facular_ppm, net_ppm: s.disk.net_ppm, regions: s.regions.length, flares: s.flares.length, biggest_uhem: Math.max(0, ...s.regions.map(g => g.area_uhem)) },
    issue_year: fc.issue_year,
  }
}

// Chart windows over the record and the forecast.
export function chartWindow(block, key) {
  const start = monthYear(monthIndex(block.record.first_month)), end = monthYear(monthIndex(block.forecast.first_month) + block.forecast.months - 1)
  if (key === 'modern') return [Math.max(start, 1950), end]
  if (key === 'focus') return [Math.max(start, block.forecast.issue_year - 22), end]
  return [start, end]
}

// [[year, value], ...] of a monthly series starting at a month label, inside [x0, x1] (nulls dropped).
export function monthlyPoints(firstMonth, values, x0 = -Infinity, x1 = Infinity, stride = 1) {
  const m0 = monthIndex(firstMonth), out = []
  for (let k = 0; k < values.length; k += stride) {
    const y = monthYear(m0 + k)
    if (y >= x0 && y <= x1 && values[k] !== null) out.push([y, values[k]])
  }
  return out
}

// The vertical position of a value in a w x h chart box (y up, `pad` free at top and bottom).
export const chartY = (v, [lo, hi], h = 50, pad = 3) => h - pad - (v - lo) / ((hi - lo) || 1) * (h - 2 * pad)

// Polyline points in a w x h box (y up) for [[x, y], ...].
export function polyline(points, [x0, x1], [lo, hi], w = 100, h = 50, pad = 3) {
  const X = x => (x - x0) / ((x1 - x0) || 1) * w
  return points.map(([x, v]) => `${X(x).toFixed(2)},${chartY(v, [lo, hi], h, pad).toFixed(2)}`).join(' ')
}

// A closed band between two series (same x) for an SVG polygon.
export function band(lower, upper, xr, yr, w = 100, h = 50, pad = 3) {
  return `${polyline(lower, xr, yr, w, h, pad)} ${polyline([...upper].reverse(), xr, yr, w, h, pad)}`
}

// The sunspot chart: record (thinned to the window), smoothed, the fitted model and the forecast with its bands.
export function sunspotChart(block, windowKey) {
  const [x0, x1] = chartWindow(block, windowKey), r = block.record, f = block.forecast
  const stride = x1 - x0 > 80 ? 2 : 1
  const monthly = monthlyPoints(r.first_month, r.sunspot_number, x0, x1, stride)
  const smoothed = monthlyPoints(r.first_month, r.smoothed, x0, x1)
  const model = monthlyPoints(r.first_month, r.model, x0, x1, stride)
  const q = Object.fromEntries(['p5', 'p25', 'p50', 'p75', 'p95', 'central'].map(k => [k, monthlyPoints(f.first_month, f.sunspot[k], x0, x1)]))
  const hi = Math.max(50, ...monthly.map(p => p[1]), ...q.p95.map(p => p[1])) * 1.05
  const extrema = []
  for (const c of block.cycles) {
    if (c.minimum_year >= x0 && c.minimum_year <= x1) extrema.push({ kind: 'minimum', year: c.minimum_year, value: c.minimum, cycle: c.number, predicted: false })
    if (c.maximum_year !== null && c.maximum_year >= x0 && c.maximum_year <= x1) extrema.push({ kind: 'maximum', year: c.maximum_year, value: c.maximum, cycle: c.number, predicted: false })
  }
  for (const t of f.turning_points) if (t.year_median !== null && t.year_median >= x0 && t.year_median <= x1) extrema.push({ kind: t.kind, year: t.year_median, value: t.value_median, cycle: t.cycle, predicted: true, year_p5: t.year_p5, year_p95: t.year_p95, value_p5: t.value_p5, value_p95: t.value_p95 })
  return { x: [x0, x1], y: [0, hi], monthly, smoothed, model, forecast: q, issue: f.issue_year, extrema }
}

// Observed cycles and the predicted ones, for the minima and maxima table.
export function cycleTable(block) {
  const rows = block.cycles.map(c => ({
    cycle: c.number, predicted: false, in_progress: !c.complete,
    minimum_year: c.minimum_year, minimum: c.minimum, maximum_year: c.maximum_year, maximum: c.maximum,
    length_years: c.length_years, rise_years: c.rise_years, table: c.table ?? null, fit: c.fit ?? null,
  }))
  const byCycle = new Map()
  for (const t of block.forecast.turning_points) {
    if (!byCycle.has(t.cycle)) byCycle.set(t.cycle, {})
    byCycle.get(t.cycle)[t.kind] = t
  }
  for (const [cycle, t] of byCycle) {
    const row = rows.find(r => r.cycle === cycle)
    if (row) { if (t.maximum && row.maximum === null) row.predicted_maximum = t; continue }
    rows.push({ cycle, predicted: true, in_progress: false, minimum: t.minimum ?? null, maximum: t.maximum ?? null })
  }
  return rows
}

// Per site: the noon UV index of the frame's calendar month under the quiet Sun, now, and at the reference maximum.
export function uvSites(frame) {
  const s = frame.solar, b = s.block, m = s.month % 12, change = summarizeSolar(frame).uv_index_change_pct
  return b.atmosphere.sites.map(site => {
    const quiet = site.quiet_noon_uv_index_by_month[m]
    return { name: site.name, latitude_deg: site.latitude_deg, ozone_du: site.ozone_du_by_month[m], quiet, now: quiet * (1 + change / 100), reference: site.reference_noon_uv_index_by_month[m], change_pct: change, year_max: Math.max(...site.quiet_noon_uv_index_by_month) }
  })
}

// Annual flare counts of the record (synthetic draws and expectation) and the forecast's annual outlook.
export function flareYears(block, x0 = -Infinity, x1 = Infinity) {
  const past = block.flares.record_annual.filter(y => y.year + 0.5 >= x0 && y.year + 0.5 <= x1)
  const future = block.forecast.annual.filter(y => y.year + 0.5 >= x0 && y.year + 0.5 <= x1)
  return { past, future }
}

// The butterfly diagram as RGBA pixels (time along x, latitude up), normalised to the largest density.
export function butterflyImage(block, [x0, x1], width = 400, height = 90) {
  const bf = block.butterfly, pixels = new Uint8ClampedArray(width * height * 4)
  const lats = bf.latitudes_deg, lo = lats[0], hi = lats.at(-1)
  const max = Math.max(1e-9, ...bf.density_uhem_per_deg.flat())
  for (let px = 0; px < width; px++) {
    const year = x0 + (px + 0.5) / width * (x1 - x0)
    let t = bf.years.findIndex(y => y >= year)
    if (t < 0) t = bf.years.length - 1
    for (let py = 0; py < height; py++) {
      const lat = hi - (py + 0.5) / height * (hi - lo), j = Math.min(lats.length - 1, Math.max(0, Math.round((lat - lo) / (lats[1] - lats[0]))))
      const v = year < bf.years[0] || year > bf.years.at(-1) + 0.25 ? 0 : bf.density_uhem_per_deg[t][j] / max
      const [r, g, bl] = densityRgb(v), i = (py * width + px) * 4
      pixels[i] = r; pixels[i + 1] = g; pixels[i + 2] = bl; pixels[i + 3] = 255
    }
  }
  return { width, height, pixels, max, latitude: [lo, hi] }
}

function densityRgb(v) {
  const t = Math.sqrt(Math.max(0, Math.min(1, v)))
  return [Math.round(9 + 246 * Math.min(1, t * 1.4)), Math.round(21 + 200 * Math.max(0, t - 0.3) / 0.7), Math.round(29 + 60 * Math.max(0, t - 0.75) / 0.25)]
}
export const densityColor = v => { const [r, g, b] = densityRgb(v); return `rgb(${r},${g},${b})` }

// Disk geometry of every cell: position in solar radii, mu = cos(angle from disk centre), heliographic latitude and
// longitude (observer in the solar equator plane); null off the disk. The engine counts a limb cell as on the disk
// when part of it is: such cells (centre just outside) are placed on the limb.
export function diskGeometry(block, grid) {
  const R = block.disk.solar_radius_m, quiet = block.disk.quiet_brightness_k, out = []
  for (let i = 0; i < grid.nx * grid.ny; i++) {
    let x = (grid.origin_east_m + (i % grid.nx + 0.5) * grid.spacing_east_m) / R, y = (grid.origin_north_m + (Math.floor(i / grid.nx) + 0.5) * grid.spacing_north_m) / R
    let r2 = x * x + y * y
    if (quiet[i] === null) { out.push({ x, y, mu: null, latitude_deg: null, longitude_deg: null }); continue }
    if (r2 >= 0.998) { const f = Math.sqrt(0.998 / r2); x *= f; y *= f; r2 = 0.998 }
    const mu = Math.sqrt(1 - r2)
    out.push({ x, y, mu, latitude_deg: Math.asin(y) * 180 / Math.PI, longitude_deg: Math.atan2(x, mu) * 180 / Math.PI })
  }
  return out
}

// Where a region sits on the disk: position in solar radii and mu.
export function regionPosition(region) {
  const lat = region.latitude_deg * Math.PI / 180, lon = region.longitude_deg * Math.PI / 180
  return { x: Math.cos(lat) * Math.sin(lon), y: Math.sin(lat), mu: Math.cos(lat) * Math.cos(lon) }
}

// Contrast of a cell to the quiet Sun in intensity (per cent): (T / T_quiet)^4 - 1.
export const contrastPct = (t, quiet) => t === null || quiet === null ? null : ((t / quiet) ** 4 - 1) * 100

// Photosphere colours: dark red through orange and yellow to white by brightness temperature. The default range
// spans the limb-darkened disk (about 6100 K at the centre, 5200 K near the limb) and the darker spot cells.
export function brightnessColor(t, lo = 4700, hi = 6250) {
  if (t === null) return '#05080b'
  const v = Math.max(0, Math.min(1, (t - lo) / (hi - lo)))
  return `rgb(${Math.round(90 + 165 * Math.min(1, v * 1.6))},${Math.round(20 + 225 * Math.pow(v, 1.35))},${Math.round(8 + 200 * Math.pow(v, 3.2))})`
}

// Contrast colours: spots (negative) fade to near black, faculae (positive) to pale yellow-white, quiet Sun orange.
export function contrastColor(c) {
  if (c === null) return '#05080b'
  if (c < 0) { const t = Math.min(1, -c / 40); return `rgb(${Math.round(232 - 200 * t)},${Math.round(128 - 108 * t)},${Math.round(40 - 20 * t)})` }
  const t = Math.min(1, c / 4)
  return `rgb(${Math.round(232 + 23 * t)},${Math.round(128 + 117 * t)},${Math.round(40 + 170 * t)})`
}

// Old cycle blue, cycle in progress orange, later ones green.
export function cycleColor(cycle, current) {
  return cycle < current ? '#6fa8ff' : cycle === current ? '#ffb347' : '#9fe870'
}

export const flareLabel = s => {
  const letters = ['A', 'B', 'C', 'M', 'X']
  let k = Math.max(0, Math.min(4, Math.floor(Math.log10(s) + 8 + 1e-9)))
  let v = Math.round(s / 10 ** (k - 8) * 10) / 10
  if (v >= 10 && k < 4) { k++; v = Math.round(s / 10 ** (k - 8) * 10) / 10 }
  return `${letters[k]}${v.toFixed(1)}`
}

export const fmtYear = y => y === null || y === undefined ? '—' : `${Math.floor(y)}-${String(Math.floor((y - Math.floor(y)) * 12) + 1).padStart(2, '0')}`

// ---- Long term: grand minima and maxima ----------------------------------------------------------------------------

export const EPOCH_COLORS = { grand_minimum: '#4f8bd6', grand_maximum: '#ff9f43', minimum: '#8fb8de', maximum: '#ffc98a' }
export const EPOCH_LABELS = { grand_minimum: 'grand minimum', grand_maximum: 'grand maximum', minimum: 'minimum', maximum: 'maximum' }
export const LONG_TERM_WINDOWS = [
  { key: 'all', label: 'Whole span' },
  { key: 'telescopic', label: 'Telescopic era (1600–)' },
  { key: 'maunder', label: 'Maunder Minimum (1600–1760)' },
  { key: 'future', label: '1900 to the horizon' },
]
const SOURCES = ['envelope', 'table', 'record_fit', 'record', 'forecast']

export function validateLongTerm(lt) {
  check(lt && Number.isInteger(lt.start_year) && Number.isInteger(lt.horizon_year) && lt.horizon_year > lt.start_year, 'Invalid long-term span.')
  const n = lt.horizon_year - lt.start_year + 1, s = lt.series
  check(s && ['sunspot', 'decadal_sunspot', 'sunspot_grand_minimum_scenario', 'envelope', 'background_sunspot', 'uv_200_242_pct', 'ozone_pct', 'cosmogenic_production', 'co2_ppm'].every(k => series(s[k], n)), 'Invalid long-term series.')
  check(Array.isArray(s.source) && s.source.length === n && s.source.every(v => SOURCES.includes(v)), 'Invalid long-term sources.')
  check(['low', 'central', 'high', 'grand_minimum_scenario'].every(k => series(s.tsi_w_m2?.[k], n)), 'Invalid long-term TSI.')
  check(['solar_low', 'solar_central', 'solar_high', 'solar_grand_minimum', 'volcanic', 'co2'].every(k => series(s.forcing_w_m2?.[k], n)), 'Invalid long-term forcings.')
  check(['solar_low', 'solar_central', 'solar_high', 'solar_grand_minimum', 'volcanic', 'co2', 'total', 'total_grand_minimum', 'regional_winter', 'regional_winter_grand_minimum'].every(k => series(s.temperature_k?.[k], n)), 'Invalid long-term temperatures.')
  check(Array.isArray(lt.epochs) && lt.epochs.every(e => typeof e.name === 'string' && EPOCH_COLORS[e.kind] && finite(e.start_year) && e.end_year > e.start_year && finite(e.mean_sunspot)), 'Invalid long-term epochs.')
  check(Array.isArray(lt.episodes) && lt.episodes.every(e => ['grand_minimum', 'grand_maximum'].includes(e.kind) && e.end_year >= e.start_year), 'Invalid long-term episodes.')
  check(Array.isArray(lt.cycles) && lt.cycles.every((c, k) => Number.isInteger(c.number) && finite(c.amplitude) && (k === 0 || c.number === lt.cycles[k - 1].number + 1)), 'Invalid long-term cycles.')
  const d = lt.dynamo
  check(d && d.statistics && finite(d.statistics.grand_minimum_fraction) && Array.isArray(d.outlook?.cycles) && Array.isArray(d.holocene?.amplitudes), 'Invalid dynamo block.')
  check(d.outlook.cycles.every((c, k) => c.grand_minimum_begun >= 0 && c.grand_minimum_begun <= 1 && (k === 0 || c.grand_minimum_begun >= d.outlook.cycles[k - 1].grand_minimum_begun)), 'Dynamo outlook shares must be cumulative.')
  if (d.example) check(['field', 'emergence', 'efficiency', 'amplitude'].every(k => series(d.example[k], d.example.amplitude.length)), 'Invalid dynamo example.')
  return lt
}

export function longTermWindow(lt, key) {
  const a = lt.start_year, b = lt.horizon_year + 1
  if (key === 'telescopic') return [Math.max(a, 1600), b]
  if (key === 'maunder') return [Math.max(a, 1600), Math.min(b, 1760)]
  if (key === 'future') return [Math.max(a, 1900), b]
  return [a, b]
}

// [[year + 0.5, value], ...] of an annual long-term series inside [x0, x1].
export function yearlyPoints(lt, values, [x0, x1], stride = 1) {
  const out = []
  for (let k = 0; k < values.length; k += stride) { const y = lt.start_year + k + 0.5; if (y >= x0 && y <= x1) out.push([y, values[k]]) }
  return out
}

// The value of every long-term series at a calendar year (the frame's year), or null outside the span.
export function longTermAt(lt, year) {
  const k = Math.floor(year) - lt.start_year
  if (k < 0 || k >= lt.series.sunspot.length) return null
  const s = lt.series
  return { year: Math.floor(year), sunspot: s.sunspot[k], background: s.background_sunspot[k], tsi: s.tsi_w_m2.central[k], production: s.cosmogenic_production[k], source: s.source[k],
    solar_k: s.temperature_k.solar_central[k], volcanic_k: s.temperature_k.volcanic[k], co2_k: s.temperature_k.co2[k], total_k: s.temperature_k.total[k] }
}

// Epoch bands (clipped to the window) and the episodes found in the series.
export function epochBands(lt, [x0, x1]) {
  return lt.epochs.filter(e => e.end_year > x0 && e.start_year < x1).map(e => ({ ...e, from: Math.max(x0, e.start_year), to: Math.min(x1, e.end_year), color: EPOCH_COLORS[e.kind] }))
}

// Catalogue epochs with what the model makes of them, and whether the series shows them as episodes.
export function epochTable(lt) {
  return lt.epochs.map(e => {
    const found = lt.episodes.filter(x => x.epoch === e.name)
    return { ...e, detected: found.length ? found.map(x => `${x.start_year}–${x.end_year}`).join(', ') : null, label: EPOCH_LABELS[e.kind], color: EPOCH_COLORS[e.kind] }
  })
}

// The dynamo example: per cycle its amplitude, emergence and Babcock-Leighton efficiency, and where the episode lies.
export function dynamoExample(lt) {
  const x = lt.dynamo.example
  if (!x) return null
  const c = lt.dynamo.config, threshold = c.grand_minimum_mean / c.cycle_mean_fraction
  const rows = x.amplitude.map((a, k) => ({ k, cycle: x.first_cycle + k, amplitude: a, field: x.field[k], emergence: x.emergence[k], efficiency: x.efficiency[k],
    inside: k >= x.episode_start && k < x.episode_start + x.episode_cycles, low: a < threshold }))
  // The trigger: the cycle before the episode whose Babcock-Leighton source was weakest.
  const before = rows.slice(Math.max(0, x.episode_start - 2), x.episode_start)
  const trigger = before.reduce((best, r) => (best === null || r.efficiency < best.efficiency ? r : best), null)
  const recovery = rows.find(r => r.k >= x.episode_start + x.episode_cycles) ?? null
  return { rows, threshold, trigger, recovery, start: x.episode_start, cycles: x.episode_cycles, maxAmplitude: Math.max(...x.amplitude) }
}

// The synthetic Holocene of the dynamo map: cycle amplitudes in years and the grand-minimum bands.
export function holoceneStrip(lt) {
  const h = lt.dynamo.holocene, years = h.amplitudes.length * h.years_per_cycle
  return { years, points: h.amplitudes.map((a, k) => [k * h.years_per_cycle, a]), bands: h.episodes.map(e => ({ kind: e.kind, from: e.start * h.years_per_cycle, to: (e.start + e.cycles) * h.years_per_cycle })),
    minima: h.episodes.filter(e => e.kind === 'grand_minimum').length, maxima: h.episodes.filter(e => e.kind === 'grand_maximum').length }
}
