// Solar wind support: validation of the documented terraflux.solar.wind/v1 block (the storm's maps of the aurora,
// the heavy-ion outflow and the radiation, the hourly series, the solar-cycle climatology, the scenarios and the
// budgets), per-cell layer values, metrics, chart data, colours and the display geometry of the magnetosphere
// (dipole field lines and the magnetopause as drawn shapes). No physics runs in the browser: every number comes
// from the engine output; the field lines only sketch the topology around the engine's oval and magnetopause.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }
const DEG = Math.PI / 180

export const WIND_SCHEMA = 'terraflux.solar.wind/v1'
export const WIND_METHOD = 'dipole_newell_obrien_milan_force_field'
export const WIND_LAYER_KEYS = ['windaurora', 'windoutflow', 'winddose', 'windsep', 'windcutoff', 'windthermo', 'windregion']
export const REGIONS = [
  { key: 0, label: 'Closed field (shielded)', color: '#24465a' },
  { key: 1, label: 'Auroral oval', color: '#58e07a' },
  { key: 2, label: 'Polar cap (open field lines)', color: '#ff9f5a' },
  { key: 3, label: 'Cusp (solar wind entry)', color: '#ff5fb0' },
]
export const G_SCALE = ['none', 'G1 minor', 'G2 moderate', 'G3 strong', 'G4 severe', 'G5 extreme']

const series = (v, n) => Array.isArray(v) && v.length === n && v.every(x => x === null || finite(x))

export function validateSolarWind(block, frames) {
  const b = block, grid = frames[0].forecast.grid, count = grid.nx * grid.ny
  check(b?.schema_version === WIND_SCHEMA && b.method === WIND_METHOD, 'Unsupported solar wind contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Solar wind source must be engine_output or synthetic.')
  const d = b.dipole
  check(d && finite(d.moment_a_m2) && d.moment_a_m2 > 0 && finite(d.north_pole_latitude_deg) && finite(d.north_pole_longitude_deg), 'Invalid dipole.')
  const s = b.static
  check(s && ['magnetic_latitude_deg', 'magnetic_longitude_deg', 'cutoff_gv'].every(k => series(s[k], count)), 'Solar wind static maps must match the grid.')
  check(s.land_mask === null || (Array.isArray(s.land_mask) && s.land_mask.length === count), 'Invalid land mask.')
  const st = b.storm, n = st?.hours + 1
  check(Number.isInteger(st?.hours) && st.hours > 0 && st.series && Object.values(st.series).every(v => series(v, n)), 'Invalid storm series.')
  check(['kp', 'dst_nt', 'speed_km_s', 'bz_nt', 'hemispheric_power_gw', 'oxygen_escape_per_s', 'hydrogen_escape_per_s', 'dose_pole_usv_h'].every(k => st.series[k]), 'Storm series are incomplete.')
  check(Array.isArray(st.frames) && st.frames.length === frames.length, 'Solar wind map frames must match the timeline.')
  frames.forEach((frame, k) => {
    const x = st.frames[k]
    check(x.elapsed_seconds === frame.forecast.elapsed_seconds, 'Solar wind frame times must match the timeline.')
    check(series(x.aurora_mw_m2, count) && x.aurora_mw_m2.every(v => v >= 0) && series(x.oxygen_outflow_1e7_cm2_s, count) && x.oxygen_outflow_1e7_cm2_s.every(v => v >= 0), 'Invalid aurora or outflow map.')
    check(Array.isArray(x.region) && x.region.length === count && x.region.every(v => Number.isInteger(v) && v >= 0 && v <= 3), 'Invalid field regions.')
    check(series(x.dose_by_cutoff_usv_h, 61) && series(x.sep_by_cutoff_1e9_cm2_s, 41), 'Invalid radiation tables.')
    check(x.oval && finite(x.oval.cap_colatitude_deg) && x.magnetopause && finite(x.magnetopause.standoff_re) && x.subsolar && finite(x.subsolar.magnetic_longitude_deg), 'Invalid oval, magnetopause or Sun.')
    check(finite(x.kp) && x.kp >= 0 && x.kp <= 9 && finite(x.dst_nt), 'Invalid indices.')
    frame.solarWind = { index: k, block: b, map: x, hour: x.hour, time: x.time_utc, static: s, dipole: d, aurora: x.aurora_mw_m2, outflow: x.oxygen_outflow_1e7_cm2_s, region: x.region,
      thermo: frame.forecast.fields.air_temperature.values, synthetic: b.source_kind === 'synthetic' }
  })
  check(Array.isArray(b.dipole_scenarios) && Array.isArray(b.storm_scenarios) && Array.isArray(b.cities) && b.budgets?.mean_escape, 'Solar wind scenarios or budgets are missing.')
  if (b.cycle !== null) check(Array.isArray(b.cycle?.months) && Object.values(b.cycle.series).every(v => series(v, b.cycle.months.length)), 'Invalid solar-cycle series.')
  return b
}

