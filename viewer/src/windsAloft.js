// Winds aloft: the wind at the standard winds-aloft (FB) levels over every cell of a frame, diagnosed from the engine's
// single-layer state with the balance the engine's own severe-convection kernel uses (severe_convection.cpp):
//   - the geostrophic wind of the pressure field, V_g = (1/(rho f)) k x grad p, on an f-plane at the grid anchor, with
//     the kernel's density (severe config, default 1.2 kg/m3) and its stencil (central inside, one-sided at the edges);
//   - the thermal wind of the horizontal temperature gradient, dV/dz = (g / (f T)) k x grad T. At the surface this is
//     the kernel's local thermal wind. Aloft, each scale of the temperature field reaches only as high as
//     quasi-geostrophic theory allows: a surface anomaly of horizontal scale L is balanced over the depth
//     H = |f| L / N (the Prandtl ratio f/N of the Eady / surface-QG problem, N = 0.01 1/s), so its wind grows as
//     rate * H * (1 - exp(-z / H)). The domain-wide planar trend of the temperature is taken as synoptic (balanced
//     through the whole troposphere); the rest is split into Gaussian scale bands (100-1 600 km). So synoptic
//     gradients build the jet up to the tropopause (11 km, constant above), while shallow mesoscale features (the
//     edges of the synthetic air masses, foehn, cold pools) stay near the ground instead of producing absurd jets;
//   - in the boundary layer (the lowest 1 000 m) a straight (Ekman) blend from the engine's 10 m wind to the
//     geostrophic wind, the straight low-level hodograph the kernel assumes.
// Where the temperature field is smooth on the synoptic scale the 0-6 km shear equals the kernel's deep shear
// |V_g - V_s + thermal wind(6 km)|; sharp small-scale gradients give less deep shear than the kernel's local estimate.
// Inside a tropical cyclone the storm's extra surface wind weakens linearly to nothing at 12 km (a warm-core vortex).
// Temperatures aloft: the surface temperature cooled at the ISA lapse rate (6.5 K/km) to the tropopause.
// A diagnostic of engine output, not an upper-air model: no jet-stream dynamics, one temperature gradient for the
// whole troposphere. Without a pressure field it is not available; without rotation (f = 0) the wind does not turn or
// grow with height. Pure functions: no Vue, no three.js.
import { KT_PER_MS, FT_PER_M, norm360, surfaceWind, elevationAt, HPA } from './station.js'

export const OMEGA = 7.2921159e-5          // Earth's rotation (rad/s), as in pressure_wind.hpp
export const GRAVITY = 9.80665             // as column::kGravityMS2
export const BOUNDARY_LAYER_M = 1000
export const TROPOPAUSE_M = 11000
export const LAPSE_K_PER_M = 0.0065
export const CYCLONE_DEPTH_M = 12000
export const BUOYANCY_FREQUENCY = 0.01      // N (1/s), standard troposphere
export const SCALE_BANDS_M = [100e3, 200e3, 400e3, 800e3, 1600e3]

// The levels of the winds-aloft (FB) forecast: altitudes to 12 000 ft, flight levels above (pressure altitudes, taken
// as ISA altitudes here). SFC is the engine's 10 m wind.
export const SURFACE_LEVEL = { key: 'SFC', label: 'SFC', ft: 0, surface: true }
export const FB_LEVELS = [3000, 6000, 9000, 12000, 18000, 24000, 30000, 34000, 39000].map(ft => ({ key: ft >= 18000 ? `FL${ft / 100}` : String(ft), label: ft >= 18000 ? `FL${ft / 100}` : `${ft.toLocaleString('en-US')} ft`, ft }))
export const ALL_LEVELS = [SURFACE_LEVEL, ...FB_LEVELS]

