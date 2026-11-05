// Ocean carbon and habitability support: validation of the carbon block of the ocean timeline (atmospheric CO2
// over land and sea, the ocean's CO2 uptake, pH and aragonite saturation, oxygen, habitat classes and dead
// zones), per-frame summaries, dead-zone patches, water-column profiles, time series and colours. No chemistry runs
// in the browser: the viewer checks the engine output and composes the view from it.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }
const close = (a, b, rel, abs = 0) => Math.abs(a - b) <= Math.max(rel * Math.max(Math.abs(a), Math.abs(b)), abs)
const cellArray = (v, count) => Array.isArray(v) && v.length === count && v.every(finite)

export const CARBON_METHOD = 'z_level_npzd_lite_carbonate_wanninkhof_metabolic_index'
export const MT_PER_MOL = 44.01e-12

// Habitat classes of a water level (and of a column: its worst level).
export const HABITAT_CLASSES = [
  { key: 'habitable', label: 'Habitable', color: '#2bb3a3', tooltip: 'Enough oxygen for fish and shellfish, water not corrosive to shells.' },
  { key: 'stressed', label: 'Stressed', color: '#e9c46a', tooltip: 'Corrosive to aragonite shells (Ω < 1) or too little oxygen for the warm water\'s demand (metabolic index below 3).' },
  { key: 'hypoxic', label: 'Hypoxic · dead zone', color: '#e4572e', tooltip: 'Below 60 µmol/kg (about 2 mg/L): fish and shrimp leave or die.' },
  { key: 'anoxic', label: 'Anoxic', color: '#2a0410', tooltip: 'Practically no oxygen: only microbes; nitrate and then sulfate are breathed (hydrogen sulfide).' },
]
export const habitatInfo = cls => HABITAT_CLASSES[cls] ?? { key: 'land', label: 'Land', color: '#3a4a3a', tooltip: '' }

// The sea colour options of the carbon view.
export const CARBON_BASES = [
  { key: 'bottomo2', label: 'Bottom-water oxygen (red hypoxic → blue rich)' },
  { key: 'habitat', label: 'Habitat of the column (worst level)' },
  { key: 'depth', label: 'Oxygen at the chosen depth' },
  { key: 'ph', label: 'Surface pH (acidification)' },
  { key: 'omega', label: 'Surface aragonite saturation Ω' },
  { key: 'omegabottom', label: 'Bottom-water aragonite saturation Ω' },
  { key: 'flux', label: 'Air–sea CO₂ flux (blue uptake ↔ red outgassing)' },
  { key: 'air', label: 'CO₂ in the air above the sea' },
  { key: 'production', label: 'Plankton production' },
  { key: 'nitrate', label: 'Surface nitrate (river fertiliser)' },
]

