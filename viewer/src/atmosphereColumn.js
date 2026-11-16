// The atmosphere above one data point, for the inspector's Atmosphere tab (AtmosphereColumn.vue): the layered column
// from the ground up, the cloud in it (genus, base, top, thickness, cover, ceiling), the levels that shape it (cloud
// base / condensation level, level of free convection, freezing level, capping inversion, top / equilibrium level,
// tropopause), and how the cloud builds up from the ground to its top, in steps with their heights. Pure functions.
//
// What comes from the engine and what is derived here:
//   - cloud genus, base, top, cover, mechanism, trigger, lifting depth, heated CAPE/CIN, front: cloud formation block;
//   - LCL, LFC, equilibrium level, CAPE, CIN: column diagnostics; freezing level, cloud-base/top temperatures, updraft,
//     precipitation: precipitation block; capping inversion and cap state: column config and severe block;
//   - the temperature, humidity and pressure of every layer are recomputed here with the column model's documented
//     environment formulas (T = Ts − Γ·min(z, tropopause) with the optional capping inversion, q = qs·exp(−z/Hq),
//     hydrostatic pressure): the profile the engine starts from, before its convective adjustment;
//   - the freezing level falls back to that profile when the precipitation block is absent (marked as derived).
// All heights are metres above the ground, as in the engine output.
import { CLOUD_TYPES, cloudTypeInfo } from './cloudFormation.js'
import { CLOUD_FORMS } from './clouds3d.js'
import { precipitationTypeInfo } from './weatherMap.js'

const G = 9.80665, RD = 287.04, KELVIN = 273.15
export const DEFAULT_COLUMN = { layer_count: 24, layer_thickness_m: 500, environment_lapse_rate_k_m: 0.0065, tropopause_height_m: 11000, humidity_scale_height_m: 2500, inversion_base_m: 0, inversion_depth_m: 0, inversion_strength_k: 0, lapse_rate_above_inversion_k_m: -1 }
// Aviation: the ceiling is the base of the lowest layer covering more than half of the sky (broken, 5/8, or more).
export const CEILING_COVER = 5 / 8

const finite = v => typeof v === 'number' && Number.isFinite(v)
const at = (array, i) => (Array.isArray(array) || ArrayBuffer.isView(array) ? array[i] : undefined)

// The column model's environment temperature (K) at height z above the ground.
export function environmentTemperature(config, surfaceK, z) {
  const c = { ...DEFAULT_COLUMN, ...config }, zc = Math.min(z, c.tropopause_height_m)
  if (!(c.inversion_strength_k > 0)) return surfaceK - c.environment_lapse_rate_k_m * zc
  const base = c.inversion_base_m, top = base + c.inversion_depth_m
  const above = c.lapse_rate_above_inversion_k_m < 0 ? c.environment_lapse_rate_k_m : c.lapse_rate_above_inversion_k_m
  const tBase = surfaceK - c.environment_lapse_rate_k_m * Math.min(base, zc)
  if (zc <= base) return tBase
  if (zc <= top) return tBase + c.inversion_strength_k * (zc - base) / c.inversion_depth_m
  return tBase + c.inversion_strength_k - above * (zc - top)
}

// Saturation vapour pressure over water (Magnus, Pa) and the relative humidity (%) of specific humidity q at T, p.
export const saturationPressure = k => 611.2 * Math.exp(17.67 * (k - KELVIN) / (k - 29.65))
export function relativeHumidity(q, k, p) {
  const e = q * p / (0.622 + 0.378 * q)
  return 100 * e / saturationPressure(k)
}

// The layers of the column: bottom, top, and temperature (°C), humidity (%), pressure (hPa) at the layer middle.
export function columnLayers(config, { surfaceK, surfaceQ = null, surfacePa = null }) {
  const c = { ...DEFAULT_COLUMN, ...config }, n = c.layer_count, dz = c.layer_thickness_m, out = []
  let p = finite(surfacePa) ? surfacePa : 101325, z = 0
  const q = h => (finite(surfaceQ) ? surfaceQ * Math.exp(-h / c.humidity_scale_height_m) : null)
  // Hydrostatic pressure with the virtual temperature, in 50 m steps.
  const pressureTo = (from, to, start) => {
    let pp = start
    for (let h = from; h < to - 1e-6; h += 50) {
      const step = Math.min(50, to - h), mid = h + step / 2, t = environmentTemperature(c, surfaceK, mid), qq = q(mid) ?? 0
      pp *= Math.exp(-G * step / (RD * t * (1 + 0.608 * qq)))
    }
    return pp
  }
  for (let k = 0; k < n; k++) {
    const mid = z + dz / 2, pMid = pressureTo(z, mid, p), t = environmentTemperature(c, surfaceK, mid), qm = q(mid)
    out.push({ bottom: z, top: z + dz, temperatureC: t - KELVIN, humidityPct: qm === null ? null : Math.min(100, relativeHumidity(qm, t, pMid)), pressureHpa: pMid / 100 })
    p = pressureTo(z, z + dz, p); z += dz
  }
  return out
}