// Linear interpolation in a table with a fixed step, clamped to its ends.
export function tableAt(table, step, x) {
  const u = Math.max(0, x) / step, k = Math.min(table.length - 2, Math.floor(u)), w = Math.min(1, u - k)
  return table[k] + w * (table[k + 1] - table[k])
}

// The dose at 12 km and the solar-proton ionisation of a cell follow from its (storm-reduced) cutoff rigidity.
export const cellDose = (w, i) => tableAt(w.map.dose_by_cutoff_usv_h, 0.25, w.static.cutoff_gv[i] * w.map.cutoff_factor)
export const cellSep = (w, i) => { const c = w.static.cutoff_gv[i] * w.map.cutoff_factor; return c >= 2 ? 0 : tableAt(w.map.sep_by_cutoff_1e9_cm2_s, 0.05, c) }
export const magneticLocalTime = (mlon, sunMlon) => ((12 + (mlon - sunMlon) / 15) % 24 + 24) % 24

export function windLayerValues(frame, key) {
  const w = frame.solarWind
  if (!w) return []
  const n = w.static.cutoff_gv.length
  switch (key) {
    case 'windaurora': return w.aurora
    case 'windoutflow': return w.outflow
    case 'winddose': return Array.from({ length: n }, (_, i) => cellDose(w, i))
    case 'windsep': return Array.from({ length: n }, (_, i) => cellSep(w, i))
    case 'windcutoff': return w.static.cutoff_gv.map(c => c * w.map.cutoff_factor)
    case 'windthermo': return w.thermo
    case 'windregion': return w.region
    default: return []
  }
}

// A colour scale that stays fixed over the storm (so that frames compare).
export function windLayerRange(frames, key) {
  if (key === 'windregion') return [0, 3]
  const first = frames[0]?.solarWind
  if (!first) return [0, 1]
  if (key === 'windcutoff') return [0, Math.max(...first.static.cutoff_gv)]
  let lo = Infinity, hi = -Infinity
  for (const f of frames) {
    const v = windLayerValues(f, key)
    for (const x of v) { if (x < lo) lo = x; if (x > hi) hi = x }
  }
  if (key === 'windaurora' || key === 'windoutflow' || key === 'windsep') lo = 0
  return [lo, hi > lo ? hi : lo + 1]
}

// Metrics of the current hour from the series (the frame's hour).
export function summarizeWind(frame) {
  const w = frame.solarWind, b = w.block, s = b.storm.series, k = Math.round(w.hour), at = key => s[key][k]
  const amu = 1.66053907e-27
  return {
    hour: w.hour, time: w.time, phase: w.map.phase, speed: at('speed_km_s'), density: at('density_cm3'), bz: at('bz_nt'), by: at('by_nt'), pressure: at('pressure_npa'),
    kp: at('kp'), g: w.map.g_scale, dst: at('dst_nt'), standoff: at('standoff_re'), potential: at('cross_cap_potential_kv'), open_flux: at('open_flux_gwb'),
    cap_mlat: at('cap_boundary_mlat_deg'), oval_mlat: at('oval_equatorward_midnight_mlat_deg'), hp: at('hemispheric_power_gw'), joule: at('joule_heating_gw'),
    exo: at('exospheric_k'), density_ratio: at('density_400km_ratio'), h_escape: at('hydrogen_escape_per_s'), h_kg: at('hydrogen_escape_per_s') * 1.00784 * amu,
    jeans_share: at('hydrogen_jeans_per_s') / at('hydrogen_escape_per_s'), polar_wind: at('hydrogen_polar_wind_per_s'), o_escape: at('oxygen_escape_per_s'),
    o_kg: at('oxygen_escape_per_s') * 15.999 * amu, he_kg: at('helium_escape_per_s') * 4.0026 * amu, phi: at('phi_gv'), neutron_pole: at('neutron_pole_change_pct'),
    neutron_equator: at('neutron_equator_change_pct'), sep: at('sep_flux_pfu'), sep_pole: at('sep_ion_pairs_pole_cm2_s'), dose_pole: at('dose_pole_usv_h'),
    dose_mid: at('dose_50deg_usv_h'), dose_equator: at('dose_equator_usv_h'), ozone_north: at('ozone_upper_north_pct'), ozone_south: at('ozone_upper_south_pct'),
    inside_geostationary: at('standoff_re') < 6.6,
  }
}