export function validateCarbon(block, frames) {
  const b = block, grid = frames[0].forecast.grid, count = grid.nx * grid.ny, area = grid.spacing_east_m * grid.spacing_north_m, areaKm2 = area / 1e6
  const mask = b.ocean_mask, top = b.carbon
  check(top && top.method === CARBON_METHOD, 'Unsupported ocean carbon contract.')
  const levels = top.levels_m
  check(Array.isArray(levels) && levels.length >= 1 && levels.length <= 60 && levels.every((v, k) => finite(v) && v > (k ? levels[k - 1] : 0)), 'Carbon levels must deepen.')
  check(cellArray(top.seafloor_depth_m, count) && top.seafloor_depth_m.every((v, i) => mask[i] ? v > 0 : v === 0), 'Invalid seafloor depths.')
  check(cellArray(top.emission_mol_m2_s, count) && top.emission_mol_m2_s.every(v => v >= 0), 'Invalid CO2 emission map.')
  check(Array.isArray(top.sources) && top.sources.every(s => s && typeof s.name === 'string' && finite(s.mt_co2_per_year) && s.mt_co2_per_year >= 0 && Number.isInteger(s.peak_cell) && s.peak_cell >= 0 && s.peak_cell < count), 'Invalid CO2 sources.')
  const emitted = top.emission_mol_m2_s.reduce((a, v) => a + v, 0) * area * 365.25 * 86400 * MT_PER_MOL
  check(close(emitted, top.sources.reduce((a, s) => a + s.mt_co2_per_year, 0), 1e-3, 1e-6), 'The emission map must add up to the sources.')
  const h = top.habitat
  check(h && finite(h.hypoxia_umol_kg) && finite(h.anoxia_umol_kg) && h.anoxia_umol_kg <= h.hypoxia_umol_kg && finite(h.corrosive_omega) && finite(h.dead_zone_column_fraction), 'Invalid habitat thresholds.')
  check(Array.isArray(top.background_ppm) && top.background_ppm.every(p => Array.isArray(p) && p.length === 2 && p.every(finite)), 'Invalid CO2 background scenario.')
  const L = levels.length, tops = levels.map((v, k) => k ? levels[k - 1] : 0)
  const rivers = (b.forcing_source?.rivers ?? []).map((r, k) => ({ name: r.name, cell: r.mouth_cell, discharge: r.discharge_m3_s, chemistry: top.river_chemistry?.[k] ?? null }))
  const sea = mask.reduce((a, m, i) => (m && a.push(i), a), [])
  let initialExcess = null
  frames.forEach((frame, index) => {
    const c = b.frames[index].carbon
    check(c && finite(c.year) && c.atmosphere && c.ocean, 'Every ocean frame needs its carbon block.')
    const a = c.atmosphere, o = c.ocean
    check(cellArray(a.co2_ppm, count) && a.co2_ppm.every(v => v > 100 && v < 20000), 'Invalid atmospheric CO2 map.')
    const airKeys = ['background_ppm', 'max_ppm', 'ocean_mean_ppm', 'land_mean_ppm', 'emitted_mt_co2', 'land_uptake_mt_co2', 'ocean_uptake_mt_co2', 'ventilation_mt_co2', 'transport_mt_co2', 'background_shift_mt_co2', 'excess_mt_co2', 'initial_excess_mt_co2']
    check(airKeys.every(k => finite(a[k])), 'Invalid atmospheric CO2 totals.')
    check(close(a.max_ppm, Math.max(...a.co2_ppm), 1e-4), 'Peak CO2 disagrees with the map.')
    if (initialExcess === null) initialExcess = a.initial_excess_mt_co2
    const airTerms = a.emitted_mt_co2 - a.land_uptake_mt_co2 - a.ocean_uptake_mt_co2 - a.ventilation_mt_co2 + a.transport_mt_co2 + a.background_shift_mt_co2
    const airScale = ['emitted_mt_co2', 'land_uptake_mt_co2', 'ocean_uptake_mt_co2', 'ventilation_mt_co2', 'transport_mt_co2', 'background_shift_mt_co2'].reduce((s, k) => s + Math.abs(a[k]), 0)
    check(close(a.excess_mt_co2 - initialExcess, airTerms, 1e-3, 1e-4 * airScale + 1e-3), 'Atmospheric CO2 budget: the excess changes by emissions - land and ocean uptake - ventilation + transport + background shift.')
    const maps = ['co2_flux_mol_m2_yr', 'surface_pco2_uatm', 'surface_ph', 'surface_omega_aragonite', 'bottom_omega_aragonite', 'bottom_oxygen_umol_kg', 'minimum_oxygen_umol_kg', 'minimum_oxygen_depth_m', 'habitable_fraction', 'production_g_c_m2_day', 'surface_nitrate_umol_kg']
    check(maps.every(k => cellArray(o[k], count)), 'Invalid ocean carbon maps.')
    check(Array.isArray(o.column_class) && o.column_class.length === count && o.column_class.every(v => Number.isInteger(v) && v >= -1 && v <= 3), 'Invalid habitat classes.')
    for (const key of ['level_oxygen_umol_kg', 'level_ph', 'level_omega_aragonite', 'level_class']) check(Array.isArray(o[key]) && o[key].length === L && o[key].every(row => cellArray(row, count)), 'Level arrays must hold one row per level.')
    check(Array.isArray(o.dead_zone_cells) && o.dead_zone_cells.every(i => Number.isInteger(i) && i >= 0 && i < count && mask[i]) && new Set(o.dead_zone_cells).size === o.dead_zone_cells.length, 'Dead zones lie at sea.')
    const dead = new Uint8Array(count)
    o.dead_zone_cells.forEach(i => { dead[i] = 1 })
    let anoxic = 0, corrosive = 0
    for (let i = 0; i < count; i++) {
      if (!mask[i]) {
        check(o.column_class[i] === -1 && maps.every(k => o[k][i] === 0) && o.level_class.every(row => row[i] === -1), 'Land cells carry no sea chemistry.')
        continue
      }
      const present = o.level_class.map(row => row[i] >= 0)
      const n = present.filter(Boolean).length
      check(n >= 1 && present.every((p, k) => p === k < n), 'Each column runs from the surface down without gaps.')
      check(o.column_class[i] === Math.max(...o.level_class.slice(0, n).map(row => row[i])), 'A column takes the class of its worst level.')
      check(o.habitable_fraction[i] >= 0 && o.habitable_fraction[i] <= 1 && o.surface_ph[i] > 6 && o.surface_ph[i] < 9 && o.surface_omega_aragonite[i] >= 0, 'Invalid surface chemistry.')
      check(close(o.bottom_oxygen_umol_kg[i], o.level_oxygen_umol_kg[n - 1][i], 5e-3, 0.01), 'Bottom oxygen is the deepest level\'s.')
      if (o.bottom_oxygen_umol_kg[i] < h.hypoxia_umol_kg * 0.999) check(dead[i], 'Hypoxic bottom water makes a dead zone.')
      if (dead[i]) check(o.column_class[i] >= 2, 'A dead zone has hypoxic water.')
      if (o.column_class[i] === 3) anoxic++
      if (o.surface_omega_aragonite[i] < h.corrosive_omega || o.bottom_omega_aragonite[i] < h.corrosive_omega) corrosive++
    }
    const t = o.totals
    check(t && ['sea_area_km2', 'dead_zone_area_km2', 'anoxic_area_km2', 'corrosive_area_km2', 'volume_km3', 'hypoxic_volume_km3', 'habitable_volume_pct', 'dead_zone_pct', 'mean_surface_ph', 'mean_surface_omega_aragonite', 'uptake_mt_co2_yr', 'production_mt_c_yr'].every(k => finite(t[k])), 'Invalid carbon totals.')
    check(close(t.sea_area_km2, sea.length * areaKm2, 1e-4) && close(t.dead_zone_area_km2, o.dead_zone_cells.length * areaKm2, 1e-4, 1e-6) && close(t.anoxic_area_km2, anoxic * areaKm2, 1e-4, 1e-6) && close(t.corrosive_area_km2, corrosive * areaKm2, 1e-4, 1e-6), 'Carbon area totals disagree with the maps.')
    check(close(t.uptake_mt_co2_yr, sea.reduce((s, i) => s + o.co2_flux_mol_m2_yr[i], 0) * area * MT_PER_MOL, 2e-3, 0.05), 'Ocean uptake disagrees with the flux map.')
    const g = o.budget
    const budgetKeys = ['carbon_inventory_mol', 'carbon_change_mol', 'air_sea_carbon_mol', 'river_carbon_mol', 'export_carbon_mol', 'ventilation_carbon_mol', 'boundary_carbon_mol', 'transport_carbon_mol', 'nitrogen_change_mol', 'river_nitrogen_mol', 'export_nitrogen_mol', 'ventilation_nitrogen_mol', 'boundary_nitrogen_mol', 'denitrified_nitrogen_mol', 'transport_nitrogen_mol']
    check(g && budgetKeys.every(k => finite(g[k])), 'Invalid ocean carbon budget.')
    const carbon = g.air_sea_carbon_mol + g.river_carbon_mol - g.export_carbon_mol + g.ventilation_carbon_mol + g.boundary_carbon_mol + g.transport_carbon_mol
    check(close(g.carbon_change_mol, carbon, 1e-6, 1e-9 * g.carbon_inventory_mol), 'Ocean carbon budget: air-sea + rivers - export + ventilation + open boundary must explain the change.')
    check(Math.abs(g.transport_carbon_mol) <= 1e-6 * g.carbon_inventory_mol, 'The transport must conserve carbon.')
    const nitrogen = g.river_nitrogen_mol - g.export_nitrogen_mol + g.ventilation_nitrogen_mol + g.boundary_nitrogen_mol - g.denitrified_nitrogen_mol + g.transport_nitrogen_mol
    check(close(g.nitrogen_change_mol, nitrogen, 1e-6, 1e-9 * g.carbon_inventory_mol), 'Ocean nitrogen budget does not close.')
    if (index === 0) check(budgetKeys.filter(k => k !== 'carbon_inventory_mol').every(k => g[k] === 0), 'The first frame starts the carbon budget.')
    frame.carbon = {
      year: c.year, levels, tops, seafloor: top.seafloor_depth_m, emission: top.emission_mol_m2_s, sources: top.sources, habitat: h, rivers, config: top, mask,
      ppm: a.co2_ppm, background: a.background_ppm, maxPpm: a.max_ppm, oceanPpm: a.ocean_mean_ppm, landPpm: a.land_mean_ppm,
      air: { emitted: a.emitted_mt_co2, land: a.land_uptake_mt_co2, ocean: a.ocean_uptake_mt_co2, ventilation: a.ventilation_mt_co2, transport: a.transport_mt_co2, shift: a.background_shift_mt_co2, excess: a.excess_mt_co2, initialExcess },
      flux: o.co2_flux_mol_m2_yr, pco2: o.surface_pco2_uatm, ph: o.surface_ph, omega: o.surface_omega_aragonite, omegaBottom: o.bottom_omega_aragonite,
      o2Bottom: o.bottom_oxygen_umol_kg, o2Min: o.minimum_oxygen_umol_kg, o2MinDepth: o.minimum_oxygen_depth_m, habitable: o.habitable_fraction,
      production: o.production_g_c_m2_day, nitrate: o.surface_nitrate_umol_kg, columnClass: o.column_class, dead, deadCells: o.dead_zone_cells,
      levelO2: o.level_oxygen_umol_kg, levelPh: o.level_ph, levelOmega: o.level_omega_aragonite, levelClass: o.level_class,
      totals: t, budget: g, averagingHours: o.averaging_hours,
    }
  })
}

