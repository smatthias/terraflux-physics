// Cosmic dust support: validation of the documented terraflux.cosmic.dust/v1 block (the hourly influx maps of one day,
// the fate of the particles, the altitude profiles, the budgets and the scenarios), metrics of a frame, chart and table
// data and colours. No physics runs in the browser: every number comes from the engine output.
const finite = n => typeof n === 'number' && Number.isFinite(n)
const check = (ok, message) => { if (!ok) throw new Error(message) }

export const DUST_SCHEMA = 'terraflux.cosmic.dust/v1'
export const DUST_METHOD = 'gruen_distribution_single_body_entry_langmuir_evaporation'
export const DUST_SCENARIOS = [
  { key: 'low', label: 'Low' }, { key: 'central', label: 'Central' }, { key: 'high', label: 'High' },
]
export const DUST_STATES = [
  { key: 'ablated', label: 'Evaporated (metal atoms, meteoric smoke)', color: '#ff8a65' },
  { key: 'melted', label: 'Reach the ground melted (cosmic spherules)', color: '#ffd166' },
  { key: 'partially_melted', label: 'Reach the ground partly melted', color: '#c9a86a' },
  { key: 'unmelted', label: 'Reach the ground unmelted', color: '#9fe870' },
]
export const SOURCE_SYMBOLS = { apex: '☄', helion: '◐', antihelion: '◑', 'north toroidal': '△', 'south toroidal': '▽' }

const series = (v, n) => Array.isArray(v) && v.length === n && v.every(finite)

export function validateCosmicDust(block, frames) {
  const b = block, grid = frames[0].forecast.grid, count = grid.nx * grid.ny
  check(b?.schema_version === DUST_SCHEMA && b.method === DUST_METHOD, 'Unsupported cosmic dust contract.')
  check(['engine_output', 'synthetic'].includes(b.source_kind), 'Cosmic dust source must be engine_output or synthetic.')
  const f = b.fate
  check(f && ['influx_t_per_day', 'ablated_t_per_day', 'surviving_t_per_day', 'water_in_t_per_day', 'water_released_t_per_day', 'kinetic_power_w'].every(k => finite(f[k]) && f[k] >= 0), 'Invalid dust fate.')
  check(Math.abs(f.ablated_t_per_day + f.surviving_t_per_day - f.influx_t_per_day) <= 0.01 * f.influx_t_per_day + 1e-9, 'Ablated and surviving dust must add up to the influx.')
  const p = b.profiles, n = p?.altitude_km?.length
  check(Number.isInteger(n) && n >= 2 && ['ablation_t_per_day_per_bin', 'water_kg_per_day_per_bin', 'energy_w_per_bin', 'air_density_kg_m3'].every(k => series(p[k], n)), 'Invalid dust altitude profiles.')
  check(Array.isArray(b.population?.bins) && b.population.bins.length >= 2 && b.population.bins.every(x => finite(x.mass_g) && finite(x.mass_fraction) && finite(x.surviving_fraction)), 'Invalid dust size bins.')
  check(['low', 'central', 'high'].every(k => b.scenarios?.[k] && finite(b.scenarios[k].influx_t_per_day)), 'Invalid dust scenarios.')
  check(b.scenarios.low.influx_t_per_day <= b.scenarios.central.influx_t_per_day && b.scenarios.central.influx_t_per_day <= b.scenarios.high.influx_t_per_day, 'Dust scenarios must be ordered.')
  const m = b.map
  check(m && Array.isArray(m.frames) && m.frames.length === frames.length && series(m.daily_mean_ug_m2_day, count), 'Dust map frames must match the timeline.')
  check(Array.isArray(m.land_mask) && (m.land_mask.length === 0 || m.land_mask.length === count), 'Invalid land mask.')
  frames.forEach((frame, k) => {
    const x = m.frames[k]
    check(x.elapsed_seconds === frame.forecast.elapsed_seconds && series(x.influx_ug_m2_day, count) && x.influx_ug_m2_day.every(v => v >= 0), 'Invalid dust map frame.')
    check(Array.isArray(x.daylight) && x.daylight.length === count && Array.isArray(x.source_zenith_points), 'Invalid daylight or source positions.')
    frame.dust = { index: k, block: b, hour: x.hour_utc, influx: x.influx_ug_m2_day, daylight: x.daylight, points: x.source_zenith_points, subsolar: x.subsolar, daily: m.daily_mean_ug_m2_day,
      land: m.land_mask, synthetic: b.source_kind === 'synthetic' }
  })
  return b
}

