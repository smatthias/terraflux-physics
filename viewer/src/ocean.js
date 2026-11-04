// Ocean circulation support: validation of the documented
// terraflux.ocean.circulation/v1 block (currents, thickness, salinity,
// temperature, deep-water formation, streams and river exchange points),
// styling for the ocean-streams view and per-frame metrics. No dynamics run in
// the browser: the viewer checks the engine output and composes layers from it.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const SCHEMA = 'terraflux.ocean.circulation/v1'
export const METHOD = 'reduced_gravity_c_grid_sverdrup_stommel_formation'

const FRAME_ARRAYS = ['u_m_s', 'v_m_s', 'thickness_m', 'salinity_psu', 'surface_salinity_psu', 'temperature_k', 'sinking_m_s', 'front', 'stream_id']
const FRAME_NUMBERS = ['overturning_sv', 'mean_upwelling_m_s', 'river_input_m3_s', 'net_precipitation_m3_s', 'sponge_volume_m3_s', 'sponge_salt_kg_s', 'volume_m3', 'salt_kg',
  'kinetic_energy_j', 'volume_balance_error_m3', 'salt_balance_error_kg', 'formation_area_km2', 'mean_salinity_psu', 'min_salinity_psu', 'max_speed_m_s']
const DIRECTIONS = ['northward', 'southward', 'eastward', 'westward']
export const PROCESSES = ['rain', 'coastal_runoff', 'air_sea', 'thermal_layers', 'sea_ice', 'ice_sheet', 'warming', 'hosing']
export const LENS_KINDS = ['rain', 'coastal_runoff', 'river', 'glacial', 'other']
const ICE_TO_WATER = 0.917, GLOBAL_OCEAN_AREA_M2 = 3.618e14
const FLUXES = ['shortwave_w_m2', 'longwave_w_m2', 'sensible_w_m2', 'latent_w_m2', 'rain_heat_w_m2', 'net_heat_w_m2']
const close = (a, b, rel, abs = 0) => Math.abs(a - b) <= Math.max(rel * Math.max(Math.abs(a), Math.abs(b)), abs)
const cellArray = (v, count) => Array.isArray(v) && v.length === count && v.every(finite)