// Where the profile first reaches 0 °C going up (null: already below freezing at the ground, or never).
export function freezingLevel(config, surfaceK, top = 20000) {
  if (!(surfaceK > KELVIN)) return null
  for (let z = 0; z <= top; z += 10) if (environmentTemperature(config, surfaceK, z) <= KELVIN) return z
  return null
}

// Round heights the way the inspector prints them: to 10 m, with a thin space for thousands.
export const metres = v => `${Math.round(v / 10) * 10}`.replace(/\B(?=(\d{3})+(?!\d))/g, '\u2009') + ' m'
const celsius = v => `${v > -0.05 && v < 0 ? '0' : v.toFixed(Math.abs(v) < 10 ? 1 : 0)} °C`

const TRIGGERS = {
  free: 'nothing holds it down (almost no convective inhibition), so the warm air rises freely',
  thermals: 'the thermals of the heated mixed layer reach the condensation level',
  frontal: 'lifting at the front carries the air up',
  convergence: 'converging winds push the air upwards',
  overrunning: 'warm air glides up over the wedge of colder air at the front',
}
const MECHANISMS = { convective: 'convection (air heated at the ground rises)', frontal: 'lifting at a front', frontal_convective: 'convection released by lifting at a front' }

// The full description of the column above cell i of frame d (the viewer's frame object), or null when the data has
// no vertical information (no cloud formation and no column diagnostics).
export function atmosphereColumn(d, i) {
  const cl = d?.clouds, cols = d?.coupled?.columns, pr = d?.precipitation, sv = d?.severe
  if (!cl && !cols) return null
  const config = { ...DEFAULT_COLUMN, ...(cols?.config ?? {}) }
  const surfaceK = at(d.forecast?.fields?.air_temperature?.values, i)
  if (!finite(surfaceK)) return null
  const surfaceQ = at(d.coupled?.specificHumidity, i), surfacePa = at(d.coupled?.pressure, i), rh = at(d.coupled?.humidity, i)
  const layers = columnLayers(config, { surfaceK, surfaceQ: finite(surfaceQ) ? surfaceQ : null, surfacePa: finite(surfacePa) ? surfacePa : null })
  const heating = finite(cl?.heating) ? cl.heating : 0

  // The cloud of the cell.
  let cloud = null
  const type = at(cl?.type, i)
  if (type && type !== 'none' && finite(at(cl.base, i)) && finite(at(cl.top, i))) {
    const info = cloudTypeInfo(type), base = cl.base[i], top = cl.top[i], fraction = at(cl.fraction, i) ?? null
    cloud = {
      key: type, label: info.label, abbreviation: info.abbreviation, level: info.level, color: info.color, note: CLOUD_FORMS[type]?.note ?? '',
      base, top, thickness: top - base, fraction, ceiling: finite(fraction) && fraction >= CEILING_COVER ? base : null,
      mechanism: at(cl.mechanism, i) ?? 'none', trigger: at(cl.trigger, i) ?? 'none', thunderstorm: !!at(cl.thunderstorm, i),
      lifting: at(cl.lifting, i) ?? null, cape: at(cl.cape, i) ?? null, cin: at(cl.cin, i) ?? null, front: at(cl.frontType, i) ?? 'none', side: at(cl.side, i) ?? 0,
      baseC: finite(at(pr?.baseTemperature, i)) ? pr.baseTemperature[i] - KELVIN : null, topC: finite(at(pr?.topTemperature, i)) ? pr.topTemperature[i] - KELVIN : null,
    }
  }
  const columnTop = config.layer_count * config.layer_thickness_m
  const lcl = at(cols?.lcl, i), lfc = at(cols?.lfc, i), el = at(cols?.el, i)
  const engineFreezing = at(pr?.freezingLevel, i)
  const freezing = finite(engineFreezing) ? { height: engineFreezing, derived: false } : (() => { const z = freezingLevel(config, surfaceK, Math.max(columnTop, 16000)); return z === null ? null : { height: z, derived: true } })()
  const precipitation = pr && at(pr.type, i) && pr.type[i] !== 'none'
    ? { type: pr.type[i], phase: at(pr.phase, i), rate: at(pr.rate, i), hail: at(pr.hail, i) ?? 0, lightning: !!at(pr.lightning, i), virga: !!at(pr.virga, i), updraft: at(pr.updraft, i) ?? null }
    : pr && at(pr.virga, i) ? { type: 'virga', phase: null, rate: 0, hail: 0, lightning: false, virga: true, updraft: at(pr.updraft, i) ?? null } : null
  const inversion = config.inversion_strength_k > 0 ? { bottom: config.inversion_base_m, top: config.inversion_base_m + config.inversion_depth_m, strength: config.inversion_strength_k } : null
  const cap = sv ? { capped: !!at(sv.capped, i), broken: !!at(sv.capBroken, i), strength: at(sv.cap, i) ?? null, possible: !!at(sv.possible, i) } : null

  // The levels drawn in the column.
  const levels = []
  const add = (key, height, label, colour, source) => { if (finite(height) && height >= 0) levels.push({ key, height, label, colour, source }) }
  if (cloud) add('base', cloud.base, 'Cloud base', '#7fe0ff', 'engine')
  // The column diagnostics are for the unheated surface air; a convective cloud grows from the heated air.
  const unheated = heating > 0 ? ' (unheated air)' : ''
  if (finite(lcl) && (!cloud || Math.abs(lcl - cloud.base) > 100)) add('lcl', lcl, cloud ? `LCL${unheated}` : 'Condensation level (LCL)', '#9fd3e3', 'engine')
  if (finite(lfc) && (!cloud || cloud.mechanism !== 'frontal')) add('lfc', lfc, `LFC${unheated}`, '#58e0a0', 'engine')
  if (freezing) add('freezing', freezing.height, freezing.derived ? 'Freezing level (from the profile)' : 'Freezing level 0 °C', '#b9d8ff', freezing.derived ? 'derived' : 'engine')
  if (inversion) add('inversion', inversion.bottom, 'Capping inversion', '#ffb454', 'config')
  if (cloud) add('top', cloud.top, cloud.key === 'cumulonimbus' ? 'Top · anvil' : 'Cloud top', '#c79bff', 'engine')
  if (finite(el) && (!cloud || Math.abs(el - cloud.top) > 100)) add('el', el, `EL${unheated}`, '#a98bd6', 'engine')
  if (config.tropopause_height_m <= Math.max(columnTop, (cloud?.top ?? 0) + 1500)) add('tropopause', config.tropopause_height_m, 'Tropopause', '#93a5af', 'config')
  levels.sort((a, b) => a.height - b.height)

  const T = z => environmentTemperature(config, surfaceK, z) - KELVIN
  const sst = at(d.surface?.sst, i), land = at(d.surface?.land, i)
  const surfaceNote = land === 0 && finite(sst) ? ` over the sea (surface water ${celsius(sst - KELVIN)})` : land === 1 ? ' over land' : ''
  const steps = buildSteps({ cloud, surfaceNote, capTop: cl?.config?.max_cloud_top_m ?? null, surfaceC: surfaceK - KELVIN, rh: finite(rh) ? rh : layers[0]?.humidityPct ?? null, heating, lcl, lfc, el, freezing, inversion, cap, precipitation, tropopause: config.tropopause_height_m, T, lapse: config.environment_lapse_rate_k_m })
  const top = Math.min(20000, Math.max(columnTop, (cloud?.top ?? 0) + 1000, finite(el) ? el + 500 : 0))
  return {
    cloud, layers, levels, steps, precipitation, inversion, cap, freezing,
    ground: { temperatureC: surfaceK - KELVIN, humidityPct: finite(rh) ? rh : null, pressureHpa: finite(surfacePa) ? surfacePa / 100 : null, heatingK: heating, land: at(d.surface?.land, i) ?? null },
    diagnostics: { lcl: finite(lcl) ? lcl : null, lfc: finite(lfc) ? lfc : null, el: finite(el) ? el : null, cape: at(cols?.cape, i) ?? null, cin: at(cols?.cin, i) ?? null },
    top, tropopause: config.tropopause_height_m, config, synthetic: !!(cl?.synthetic || d?.coupled?.synthetic),
    title: cloud ? cloud.label : 'Clear sky', subtitle: cloud ? `${cloud.abbreviation} · ${MECHANISMS[cloud.mechanism] ?? cloud.mechanism}` : finite(lcl) ? 'no cloud diagnosed here now' : 'no cloud and no condensation level',
  }
}