// Connected dead zones (4-neighbours) with their area, the lowest bottom oxygen, anoxic and corrosive cells, the
// cell at their centre and the nearest river mouth; largest first.
export function deadZonePatches(frame) {
  const c = frame.carbon, grid = frame.forecast.grid, nx = grid.nx, count = nx * grid.ny, areaKm2 = grid.spacing_east_m * grid.spacing_north_m / 1e6
  if (!c) return []
  const seen = new Uint8Array(count), patches = []
  for (const start of c.deadCells) {
    if (seen[start]) continue
    const cells = [], stack = [start]
    seen[start] = 1
    while (stack.length) {
      const i = stack.pop(), x = i % nx, y = Math.floor(i / nx)
      cells.push(i)
      for (const j of [x + 1 < nx ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < grid.ny ? i + nx : -1, y > 0 ? i - nx : -1]) if (j >= 0 && c.dead[j] && !seen[j]) { seen[j] = 1; stack.push(j) }
    }
    let sx = 0, sy = 0, worst = cells[0], anoxic = 0, corrosive = 0, depth = 0
    for (const i of cells) {
      sx += i % nx; sy += Math.floor(i / nx); depth += c.seafloor[i]
      if (c.o2Bottom[i] < c.o2Bottom[worst]) worst = i
      if (c.columnClass[i] === 3) anoxic++
      if (c.omegaBottom[i] < c.habitat.corrosive_omega) corrosive++
    }
    const cx = sx / cells.length, cy = sy / cells.length
    const centre = cells.reduce((best, i) => Math.hypot(i % nx - cx, Math.floor(i / nx) - cy) < Math.hypot(best % nx - cx, Math.floor(best / nx) - cy) ? i : best, cells[0])
    // The nearest river mouth: distance from the mouth to the closest cell of the patch.
    let river = null, distance = Infinity
    for (const r of c.rivers) {
      if (!(r.cell >= 0)) continue
      for (const i of cells) {
        const d = Math.hypot(r.cell % nx - i % nx, Math.floor(r.cell / nx) - Math.floor(i / nx)) * Math.sqrt(areaKm2)
        if (d < distance) { distance = d; river = r.name }
      }
    }
    patches.push({ cells, area_km2: cells.length * areaKm2, min_bottom_oxygen_umol_kg: c.o2Bottom[worst], worst_cell: worst, centre_cell: centre, anoxic_cells: anoxic, corrosive_cells: corrosive,
      mean_depth_m: depth / cells.length, nearest_river: river, river_distance_km: river === null ? null : distance })
  }
  return patches.sort((p, q) => q.area_km2 - p.area_km2)
}

