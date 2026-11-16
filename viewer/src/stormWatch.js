// The storm watch: tornado watch areas, supercells, where tornadoes are possible, and the hurricanes, all taken from the
// engine's own output (severe convection v1 and storm systems v1). Nothing here is estimated: the view only selects and
// arranges what the model computed, and states the rule where it selects.
//   - Tornado watch: the engine's `tornado_possible` cells (moderate or significant STP under a cumulonimbus), plus the
//     neighbouring cells whose environment the engine rates moderate or significant: like a real watch, the area where
//     conditions favour tornadoes around the active storms, not a warning of a tornado.
//   - Supercells: cells where the engine has a cumulonimbus, a broken cap and |storm-relative helicity| >= 100 m2/s2
//     (enough to turn an updraft into a rotating storm), moving with the engine's Bunkers storm motion.
//   - Hurricanes: the engine's tracked storms, with their radius of maximum wind, maximum wind, category and track; the
//     gale radius is read from the engine's storm wind field (>= 17 m/s).
// Pure functions; the 3D drawing is in stormScene.js.

export const SUPERCELL_HELICITY = 100     // m2/s2: the usual threshold for rotating (supercell) storms
export const GALE_M_S = 17                 // tropical-storm force, the edge of a cyclone's wind field
// How fast things turn in the drawing. Rotation is sped up, keeping the order (faster where the model's rotation is
// stronger): a 50 m/s hurricane with a 40 km core turns at about 0.8 rad/s.
export const HURRICANE_SPIN_LAPSE = 640
export const supercellSpin = helicity => 0.25 + Math.min(1.5, Math.abs(helicity) / 300)

const neighbours = (i, nx, ny) => {
  const x = i % nx, y = Math.floor(i / nx), out = []
  for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) if ((a || b) && x + a >= 0 && y + b >= 0 && x + a < nx && y + b < ny) out.push((y + b) * nx + x + a)
  return out
}

// The watch mask and its regions (8-connected), each with its strongest cell.
export function tornadoWatch(severe, nx, ny) {
  const n = nx * ny, watch = new Uint8Array(n)
  for (let i = 0; i < n; i++) if (severe.possible[i]) {
    watch[i] = 1
    for (const j of neighbours(i, nx, ny)) if (severe.risk[j] === 'moderate' || severe.risk[j] === 'significant') watch[j] = 1
  }
  const seen = new Uint8Array(n), regions = []
  for (let i = 0; i < n; i++) {
    if (!watch[i] || seen[i]) continue
    const cells = [], stack = [i]; seen[i] = 1
    while (stack.length) { const c = stack.pop(); cells.push(c); for (const j of neighbours(c, nx, ny)) if (watch[j] && !seen[j]) { seen[j] = 1; stack.push(j) } }
    const strongest = cells.reduce((a, c) => (severe.stp[c] > severe.stp[a] ? c : a), cells[0])
    regions.push({ cells, strongest, maxStp: severe.stp[strongest], possible: cells.filter(c => severe.possible[c]).length,
      maxHelicity: Math.max(...cells.map(c => Math.abs(severe.helicity[c]))), maxCape: Math.max(...cells.map(c => severe.cape[c] ?? 0)) })
  }
  regions.sort((a, b) => b.maxStp - a.maxStp)
  return { watch, regions, cells: watch.reduce((a, v) => a + v, 0) }
}

// The outline of a mask: the edges between cells inside and outside, as segments in grid coordinates (cell x spans
// x..x+1, row y spans y..y+1, row 0 in the south).
export function maskOutline(mask, nx, ny) {
  const segments = [], inside = (x, y) => x >= 0 && y >= 0 && x < nx && y < ny && !!mask[y * nx + x]
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    if (!inside(x, y)) continue
    if (!inside(x - 1, y)) segments.push([x, y, x, y + 1])
    if (!inside(x + 1, y)) segments.push([x + 1, y, x + 1, y + 1])
    if (!inside(x, y - 1)) segments.push([x, y, x + 1, y])
    if (!inside(x, y + 1)) segments.push([x, y + 1, x + 1, y + 1])
  }
  return segments
}

// Supercells of a frame, strongest first.
export function supercells(frame) {
  const s = frame.severe, c = frame.clouds
  if (!s || !c) return []
  const out = []
  for (let i = 0; i < s.stp.length; i++) {
    if (c.type[i] !== 'cumulonimbus' || !s.capBroken[i] || Math.abs(s.helicity[i]) < SUPERCELL_HELICITY) continue
    const me = s.motionEast[i], mn = s.motionNorth[i]
    out.push({ i, stp: s.stp[i], helicity: s.helicity[i], risk: s.risk[i], possible: !!s.possible[i], motionEast: me, motionNorth: mn, speed: Math.hypot(me, mn),
      lcl: s.lcl[i], cape: s.cape[i], shear1: s.shear1[i], shear6: s.shear6[i], base: c.base[i], top: c.top[i], spin: supercellSpin(s.helicity[i]), cyclonic: s.helicity[i] >= 0 })
  }
  return out.sort((a, b) => b.stp - a.stp)
}