// Polylines for a chart of several series on one time axis: points "x,y" in a 100 x 50 box.
export function chartLines(x, lines, { lo = null, hi = null, log = false, height = 50 } = {}) {
  const t = v => (log ? Math.log10(Math.max(v, 1e-300)) : v)
  const all = lines.flatMap(l => l.values.filter(finite).map(t))
  let a = lo === null ? Math.min(...all) : t(lo), b = hi === null ? Math.max(...all) : t(hi)
  if (!(b > a)) { a -= 1; b += 1 }
  // The reported range is the data's; the drawing leaves a little room above and below.
  const lower = a, upper = b, pad = 0.04 * (b - a)
  a -= pad; b += pad
  const x0 = x[0], x1 = x[x.length - 1]
  const pos = (xi, v) => `${(((xi - x0) / (x1 - x0 || 1)) * 100).toFixed(2)},${(height - ((t(v) - a) / (b - a)) * height).toFixed(2)}`
  return { lo: log ? 10 ** lower : lower, hi: log ? 10 ** upper : upper, y: v => height - ((t(v) - a) / (b - a)) * height,
    lines: lines.map(l => ({ ...l, points: l.values.map((v, i) => (finite(v) ? pos(x[i], v) : null)).filter(Boolean).join(' ') })) }
}

// ---- Colours ----------------------------------------------------------------------------------------------------
const mix = (a, b, u) => a.map((x, k) => Math.round(x + (b[k] - x) * u))
const hex = c => `#${c.map(x => x.toString(16).padStart(2, '0')).join('')}`
function ramp(stops, u) {
  const x = Math.min(1, Math.max(0, u)) * (stops.length - 1), k = Math.min(stops.length - 2, Math.floor(x))
  return hex(mix(stops[k], stops[k + 1], x - k))
}
const AURORA = [[13, 27, 36], [20, 90, 60], [60, 220, 110], [190, 255, 120], [255, 120, 160]]
const OUTFLOW = [[13, 27, 36], [90, 40, 20], [220, 110, 40], [255, 190, 90], [255, 245, 210]]
const DOSE = [[30, 40, 90], [60, 90, 180], [120, 170, 220], [250, 200, 90], [240, 80, 60]]
const SEP = [[13, 27, 36], [80, 30, 90], [170, 60, 160], [255, 110, 140], [255, 220, 200]]
const CUTOFF = [[255, 150, 80], [220, 200, 110], [110, 170, 200], [50, 80, 150], [25, 35, 80]]
const THERMO = [[40, 60, 140], [90, 120, 200], [200, 200, 160], [250, 160, 80], [220, 60, 50]]
export function windColor(key, v, lo, hi) {
  if (key === 'windregion') return REGIONS[v]?.color ?? '#24465a'
  const u = (v - lo) / ((hi - lo) || 1)
  if (key === 'windaurora') return ramp(AURORA, Math.sqrt(Math.max(0, u)))
  if (key === 'windoutflow') return ramp(OUTFLOW, Math.sqrt(Math.max(0, u)))
  if (key === 'windsep') return ramp(SEP, Math.sqrt(Math.max(0, u)))
  if (key === 'winddose') return ramp(DOSE, u)
  if (key === 'windcutoff') return ramp(CUTOFF, u)
  return ramp(THERMO, u)
}

// ---- Geometry ---------------------------------------------------------------------------------------------------