// Gradient (per metre) with the engine's stencil: central differences inside, one-sided at the edges.
export function gradientOf(values, grid) {
  const { nx, ny, spacing_east_m: dx, spacing_north_m: dy } = grid, east = new Float64Array(nx * ny), north = new Float64Array(nx * ny)
  for (let y = 0; y < ny; y++) {
    const s = y === 0 ? y : y - 1, n = y + 1 === ny ? y : y + 1
    for (let x = 0; x < nx; x++) {
      const w = x === 0 ? x : x - 1, e = x + 1 === nx ? x : x + 1, i = y * nx + x
      east[i] = (values[y * nx + e] - values[y * nx + w]) / ((e - w) * dx)
      north[i] = (values[n * nx + x] - values[s * nx + x]) / ((n - s) * dy)
    }
  }
  return { east, north }
}

// Separable Gaussian smoothing (standard deviation sigma, m), renormalised at the edges.
export function smoothField(values, grid, sigma) {
  const { nx, ny, spacing_east_m: dx, spacing_north_m: dy } = grid, tmp = new Float64Array(nx * ny), out = new Float64Array(nx * ny)
  const wx = Array.from({ length: nx }, (_, k) => Math.exp(-0.5 * (k * dx / sigma) ** 2)), wy = Array.from({ length: ny }, (_, k) => Math.exp(-0.5 * (k * dy / sigma) ** 2))
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) { let s = 0, w = 0; for (let xx = 0; xx < nx; xx++) { const k = wx[Math.abs(xx - x)]; if (k < 1e-6) continue; s += k * values[y * nx + xx]; w += k } tmp[y * nx + x] = s / w }
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) { let s = 0, w = 0; for (let yy = 0; yy < ny; yy++) { const k = wy[Math.abs(yy - y)]; if (k < 1e-6) continue; s += k * tmp[yy * nx + x]; w += k } out[y * nx + x] = s / w }
  return out
}

// Least-squares plane a + b east + c north through a field (east, north in m from the grid centre): on a regional
// grid the domain-wide trend is the best estimate of the synoptic gradient, which no smoothing of the grid can show.
export function planeFit(values, grid) {
  const { nx, ny, spacing_east_m: dx, spacing_north_m: dy } = grid
  let sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sv = 0, sxv = 0, syv = 0, n = 0
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    const e = (x - (nx - 1) / 2) * dx, m = (y - (ny - 1) / 2) * dy, v = values[y * nx + x]
    sx += e; sy += m; sxx += e * e; syy += m * m; sxy += e * m; sv += v; sxv += e * v; syv += m * v; n++
  }
  // The grid is symmetric about its centre: sx = sy = sxy = 0, so the normal equations decouple.
  const b = sxx ? sxv / sxx : 0, c = syy ? syv / syy : 0
  return { a: sv / n, east: b, north: c, at: i => sv / n + b * ((i % nx) - (nx - 1) / 2) * dx + c * (Math.floor(i / nx) - (ny - 1) / 2) * dy }
}

// The thermal wind split by scale: the domain-wide planar trend (synoptic: balanced through the whole troposphere,
// depth Infinity) and Gaussian bands of the rest. Band k holds the scales between SCALE_BANDS_M[k - 1] and
// SCALE_BANDS_M[k] (the first everything below 100 km, the last everything above 1 600 km, L = 3 200 km); its
// balanced depth is H = |f| L / N. Each entry: { depth, east, north } with the rate (m/s per m) per cell.
export function thermalBands(temperature, grid, f) {
  const n = temperature.length, plane = planeFit(temperature, grid)
  const residual = Float64Array.from(temperature, (t, i) => t - plane.at(i))
  const smoothed = SCALE_BANDS_M.map(sigma => smoothField(residual, grid, sigma))
  const fields = [residual, ...smoothed], bands = []
  const rates = (ge, gn) => ({ east: Float64Array.from({ length: n }, (_, i) => -(GRAVITY / (f * temperature[i])) * gn(i)), north: Float64Array.from({ length: n }, (_, i) => (GRAVITY / (f * temperature[i])) * ge(i)) })
  bands.push({ depth: Infinity, ...rates(() => plane.east, () => plane.north) })
  for (let k = 0; k < fields.length; k++) {
    const band = k < smoothed.length ? fields[k].map((v, i) => v - fields[k + 1][i]) : fields[k]
    const L = SCALE_BANDS_M[k] ?? SCALE_BANDS_M.at(-1) * 2, gt = gradientOf(band, grid)
    bands.push({ depth: Math.abs(f) * L / BUOYANCY_FREQUENCY, ...rates(i => gt.east[i], i => gt.north[i]) })
  }
  return bands
}

