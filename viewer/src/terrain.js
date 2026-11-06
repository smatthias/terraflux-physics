// How terrain is shown realistically: how much the relief is exaggerated, where snow and trees stop, and where the land
// is rough. Pure functions on per-cell arrays; the surface and its shader do the drawing.

// Scene units the full relief should rise to at the default height scale (2): enough to read mountains as mountains.
export const RELIEF_TARGET = 4.2

export function reliefRange(elevations) {
  let min = Infinity, max = -Infinity
  for (const v of elevations ?? []) if (v !== null && v !== undefined && Number.isFinite(v)) { if (v < min) min = v; if (v > max) max = v }
  return min === Infinity ? { min: 0, max: 0, range: 0 } : { min, max, range: max - min }
}

// Scene units per metre of height at height scale 1. True scale (sceneScale, units per horizontal metre) flattens
// regional terrain (2.5 km of Alps over 960 km wide is a sheet of paper), so the relief is raised to RELIEF_TARGET at
// height scale 2, but never drawn flatter than true scale.
export function verticalScale(range, sceneScale, target = RELIEF_TARGET) {
  return Math.max(sceneScale, target / 2 / Math.max(range, 1))
}
// The resulting vertical exaggeration (1 = true scale), for the scene note.
export const exaggerationFactor = (vscale, sceneScale, heightScale) => vscale * heightScale / sceneScale

// Permanent snow line (m) by latitude: about 5,000 m in the tropics, 2,900 m in the Alps, sea level near the poles.
const SNOWLINE = [[0, 5000], [25, 5000], [35, 3700], [45, 2950], [55, 1900], [65, 900], [72, 250], [80, 0], [90, 0]]
export function snowlineFor(latitudeDeg) {
  const a = Math.min(90, Math.abs(Number.isFinite(latitudeDeg) ? latitudeDeg : 45))
  for (let k = 1; k < SNOWLINE.length; k++) {
    const [a0, h0] = SNOWLINE[k - 1], [a1, h1] = SNOWLINE[k]
    if (a <= a1) return h0 + (h1 - h0) * (a - a0) / ((a1 - a0) || 1)
  }
  return 0
}
// Trees stop some 800–1,000 m below the snow line.
export const treelineFor = snowline => Math.max(0, snowline - 900)

// Metres per rock stratum: a few dozen layers over the relief, between 12 and 350 m thick.
export const strataSpacing = range => Math.min(350, Math.max(12, range / 22))

// How rough each cell is (0 smooth … 1 craggy): steep and high ground is rough, valley floors, river channels, water and
// ice sheets are smooth. Steepness is relative to the steepest cell, so the same landscape reads alike at any grid size.
export function roughness({ elevations, nx, ny, water = null, ice = null, channel = null }) {
  const n = nx * ny, out = new Float32Array(n), slope = new Float32Array(n)
  const { min, range } = reliefRange(elevations)
  const at = (x, y) => elevations[Math.min(ny - 1, Math.max(0, y)) * nx + Math.min(nx - 1, Math.max(0, x))] ?? min
  let steepest = 1e-9
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    const g = Math.hypot(at(x + 1, y) - at(x - 1, y), at(x, y + 1) - at(x, y - 1))
    slope[y * nx + x] = g; if (g > steepest) steepest = g
  }
  const smooth = (a, b, v) => { const t = Math.min(1, Math.max(0, (v - a) / (b - a))); return t * t * (3 - 2 * t) }
  for (let i = 0; i < n; i++) {
    const e = elevations[i]
    if (e === null || e === undefined || !Number.isFinite(e) || range <= 0) continue
    const steep = smooth(0.08, 0.55, slope[i] / steepest), high = smooth(0.3, 0.85, (e - min) / range)
    let w = Math.min(1, steep * 0.85 + high * 0.55)
    if (water) w *= 1 - Math.min(1, water[i])
    if (ice) w *= 1 - 0.96 * Math.min(1, ice[i])   // ice sheets are smooth; nunataks poke through
    if (channel?.[i]) w *= 0.15
    out[i] = w
  }
  return out
}