// Unit vectors (Earth-fixed: x to 0 deg longitude on the equator, z to the north pole).
export function geographicVector(lat, lon) { return [Math.cos(lat * DEG) * Math.cos(lon * DEG), Math.cos(lat * DEG) * Math.sin(lon * DEG), Math.sin(lat * DEG)] }
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
const norm = a => { const l = Math.hypot(...a) || 1; return a.map(x => x / l) }
const add = (...vs) => vs.reduce((s, v) => s.map((x, k) => x + v[k]), [0, 0, 0])
const scale = (v, s) => v.map(x => x * s)

// The dipole's magnetic frame (as in the engine): pole p, y_m = z x p, x_m = y_m x p.
export function magneticFrame(dipole) {
  const p = geographicVector(dipole.north_pole_latitude_deg, dipole.north_pole_longitude_deg)
  let y = [-p[1], p[0], 0]
  if (Math.hypot(y[0], y[1]) < 1e-12) y = [0, 1, 0]
  y = norm(y)
  return { p, x: cross(y, p), y }
}
export function magneticPosition(lat, lon, dipole) {
  const f = magneticFrame(dipole), r = geographicVector(lat, lon)
  return { lat: Math.asin(Math.max(-1, Math.min(1, dot(r, f.p)))) / DEG, lon: Math.atan2(dot(r, f.y), dot(r, f.x)) / DEG }
}
// Earth-fixed unit vector of a magnetic latitude and longitude.
export function magneticToVector(mlat, mlon, dipole) {
  const f = magneticFrame(dipole), c = Math.cos(mlat * DEG)
  return add(scale(f.x, c * Math.cos(mlon * DEG)), scale(f.y, c * Math.sin(mlon * DEG)), scale(f.p, Math.sin(mlat * DEG)))
}
export const vectorToLatLon = v => ({ lat: Math.asin(Math.max(-1, Math.min(1, v[2]))) / DEG, lon: Math.atan2(v[1], v[0]) / DEG })

// The oval's boundaries (as in the engine): poleward = cap + offset cos(MLT), equatorward adds the widths and the storm shift.
export function ovalBoundary(oval, mlt, kind) {
  const c = Math.cos((2 * Math.PI * mlt) / 24), pole = oval.cap_colatitude_deg + oval.midnight_offset_deg * c
  if (kind === 'poleward') return pole
  return pole + oval.width_day_deg + (oval.width_night_deg - oval.width_day_deg) * 0.5 * (1 + c) + oval.storm_shift_deg
}
// Boundary rings as Earth-fixed vectors for one hemisphere (+1 north, -1 south).
export function ovalRing(w, hemisphere, kind, steps = 96) {
  const out = []
  for (let k = 0; k <= steps; k++) {
    const mlt = (24 * k) / steps, colat = ovalBoundary(w.map.oval, mlt, kind), mlon = w.map.subsolar.magnetic_longitude_deg + (mlt - 12) * 15
    out.push(magneticToVector(hemisphere * (90 - colat), mlon, w.dipole))
  }
  return out
}

// Rotation from Earth-fixed vectors to a Sun-fixed scene (Sun along +x, north tilted by the subsolar latitude).
// Returned as a function mapping an Earth-fixed vector to [x, y, z] of the scene (y up).
export function sunFixedRotation(subsolar) {
  const s = geographicVector(subsolar.latitude_deg, subsolar.longitude_deg), n = [0, 0, 1]
  const a1 = s, a3 = norm(cross(n, s)), a2 = cross(a3, a1)
  const d = subsolar.latitude_deg * DEG, N = [Math.sin(d), Math.cos(d), 0], S = [1, 0, 0]
  const b1 = S, b3 = norm(cross(N, S)), b2 = cross(b3, b1)
  return v => { const c1 = dot(v, a1), c2 = dot(v, a2), c3 = dot(v, a3); return add(scale(b1, c1), scale(b2, c2), scale(b3, c3)) }
}

// Display radius of a distance in Earth radii: compressed so that the magnetosphere fits around a visible Earth.
export const displayRadius = (r, earth, exponent = 0.45) => earth * Math.pow(Math.max(r, 1), exponent)

