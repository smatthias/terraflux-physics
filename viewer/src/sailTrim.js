// The sail settings for the conditions: the sail plan (mainsail reefs, headsail size, gennaker, storm sails) and the
// trim (boom and headsail sheet angles to leeward, heel, traveller, vang, outhaul, backstay, car). Pure functions; the
// 2D drawing is SailTrim.vue, the 3D boat stationMarkers.js. Requirements: docs/weather/route-weather-and-sail-routing.md
// (S16). Rules of thumb of cruising practice, not a sail maker's trim guide; the heel model is illustrative.
import { apparentWind } from './sailing.js'

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v))
export const GALE_KT = 34, STORM_KT = 48, GENNAKER_MAX_KT = 16
// Mainsail area left by each reef (fraction of the full main): the hoist shortens and the foot with it.
export const REEF_AREA = [1, 0.78, 0.6, 0.45]
export const REEF_HOIST = [1, 0.84, 0.7, 0.57]
export const DEFAULT_TRIM_SETTINGS = { reefKt: 18, gennaker: true }
export const HEEL_K = 0.065, HEEL_DRAG = 0.35

// The wind the sail plan is chosen for: the apparent wind upwind, the true wind (or the apparent, if larger) downwind,
// where the apparent wind drops and would tempt too much sail (reef for the true wind when running).
export const effectiveWind = ({ tws, aws, awa }) => (Math.abs(awa) < 90 ? aws : Math.max(tws, aws))