export function validateOcean(block, frames, terrain) {
  const b = block, grid = frames[0].forecast.grid, count = grid.nx * grid.ny
  check(b?.schema_version === SCHEMA && b.method === METHOD, 'Unsupported ocean contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Ocean source must be engine_output or synthetic.')
  check(terrain && Array.isArray(terrain.values) && terrain.values.length === count, 'Ocean circulation requires the terrain grid.')
  check(b.request && b.request.physics && finite(b.request.physics.reference_salinity_psu) && finite(b.request.physics.mixed_layer_depth_m) && b.request.ocean && finite(b.request.ocean.initial_thickness_m), 'Invalid ocean request echo.')
  check(Array.isArray(b.ocean_mask) && b.ocean_mask.length === count && b.ocean_mask.every(v => typeof v === 'boolean') && b.ocean_mask.some(Boolean), 'Invalid ocean mask.')
  check(Array.isArray(b.sponge) && b.sponge.length === count && b.sponge.every((v, i) => finite(v) && v >= 0 && v <= 1 && (b.ocean_mask[i] || v === 0)), 'Invalid sponge strengths.')
  check(finite(b.initial_volume_m3) && b.initial_volume_m3 > 0 && finite(b.initial_salt_kg) && b.initial_salt_kg >= 0, 'Invalid initial ocean volume or salt.')
  check(Array.isArray(b.frames) && b.frames.length === frames.length, 'Ocean frame count mismatch.')
  const mask = b.ocean_mask, physics = b.request.physics
  // Extensions: rain on the sea and the coast, air-sea exchange, the layered column.
  const processes = b.processes ?? []
  check(Array.isArray(processes) && processes.every(p => PROCESSES.includes(p)) && new Set(processes).size === processes.length, 'Invalid ocean processes.')
  const layered = processes.includes('thermal_layers'), airSea = processes.includes('air_sea'), raining = processes.includes('rain')
  const fractions = layered ? b.thermal_layers?.fractions : null
  if (layered) check(Number.isInteger(b.thermal_layers?.count) && b.thermal_layers.count >= 2 && b.thermal_layers.count <= 40 && Array.isArray(fractions) && fractions.length === b.thermal_layers.count && fractions.every(v => finite(v) && v > 0) && Math.abs(fractions.reduce((a, v) => a + v, 0) - 1) < 1e-9, 'Invalid thermal layers.')
  const receiver = raining ? b.coastal?.receiver : null
  if (raining) check(b.coastal && finite(b.coastal.band_m) && b.coastal.band_m >= 0 && finite(b.coastal.initial_storage_m3) && Array.isArray(receiver) && receiver.length === count && receiver.every((r, i) => Number.isInteger(r) && (r === -1 || (r >= 0 && r < count && mask[r] && !mask[i]))), 'Invalid coastal drainage.')
  if (processes.length) check(finite(b.initial_heat_content_j), 'Invalid initial heat content.')
  const polar = ['sea_ice', 'ice_sheet', 'warming', 'hosing'].some(p => processes.includes(p))
  if (processes.includes('sea_ice')) check(layered && airSea, 'Sea ice needs the layered column and the air-sea exchange.')
  if (processes.includes('ice_sheet')) {
    const s = b.ice_sheet
    check(s && Array.isArray(s.domain) && s.domain.length === count && s.domain.every((v, i) => typeof v === 'boolean' && (!v || !mask[i])) && s.domain.some(Boolean), 'The ice-sheet domain must be land.')
    check(cellArray(s.bed_elevation_m, count) && finite(s.initial_volume_m3) && s.initial_volume_m3 > 0 && finite(s.initial_sea_level_equivalent_m), 'Invalid ice-sheet bed or initial volume.')
    check(close(s.initial_sea_level_equivalent_m, s.initial_volume_m3 * ICE_TO_WATER / GLOBAL_OCEAN_AREA_M2, 1e-3, 1e-4), 'Initial sea-level equivalent disagrees with the ice volume.')
  }
  let previous = null
  frames.forEach((frame, i) => {
    const f = b.frames[i]
    check(f && f.elapsed_seconds === frame.forecast.elapsed_seconds, 'Ocean frame times must match the timeline.')
    check(FRAME_ARRAYS.every(k => Array.isArray(f[k]) && f[k].length === count), 'Ocean arrays do not match the grid.')
    check(FRAME_NUMBERS.every(k => finite(f[k])), 'Invalid ocean frame numbers.')
    check(f.cumulative_m3 && ['river', 'net_precipitation', 'sinking', 'sponge'].every(k => finite(f.cumulative_m3[k])), 'Invalid cumulative ocean totals.')
    check(Number.isInteger(f.formation_cells) && f.formation_cells >= 0 && Number.isInteger(f.stream_count) && f.stream_count >= 0, 'Invalid ocean counters.')
    let formation = 0, maxSpeed = 0, streamCells = 0
    const speed = new Array(count).fill(0)
    for (let j = 0; j < count; j++) {
      const water = mask[j]
      check(finite(f.u_m_s[j]) && finite(f.v_m_s[j]) && Math.abs(f.u_m_s[j]) <= 20 && Math.abs(f.v_m_s[j]) <= 20, 'Invalid current components.')
      check(finite(f.thickness_m[j]) && f.thickness_m[j] >= 0 && finite(f.salinity_psu[j]) && f.salinity_psu[j] >= 0 && f.salinity_psu[j] <= 45, 'Invalid thickness or salinity.')
      // The slab's fresh surface lens cannot be saltier than the layer; evaporation and the layered column can make it so.
      check(finite(f.surface_salinity_psu[j]) && f.surface_salinity_psu[j] >= 0 && f.surface_salinity_psu[j] <= 45 && (layered || airSea || f.surface_salinity_psu[j] <= f.salinity_psu[j] + 1e-6), 'Surface salinity cannot exceed the bulk salinity.')
      check(finite(f.temperature_k[j]) && (!water || (f.temperature_k[j] > 250 && f.temperature_k[j] < 330)), 'Invalid ocean temperature.')
      check(finite(f.sinking_m_s[j]) && f.sinking_m_s[j] >= 0 && typeof f.front[j] === 'boolean' && Number.isInteger(f.stream_id[j]) && f.stream_id[j] >= -1, 'Invalid sinking, front or stream id.')
      if (!water) check(f.u_m_s[j] === 0 && f.v_m_s[j] === 0 && f.thickness_m[j] === 0 && f.sinking_m_s[j] === 0 && f.stream_id[j] === -1 && !f.front[j], 'Land cells carry no ocean state.')
      speed[j] = Math.hypot(f.u_m_s[j], f.v_m_s[j])
      if (f.stream_id[j] >= 0) check(water && speed[j] >= physics.stream_speed_threshold_m_s * 0.999, 'Stream cells must be water flowing above the threshold.')
      if (f.sinking_m_s[j] > 0) formation++
      if (f.stream_id[j] >= 0) streamCells++
      maxSpeed = Math.max(maxSpeed, speed[j])
    }
    check(f.formation_cells === formation, 'Formation cell count disagrees with the sinking array.')
    check(Math.abs(f.max_speed_m_s - maxSpeed) <= 1e-3 * Math.max(1, maxSpeed), 'Peak speed disagrees with the arrays.')
    check(Math.abs(f.overturning_sv * 1e6 - f.sinking_m_s.reduce((a, v) => a + v, 0) * grid.spacing_east_m * grid.spacing_north_m) <= 1e-3 * Math.max(1e6, f.overturning_sv * 1e6), 'Overturning disagrees with the sinking array.')
    check(Array.isArray(f.streams) && f.streams.length === f.stream_count && f.streams.every(s => s && Number.isInteger(s.id) && Number.isInteger(s.core_cell) && s.core_cell >= 0 && s.core_cell < count && mask[s.core_cell] && Number.isInteger(s.cell_count) && s.cell_count >= 2 && finite(s.max_speed_m_s) && finite(s.mean_speed_m_s) && s.mean_speed_m_s <= s.max_speed_m_s + 1e-6 && finite(s.transport_sv) && finite(s.mean_salinity_psu) && finite(s.mean_temperature_k) && DIRECTIONS.includes(s.direction) && Array.isArray(s.path) && s.path.length >= 1 && s.path.every(p => Array.isArray(p) && p.length === 2 && p.every(v => finite(v) && v >= 0 && v <= 1))), 'Invalid stream descriptions.')
    check(f.streams.every((s, k) => k === 0 || s.transport_sv <= f.streams[k - 1].transport_sv + 1e-9), 'Streams are sorted by transport.')
    check(f.streams.every(s => f.stream_id[s.core_cell] === s.id), 'Stream cores must lie in their stream.')
    check(Array.isArray(f.exchange_points) && f.exchange_points.every(p => p && typeof p.name === 'string' && Number.isInteger(p.mouth_cell) && p.mouth_cell >= 0 && p.mouth_cell < count && mask[p.mouth_cell] && finite(p.discharge_m3_s) && p.discharge_m3_s >= 0 && finite(p.mouth_salinity_psu) && finite(p.minimum_salinity_psu) && p.minimum_salinity_psu <= p.mouth_salinity_psu + 1e-6 && finite(p.plume_area_km2) && p.plume_area_km2 >= 0 && finite(p.plume_freshwater_m3) && p.plume_freshwater_m3 >= 0 && finite(p.plume_extent_km) && p.plume_extent_km >= 0 && finite(p.deflection_deg) && p.deflection_deg >= 0 && p.deflection_deg < 360), 'Invalid exchange points.')
    // Budgets: volume and salt change by the explicit sources only.
    const c = f.cumulative_m3
    const fresh = (c.rain_sea ?? 0) + (c.coastal_runoff ?? 0) - (c.evaporation ?? 0) + (c.ice_water ?? 0) + (c.glacial ?? 0)
    check(Math.abs(f.volume_m3 - b.initial_volume_m3 - c.river - c.net_precipitation - c.sponge - fresh) <= 1e-6 * Math.max(b.initial_volume_m3, 1), 'Ocean volume budget: rivers + net precipitation + sponge + rain + coastal runoff - evaporation + sea-ice melt - freezing + glacial water must explain the change.')
    check(Math.abs(f.volume_balance_error_m3) <= 1e-6 * Math.max(b.initial_volume_m3, 1) && Math.abs(f.salt_balance_error_kg) <= 1e-6 * Math.max(b.initial_salt_kg, 1), 'Per-step ocean budget errors too large.')
    if (previous) check(c.river + 1e-6 >= previous.cumulative_m3.river && c.sinking + 1e-6 >= previous.cumulative_m3.sinking, 'Cumulative river input and sinking cannot decrease.')
    else check(c.river === 0 && c.sinking === 0 && c.net_precipitation === 0 && c.sponge === 0, 'The first frame is the initial state.')
    const reference = physics.reference_salinity_psu
    frame.ocean = {
      u: f.u_m_s, v: f.v_m_s, speed, thickness: f.thickness_m, salinity: f.salinity_psu, surfaceSalinity: f.surface_salinity_psu, temperature: f.temperature_k,
      sinking: f.sinking_m_s, front: f.front, streamId: f.stream_id, freshwater: f.surface_salinity_psu.map((s, j) => mask[j] ? Math.max(0, (reference - s) / reference) : 0),
      streams: f.streams, exchangePoints: f.exchange_points, overturning: f.overturning_sv, upwelling: f.mean_upwelling_m_s, riverInput: f.river_input_m3_s,
      netPrecipitation: f.net_precipitation_m3_s, sponge: f.sponge_volume_m3_s, volume: f.volume_m3, salt: f.salt_kg, kineticEnergy: f.kinetic_energy_j,
      cumulative: c, formationCells: f.formation_cells, formationArea: f.formation_area_km2, meanSalinity: f.mean_salinity_psu, minSalinity: f.min_salinity_psu,
      maxSpeed: f.max_speed_m_s, streamCells, mask, spongeStrength: b.sponge, config: b.request, forcing: b.forcing_source, synthetic: b.source_kind === 'synthetic',
      processes, rain: null, airSea: null, thermal: null, lenses: [], heat: null, seaIce: null, iceSheet: null, glacial: null, hosing: null, oxygen: null, polar,
    }
    if (processes.length) Object.assign(frame.ocean, validateExtensions(b, f, i, grid, { layered, airSea, raining, fractions, receiver }))
    if (polar) Object.assign(frame.ocean, validatePolarOcean(b, f, i, grid, processes, layered))
    previous = f
  })
  return b
}