// Sketch of the magnetosphere in scene coordinates around the Earth's centre: closed dipole field lines (clipped
// at the magnetopause on the day side), open field lines from the polar caps swept into the tail lobes, and the
// magnetopause (Shue shape) as meridians and rings. `toScene` maps an Earth-fixed vector to the scene.
export function magnetosphereSketch(w, toScene, earth, { exponent = 0.45, tail = 40 } = {}) {
  const m = norm(toScene(geographicVector(w.dipole.north_pole_latitude_deg, w.dipole.north_pole_longitude_deg)))
  const X = [1, 0, 0], u = norm(add(X, scale(m, -dot(X, m)))), v = cross(m, u)
  const Z = norm(add(m, scale(X, -dot(m, X)))), Y = cross(Z, X)   // GSM: Z the dipole axis projected, Y towards dusk
  const r0 = w.map.magnetopause.standoff_re, alpha = w.map.magnetopause.flaring
  const mp = theta => r0 * Math.pow(2 / (1 + Math.cos(theta)), alpha)
  const place = p => { const r = Math.hypot(...p); return r < 1e-9 ? p : scale(p, displayRadius(r, earth, exponent) / r) }
  const closed = [], open = [], pause = []
  // Closed lines in meridian planes around the dipole axis (psi = 0 towards the Sun).
  for (const psi of [0, 60, 120, 180, 240, 300]) {
    const dir = add(scale(u, Math.cos(psi * DEG)), scale(v, Math.sin(psi * DEG)))
    for (const L of [1.6, 2.5, 4, 6.5]) {
      const lam0 = Math.acos(Math.sqrt(1 / L)), pts = []
      let inside = true
      for (let k = 0; k <= 48; k++) {
        const lam = -lam0 + (2 * lam0 * k) / 48, r = L * Math.cos(lam) ** 2
        const p = add(scale(dir, r * Math.cos(lam)), scale(m, r * Math.sin(lam)))
        const theta = Math.acos(Math.max(-1, Math.min(1, p[0] / Math.max(r, 1e-9))))
        if (r > mp(theta)) { inside = false; break }
        pts.push(place(p))
      }
      if (inside) closed.push({ L, psi, points: pts })
    }
  }
  // Open lines: foot points inside each cap, swept anti-sunward (cubic Bezier in Earth radii).
  const bez = (a, b, c, d, t) => add(scale(a, (1 - t) ** 3), scale(b, 3 * (1 - t) ** 2 * t), scale(c, 3 * (1 - t) * t * t), scale(d, t ** 3))
  for (const hemi of [1, -1]) {
    for (const mlt of [0, 3, 6, 9, 12, 15, 18, 21]) {
      const colat = 0.75 * ovalBoundary(w.map.oval, mlt, 'poleward'), az = ((mlt - 12) * 15) * DEG
      const horizontal = add(scale(u, Math.cos(az)), scale(v, Math.sin(az)))
      const foot = add(scale(m, hemi * Math.cos(colat * DEG)), scale(horizontal, Math.sin(colat * DEG)))
      const up = norm(foot)
      const lobe = 0.55 * mp(Math.PI * 0.75), side = Math.sin(az)   // +1 dusk, -1 dawn
      const gsm = (x, y, z) => add(scale(X, x), scale(Y, y), scale(Z, z))
      const a = foot, b = add(foot, scale(up, 3)), c = gsm(-6, side * lobe * 0.4, hemi * lobe * 0.55), d = gsm(-tail, side * lobe * 0.5, hemi * lobe * 0.5)
      const pts = []
      for (let k = 0; k <= 40; k++) pts.push(place(bez(a, b, c, d, k / 40)))
      open.push({ hemisphere: hemi, mlt, points: pts })
    }
  }
  // The magnetopause: meridians (every 30 deg around the Sun-Earth line) and rings.
  for (let phi = 0; phi < 360; phi += 30) {
    const pts = []
    for (let th = 0; th <= 150; th += 5) {
      const r = mp(th * DEG), t = th * DEG
      pts.push(place([r * Math.cos(t), r * Math.sin(t) * Math.cos(phi * DEG), r * Math.sin(t) * Math.sin(phi * DEG)]))
    }
    pause.push(pts)
  }
  const rings = [30, 60, 90, 120].map(th => { const pts = []; for (let phi = 0; phi <= 360; phi += 10) { const r = mp(th * DEG), t = th * DEG; pts.push(place([r * Math.cos(t), r * Math.sin(t) * Math.cos(phi * DEG), r * Math.sin(t) * Math.sin(phi * DEG)])) } return pts })
  return { axis: m, closed, open, pause: [...pause, ...rings], nose: place([r0, 0, 0]) }
}