// Metrics of one frame for the metrics strip.
export function summarizeCarbon(frame) {
  const c = frame.carbon
  if (!c) return null
  const mask = c.mask, t = c.totals, sea = [], land = []
  mask.forEach((m, i) => (m ? sea : land).push(i))
  const shelf = sea.filter(i => c.seafloor[i] < 100)
  const mean = (values, cells) => cells.length ? cells.reduce((s, i) => s + values[i], 0) / cells.length : null
  const peak = c.ppm.reduce((best, v, i) => v > c.ppm[best] ? i : best, 0)
  const nearestSource = c.sources.reduce((best, s) => best === null || Math.abs(s.peak_cell - peak) < Math.abs(best.peak_cell - peak) ? s : best, null)
  const patches = deadZonePatches(frame)
  const levels = c.levels.length
  const stressed = sea.filter(i => c.columnClass[i] === 1).length
  return {
    year: c.year, background_ppm: c.background, ocean_ppm: c.oceanPpm, land_ppm: c.landPpm, max_ppm: c.maxPpm, max_excess_ppm: c.maxPpm - c.background, peak_cell: peak,
    peak_source: nearestSource?.name ?? null, emissions_mt_yr: c.sources.reduce((s, x) => s + x.mt_co2_per_year, 0),
    uptake_mt_co2_yr: t.uptake_mt_co2_yr, mean_flux_mol_m2_yr: mean(c.flux, sea), uptake_share_pct: sea.filter(i => c.flux[i] > 0).length / Math.max(1, sea.length) * 100,
    mean_ph: t.mean_surface_ph, min_ph: Math.min(...sea.map(i => c.ph[i])), mean_omega: t.mean_surface_omega_aragonite, min_bottom_omega: Math.min(...sea.map(i => c.omegaBottom[i])),
    mean_pco2_uatm: mean(c.pco2, sea),
    dead_zone_km2: t.dead_zone_area_km2, dead_zone_pct: t.dead_zone_pct, anoxic_km2: t.anoxic_area_km2, corrosive_km2: t.corrosive_area_km2,
    stressed_km2: stressed * t.sea_area_km2 / Math.max(1, sea.length), sea_km2: t.sea_area_km2,
    habitable_volume_pct: t.habitable_volume_pct, hypoxic_volume_km3: t.hypoxic_volume_km3, volume_km3: t.volume_km3,
    production_mt_c_yr: t.production_mt_c_yr, mean_production_g_m2_day: mean(c.production, sea),
    shelf_bottom_oxygen_umol_kg: mean(c.o2Bottom, shelf), min_bottom_oxygen_umol_kg: Math.min(...sea.map(i => c.o2Bottom[i])),
    patches, largest_patch_km2: patches[0]?.area_km2 ?? 0, levels,
  }
}

