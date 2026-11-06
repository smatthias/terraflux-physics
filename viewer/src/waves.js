// Waves on the sea, estimated from the wind. The engine has no wave model, so the viewer diagnoses the local wind sea
// with the standard parametric growth laws and draws it as an animated 3D water surface:
//   - JONSWAP fetch-limited growth (Hasselmann et al. 1973, as in the Coastal Engineering Manual):
//       g Hs / U^2 = 1.6e-3 (g F / U^2)^(1/2),   g Tp / U = 0.286 (g F / U^2)^(1/3)
//     capped at the fully developed Pierson–Moskowitz sea (g Hs / U^2 = 0.243, g Tp / U = 7.14);
//   - the fetch F measured upwind over open water to the nearest land (plus an allowance where the upwind path leaves
//     the map over the sea), and an effective fetch for tropical cyclones, whose waves cannot grow without bound;
//   - depth-limited height in the shallows (breaking, Hs <= 0.6 h) and wavelength and speed from the linear
//     dispersion relation w^2 = g k tanh(k h), so waves slow down and shorten towards the coast;
//   - damping under sea ice.
// This is a reading aid: no swell from distant storms, no duration limit, no refraction. Pure functions; the water
// mesh is in waterSurface.js.

export const G = 9.81
export const OPEN_FETCH_M = 300e3        // allowance where the upwind path leaves the map over the sea
export const CYCLONE_FETCH_M = 250e3     // effective fetch of a tropical cyclone's moving wind field
export const CYCLONE_INFLOW_DEG = 20     // surface winds spiral in towards the eye

// Wavenumber k (rad/m) of period T (s) in depth h (m; Infinity = deep water): w^2 = g k tanh(k h), by Newton's method.
export function wavenumber(T, h = Infinity) {
  const w = 2 * Math.PI / T, deep = w * w / G
  if (!Number.isFinite(h) || h <= 0 || deep * h > 10) return deep
  let k = Math.max(deep, w / Math.sqrt(G * h))
  for (let n = 0; n < 30; n++) {
    const t = Math.tanh(k * h), f = G * k * t - w * w, df = G * t + G * k * h * (1 - t * t)
    const next = k - f / df
    if (Math.abs(next - k) < 1e-10 * k) { k = next; break }
    k = next
  }
  return k
}

// The wind sea for a 10 m wind U (m/s) over fetch F (m) in depth h (m).
//   { hs (m), tp (s), length (m), speed (phase speed, m/s), group (m/s), regime }
export function windSea(U, F, h = Infinity) {
  if (!(U > 0.5) || !(F > 0)) return { hs: 0, tp: 0, length: 0, speed: 0, group: 0, regime: 'calm' }
  const x = G * F / (U * U)
  let hs = 1.6e-3 * Math.sqrt(x) * U * U / G, tp = 0.286 * Math.cbrt(x) * U / G, regime = 'fetch-limited'
  const hsMax = 0.243 * U * U / G, tpMax = 7.14 * U / G
  if (hs >= hsMax) { hs = hsMax; tp = tpMax; regime = 'fully developed' }
  if (Number.isFinite(h) && h > 0 && hs > 0.6 * h) { hs = 0.6 * h; regime = 'depth-limited' }
  const k = wavenumber(tp, h), w = 2 * Math.PI / tp, speed = w / k
  const kh = Number.isFinite(h) ? k * h : Infinity, n = Number.isFinite(kh) && kh < 20 ? 0.5 * (1 + 2 * kh / Math.sinh(2 * kh)) : 0.5
  return { hs, tp, length: 2 * Math.PI / k, speed, group: n * speed, regime }
}

// The Douglas sea scale: the state of the sea by its significant wave height.
const DOUGLAS = [[0.05, 'calm (glassy)'], [0.1, 'calm (rippled)'], [0.5, 'smooth'], [1.25, 'slight'], [2.5, 'moderate'], [4, 'rough'], [6, 'very rough'], [9, 'high'], [14, 'very high'], [Infinity, 'phenomenal']]
export function seaState(hs) {
  const hit = DOUGLAS.find(([limit]) => hs < limit)
  return hit ? hit[1] : DOUGLAS.at(-1)[1]
}

// Fetch (m) of every sea cell: the distance upwind over sea to the first land cell, walking in half-cell steps against
// the wind; if the path leaves the map over the sea, OPEN_FETCH_M is added. dirs are unit vectors the wind blows towards
// (east, north components), sea a mask (1 = sea).
export function fetchField({ sea, nx, ny, dxm, dym, dirs }) {
  const out = new Float32Array(nx * ny), stepCells = 0.5
  for (let i = 0; i < nx * ny; i++) {
    if (!sea[i]) continue
    const [de, dn] = dirs[i]
    if (!de && !dn) { out[i] = 0; continue }
    let x = i % nx + 0.5, y = Math.floor(i / nx) + 0.5, dist = 0, open = true
    // Step length in metres for a half-cell step along this direction.
    const len = Math.hypot(de * dxm, dn * dym) * stepCells / Math.max(Math.abs(de), Math.abs(dn))
    for (let n = 0; n < 4 * (nx + ny); n++) {
      x -= de / Math.max(Math.abs(de), Math.abs(dn)) * stepCells; y -= dn / Math.max(Math.abs(de), Math.abs(dn)) * stepCells
      if (x < 0 || y < 0 || x >= nx || y >= ny) break
      if (!sea[Math.floor(y) * nx + Math.floor(x)]) { open = false; break }
      dist += len
    }
    out[i] = dist + (open ? OPEN_FETCH_M : len)
  }
  return out
}