// The per-frame extension blocks: shapes, land/sea consistency and the budget identities.
function validateExtensions(b, f, index, grid, { layered, airSea, raining, fractions, receiver }) {
  const count = grid.nx * grid.ny, area = grid.spacing_east_m * grid.spacing_north_m, mask = b.ocean_mask, c = f.cumulative_m3
  const sea = mask.reduce((a, m, i) => (m && a.push(i), a), [])
  check(['rain_sea', 'rain_coast', 'rain_inland', 'coastal_runoff', 'coastal_loss', 'evaporation'].every(k => finite(c[k])), 'Invalid cumulative rain, runoff and evaporation totals.')
  check(c.rain_sea >= 0 && c.rain_coast >= 0 && c.coastal_runoff >= 0 && c.coastal_loss >= 0, 'Cumulative rain and runoff cannot be negative.')
  const h = f.heat_budget
  check(h && ['heat_content_j', 'heat_balance_error_j', 'surface_heat_w', 'cumulative_surface_heat_j'].every(k => finite(h[k])), 'Invalid ocean heat budget.')
  check(Math.abs(h.heat_balance_error_j) <= 1e-12 * f.volume_m3 * 1025 * 3990 * 300, 'Ocean heat budget does not close.')
  const out = { heat: { content: h.heat_content_j, initial: b.initial_heat_content_j, error: h.heat_balance_error_j, surface: h.surface_heat_w, cumulativeSurface: h.cumulative_surface_heat_j } }
  if (raining) {
    const r = f.rain
    check(r && cellArray(r.rate_mm_h, count) && cellArray(r.coastal_store_mm, count) && cellArray(r.coastal_inflow_m3_s, count), 'Invalid rain arrays.')
    check(r.rate_mm_h.every(v => v >= 0 && v <= 500) && r.coastal_store_mm.every((v, i) => v >= 0 && (!mask[i] || v === 0)) && r.coastal_inflow_m3_s.every((v, i) => v >= 0 && (mask[i] || v === 0)), 'Rain falls everywhere, land stores its runoff and only the sea receives it.')
    const totals = ['rain_on_sea_m3_s', 'rain_on_coastal_sea_m3_s', 'rain_on_coast_m3_s', 'rain_inland_m3_s', 'coastal_runoff_m3_s', 'coastal_loss_m3_s', 'coastal_storage_m3', 'max_rate_mm_h']
    check(totals.every(k => finite(r[k]) && r[k] >= 0) && finite(r.coastal_balance_error_m3), 'Invalid rain totals.')
    check(close(c.rain_coast, c.coastal_runoff + c.coastal_loss + r.coastal_storage_m3 - b.coastal.initial_storage_m3, 1e-6, 1), 'Coastal water budget: rain on the coast = runoff + land loss + storage change.')
    if (index > 0) {
      const onSea = sea.reduce((a, i) => a + r.rate_mm_h[i], 0) * area / 3.6e6
      check(close(onSea, r.rain_on_sea_m3_s, 2e-3, 1e-3), 'Rain on the sea disagrees with the rain map.')
      check(close(r.coastal_inflow_m3_s.reduce((a, v) => a + v, 0), r.coastal_runoff_m3_s, 2e-3, 1e-3), 'Coastal runoff disagrees with the inflow map.')
    }
    out.rain = {
      rate: r.rate_mm_h, store: r.coastal_store_mm, inflow: r.coastal_inflow_m3_s, receiver, band: b.coastal.band_m,
      onSea: r.rain_on_sea_m3_s, onCoastalSea: r.rain_on_coastal_sea_m3_s, onCoast: r.rain_on_coast_m3_s, inland: r.rain_inland_m3_s,
      runoff: r.coastal_runoff_m3_s, loss: r.coastal_loss_m3_s, storage: r.coastal_storage_m3, maxRate: r.max_rate_mm_h, rainingSea: r.raining_sea_cells, rainingLand: r.raining_land_cells,
    }
  }
  if (airSea) {
    const a = f.air_sea
    check(a && [...FLUXES, 'evaporation_mm_day', 'relative_humidity', 'cloud_fraction'].every(k => cellArray(a[k], count)), 'Invalid air-sea arrays.')
    check(a.cloud_fraction.every(v => v >= 0 && v <= 1) && a.relative_humidity.every(v => v >= 0 && v <= 1.2), 'Invalid humidity or cloud fraction.')
    check(['evaporation_m3_s', 'mean_evaporation_mm_day', 'max_evaporation_mm_day', ...FLUXES.map(k => 'mean_' + k)].every(k => finite(a[k])), 'Invalid air-sea totals.')
    // Under sea ice the fluxes are those of the open fraction and the water also gives heat to the ice.
    const toIce = f.sea_ice?.ocean_to_ice_heat_w_m2
    if (toIce !== undefined) check(cellArray(toIce, count), 'Invalid ocean-to-ice heat flux.')
    for (const i of sea) {
      const parts = a.shortwave_w_m2[i] + a.longwave_w_m2[i] + a.sensible_w_m2[i] + a.latent_w_m2[i] - (toIce ? toIce[i] : 0)
      const scale = Math.abs(a.shortwave_w_m2[i]) + Math.abs(a.longwave_w_m2[i]) + Math.abs(a.sensible_w_m2[i]) + Math.abs(a.latent_w_m2[i]) + (toIce ? Math.abs(toIce[i]) : 0)
      check(Math.abs(parts - a.net_heat_w_m2[i]) <= 0.01 * scale + 2, 'Net heat flux must be shortwave + longwave + sensible + latent (minus the heat given to sea ice).')
    }
    check([...FLUXES, 'evaporation_mm_day'].every(k => mask.every((m, i) => m || a[k][i] === 0)), 'Land cells carry no air-sea fluxes.')
    if (index > 0) check(close(sea.reduce((s, i) => s + a.evaporation_mm_day[i], 0) * area / 8.64e7, a.evaporation_m3_s, 0.01, 1), 'Evaporation total disagrees with the evaporation map.')
    out.airSea = {
      shortwave: a.shortwave_w_m2, longwave: a.longwave_w_m2, sensible: a.sensible_w_m2, latent: a.latent_w_m2, rainHeat: a.rain_heat_w_m2, net: a.net_heat_w_m2,
      evaporation: a.evaporation_mm_day, humidity: a.relative_humidity, cloud: a.cloud_fraction, evaporationTotal: a.evaporation_m3_s,
      meanEvaporation: a.mean_evaporation_mm_day, maxEvaporation: a.max_evaporation_mm_day,
      means: { shortwave: a.mean_shortwave_w_m2, longwave: a.mean_longwave_w_m2, sensible: a.mean_sensible_w_m2, latent: a.mean_latent_w_m2, rain: a.mean_rain_heat_w_m2, net: a.mean_net_heat_w_m2 },
    }
  }
  if (layered) {
    const t = f.thermal, L = fractions.length
    check(t && Array.isArray(t.layer_temperature_k) && t.layer_temperature_k.length === L && Array.isArray(t.layer_salinity_psu) && t.layer_salinity_psu.length === L &&
      t.layer_temperature_k.every(v => cellArray(v, count)) && t.layer_salinity_psu.every(v => cellArray(v, count)) && ['sst_k', 'mixed_layer_depth_m', 'isothermal_layer_depth_m'].every(k => cellArray(t[k], count)), 'Invalid layered column arrays.')
    for (let j = 0; j < count; j++) {
      if (!mask[j]) { check(t.layer_temperature_k.every(v => v[j] === 0) && t.sst_k[j] === 0, 'Land cells carry no column.'); continue }
      check(t.layer_temperature_k.every(v => v[j] > 250 && v[j] < 330) && t.layer_salinity_psu.every(v => v[j] >= 0 && v[j] <= 45), 'Invalid column temperature or salinity.')
      check(Math.abs(t.sst_k[j] - t.layer_temperature_k[0][j]) <= 0.01 && Math.abs(f.surface_salinity_psu[j] - t.layer_salinity_psu[0][j]) <= 0.002, 'The surface must be the top layer.')
      check(t.mixed_layer_depth_m[j] > 0 && t.mixed_layer_depth_m[j] <= f.thickness_m[j] + 0.05 && t.isothermal_layer_depth_m[j] > 0 && t.isothermal_layer_depth_m[j] <= f.thickness_m[j] + 0.05, 'Mixed-layer depths must lie within the layer.')
    }
    check(['mean_sst_k', 'min_sst_k', 'max_sst_k', 'mean_mixed_layer_depth_m', 'max_mixed_layer_depth_m'].every(k => finite(t[k])) && Number.isInteger(t.barrier_layer_cells) && t.barrier_layer_cells >= 0 && t.min_sst_k <= t.mean_sst_k + 1e-6 && t.mean_sst_k <= t.max_sst_k + 1e-6, 'Invalid column statistics.')
    out.thermal = {
      fractions, layerT: t.layer_temperature_k, layerS: t.layer_salinity_psu, sst: t.sst_k, mld: t.mixed_layer_depth_m, ild: t.isothermal_layer_depth_m,
      meanSst: t.mean_sst_k, minSst: t.min_sst_k, maxSst: t.max_sst_k, meanMld: t.mean_mixed_layer_depth_m, maxMld: t.max_mixed_layer_depth_m, barrierCells: t.barrier_layer_cells,
    }
  }
  const lenses = f.surface_lenses
  check(Array.isArray(lenses) && lenses.every(l => l && LENS_KINDS.includes(l.kind) && Number.isInteger(l.freshest_cell) && l.freshest_cell >= 0 && l.freshest_cell < count && mask[l.freshest_cell] &&
    Number.isInteger(l.cells) && l.cells >= 1 && finite(l.area_km2) && l.area_km2 > 0 && finite(l.min_surface_salinity_psu) && finite(l.max_deficit_psu) && finite(l.freshwater_m3) && finite(l.mean_sst_k) &&
    l.source_shares && ['rain', 'coastal_runoff', 'river'].every(k => finite(l.source_shares[k]) && l.source_shares[k] >= 0 && l.source_shares[k] <= 1) &&
    (l.source_shares.glacial === undefined || (finite(l.source_shares.glacial) && l.source_shares.glacial >= 0 && l.source_shares.glacial <= 1))) &&
    lenses.every((l, k) => k === 0 || l.area_km2 <= lenses[k - 1].area_km2 + 1e-9), 'Invalid surface lenses.')
  out.lenses = lenses
  return out
}