// Time series over the frames, for the charts.
export function carbonSeries(frames) {
  return frames.filter(f => f.carbon).map(f => {
    const c = f.carbon, t = c.totals
    return {
      elapsed_seconds: f.forecast.elapsed_seconds, year: c.year,
      dead_zone_km2: t.dead_zone_area_km2, anoxic_km2: t.anoxic_area_km2, corrosive_km2: t.corrosive_area_km2,
      uptake_mt_co2_yr: t.uptake_mt_co2_yr, mean_ph: t.mean_surface_ph, mean_omega: t.mean_surface_omega_aragonite,
      background_ppm: c.background, ocean_ppm: c.oceanPpm, land_ppm: c.landPpm, max_ppm: c.maxPpm,
      production_mt_c_yr: t.production_mt_c_yr, habitable_volume_pct: t.habitable_volume_pct,
      cumulative_uptake_mt_co2: c.budget.air_sea_carbon_mol * MT_PER_MOL,
    }
  })
}

// Whole-run summary (annual maxima and means) of a series: the numbers to compare scenarios.
export function carbonRunSummary(series) {
  if (!series.length) return null
  const avg = k => series.reduce((s, p) => s + p[k], 0) / series.length
  const max = k => Math.max(...series.map(p => p[k])), min = k => Math.min(...series.map(p => p[k]))
  const peak = series.reduce((best, p) => p.dead_zone_km2 > best.dead_zone_km2 ? p : best, series[0])
  return {
    first_year: series[0].year, last_year: series.at(-1).year, background_ppm: avg('background_ppm'),
    mean_ph: avg('mean_ph'), min_ph: min('mean_ph'), mean_omega: avg('mean_omega'),
    max_dead_zone_km2: max('dead_zone_km2'), peak_dead_zone_year: peak.year, max_anoxic_km2: max('anoxic_km2'), max_corrosive_km2: max('corrosive_km2'),
    mean_uptake_mt_co2_yr: avg('uptake_mt_co2_yr'), total_uptake_mt_co2: series.at(-1).cumulative_uptake_mt_co2,
    mean_habitable_volume_pct: avg('habitable_volume_pct'), min_habitable_volume_pct: min('habitable_volume_pct'),
    mean_production_mt_c_yr: avg('production_mt_c_yr'),
  }
}