// A hurricane as drawn: centre, core and gale radii (m), spin (rad/s in the drawing, sign: + counter-clockwise seen from
// above), category. The gale radius is the farthest cell around this storm where the engine's storm wind reaches gale.
export function hurricaneVisual(storm, stormWind, grid, { northern = true, others = [] } = {}) {
  let gale = storm.radius_max_wind_m * 2
  if (stormWind) for (let i = 0; i < stormWind.length; i++) {
    if (!(stormWind[i] >= GALE_M_S)) continue
    const e = grid.origin_east_m + (i % grid.nx + 0.5) * grid.spacing_east_m, n = grid.origin_north_m + (Math.floor(i / grid.nx) + 0.5) * grid.spacing_north_m
    const r = Math.hypot(e - storm.east_m, n - storm.north_m)
    // Cells nearer another storm belong to that one.
    if (others.some(o => o !== storm && Math.hypot(e - o.east_m, n - o.north_m) < r)) continue
    if (r > gale) gale = r
  }
  gale = Math.min(gale, storm.radius_max_wind_m * 12)
  const spin = HURRICANE_SPIN_LAPSE * storm.max_wind_m_s / storm.radius_max_wind_m * (northern ? 1 : -1)
  return { east: storm.east_m, north: storm.north_m, core: storm.radius_max_wind_m, gale, eye: storm.radius_max_wind_m * 0.45, spin, category: storm.category, wind: storm.max_wind_m_s, overLand: storm.over_land }
}

// The part of a track since a previous position (the nearest track point to it): the path the storm took between two
// frames, for gliding the drawing along the computed track. Always at least the last point.
export function pathSince(track, previous) {
  if (!track?.length) return []
  if (!previous) return [track.at(-1)]
  let best = 0, bestD = Infinity
  track.forEach((p, k) => { const d = Math.hypot(p[0] - previous[0], p[1] - previous[1]); if (d < bestD) { bestD = d; best = k } })
  return track.slice(best)
}

// A point a fraction t (0..1) of the way along a polyline (by length).
export function alongPath(path, t) {
  if (!path.length) return null
  if (path.length === 1 || t <= 0) return path[0]
  const lengths = [0]
  for (let k = 1; k < path.length; k++) lengths.push(lengths[k - 1] + Math.hypot(path[k][0] - path[k - 1][0], path[k][1] - path[k - 1][1]))
  const total = lengths.at(-1), target = Math.min(1, t) * total
  if (total === 0) return path.at(-1)
  const k = lengths.findIndex(l => l >= target)
  if (k <= 0) return path[0]
  const f = (target - lengths[k - 1]) / ((lengths[k] - lengths[k - 1]) || 1)
  return [path[k - 1][0] + (path[k][0] - path[k - 1][0]) * f, path[k - 1][1] + (path[k][1] - path[k - 1][1]) * f]
}

// The watch bulletin of a frame, in words (the report and the data stream use it).
export function watchBulletin(frame, nx, ny, cellKm2) {
  const out = { tornado: null, supercells: supercells(frame), storms: discreteSupercells(frame, nx, ny), hurricanes: frame.storms?.list ?? [] }
  if (frame.severe) {
    const w = tornadoWatch(frame.severe, nx, ny)
    out.tornado = { ...w, areaKm2: w.cells * cellKm2, maxStp: frame.severe.maxStp, possibleCells: frame.severe.possibleCells, capBrokenCells: frame.severe.capBrokenCells }
  }
  return out
}