// The sail plan and trim. tws, twa: true wind (kt, deg signed: positive = wind over starboard); aws, awa: apparent.
// boat: polars.js boat (sail areas); settings: { reefKt, gennaker }.
export function sailPlan({ tws, twa, aws, awa }, boat, settings = {}) {
  const opts = { ...DEFAULT_TRIM_SETTINGS, ...settings }, R = opts.reefKt, eff = effectiveWind({ tws, aws, awa })
  const A = Math.abs(awa), T = Math.abs(twa), downwind = T >= 100
  // The mainsail.
  let reef = eff < R ? 0 : eff < R + 7 ? 1 : eff < R + 14 ? 2 : 3, main, headsail, storm = false, bare = false
  if (Math.max(tws, eff) >= STORM_KT) { bare = true; reef = 3 }
  else if (Math.max(tws, eff) >= GALE_KT) storm = true
  if (bare) main = { kind: 'none', label: 'No sail (bare poles)', hoist: 0, areaM2: 0 }
  else if (storm) main = { kind: 'trysail', label: 'Storm trysail', hoist: 0.45, areaM2: boat.trysailM2 }
  else main = { kind: 'main', label: reef ? `Mainsail, ${['', '1st', '2nd', '3rd'][reef]} reef` : 'Full mainsail', hoist: REEF_HOIST[reef], areaM2: boat.mainM2 * REEF_AREA[reef], reef }
  // The headsail (or the gennaker off the wind in moderate winds, or goose-winged on a dead run).
  if (bare) headsail = { kind: 'none', label: 'None', areaM2: 0, furl: 0 }
  else if (storm) headsail = { kind: 'storm', label: 'Storm jib', areaM2: boat.stormJibM2, furl: 0 }
  else if (opts.gennaker && downwind && tws <= GENNAKER_MAX_KT && reef === 0 && tws >= 4) headsail = { kind: 'gennaker', label: 'Gennaker (asymmetric spinnaker)', areaM2: boat.gennakerM2, furl: 0 }
  else if (eff < 14) headsail = { kind: 'genoa', label: 'Full genoa', areaM2: boat.genoaM2, furl: 0 }
  else if (eff < R + 3) headsail = { kind: 'genoa', label: 'Genoa furled to 75 %', areaM2: boat.genoaM2 * 0.75, furl: 0.25 }
  else if (eff < R + 10) headsail = { kind: 'jib', label: 'Working jib (genoa furled to 60 %)', areaM2: boat.jibM2, furl: 0.4 }
  else headsail = { kind: 'jib', label: 'Small jib (genoa furled to 40 %)', areaM2: boat.genoaM2 * 0.4, furl: 0.6 }
  const gooseWing = T >= 165 && ['genoa', 'jib'].includes(headsail.kind)
  // Sheets: angles from the centreline to leeward (the boom limited by the shrouds, the headsail by its lead).
  const boomDeg = main.areaM2 > 0 ? clamp(0.75 * (A - 18), 3, 85) : 0
  const jibDeg = headsail.kind === 'gennaker' ? clamp(0.8 * (A - 30), 25, 95) : gooseWing ? 80 : clamp(0.6 * (A - 20), 8, 40)
  const leeward = awa >= 0 ? 'port' : 'starboard', lee = awa >= 0 ? -1 : 1   // +1 = to starboard
  // Heel: the side force ~ AWS^2 times the heeling part of the sail force - lift across the apparent wind plus drag
  // along it (drag about 0.35 of lift): cos A + 0.35 sin A up to a beam reach, the drag's share 0.35 sin A beyond -
  // most close-hauled; less sail, less heel.
  const fullArea = boat.mainM2 + boat.genoaM2, areaFraction = Math.min(1.2, (main.areaM2 + (headsail.kind === 'gennaker' ? headsail.areaM2 * 0.35 : headsail.areaM2)) / fullArea)
  const ar = A * Math.PI / 180, side = A <= 90 ? Math.cos(ar) + HEEL_DRAG * Math.sin(ar) : HEEL_DRAG * Math.sin(ar)
  const heelDeg = clamp(HEEL_K * aws * aws * side * areaFraction, 0, 30)
  const luffing = A < 20
  // Trim words, from the conditions.
  const trim = []
  if (luffing) trim.push('Sails luffing: too close to the wind, bear away to fill them')
  else if (A < 45) {
    trim.push(eff < 8 ? 'Traveller to windward, mainsheet eased for twist (light air)' : eff < R ? 'Traveller centred, mainsheet in, leech telltales flying' : 'Traveller down to leeward to depower, mainsheet hard in')
    trim.push(eff < R ? 'Vang eased' : 'Vang on to flatten the leech')
    trim.push(eff < 10 ? 'Outhaul eased for a fuller foot' : 'Outhaul tight for a flat foot')
    if (eff >= R - 4) trim.push('Backstay on to flatten the main')
    trim.push(eff < 10 ? 'Headsail car forward' : eff >= R ? 'Headsail car aft to open the leech' : 'Headsail car in the middle')
  } else if (A < 110) {
    trim.push('Mainsheet eased until the luff just lifts, then in a little')
    trim.push('Vang on to hold the boom down')
    trim.push('Headsail sheet eased, car forward')
  } else {
    trim.push(headsail.kind === 'gennaker' ? 'Gennaker tack to the bowsprit, sheet eased until the luff curls' : gooseWing ? 'Headsail poled out to windward (goose-wing), main on the other side' : 'Headsail blanketed by the main: furl it or pole it out')
    trim.push('Boom out against the shrouds, preventer rigged')
    trim.push('Vang on; watch for an accidental gybe')
  }
  if (heelDeg > 22) trim.push(`Heel ${Math.round(heelDeg)}°: reef or depower`)
  const level = bare ? 'severe' : storm ? 'severe' : reef >= 2 ? 'warning' : reef === 1 ? 'caution' : 'ok'
  const text = bare ? 'Storm: no sail can be carried; heave to, run off under bare poles or stay in harbour'
    : storm ? 'Gale: storm trysail and storm jib'
    : `${main.label} and ${headsail.label.charAt(0).toLowerCase()}${headsail.label.slice(1)}${gooseWing ? ', goose-winged' : ''}`
  return { effectiveKt: eff, reef, main, headsail, gooseWing, boomDeg, jibDeg, leeward, lee, heelDeg, luffing, trim, level, text, areaM2: main.areaM2 + headsail.areaM2, sailing: !bare }
}

// The sail plan for a boat on a heading at a speed in a true wind (from, kt): the apparent wind first.
export function sailPlanFor({ twdFrom, tws, headingDeg, speedKt }, boat, settings = {}) {
  const aw = apparentWind(twdFrom, tws, headingDeg, speedKt)
  return { ...sailPlan({ tws, twa: aw.twa, aws: aw.aws, awa: aw.awa }, boat, settings), ...aw }
}

// A short label of the sail plan for tables and the 3D card ("2nd reef · jib", "gennaker", "storm sails").
export function sailShort(plan) {
  if (!plan.sailing) return 'bare poles'
  if (plan.main.kind === 'trysail') return 'storm sails'
  const main = plan.reef ? `${['', '1st', '2nd', '3rd'][plan.reef]} reef` : 'full main'
  const head = { gennaker: 'gennaker', genoa: plan.headsail.furl ? 'genoa 75 %' : 'genoa', jib: plan.headsail.furl >= 0.6 ? 'small jib' : 'jib', storm: 'storm jib', none: '' }[plan.headsail.kind]
  return `${main} · ${head}${plan.gooseWing ? ' (goose-wing)' : ''}`
}