// The water column of one sea cell: per level the depth range, O2, pH, aragonite saturation and class.
export function carbonColumn(frame, cell) {
  const c = frame.carbon
  if (!c || !c.mask[cell]) return null
  const levels = [], shelf = c.seafloor[cell] <= c.levels.at(-1)
  for (let k = 0; k < c.levels.length; k++) {
    if (c.levelClass[k][cell] < 0) break
    // The deepest level of a shelf column reaches the seafloor (a thin remainder joins it); open columns stop at the last level.
    const last = k + 1 === c.levels.length || c.levelClass[k + 1][cell] < 0
    const bottom = last && shelf ? c.seafloor[cell] : c.levels[k]
    levels.push({ level: k, top: c.tops[k], bottom, oxygen_umol_kg: c.levelO2[k][cell], ph: c.levelPh[k][cell], omega: c.levelOmega[k][cell], cls: c.levelClass[k][cell] })
  }
  const deepest = levels.at(-1)
  return {
    cell, levels, seafloor_m: c.seafloor[cell], shelf, bottom_m: deepest.bottom,
    dead_zone: !!c.dead[cell], column_class: c.columnClass[cell], habitable_fraction: c.habitable[cell],
    hypoxia: c.habitat.hypoxia_umol_kg, anoxia: c.habitat.anoxia_umol_kg, corrosive: c.habitat.corrosive_omega,
    hypoxic_m: levels.filter(l => l.cls >= 2).reduce((s, l) => s + l.bottom - l.top, 0),
  }
}