// How the cloud builds up, from the ground to its top: one step per level, with its height. Textbook meteorology
// applied to the values of this point.
export function buildSteps({ cloud, surfaceNote = '', capTop = null, surfaceC, rh, heating, lcl, lfc, el, freezing, inversion, cap, precipitation, tropopause, T, lapse }) {
  const steps = [], push = (key, height, title, text) => steps.push({ key, height, title, text })
  const convective = cloud && ['convective', 'frontal_convective'].includes(cloud.mechanism)
  const air = `${celsius(surfaceC)}${finite(rh) ? `, ${Math.round(rh)} % humidity` : ''}${surfaceNote}`
  if (heating > 0 && (convective || !cloud)) push('ground', 0, 'The ground heats the air', heating > 0 ? `The surface warms the air above it by ${heating.toFixed(1)} K (${air}). Warm bubbles of air — thermals — start to rise.` : `Air at the ground: ${air}. No surface heating now (night).`)
  else push('ground', 0, 'Air at the ground', `${air}.${cloud?.front && cloud.front !== 'none' ? ` A ${cloud.front} front runs through this point.` : cloud?.side === 1 ? ' On the warm side of a front.' : cloud?.side === -1 ? ' On the cold side of a front.' : ''}`)
  if (inversion && (!cloud || inversion.bottom < cloud.top)) {
    const state = cap?.broken ? ' Here the heated air breaks through it.' : cap?.capped ? ' Here it holds the rising air down.' : ''
    push('inversion', inversion.bottom, 'A lid of warm air', `From ${metres(inversion.bottom)} to ${metres(inversion.top)} the air gets ${inversion.strength.toFixed(1)} K warmer with height instead of colder: a capping inversion that rising air must overcome.${state}`)
  }
  if (cloud) {
    const rise = cloud.base / 2
    const how = TRIGGERS[cloud.trigger] ?? 'the air is lifted'
    const lifted = finite(cloud.lifting) && cloud.lifting > 0 && !convective ? ` The lifting here is about ${metres(cloud.lifting)}.` : ''
    push('rise', rise, 'Rising and cooling', `${how[0].toUpperCase()}${how.slice(1)}. Rising air expands and cools by about 9.8 °C per kilometre while it is not saturated.${lifted}`)
    const belowFreezing = freezing && freezing.height <= cloud.base
    const baseTemp = finite(cloud.baseC) ? ` It is about ${celsius(cloud.baseC)} there.` : ''
    const lclNote = finite(lcl) && Math.abs(lcl - cloud.base) > 100 ? ` (Air that was not heated would condense already at ${metres(lcl)}.)` : ''
    push('base', cloud.base, `Cloud base · ${metres(cloud.base)}`, `The rising air has cooled to its dew point: water vapour condenses into tiny droplets and the cloud begins.${baseTemp}${belowFreezing ? ' The whole cloud is colder than 0 °C: supercooled droplets and ice.' : ''}${lclNote}`)
    if (convective && heating > 0) push('buoyant', Math.min(cloud.top, cloud.base + Math.max(200, cloud.thickness * 0.15)), 'Rising on its own', `Inside the cloud, condensation releases heat: the cloud air stays warmer than its surroundings and keeps rising${finite(cloud.cape) && cloud.cape > 0 ? `, driven by ${Math.round(cloud.cape)} J/kg of convective energy (CAPE of the heated air)` : ''}.${finite(lfc) ? ` (Air that was not heated would rise on its own only above ${metres(lfc)}.)` : ''}`)
    else if (convective && finite(lfc) && lfc > cloud.base + 50 && lfc < cloud.top) push('lfc', lfc, `Free convection · ${metres(lfc)}`, `From here the cloud air is warmer than its surroundings and rises on its own${finite(cloud.cape) && cloud.cape > 0 ? `, driven by ${Math.round(cloud.cape)} J/kg of convective energy (CAPE)` : ''}. Condensation releases heat that keeps it rising.`)
    if (freezing && freezing.height > cloud.base && freezing.height < cloud.top) {
      const up = precipitation?.updraft
      push('freezing', freezing.height, `Freezing level · ${metres(freezing.height)}`, `Above this level the air is below 0 °C${freezing.derived ? ' (from the temperature profile)' : ''}: the droplets become supercooled, ice crystals and snowflakes grow${cloud.key === 'cumulonimbus' ? `, and in the updraft${finite(up) && up > 0 ? ` of about ${Math.round(up)} m/s` : ''} graupel and hail` : ''}.`)
    }
    const depth = `The cloud is ${metres(cloud.thickness)} thick.`
    const topTemp = finite(cloud.topC) ? ` About ${celsius(cloud.topC)} at the top.` : ''
    const topText = {
      cumulonimbus: `The rising air is no longer warmer than its surroundings (equilibrium level): it spreads out sideways as an anvil of ice${cloud.top > tropopause ? `, overshooting the tropopause at ${metres(tropopause)}` : tropopause - cloud.top < 1500 ? ', just under the tropopause' : ''}. ${depth}${topTemp}${finite(capTop) && Math.abs(cloud.top - capTop) < 1 ? ` (The model caps cloud tops at ${metres(capTop)}.)` : ''}`,
      towering_cumulus: `The towers stop where the rising air loses its buoyancy. ${depth}${topTemp} With more energy they can grow into a cumulonimbus.`,
      cumulus: `The thermals run out of buoyancy: a fair-weather heap with a flat base and a rounded top. ${depth}${topTemp}`,
    }[cloud.key] ?? `The top of the lifted layer: the air was lifted and cooled to saturation over this depth. ${depth}${topTemp}`
    push('top', cloud.top, `${cloud.key === 'cumulonimbus' ? 'Anvil top' : 'Cloud top'} · ${metres(cloud.top)}`, topText)
    if (precipitation) {
      const extras = [precipitation.hail > 0 ? `hail up to ${precipitation.hail.toFixed(1)} cm` : '', precipitation.lightning ? 'lightning' : ''].filter(Boolean).join(' and ')
      const what = precipitation.virga ? 'Light precipitation falls from the base but evaporates before it reaches the ground (virga).' : `From the base to the ground: ${precipitationTypeInfo(precipitation.type).label.toLowerCase().replace(' (continuous)', '')}, ${precipitation.rate.toFixed(1)} mm/h${extras ? `, with ${extras}` : ''}.`
      push('precipitation', 0, 'What falls out', what.charAt(0).toUpperCase() + what.slice(1))
    }
  } else {
    if (finite(lcl)) push('lcl', lcl, `Condensation level · ${metres(lcl)}`, `If the air at the ground were lifted, it would cool to its dew point and form a cloud base here (${celsius(T(lcl))} in the surroundings). Nothing lifts it that far now.`)
    if (finite(lfc) && finite(el)) push('el', el, `Could grow to · ${metres(el)}`, `Lifted past ${metres(lfc)} the air would rise on its own up to ${metres(el)}: the room a shower or storm would have here.`)
  }
  if (tropopause > (cloud?.top ?? 0) && tropopause < 20000) push('tropopause', tropopause, `Tropopause · ${metres(tropopause)}`, `Above here the temperature stops falling (${celsius(T(tropopause))}): the lid of the weather; clouds do not grow higher.`)
  // Order by height; what falls out belongs at the ground but reads best last.
  return steps.filter(s => s.key !== 'precipitation').sort((a, b) => a.height - b.height).concat(steps.filter(s => s.key === 'precipitation'))
}

// The colour of a temperature for the layers: warm orange through pale at 0 °C to deep blue at −60 °C.
export function temperatureColour(c) {
  const stops = [[-60, [40, 70, 160]], [-30, [70, 130, 210]], [-10, [140, 190, 235]], [0, [215, 235, 245]], [12, [245, 215, 150]], [25, [245, 160, 90]], [40, [220, 80, 60]]]
  const v = Math.max(-60, Math.min(40, c))
  let k = 0
  while (k < stops.length - 2 && v > stops[k + 1][0]) k++
  const [a, ca] = stops[k], [b, cb] = stops[k + 1], t = (v - a) / (b - a)
  return `rgb(${ca.map((x, j) => Math.round(x + (cb[j] - x) * t)).join(',')})`
}

export const CLOUD_KEYS = CLOUD_TYPES.map(t => t.key)