// Polar processes: sea ice, the coupled ice sheet, glacial fresh water and hosing, surface oxygen.
function validatePolarOcean(b, f, index, grid, processes, layered) {
  const count = grid.nx * grid.ny, area = grid.spacing_east_m * grid.spacing_north_m, mask = b.ocean_mask, c = f.cumulative_m3
  const out = {}
  check(finite(c.ice_water) && finite(c.glacial) && c.glacial >= -1e-6, 'Invalid cumulative sea-ice and glacial water.')
  const g = f.glacial
  check(g && finite(g.meltwater_m3_s) && g.meltwater_m3_s >= 0 && finite(g.ice_m3_s) && g.ice_m3_s >= 0 && finite(g.cumulative_m3) && close(g.cumulative_m3, c.glacial, 1e-9, 1e-3), 'Invalid glacial fresh water.')
  if (index === 0) check(c.ice_water === 0 && c.glacial === 0, 'The first frame is the initial state.')
  out.glacial = { meltwater: g.meltwater_m3_s, ice: g.ice_m3_s, total: g.meltwater_m3_s + g.ice_m3_s, cumulative: g.cumulative_m3 }
  if (processes.includes('hosing')) {
    check(finite(f.hosing_sv) && f.hosing_sv >= 0 && f.hosing_sv <= 10, 'Invalid hosing rate.')
    out.hosing = f.hosing_sv
  }
  if (processes.includes('sea_ice')) {
    const s = f.sea_ice
    check(s && ['ice_thickness_m', 'ice_concentration', 'ice_surface_temperature_k', 'ocean_to_ice_heat_w_m2'].every(k => cellArray(s[k], count)), 'Invalid sea-ice arrays.')
    check(['ice_area_km2', 'ice_extent_km2', 'ice_volume_km3', 'ice_water_m3_s', 'cumulative_ice_water_m3'].every(k => finite(s[k])) && s.ice_area_km2 >= 0 && s.ice_extent_km2 >= 0 && s.ice_volume_km3 >= 0, 'Invalid sea-ice totals.')
    check(close(s.cumulative_ice_water_m3, c.ice_water, 1e-9, 1e-3), 'Sea-ice water disagrees with the cumulative budget.')
    let iceArea = 0, iceVolume = 0, extent = 0
    for (let j = 0; j < count; j++) {
      const h = s.ice_thickness_m[j], a = s.ice_concentration[j], ts = s.ice_surface_temperature_k[j]
      check(h >= 0 && h <= 50 && a >= 0 && a <= 1, 'Sea-ice thickness and concentration out of range.')
      if (!mask[j]) { check(h === 0 && a === 0 && ts === 0, 'Land cells carry no sea ice.'); continue }
      if (a > 0) check(ts > 180 && ts <= 273.2, 'The ice surface cannot be warmer than melting.')
      iceArea += a * area; iceVolume += a * h * area
      if (a > 0.15) extent += area
    }
    check(close(s.ice_area_km2 * 1e6, iceArea, 2e-3, area * 1e-3), 'Sea-ice area disagrees with the concentration map.')
    check(close(s.ice_volume_km3 * 1e9, iceVolume, 2e-3, area * 1e-2), 'Sea-ice volume disagrees with the thickness and concentration maps.')
    check(close(s.ice_extent_km2 * 1e6, extent, 1e-3, 1), 'Sea-ice extent disagrees with the 15 % concentration contour.')
    out.seaIce = {
      thickness: s.ice_thickness_m, concentration: s.ice_concentration, surfaceTemperature: s.ice_surface_temperature_k, toIce: s.ocean_to_ice_heat_w_m2,
      area: s.ice_area_km2, extent: s.ice_extent_km2, volume: s.ice_volume_km3, water: s.ice_water_m3_s, cumulativeWater: s.cumulative_ice_water_m3,
    }
  }
  if (processes.includes('ice_sheet')) {
    const s = f.ice_sheet, top = b.ice_sheet
    check(s && ['ice_thickness_m', 'surface_elevation_m', 'smb_m_ice_yr', 'melt_m_ice_yr'].every(k => cellArray(s[k], count)), 'Invalid ice-sheet arrays.')
    check(['volume_m3', 'sea_level_equivalent_m', 'sea_level_rise_m', 'smb_gt_yr', 'runoff_m3_s', 'calving_m3_s', 'frontal_ablation_m3_s', 'mean_front_thermal_forcing_k'].every(k => finite(s[k])) &&
      s.volume_m3 >= 0 && s.runoff_m3_s >= 0 && s.calving_m3_s >= 0 && s.frontal_ablation_m3_s >= 0, 'Invalid ice-sheet totals.')
    let volume = 0, smb = 0
    for (let j = 0; j < count; j++) {
      const h = s.ice_thickness_m[j]
      check(h >= 0 && (top.domain[j] || h === 0), 'Ice rests on the ice-sheet domain and is never negative.')
      check(s.melt_m_ice_yr[j] >= 0, 'Surface melt cannot be negative.')
      if (top.domain[j]) { check(s.surface_elevation_m[j] >= top.bed_elevation_m[j] + h - 1, 'The ice surface is the bed plus the ice.'); volume += h * area; smb += s.smb_m_ice_yr[j] * area }
    }
    check(close(s.volume_m3, volume, 2e-3, area), 'Ice-sheet volume disagrees with the thickness map.')
    check(close(s.smb_gt_yr, smb * ICE_TO_WATER / 1e9, 0.01, 0.5), 'Surface mass balance disagrees with its map.')
    check(close(s.sea_level_rise_m, (top.initial_volume_m3 - s.volume_m3) * ICE_TO_WATER / GLOBAL_OCEAN_AREA_M2, 1e-3, 1e-6), 'Sea-level rise must equal the ice lost.')
    out.iceSheet = {
      domain: top.domain, bed: top.bed_elevation_m, thickness: s.ice_thickness_m, surface: s.surface_elevation_m, smb: s.smb_m_ice_yr, melt: s.melt_m_ice_yr,
      volume: s.volume_m3, sle: s.sea_level_equivalent_m, seaLevelRise: s.sea_level_rise_m, smbGt: s.smb_gt_yr, runoff: s.runoff_m3_s, calving: s.calving_m3_s,
      frontal: s.frontal_ablation_m3_s, thermalForcing: s.mean_front_thermal_forcing_k, initialVolume: top.initial_volume_m3,
    }
  }
  if (layered && f.thermal?.surface_oxygen_umol_kg !== undefined) {
    const o2 = f.thermal.surface_oxygen_umol_kg
    check(cellArray(o2, count) && o2.every((v, j) => mask[j] ? v > 100 && v < 600 : v === 0), 'Invalid surface oxygen saturation.')
    out.oxygen = o2
  }
  return out
}