// Time-depth section of the oxygen of one cell over the frames (rectangles for a Hovmoeller plot).
export function oxygenSection(frames, cell) {
  const usable = frames.filter(f => f.carbon?.mask[cell])
  if (!usable.length) return null
  const times = usable.map(f => f.forecast.elapsed_seconds)
  const edges = times.map((t, k) => [k === 0 ? t : (t + times[k - 1]) / 2, k + 1 === times.length ? t : (t + times[k + 1]) / 2])
  if (times.length === 1) edges[0] = [times[0], times[0] + 1]
  const rects = []
  let depth = 0
  usable.forEach((f, k) => {
    const p = carbonColumn(f, cell)
    depth = Math.max(depth, p.bottom_m)
    for (const l of p.levels) rects.push({ t0: edges[k][0], t1: edges[k][1], z0: l.top, z1: l.bottom, oxygen_umol_kg: l.oxygen_umol_kg, cls: l.cls })
  })
  return { cell, start: edges[0][0], end: edges.at(-1)[1], max_depth_m: depth, rects }
}

// The sea cell to show by default: the lowest bottom oxygen over the run (the heart of the worst dead zone).
export function worstCell(frames) {
  let best = null, low = Infinity
  for (const f of frames) {
    const c = f.carbon
    if (!c) continue
    for (const i of c.deadCells.length ? c.deadCells : c.mask.map((m, i) => (m ? i : -1)).filter(i => i >= 0)) if (c.o2Bottom[i] < low) { low = c.o2Bottom[i]; best = i }
  }
  return best
}

// The carbon budgets since the start of the record, in Mt CO2 (ocean and air) for the budget table.
export function carbonBudget(frame) {
  const c = frame.carbon
  if (!c) return null
  const g = c.budget, mt = v => v * MT_PER_MOL
  return {
    ocean: [
      { key: 'air_sea', label: 'Taken up from the air', value: mt(g.air_sea_carbon_mol) },
      { key: 'river', label: 'Brought by rivers', value: mt(g.river_carbon_mol) },
      { key: 'export', label: 'Sunk out as organic matter and shells', value: -mt(g.export_carbon_mol) },
      { key: 'ventilation', label: 'Exchanged with the deep ocean', value: mt(g.ventilation_carbon_mol) },
      { key: 'boundary', label: 'Exchanged with the open ocean (boundary)', value: mt(g.boundary_carbon_mol) },
      { key: 'transport', label: 'Numerical residual', value: mt(g.transport_carbon_mol) },
    ],
    ocean_change: mt(g.carbon_change_mol),
    air: [
      { key: 'emitted', label: 'Emitted by the sources', value: c.air.emitted },
      { key: 'land', label: 'Taken up by vegetation (net)', value: -c.air.land },
      { key: 'ocean', label: 'Taken up by the sea', value: -c.air.ocean },
      { key: 'ventilation', label: 'Mixed into the free troposphere', value: -c.air.ventilation },
      { key: 'transport', label: 'Blown in (+) or out (−) of the region', value: c.air.transport },
      { key: 'shift', label: 'Background change', value: c.air.shift },
    ],
    air_change: c.air.excess - c.air.initialExcess,
    denitrified_kt_n: g.denitrified_nitrogen_mol * 14.007e-9, river_kt_n: g.river_nitrogen_mol * 14.007e-9,
  }
}