// Metrics of a frame for a scenario (the maps are drawn for the central influx and scale with the influx).
export function summarizeDust(frame, scenario = 'central') {
  const d = frame.dust, b = d.block, s = b.scenarios[scenario] ?? b.scenarios.central, scale = s.influx_t_per_day / b.scenarios.central.influx_t_per_day
  const values = d.influx.map(v => v * scale)
  let hi = -Infinity, lo = Infinity, k = 0
  values.forEach((v, i) => { if (v > hi) { hi = v; k = i } if (v < lo) lo = v })
  return { scenario: s, scale, hour: d.hour, max_ug_m2_day: hi, min_ug_m2_day: lo, contrast: hi / lo, peak_cell: k, mean_ug_m2_day: b.map.mean_influx_ug_m2_day * scale,
    surviving_share: b.fate.surviving_t_per_day / b.fate.influx_t_per_day, ablated_share: b.fate.ablated_t_per_day / b.fate.influx_t_per_day }
}

// Shares of the incoming mass by what happens to it (central run; the shares do not depend on the influx).
export function fateShares(block) {
  const f = block.fate, total = f.influx_t_per_day, s = f.surviving_by_state_t_per_day
  const values = { ablated: f.ablated_t_per_day, melted: s.melted, partially_melted: s.partially_melted, unmelted: s.unmelted }
  let x = 0
  return DUST_STATES.map(st => { const share = values[st.key] / total, seg = { ...st, t_per_day: values[st.key], share, x: x * 100, w: share * 100 }; x += share; return seg })
}

// The size distribution: per bin the share of the incoming mass and what survives, by diameter.
export function sizeRows(block) {
  return block.population.bins.map(b => ({ diameter_um: b.diameter_um, mass_g: b.mass_g, mass_share: b.mass_fraction, surviving: b.surviving_fraction, fate: b.fate, peak_k: b.mean_peak_temperature_k, water_released: b.water_released_fraction }))
}

// Altitude profiles normalised to their maxima (for one chart), with the altitudes.
export function profileRows(block) {
  const p = block.profiles, max = k => Math.max(1e-30, ...p[k])
  const ma = max('ablation_t_per_day_per_bin'), mw = max('water_kg_per_day_per_bin'), me = max('energy_w_per_bin')
  return p.altitude_km.map((h, i) => ({ altitude_km: h, ablation: p.ablation_t_per_day_per_bin[i] / ma, water: p.water_kg_per_day_per_bin[i] / mw, energy: p.energy_w_per_bin[i] / me,
    ablation_t_day: p.ablation_t_per_day_per_bin[i], water_kg_day: p.water_kg_per_day_per_bin[i], energy_w: p.energy_w_per_bin[i] }))
}

const fmt = (v, d = 2) => (v === null || v === undefined ? '—' : Math.abs(v) >= 1e5 || (Math.abs(v) < 1e-2 && v !== 0) ? v.toExponential(d - 1 > 0 ? d - 1 : 1) : v.toLocaleString(undefined, { maximumFractionDigits: d }))
export const formatNumber = fmt
// Significant figures (default 2); exponent notation below 0.01 and from 10 million.
export const formatSig = (v, n = 2) => (v === null || v === undefined ? '—' : v !== 0 && (Math.abs(v) < 1e-2 || Math.abs(v) >= 1e7) ? v.toExponential(n - 1) : v.toLocaleString('en-US', { maximumSignificantDigits: n }))