const windOf = (u, v) => { const speed = Math.hypot(u, v); return { u, v, speed, kt: speed * KT_PER_MS, fromDeg: speed > 1e-9 ? norm360(Math.atan2(-u, -v) * 180 / Math.PI) : 0 } }

// The field of a frame: { available, reason?, rotating, f, at(i, altitudeFt), profile(i), level(levelKey) }.
export function windsAloftField(frame) {
  const g = frame.forecast.grid, n = g.nx * g.ny, fields = frame.forecast.fields
  const pressure = frame.coupled?.pressure ?? (frame.atmosphere?.pressure ? frame.atmosphere.pressure.map(v => v * HPA) : null)
  if (!pressure) return { available: false, reason: 'This run has no pressure field, so the winds aloft cannot be diagnosed (they follow from the pressure and temperature gradients).' }
  const rho = frame.severe?.config?.geostrophic_air_density_kg_m3 ?? 1.2
  const f = 2 * OMEGA * Math.sin(g.geographic_anchor_latitude_deg * Math.PI / 180), rotating = Math.abs(f) >= 1e-6
  const T = fields.air_temperature.values, ue = fields.wind_east.values, ve = fields.wind_north.values
  // The thermal wind needs the temperature gradient on a level surface: over terrain the engine's surface temperature
  // falls with height, so it is first brought to sea level at the ISA lapse rate (no change on flat or sea grids).
  const elev = i => Math.max(0, elevationAt(frame, i) ?? 0)
  const seaLevelT = frame.terrain ? Array.from(T, (t, i) => t + LAPSE_K_PER_M * elev(i)) : T
  const gp = gradientOf(pressure, g), bands = rotating ? thermalBands(seaLevelT, g, f) : []
  const geo = i => (rotating ? [-gp.north[i] / (rho * f), gp.east[i] / (rho * f)] : [ue[i], ve[i]])
  // The thermal wind from the surface to height z (m): sum over the bands of rate * H * (1 - exp(-z / H)).
  const thermal = (i, z) => { let u = 0, v = 0; for (const b of bands) { const d = b.depth === Infinity ? z : b.depth * (1 - Math.exp(-z / b.depth)); u += b.east[i] * d; v += b.north[i] * d } return [u, v] }
  // Surface wind with a cyclone (station.js), and the cyclone's excess over the engine's wind field.
  const surface = new Array(n)
  const at = (i, altitudeFt) => {
    if (!surface[i]) surface[i] = surfaceWind(frame, i)
    const s = surface[i], elevation = Math.max(0, elevationAt(frame, i) ?? 0)
    if (!(altitudeFt > 0)) return { ...windOf(s.u, s.v), temperatureK: T[i], zAgl: 10 }
    const alt = altitudeFt / FT_PER_M, z = Math.max(0, alt - elevation), zThermal = Math.max(0, Math.min(alt, TROPOPAUSE_M) - elevation)
    const [gu, gv] = geo(i), [tu, tv] = thermal(i, zThermal), b = Math.min(1, z / BOUNDARY_LAYER_M)
    let u = ue[i] + b * (gu - ue[i]) + tu, v = ve[i] + b * (gv - ve[i]) + tv
    const decay = Math.max(0, 1 - z / CYCLONE_DEPTH_M)
    u += (s.u - ue[i]) * decay; v += (s.v - ve[i]) * decay
    return { ...windOf(u, v), temperatureK: T[i] - LAPSE_K_PER_M * Math.max(0, Math.min(alt, TROPOPAUSE_M) - elevation), zAgl: z }
  }
  // The station's profile at every level, with the FB rules for levels too close to the ground: no wind within
  // 1 500 ft of the station elevation, no temperature at 3 000 ft or within 2 500 ft of it.
  const profile = i => {
    const elevationFt = Math.max(0, elevationAt(frame, i) ?? 0) * FT_PER_M
    const rows = ALL_LEVELS.map(level => {
      const w = at(i, level.ft), below = !level.surface && level.ft < elevationFt + 1500
      const noTemperature = level.surface || level.ft === 3000 || level.ft < elevationFt + 2500
      return { level, ...w, below, temperatureC: noTemperature ? null : w.temperatureK - 273.15, fb: level.surface || below ? null : fbCode(w.fromDeg, w.kt, noTemperature ? null : w.temperatureK - 273.15, level.ft) }
    })
    // Vertical shear to the next level up, in kt per 1 000 ft (vector difference).
    rows.forEach((r, k) => {
      const up = rows[k + 1]
      if (!up || r.below || up.below) { r.shearKtPer1000ft = null; return }
      const dft = (up.level.ft - Math.max(r.level.ft, 33)) / 1000
      r.shearKtPer1000ft = Math.hypot(up.u - r.u, up.v - r.v) * KT_PER_MS / dft
    })
    return { elevationFt, rows, strongest: rows.filter(r => !r.below).reduce((a, r) => (r.kt > a.kt ? r : a)) }
  }
  // Every cell at one level (for the barb layers); below-ground cells are flagged.
  const level = key => {
    const l = ALL_LEVELS.find(x => x.key === key) ?? SURFACE_LEVEL
    return Array.from({ length: n }, (_, i) => ({ i, ...at(i, l.ft), below: !l.surface && l.ft < Math.max(0, elevationAt(frame, i) ?? 0) * FT_PER_M + 1500 }))
  }
  return { available: true, rotating, f, rho, at, profile, level, note: rotating ? null : 'No Coriolis force at this latitude: the wind does not turn or strengthen with height here.' }
}