// Polar metrics of one frame (null without polar processes): sea ice, Greenland, glacial water and the overturning.
export function summarizePolarOcean(frame) {
  const o = frame.ocean
  if (!o?.polar) return null
  const mask = o.mask
  const northRows = Math.ceil(frame.forecast.grid.ny * 0.7), nx = frame.forecast.grid.nx
  const north = mask.map((m, i) => m && Math.floor(i / nx) >= northRows)
  const meanOf = (values, sel) => { let s = 0, n = 0; values.forEach((v, i) => { if (sel[i]) { s += v; n++ } }); return n ? s / n : null }
  const sst = o.thermal?.sst ?? o.temperature
  return {
    overturning_sv: o.overturning, hosing_sv: o.hosing ?? 0,
    glacial_m3_s: o.glacial.total, glacial_sv: o.glacial.total / 1e6, glacial_meltwater_m3_s: o.glacial.meltwater, glacial_ice_m3_s: o.glacial.ice, cumulative_glacial_km3: o.glacial.cumulative / 1e9,
    sea_ice_area_mkm2: o.seaIce ? o.seaIce.area / 1e6 : null, sea_ice_extent_mkm2: o.seaIce ? o.seaIce.extent / 1e6 : null, sea_ice_volume_km3: o.seaIce ? o.seaIce.volume : null,
    sea_ice_water_m3_s: o.seaIce ? o.seaIce.water : null,
    greenland_sle_m: o.iceSheet ? o.iceSheet.sle : null, greenland_sea_level_rise_mm: o.iceSheet ? o.iceSheet.seaLevelRise * 1000 : null, greenland_smb_gt_yr: o.iceSheet ? o.iceSheet.smbGt : null,
    greenland_runoff_m3_s: o.iceSheet ? o.iceSheet.runoff : null, greenland_discharge_m3_s: o.iceSheet ? o.iceSheet.calving + o.iceSheet.frontal : null,
    greenland_thermal_forcing_k: o.iceSheet ? o.iceSheet.thermalForcing : null,
    north_sst_c: meanOf(sst, north) === null ? null : meanOf(sst, north) - 273.15,
    north_oxygen_umol_kg: o.oxygen ? meanOf(o.oxygen, north) : null, mean_oxygen_umol_kg: o.oxygen ? meanOf(o.oxygen, mask) : null,
    north_mld_m: o.thermal ? meanOf(o.thermal.mld, north) : null,
  }
}