// The budgets of a scenario as rows: quantity, value, what it is compared with, and the ratio.
export function budgetRows(block, scenario = 'central') {
  const s = block.scenarios[scenario] ?? block.scenarios.central, c = block.comparisons
  return [
    { group: 'Extraterrestrial water', label: 'Arriving with the dust', value: `${fmt(s.water.in_t_per_day)} t/day = ${fmt(s.water.in_t_per_year, 0)} t/yr`, against: `${(c.hydrogen_escape_kg_s * 18.015 / 2.016 * 3.15576e7 / 1000).toExponential(1)} t/yr of water lost as escaping hydrogen`, ratio: s.water.vs_hydrogen_escape_water_equivalent },
    { group: 'Extraterrestrial water', label: 'Released above 80 km (as vapour)', value: `${fmt(s.water.released_above_80km_t_per_day)} t/day`, against: `the water already there (${c.mesospheric_water_ppmv} ppmv, ${c.mesospheric_water_lifetime_days}-day lifetime)`, ratio: s.water.share_of_mesospheric_water },
    { group: 'Extraterrestrial water', label: 'Reaching the ground in particles', value: `${fmt(s.water.to_surface_kg_per_day, 1)} kg/day`, against: 'the water arriving', ratio: s.water.to_surface_kg_per_day / 1000 / s.water.in_t_per_day },
    { group: 'Extraterrestrial water', label: 'Into the oceans in a year', value: `${fmt(s.water.to_oceans_t_per_year, 0)} t/yr`, against: `the oceans (${c.ocean_mass_kg.toExponential(1)} kg)`, ratio: s.water.share_of_ocean_per_year },
    { group: 'Extraterrestrial water', label: 'At this rate over the Earth\'s history', value: `${c.earth_age_years.toExponential(1)} years`, against: 'the oceans', ratio: s.water.share_of_ocean_over_earth_history },
    { group: 'Extraterrestrial water', label: 'Against the rain', value: `${fmt(s.water.in_t_per_year, 0)} t/yr`, against: `global precipitation (${c.precipitation_kg_yr.toExponential(1)} kg/yr)`, ratio: s.water.share_of_precipitation },
    { group: 'Extraterrestrial water', label: 'Against volcanic degassing', value: `${fmt(s.water.in_t_per_year, 0)} t/yr`, against: `volcanic and hydrothermal water (${c.volcanic_water_kg_yr.toExponential(1)} kg/yr)`, ratio: s.water.vs_volcanic_degassing },
    { group: 'The Earth\'s mass', label: 'Gain from dust', value: `${fmt(s.mass.gain_t_per_year, 0)} t/yr`, against: `hydrogen (${fmt(s.mass.hydrogen_loss_t_per_year, 0)} t/yr) and helium (${fmt(s.mass.helium_loss_t_per_year, 0)} t/yr) escaping to space`, ratio: s.mass.gain_t_per_year / (s.mass.hydrogen_loss_t_per_year + s.mass.helium_loss_t_per_year) },
    { group: 'Energy', label: 'Kinetic energy of the dust', value: `${fmt(s.energy.kinetic_power_w / 1e9, 2)} GW = ${fmt(s.energy.per_m2_w)} W/m²`, against: 'sunlight (340 W/m² on average)', ratio: s.energy.share_of_sunlight },
    { group: 'Energy', label: 'Kinetic energy of the dust', value: `${fmt(s.energy.kinetic_power_w / 1e9, 2)} GW`, against: `the Earth's geothermal heat flow (${(c.geothermal_w / 1e12).toFixed(0)} TW)`, ratio: s.energy.vs_geothermal },
    { group: 'Iron', label: 'Meteoric iron', value: `${fmt(s.iron.in_t_per_year, 0)} t/yr (${fmt(s.iron.to_oceans_t_per_year, 0)} into the oceans)`, against: `iron in wind-blown desert dust (${c.aeolian_iron_kg_yr.toExponential(1)} kg/yr)`, ratio: s.iron.vs_aeolian_dust_iron },
    { group: 'Climate', label: 'Meteoric smoke in the middle atmosphere', value: `${fmt(s.smoke.burden_t, 0)} t, optical depth ${fmt(s.smoke.optical_depth)}`, against: 'the Pinatubo eruption (optical depth about 0.15)', ratio: s.smoke.optical_depth / 0.15 },
    { group: 'Climate', label: 'Radiative forcing (at most)', value: `±${fmt(s.smoke.forcing_bound_w_m2)} W/m²`, against: 'the solar-cycle forcing (about 0.2 W/m²)', ratio: s.smoke.forcing_bound_w_m2 / 0.2 },
    { group: 'Climate', label: 'Global temperature (equilibrium, at most)', value: `±${fmt(s.smoke.equilibrium_temperature_bound_k, 3)} K`, against: 'the warming since pre-industrial times (about 1.2 K)', ratio: s.smoke.equilibrium_temperature_bound_k / 1.2 },
  ]
}

// Map colours: sea and land by the influx (dark to bright), night darker.
export function dustRampColor(v, lo, hi) {
  const t = Math.max(0, Math.min(1, (v - lo) / ((hi - lo) || 1)))
  return `rgb(${Math.round(30 + 225 * Math.min(1, t * 1.3))},${Math.round(40 + 180 * Math.pow(t, 1.2))},${Math.round(90 + 60 * (1 - t) + 40 * Math.max(0, t - 0.7) / 0.3)})`
}
export const ratioLabel = r => (r === null || r === undefined || !Number.isFinite(r) ? '—' : r >= 0.01 ? `${(r * 100).toFixed(r >= 1 ? 0 : 1)} %` : `${r.toExponential(1)}`)

// Local solar time at a longitude, from the subsolar longitude (noon where the Sun is overhead).
export function localSolarTime(longitudeDeg, subsolarLongitudeDeg) {
  return ((12 + (longitudeDeg - subsolarLongitudeDeg) / 15) % 24 + 24) % 24
}