// Colours (CSS strings that THREE.Color and SVG both accept).
const ramp = (stops, t) => {
  const w = Math.max(0, Math.min(1, t))
  let a = stops[0], b = stops.at(-1)
  for (let i = 0; i + 1 < stops.length; i++) if (w >= stops[i][0] && w <= stops[i + 1][0]) { a = stops[i]; b = stops[i + 1]; break }
  const f = (w - a[0]) / ((b[0] - a[0]) || 1)
  const c = a[1].map((v, i) => Math.round(v + (b[1][i] - v) * f))
  return `rgb(${c[0]},${c[1]},${c[2]})`
}
// Oxygen: black (anoxic) -> red (hypoxic, 60) -> orange -> yellow -> teal -> blue (rich, 260 umol/kg).
export function oxygenLevelColor(umolKg) {
  return ramp([[0, [25, 4, 12]], [0.02, [150, 20, 35]], [0.23, [228, 60, 45]], [0.4, [245, 160, 60]], [0.55, [235, 220, 110]], [0.75, [60, 180, 170]], [1, [40, 90, 200]]], umolKg / 260)
}
// CO2 in the air relative to the background: green-blue drawdown <- 0 (pale) -> orange -> red (+range ppm).
export function ppmColor(excess, range = 8) {
  const t = Math.max(-1, Math.min(1, excess / range))
  if (t < 0) return ramp([[0, [70, 170, 140]], [1, [200, 210, 200]]], 1 + t)
  return ramp([[0, [205, 205, 195]], [0.4, [240, 190, 90]], [0.75, [230, 110, 50]], [1, [180, 30, 40]]], t)
}
// Air-sea CO2 flux: uptake (into the sea, positive) blue <- 0 -> red outgassing, saturating at +-range mol/m2/yr.
export function fluxColor(molM2Yr, range = 3) {
  const t = Math.max(-1, Math.min(1, molM2Yr / range))
  if (t >= 0) return ramp([[0, [225, 228, 232]], [1, [30, 90, 200]]], t)
  return ramp([[0, [235, 228, 225]], [1, [205, 50, 40]]], -t)
}
// Surface pH: acid (lo, orange-red) -> hi (deep blue); by default 7.6 -> 8.2.
export function phColor(ph, lo = 7.6, hi = 8.2) {
  return ramp([[0, [200, 60, 40]], [0.35, [235, 150, 60]], [0.6, [160, 200, 170]], [1, [40, 90, 190]]], (ph - lo) / ((hi - lo) || 1))
}

// The pH range of a run for the map colours: its lowest and highest surface pH, at least 0.1 wide.
export function phRange(frames) {
  let lo = Infinity, hi = -Infinity
  for (const f of frames) {
    const c = f.carbon
    if (!c) continue
    c.ph.forEach((v, i) => { if (c.mask[i]) { lo = Math.min(lo, v); hi = Math.max(hi, v) } })
  }
  if (!(hi >= lo)) return [7.6, 8.2]
  if (hi - lo < 0.1) { const mid = (hi + lo) / 2; lo = mid - 0.05; hi = mid + 0.05 }
  return [lo, hi]
}
// Aragonite saturation: corrosive below 1 (magenta), 1-2 orange, 2-3 yellow, >3 blue (shell builders thrive).
export function omegaColor(omega) {
  if (omega < 1) return ramp([[0, [120, 20, 90]], [1, [215, 60, 160]]], omega)
  return ramp([[0, [240, 140, 60]], [0.33, [235, 215, 100]], [0.66, [100, 190, 200]], [1, [40, 90, 190]]], (omega - 1) / 3)
}
// Production: dark (none) -> green -> yellow-green (bloom, 3 g C/m2/day).
export function productionColor(g) {
  return ramp([[0, [15, 40, 60]], [0.15, [30, 110, 90]], [0.5, [90, 180, 80]], [1, [220, 230, 90]]], Math.sqrt(Math.max(0, g) / 3))
}
// Nitrate: nutrient-poor blue -> green -> brown eutrophic (30 umol/kg and more).
export function nitrateColor(n) {
  return ramp([[0, [20, 50, 110]], [0.3, [40, 140, 120]], [0.6, [150, 170, 60]], [1, [140, 90, 40]]], Math.log10(1 + Math.max(0, n)) / Math.log10(31))
}
export const habitatColor = cls => habitatInfo(cls).color