// Polar time series over the frames, for the charts.
export function polarOceanSeries(frames) {
  return frames.filter(f => f.ocean?.polar).map(f => ({ elapsed_seconds: f.forecast.elapsed_seconds, ...summarizePolarOcean(f) }))
}

// Whole model years of a polar run: annual means and extremes (the seasonal cycle removed), from the frames
// inside [year, year + 1). Only years with as many frames as the best-sampled year count.
export function polarOceanYears(frames) {
  const YEAR = 31557600, byYear = new Map()
  for (const f of frames) {
    if (!f.ocean?.polar) continue
    const y = Math.floor(f.forecast.elapsed_seconds / YEAR + 1e-9)
    if (!byYear.has(y)) byYear.set(y, [])
    byYear.get(y).push(summarizePolarOcean(f))
  }
  const full = Math.max(0, ...[...byYear.values()].map(v => v.length))
  const avg = (rows, k) => rows.some(r => r[k] === null) ? null : rows.reduce((a, r) => a + r[k], 0) / rows.length
  const ext = (rows, k, fn) => rows.some(r => r[k] === null) ? null : fn(...rows.map(r => r[k]))
  return [...byYear.entries()].filter(([, rows]) => rows.length === full && full >= 2).map(([year, rows]) => ({
    year, samples: rows.length,
    overturning_mean_sv: avg(rows, 'overturning_sv'), overturning_max_sv: ext(rows, 'overturning_sv', Math.max),
    hosing_sv: avg(rows, 'hosing_sv'), glacial_sv: avg(rows, 'glacial_sv'),
    sea_ice_max_mkm2: ext(rows, 'sea_ice_area_mkm2', Math.max), sea_ice_min_mkm2: ext(rows, 'sea_ice_area_mkm2', Math.min),
    north_sst_c: avg(rows, 'north_sst_c'), north_oxygen_umol_kg: avg(rows, 'north_oxygen_umol_kg'), north_mld_max_m: ext(rows, 'north_mld_m', Math.max),
    greenland_smb_gt_yr: avg(rows, 'greenland_smb_gt_yr'), greenland_sea_level_rise_mm: rows.at(-1).greenland_sea_level_rise_mm,
  }))
}

// When does the overturning weaken and collapse? The first model year whose annual mean falls below 75 %, 50 %
// and 20 % of the first full year, and the first year the Arctic has no ice at its seasonal minimum.
export function overturningEvents(years) {
  if (!years.length) return null
  const reference = years[0].overturning_mean_sv
  const first = test => years.find(test)?.year ?? null
  return {
    reference_sv: reference,
    weakened_25pct: first(y => y.overturning_mean_sv < 0.75 * reference),
    weakened_50pct: first(y => y.overturning_mean_sv < 0.5 * reference),
    collapsed: first(y => y.overturning_mean_sv < 0.2 * reference),
    ice_free_summer: first(y => y.sea_ice_min_mkm2 !== null && y.sea_ice_min_mkm2 < 0.01),
    greenland_negative_smb: first(y => y.greenland_smb_gt_yr !== null && y.greenland_smb_gt_yr < 0),
    last_year: years.at(-1).year, last_pct: reference > 0 ? years.at(-1).overturning_mean_sv / reference * 100 : null,
  }
}

// Same season, different years: the frames at the same phase of the year as `frame`, one per year.
export function sameSeason(frames, frameIndex) {
  const year = 31557600, t = frames[frameIndex].forecast.elapsed_seconds, phase = ((t % year) + year) % year
  return frames.map((f, k) => ({ k, t: f.forecast.elapsed_seconds })).filter(x => Math.abs((((x.t % year) + year) % year) - phase) < 86400 * 3).map(x => x.k)
}

// Sea-ice colour: open water (null) -> grey-white thin ice -> bright white thick ice, by concentration and thickness.
export function seaIceColor(concentration, thickness) {
  if (!(concentration > 0.01)) return null
  const t = Math.min(1, thickness / 3)
  const v = Math.round(185 + 70 * t)
  return { color: `rgb(${v},${Math.min(255, v + 8)},${Math.min(255, v + 15)})`, weight: Math.min(1, concentration) }
}

// Dissolved-oxygen saturation colour: low (warm water, purple) -> high (cold water, green-teal).
export function oxygenColor(umolKg) {
  const t = Math.max(0, Math.min(1, (umolKg - 200) / 220))
  return `hsl(${Math.round(290 - 130 * t)}, 60%, ${Math.round(32 + 18 * t)}%)`
}