// ---- Tables and charts ------------------------------------------------------------------------------------------

// Cells of one hemisphere in magnetic polar coordinates for a dial of radius 45: noon at the top, dusk on the left
// (as seen from above the north pole), magnetic colatitude outwards to maxColat.
export function polarDial(frame, hemisphere, values, { maxColat = 50 } = {}) {
  const w = frame.solarWind, s = w.static, sun = w.map.subsolar.magnetic_longitude_deg, out = []
  const nx = frame.forecast.grid.nx, ny = frame.forecast.grid.ny, cell = s.cell_degrees ?? [360 / nx, 180 / ny]
  for (let i = 0; i < values.length; i++) {
    const mlat = s.magnetic_latitude_deg[i]
    if (Math.sign(mlat) !== hemisphere) continue
    const colat = 90 - Math.abs(mlat)
    if (colat > maxColat) continue
    const mlt = magneticLocalTime(s.magnetic_longitude_deg[i], sun), a = ((mlt - 12) / 24) * 2 * Math.PI, r = (colat / maxColat) * 45
    const size = Math.max(1.2, (cell[1] / maxColat) * 45 * 1.1)
    out.push({ i, x: 50 - r * Math.sin(a), y: 50 - r * Math.cos(a), size, value: values[i] })   // dusk (18) left, dawn (06) right
  }
  return out
}

const fmt = (v, d = 2) => (v === null || v === undefined || !finite(v) ? '—' : v !== 0 && (Math.abs(v) >= 1e5 || Math.abs(v) < 1e-2) ? v.toExponential(Math.max(1, d - 1)) : v.toLocaleString('en-US', { maximumFractionDigits: d }))
export const formatWind = fmt
export const sig = (v, n = 2) => (v === null || v === undefined || !finite(v) ? '—' : v !== 0 && (Math.abs(v) < 1e-2 || Math.abs(v) >= 1e7) ? v.toExponential(n - 1) : v.toLocaleString('en-US', { maximumSignificantDigits: n }))

// What the Earth loses to space (mean over the cycle window): per species, with the share that leaves through the poles.
export function escapeRows(block) {
  const e = block.budgets.mean_escape, year = 365.25 * 86400 / 1000
  return [
    { species: 'Hydrogen (H, from water, methane, H₂)', per_s: e.hydrogen_per_s, kg_s: e.hydrogen_kg_s, t_yr: e.hydrogen_kg_s * year, polar_share: e.hydrogen_polar_wind_share, route: 'thermal (Jeans) and charge exchange everywhere; a little as polar wind' },
    { species: 'Oxygen (O⁺)', per_s: e.oxygen_per_s, kg_s: e.oxygen_kg_s, t_yr: e.oxygen_kg_s * year, polar_share: 1, route: 'only along open field lines: cusp (ion fountain) and night-side oval' },
    { species: 'Helium (He, radiogenic)', per_s: e.helium_per_s, kg_s: e.helium_kg_s, t_yr: e.helium_kg_s * year, polar_share: 1, route: 'as He⁺ in the polar wind' },
  ]
}

// The hydrogen routes over the cycle as shares of the total (for a stacked chart).
export function hydrogenShares(cycle) {
  const c = cycle.series
  return c.hydrogen_escape_per_s.map((t, k) => ({ jeans: c.hydrogen_jeans_per_s[k] / t, exchange: c.hydrogen_charge_exchange_per_s[k] / t, polar: c.hydrogen_polar_wind_per_s[k] / t }))
}

// Annual means of cycle series (for a calmer chart).
export function annualMeans(cycle, key) {
  const out = new Map()
  cycle.months.forEach((m, k) => { const y = Number(m.slice(0, 4)), v = cycle.series[key][k]; if (!finite(v)) return; const a = out.get(y) ?? [0, 0]; a[0] += v; a[1] += 1; out.set(y, a) })
  return [...out.entries()].filter(([, a]) => a[1] >= 6).map(([year, a]) => ({ year, value: a[0] / a[1] }))
}