// ---- Colouring the map of the storm watch ---------------------------------------------------------------------------
// What the ground shows, with the range its colour spans (engine fields only).
export const SEVERE_BASES = [
  { key: 'risk', label: 'Tornado risk (STP class)', needs: 'severe' },
  { key: 'stp', label: 'Significant Tornado Parameter', needs: 'severe', range: [0, 4] },
  { key: 'helicity', label: 'Storm-relative helicity (0–1 km)', needs: 'severe', range: [0, 300], unit: 'm²/s²' },
  { key: 'cape', label: 'Convective energy (heated CAPE)', needs: 'severe', range: [0, 4000], unit: 'J/kg' },
  { key: 'cap', label: 'Cap strength (CIN)', needs: 'severe', range: [0, 250], unit: 'J/kg' },
  { key: 'shear', label: 'Deep-layer shear (0–6 km)', needs: 'severe', range: [0, 40], unit: 'm/s' },
  { key: 'stormwind', label: 'Hurricane wind field', needs: 'storms', range: [0, 70], unit: 'm/s' },
  { key: 'none', label: 'Land and sea only' },
]
export function severeBase(frame, choice) {
  if (choice && choice !== 'auto' && SEVERE_BASES.some(b => b.key === choice && (!b.needs || frame[b.needs]))) return choice
  return frame.storms ? 'stormwind' : frame.severe ? 'risk' : 'none'
}
export function severeValue(frame, base, i) {
  const s = frame.severe
  switch (base) {
    case 'stp': return s?.stp[i] ?? null
    case 'helicity': return s ? Math.abs(s.helicity[i]) : null
    case 'cape': return s?.cape[i] ?? null
    case 'cap': return s?.cap[i] ?? null
    case 'shear': return s?.shear6[i] ?? null
    case 'stormwind': return frame.storms?.env.storm_wind_speed_m_s[i] ?? null
    default: return null
  }
}
// Dark → yellow → orange → magenta, the usual severe-weather palette; the cap is a blue lid instead.
const HOT = [[0x16, 0x24, 0x2d], [0xff, 0xe0, 0x66], [0xff, 0x7f, 0x50], [0xc7, 0x24, 0xb1]], LID = [[0x16, 0x24, 0x2d], [0x3d, 0x7f, 0xc4], [0xa8, 0xd8, 0xff]]
export function severeColour(base, value) {
  const def = SEVERE_BASES.find(b => b.key === base)
  if (!def?.range || value === null || value === undefined || !Number.isFinite(value)) return null
  const t = Math.min(1, Math.max(0, (value - def.range[0]) / (def.range[1] - def.range[0]))), stops = base === 'cap' ? LID : HOT
  const u = t * (stops.length - 1), k = Math.min(stops.length - 2, Math.floor(u)), f = u - k
  const rgb = stops[k].map((c, j) => Math.round(c + (stops[k + 1][j] - c) * f))
  return `#${rgb.map(c => c.toString(16).padStart(2, '0')).join('')}`
}

// Discrete storms. Supercells are storms a few tens of kilometres across, several to an outbreak, so one storm is drawn
// per group of supercell cells: centres are taken in order of the tornado parameter, each at least `spacing` cells (in
// both directions) from the centres before it; every other supercell cell joins its nearest centre, whose anvil spreads
// over it. A storm is tornado-possible when any of its cells is.
export function discreteSupercells(frame, nx, ny, spacing = 2) {
  const all = supercells(frame), centres = []
  const xy = i => [i % nx, Math.floor(i / nx)], apart = (a, b) => { const [ax, ay] = xy(a), [bx, by] = xy(b); return Math.max(Math.abs(ax - bx), Math.abs(ay - by)) }
  for (const c of all) if (centres.every(s => apart(s.i, c.i) >= spacing)) centres.push({ ...c, members: [] })
  for (const c of all) {
    let best = centres[0], bestD = Infinity
    for (const s of centres) { const [ax, ay] = xy(s.i), [bx, by] = xy(c.i), d = Math.hypot(ax - bx, ay - by); if (d < bestD) { bestD = d; best = s } }
    best?.members.push(c.i)
  }
  const byCell = new Map(all.map(c => [c.i, c]))
  for (const s of centres) { s.possible = s.members.some(m => byCell.get(m).possible); s.maxStp = Math.max(...s.members.map(m => byCell.get(m).stp)) }
  return centres
}

// Why there is no tornado watch, from the engine's fields: the cap holds, or it broke but the parameter stayed low, or
// the air is uncapped but lacks rotation (helicity) or instability (CAPE).
export function noWatchReason(severe) {
  const maxSrh = Math.max(0, ...severe.helicity.map(Math.abs)), maxCape = Math.max(0, ...severe.cape)
  if (severe.capBrokenCells > 0) return `the cap is broken in ${severe.capBrokenCells} cells, but the tornado parameter stays below the moderate class there.`
  if (severe.cappedCells > 0) return 'the capping inversion holds: the convective energy is stored, not released.'
  if (maxSrh < 100) return `the air is not capped and holds up to ${Math.round(maxCape).toLocaleString('en-US')} J/kg of convective energy, but there is too little low-level rotation: storm-relative helicity at most ${Math.round(maxSrh)} m²/s² (supercells need about 100 or more).`
  if (maxCape < 500) return `there is low-level rotation (helicity up to ${Math.round(maxSrh)} m²/s²) but too little convective energy (CAPE at most ${Math.round(maxCape)} J/kg).`
  return 'the ingredients do not come together in the same place (instability, rotation, a low cloud base and no cap).'
}