export function summarizeOcean(frame) {
  const o = frame.ocean, n = o.u.length
  let water = 0, fronts = 0, fresh = 0, salty = 0
  for (let i = 0; i < n; i++) { if (!o.mask[i]) continue; water++; if (o.front[i]) fronts++; if (o.freshwater[i] > 0.01) fresh++; if (o.surfaceSalinity[i] > o.config.physics.reference_salinity_psu + 0.3) salty++ }
  const main = o.streams[0] ?? null
  return {
    overturning_sv: o.overturning,
    formation_cells: o.formationCells,
    formation_area_km2: o.formationArea,
    max_speed_m_s: o.maxSpeed,
    stream_count: o.streams.length,
    main_stream: main ? { direction: main.direction, transport_sv: main.transport_sv, max_speed_m_s: main.max_speed_m_s, cells: main.cell_count, label: streamLabel(main, frame.forecast.grid) } : null,
    river_input_m3_s: o.riverInput,
    river_input_sv: o.riverInput / 1e6,
    net_precipitation_m3_s: o.netPrecipitation,
    mean_salinity_psu: o.meanSalinity,
    min_surface_salinity_psu: o.minSalinity,
    fresh_pct: water ? fresh / water * 100 : 0,
    salty_pct: water ? salty / water * 100 : 0,
    front_cells: fronts,
    plume_area_km2: o.exchangePoints.reduce((a, p) => a + p.plume_area_km2, 0),
    plume_freshwater_km3: o.exchangePoints.reduce((a, p) => a + p.plume_freshwater_m3, 0) / 1e9,
    kinetic_energy_pj: o.kineticEnergy / 1e15,
    cumulative_river_km3: o.cumulative.river / 1e9,
    cumulative_sinking_km3: o.cumulative.sinking / 1e9,
  }
}

// A readable name for a stream from where it runs and which way it flows.
export function streamLabel(stream, grid) {
  const x = (stream.core_cell % grid.nx + 0.5) / grid.nx, y = (Math.floor(stream.core_cell / grid.nx) + 0.5) / grid.ny
  const side = x < 0.25 ? 'western boundary' : x > 0.75 ? 'eastern boundary' : y < 0.2 ? 'southern' : y > 0.8 ? 'northern' : 'interior'
  const kind = side.includes('boundary') ? 'current' : 'jet'
  return `${side} ${kind} (${stream.direction}, ${stream.transport_sv.toFixed(1)} Sv)`
}

// Colours are CSS strings for THREE.Color and SVG; hsl() must use the comma form, which THREE parses.
// Speed colour: dark blue (slow) -> cyan -> yellow -> red (fast), saturating at 1 m/s.
export function speedColor(speed) {
  const w = Math.min(1, speed)
  const stops = [[0, [20, 40, 90]], [0.15, [30, 120, 200]], [0.35, [80, 220, 220]], [0.6, [250, 220, 80]], [1.0, [230, 50, 40]]]
  let a = stops[0], b = stops[stops.length - 1]
  for (let i = 0; i + 1 < stops.length; i++) if (w >= stops[i][0] && w <= stops[i + 1][0]) { a = stops[i]; b = stops[i + 1]; break }
  const t = (w - a[0]) / ((b[0] - a[0]) || 1)
  const c = a[1].map((v, i) => Math.round(v + (b[1][i] - v) * t))
  return `rgb(${c[0]},${c[1]},${c[2]})`
}

// Salinity colour: fresh (green-white) -> reference (deep blue) -> salty (purple), over +-3 psu.
export function salinityColor(salinity, reference = 35) {
  const d = Math.max(-3, Math.min(3, salinity - reference))
  if (d < 0) { const t = -d / 3; return `rgb(${Math.round(20 + 180 * t)},${Math.round(70 + 170 * t)},${Math.round(140 + 60 * t)})` }
  const t = d / 3
  return `rgb(${Math.round(20 + 110 * t)},${Math.round(70 - 40 * t)},${Math.round(140 + 40 * t)})`
}

export function temperatureColor(kelvin) {
  const t = Math.max(0, Math.min(1, (kelvin - 275) / 22))
  return `hsl(${Math.round(230 - 230 * t)}, 70%, ${Math.round(30 + 20 * t)}%)`
}

// Time series of the overturning, main-stream transport and river input over the frames.
export function circulationSeries(frames) {
  return frames.map(f => ({ elapsed_seconds: f.forecast.elapsed_seconds, overturning_sv: f.ocean ? f.ocean.overturning : 0, stream_sv: f.ocean && f.ocean.streams[0] ? f.ocean.streams[0].transport_sv : 0, river_sv: f.ocean ? f.ocean.riverInput / 1e6 : 0, min_salinity_psu: f.ocean ? f.ocean.minSalinity : 0 }))
}

// ---- Rain, evaporation, air-sea heat and the layered column --------------------------------------------

const mean = (values, mask) => { let s = 0, n = 0; values.forEach((v, i) => { if (mask[i]) { s += v; n++ } }); return n ? s / n : 0 }

// Metrics of the extension blocks for one frame (null for a slab run without them).
export function summarizeOceanHeat(frame) {
  const o = frame.ocean
  if (!o?.processes?.length) return null
  const mask = o.mask, grid = frame.forecast.grid, area = grid.spacing_east_m * grid.spacing_north_m
  const sst = o.thermal ? o.thermal.sst : o.temperature
  let minSst = Infinity, maxSst = -Infinity
  sst.forEach((v, i) => { if (mask[i]) { minSst = Math.min(minSst, v); maxSst = Math.max(maxSst, v) } })
  const seaArea = mask.filter(Boolean).length * area
  const rainSea = o.rain?.onSea ?? 0, evaporation = o.airSea?.evaporationTotal ?? 0
  const largest = o.lenses[0] ?? null
  const heatChange = o.heat.content - o.heat.initial
  return {
    processes: o.processes,
    mean_sst_c: mean(sst, mask) - 273.15, min_sst_c: minSst - 273.15, max_sst_c: maxSst - 273.15,
    mean_mld_m: o.thermal ? o.thermal.meanMld : null, max_mld_m: o.thermal ? o.thermal.maxMld : null, barrier_layer_cells: o.thermal ? o.thermal.barrierCells : 0,
    net_heat_w_m2: o.airSea ? o.airSea.means.net : o.heat.surface / seaArea,
    fluxes_w_m2: o.airSea ? o.airSea.means : null,
    evaporation_m3_s: evaporation, mean_evaporation_mm_day: o.airSea ? o.airSea.meanEvaporation : 0, max_evaporation_mm_day: o.airSea ? o.airSea.maxEvaporation : 0,
    rain_sea_m3_s: rainSea, rain_coastal_sea_m3_s: o.rain?.onCoastalSea ?? 0, rain_coast_m3_s: o.rain?.onCoast ?? 0, rain_inland_m3_s: o.rain?.inland ?? 0,
    coastal_runoff_m3_s: o.rain?.runoff ?? 0, coastal_storage_m3: o.rain?.storage ?? 0, max_rain_mm_h: o.rain?.maxRate ?? 0,
    // Net fresh water from the air over the sea (positive = the sea gains), in mm/day over the sea area.
    precipitation_minus_evaporation_mm_day: (rainSea - evaporation) / seaArea * 8.64e7,
    lens_count: o.lenses.length, rain_lens_count: o.lenses.filter(l => l.kind === 'rain').length,
    largest_lens: largest ? { kind: largest.kind, area_km2: largest.area_km2, min_salinity_psu: largest.min_surface_salinity_psu, shares: largest.source_shares } : null,
    heat_change_ej: heatChange / 1e18, heat_balance_error_j: o.heat.error,
    cumulative: { rain_sea_km3: o.cumulative.rain_sea / 1e9, rain_coast_km3: o.cumulative.rain_coast / 1e9, runoff_km3: o.cumulative.coastal_runoff / 1e9, evaporation_km3: o.cumulative.evaporation / 1e9, surface_heat_ej: o.heat.cumulativeSurface / 1e18 },
  }
}