// The wind (east, north, m/s) of the strongest tropical cyclone at a point: the storm's wind speed there, blowing
// around the eye (counter-clockwise in the north, clockwise in the south) and spiralling in by CYCLONE_INFLOW_DEG.
export function cycloneWind(east, north, storm, speed, northernHemisphere = true) {
  const rx = east - storm.east_m, ry = north - storm.north_m, r = Math.hypot(rx, ry)
  if (!(r > 1) || !(speed > 0)) return [0, 0]
  const tx = northernHemisphere ? -ry / r : ry / r, ty = northernHemisphere ? rx / r : -rx / r
  const a = CYCLONE_INFLOW_DEG * Math.PI / 180
  const de = Math.cos(a) * tx - Math.sin(a) * rx / r, dn = Math.cos(a) * ty - Math.sin(a) * ry / r
  return [de * speed, dn * speed]
}

// The wave field of a frame: per sea cell the wind sea and its direction (towards, unit east/north).
//   sea: mask; u, v: wind east/north (m/s); depth: m (Infinity or null where unknown); ice: 0..1 cover or null;
//   storms: [{ east_m, north_m }] with stormSpeed per cell (m/s), used where the cyclone's wind exceeds the background.
export function waveField({ sea, nx, ny, dxm, dym, originEast = 0, originNorth = 0, u, v, depth = null, ice = null, storms = null, stormSpeed = null, northernHemisphere = true }) {
  const n = nx * ny, U = new Float32Array(n), dirs = new Array(n), cyclone = new Uint8Array(n)
  for (let i = 0; i < n; i++) {
    let ue = u?.[i] ?? 0, vn = v?.[i] ?? 0
    if (storms?.length && stormSpeed && stormSpeed[i] > Math.hypot(ue, vn)) {
      const east = originEast + (i % nx + 0.5) * dxm, north = originNorth + (Math.floor(i / nx) + 0.5) * dym
      let best = storms[0], bestR = Infinity
      for (const s of storms) { const r = Math.hypot(east - s.east_m, north - s.north_m); if (r < bestR) { bestR = r; best = s } }
      ;[ue, vn] = cycloneWind(east, north, best, stormSpeed[i], northernHemisphere); cyclone[i] = 1
    }
    const sp = Math.hypot(ue, vn); U[i] = sp; dirs[i] = sp > 1e-6 ? [ue / sp, vn / sp] : [0, 0]
  }
  const fetch = fetchField({ sea, nx, ny, dxm, dym, dirs })
  const hs = new Float32Array(n), tp = new Float32Array(n), length = new Float32Array(n), speed = new Float32Array(n), dirEast = new Float32Array(n), dirNorth = new Float32Array(n)
  const regime = new Array(n).fill(null)
  for (let i = 0; i < n; i++) {
    if (!sea[i]) continue
    const h = depth && Number.isFinite(depth[i]) && depth[i] > 0 ? depth[i] : Infinity
    const w = windSea(U[i], cyclone[i] ? Math.min(fetch[i], CYCLONE_FETCH_M) : fetch[i], h)
    const damp = ice ? Math.max(0, 1 - Math.min(1, ice[i] ?? 0) / 0.8) ** 2 : 1
    hs[i] = w.hs * damp; tp[i] = w.tp; length[i] = w.length; speed[i] = w.speed; regime[i] = damp < 1 && damp < 0.3 ? 'damped by sea ice' : w.regime
    dirEast[i] = dirs[i][0]; dirNorth[i] = dirs[i][1]
  }
  return { hs, tp, length, speed, dirEast, dirNorth, wind: U, fetch, regime, cyclone }
}

// ---- How the waves are drawn (not to scale; see the scene note) ----------------------------------------------------
// Real wavelengths (tens to hundreds of metres) vanish on maps hundreds of kilometres wide, so the drawn waves keep
// their order among themselves: longer waves are drawn longer (on a log scale), higher waves higher, and the crests move
// at DRAWN_SPEED scene units per second for every m/s of their real phase speed, so faster waves are seen to be faster.
// (A fixed time-lapse would not do: with the log-scaled lengths the longest waves would seem to crawl.)
export const DRAWN_SPEED = 0.06
export const drawnLength = length => Math.min(2.6, Math.max(0.35, 0.5 + 0.45 * Math.log2(Math.max(1, length) / 30)))
export function drawnWave({ hs, tp, length, speed }, heightScale = 2) {
  if (!(hs > 0) || !(tp > 0)) return { wavelength: 0.5, amplitude: 0, period: 1 }
  const wavelength = drawnLength(length), c = speed > 0 ? speed : G * tp / (2 * Math.PI)
  const amplitude = Math.min(0.45, 0.035 + 0.035 * hs) * heightScale / 2 * Math.min(1, hs / 0.3)
  return { wavelength, amplitude, period: wavelength / (DRAWN_SPEED * c) }
}

// The real wave behind a drawn wavelength (the inverse of drawnLength): length (m), deep-water period (s) and phase
// speed (m/s), and the drawn angular frequency (rad/s) that moves its crests at DRAWN_SPEED times that speed. The
// water surface uses it for its fixed wavelength bands.
export function drawnBand(wavelength) {
  const length = 30 * 2 ** ((wavelength - 0.5) / 0.45), period = Math.sqrt(2 * Math.PI * length / G), speed = length / period
  return { length, period, speed, omega: 2 * Math.PI / wavelength * DRAWN_SPEED * speed }
}