// FB winds-aloft group DDff(+/-TT): direction in tens of degrees true, speed in knots; 100-199 kt add 50 to the
// direction; 200 kt or more is coded as 199 kt; below 5 kt "9900" (light and variable). Temperature: none at 3 000 ft;
// signed to FL240; above FL240 always below zero, so the sign is left out.
export function fbCode(fromDeg, kt, temperatureC, ft) {
  const pad = v => String(Math.abs(Math.trunc(v))).padStart(2, '0')
  let wind
  const speed = Math.round(kt)
  if (speed < 5) wind = '9900'
  else {
    let dd = Math.round(norm360(fromDeg) / 10) % 36 || 36, ff = Math.min(speed, 199)
    if (ff >= 100) { dd += 50; ff -= 100 }
    wind = `${pad(dd)}${pad(ff)}`
  }
  if (temperatureC === null || temperatureC === undefined || ft === 3000) return wind
  const t = Math.round(temperatureC)
  return ft > 24000 ? `${wind}${pad(t)}` : `${wind}${t < 0 ? '-' : '+'}${pad(t)}`
}

// Moderate clear-air turbulence is associated with vertical shear of about 6 kt per 1 000 ft or more (rule of thumb).
export const CAT_SHEAR_KT_PER_1000FT = 6

// The strongest wind aloft over the whole grid (for the summary): { kt, fromDeg, level, i }.
export function strongestAloft(field, grid) {
  if (!field.available) return null
  let best = null
  for (const l of FB_LEVELS) for (const c of field.level(l.key)) if (!c.below && (!best || c.kt > best.kt)) best = { kt: c.kt, fromDeg: c.fromDeg, level: l, i: c.i }
  return best ? { ...best, x: best.i % grid.nx, y: Math.floor(best.i / grid.nx) } : null
}