// The water column of one cell: layers top first with depth bounds, temperature, salinity and density.
export function columnProfile(frame, cell) {
  const o = frame.ocean
  if (!o?.thermal || !o.mask[cell]) return null
  const physics = o.config.physics, h = o.thickness[cell]
  let top = 0
  const layers = o.thermal.fractions.map((s, k) => {
    const bottom = top + s * h, T = o.thermal.layerT[k][cell], S = o.thermal.layerS[k][cell]
    const rho = physics.reference_density_kg_m3 * (1 - physics.thermal_expansion_per_k * (T - physics.reference_temperature_k) + physics.haline_contraction_per_psu * (S - physics.reference_salinity_psu))
    const layer = { top, bottom, mid: (top + bottom) / 2, temperature_k: T, salinity_psu: S, density_kg_m3: rho }
    top = bottom
    return layer
  })
  const mld = o.thermal.mld[cell], ild = o.thermal.ild[cell]
  return { cell, thickness_m: h, layers, mixed_layer_depth_m: mld, isothermal_layer_depth_m: ild, barrier_layer_m: Math.max(0, ild - mld), sst_k: o.thermal.sst[cell] }
}

// Time-depth section of one cell over the frames (rectangles for a Hovmoeller plot).
export function columnSection(frames, cell) {
  const usable = frames.filter(f => f.ocean?.thermal && f.ocean.mask[cell])
  if (!usable.length) return null
  const times = usable.map(f => f.forecast.elapsed_seconds)
  const edges = times.map((t, k) => [k === 0 ? t : (t + times[k - 1]) / 2, k + 1 === times.length ? t : (t + times[k + 1]) / 2])
  if (times.length === 1) edges[0] = [times[0], times[0] + 1]
  const rects = [], depths = []
  let lo = Infinity, hi = -Infinity, so = Infinity, sh = -Infinity
  usable.forEach((f, k) => {
    const p = columnProfile(f, cell)
    depths.push({ elapsed_seconds: times[k], mld: p.mixed_layer_depth_m, thickness: p.thickness_m })
    for (const l of p.layers) {
      rects.push({ t0: edges[k][0], t1: edges[k][1], z0: l.top, z1: l.bottom, temperature_k: l.temperature_k, salinity_psu: l.salinity_psu })
      lo = Math.min(lo, l.temperature_k); hi = Math.max(hi, l.temperature_k); so = Math.min(so, l.salinity_psu); sh = Math.max(sh, l.salinity_psu)
    }
  })
  return { cell, start: edges[0][0], end: edges.at(-1)[1], max_depth_m: Math.max(...depths.map(d => d.thickness)), rects, depths, temperature_range_k: [lo, hi], salinity_range_psu: [so, sh] }
}

// Basin time series of the heat and freshwater exchange.
export function heatSeries(frames) {
  return frames.map(f => {
    const m = f.ocean ? summarizeOceanHeat(f) : null
    return {
      elapsed_seconds: f.forecast.elapsed_seconds,
      sst_c: m ? m.mean_sst_c : 0, mld_m: m?.mean_mld_m ?? 0, net_heat_w_m2: m ? m.net_heat_w_m2 : 0,
      evaporation_m3_s: m ? m.evaporation_m3_s : 0, rain_sea_m3_s: m ? m.rain_sea_m3_s : 0, runoff_m3_s: m ? m.coastal_runoff_m3_s : 0,
    }
  })
}

// The cell that cooled most between two frames (default focus of the column panel).
export function strongestCooling(first, last) {
  const a = first.ocean?.thermal?.sst ?? first.ocean?.temperature, b = last.ocean?.thermal?.sst ?? last.ocean?.temperature
  if (!a || !b) return null
  let best = null, drop = -Infinity
  last.ocean.mask.forEach((m, i) => { if (m && a[i] - b[i] > drop) { drop = a[i] - b[i]; best = i } })
  return best
}

// Diverging heat-flux colour: loss (blue) <- 0 -> gain (red), saturating at +-400 W/m2.
export function heatFluxColor(w) {
  const t = Math.max(-1, Math.min(1, w / 400))
  if (t < 0) return `rgb(${Math.round(235 + 205 * t)},${Math.round(240 + 150 * t)},${Math.round(245 + 60 * t)})`
  return `rgb(${Math.round(245 - 25 * t)},${Math.round(240 - 190 * t)},${Math.round(235 - 200 * t)})`
}

// Evaporation colour: dark (none or condensation) -> orange -> yellow at 15 mm/day.
export function evaporationColor(mmDay) {
  const t = Math.max(0, Math.min(1, mmDay / 15))
  return `hsl(${Math.round(20 + 35 * t)}, ${Math.round(40 + 50 * t)}%, ${Math.round(14 + 50 * t)}%)`
}

// Depth colour for the mixed layer: pale (shallow) -> deep blue at `deep` metres.
export function depthColor(m, deep = 80) {
  const t = Math.max(0, Math.min(1, m / deep))
  return `hsl(205, ${Math.round(45 + 35 * t)}%, ${Math.round(78 - 58 * t)}%)`
}

// Temperature ramp over an explicit range (column sections, SST with the run's own range).
export function rampColor(value, lo, hi) {
  const t = Math.max(0, Math.min(1, (value - lo) / ((hi - lo) || 1)))
  return `hsl(${Math.round(235 - 235 * t)}, 75%, ${Math.round(28 + 27 * t)}%)`
}
