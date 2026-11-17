// A tack or a gybe simulated in time: the heading swings through the wind on an eased S-curve at the boat's turn rate,
// the speed follows the polar with a first-order lag (nil in the no-go zone, with extra drag while the rudder works),
// the sails luff, cross and fill, the boom swings (fast in a gybe) and the heel changes sides. The result is sampled
// every dt seconds for the 2D and 3D animations, with the time and distance lost against sailing on at the steady VMG.
// Pure functions. Requirements: docs/weather/route-weather-and-sail-routing.md (S17). Illustrative dynamics.
import { angleDiff, norm360, KT_PER_MS } from './station.js'
import { apparentWind, pointOfSail } from './sailing.js'
import { polarSpeed, optimalVmgCached } from './polars.js'
import { sailPlan } from './sailTrim.js'

const MS_PER_KT = 1 / KT_PER_MS
const smooth = x => (x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x))
export const LEAD_IN_S = 3, RUDDER_DRAG = 0.3, RECOVERY_FRACTION = 0.95, DECEL_FACTOR = 0.7
export const STRONG_GYBE_KT = 25, ROUGH_TACK_HS = 2.5

// Which manoeuvre a turn to the other side is: a tack with the wind forward of the beam, a gybe with it aft.
export const manoeuvreKind = twaDeg => (Math.abs(twaDeg) < 90 ? 'tack' : 'gybe')

// The heading after a tack or gybe: the same true wind angle on the other side (twa -> -twa), or the given target TWA.
export function mirroredHeading(twdFrom, headingDeg, targetTwa = null) {
  const twa = angleDiff(twdFrom, headingDeg), next = targetTwa === null ? -twa : -Math.sign(twa || 1) * Math.abs(targetTwa)
  return norm360(twdFrom - next)
}

// Simulate the manoeuvre. conditions: { twdFrom, tws (kt), hs (m) }; start: { headingDeg, speedKt? (steady polar speed
// if omitted) }; kind: 'tack' | 'gybe' | null (by the wind angle); targetTwa: the TWA to settle on (default mirrored);
// boat: polars.js; settings: sailTrim settings. Returns { kind, samples, summary, warnings }.
export function simulateManoeuvre({ conditions, start, kind = null, targetTwa = null, boat, settings = {}, dt = 0.25 }) {
  const { twdFrom, tws, hs = 0 } = conditions, h0 = norm360(start.headingDeg)
  const twa0 = angleDiff(twdFrom, h0), k = kind ?? manoeuvreKind(twa0), side0 = Math.sign(twa0) || 1
  const h1 = mirroredHeading(twdFrom, h0, targetTwa), twa1 = angleDiff(twdFrom, h1)
  // The turn: a tack swings the bow through the wind, a gybe the stern (the signed change of heading, degrees).
  const delta = k === 'tack' ? -(twa1 - twa0) : (side0 > 0 ? -1 : 1) * (360 - Math.abs(twa0) - Math.abs(twa1))
  const turnS = Math.max(4, Math.abs(delta) / boat.turnRateDegS), tau = boat.accelTauS
  const steady = twa => polarSpeed(boat, tws, twa)
  const v0 = start.speedKt ?? steady(twa0)
  // The reference: sailing on at the steady VMG (towards the wind for a tack, away from it for a gybe).
  const vmg = optimalVmgCached(boat, tws), refVmgKt = k === 'tack' ? Math.max(v0 * Math.cos(twa0 * Math.PI / 180), 0.1) : Math.max(-v0 * Math.cos(twa0 * Math.PI / 180), 0.1)
  const axis = k === 'tack' ? twdFrom : norm360(twdFrom + 180)   // the direction progress is measured along
  const ax = Math.sin(axis * Math.PI / 180), ay = Math.cos(axis * Math.PI / 180)
  const samples = []
  let t = 0, v = v0, x = 0, y = 0, boom = null, jib = null, phase = 'approach', recoveredAt = null, minSpeed = v0
  const total = LEAD_IN_S + turnS + 8 * tau
  const plan0 = sailPlan({ tws, twa: twa0, ...apparentWind(twdFrom, tws, h0, v0) }, boat, settings)
  boom = plan0.boomDeg * plan0.lee; jib = plan0.jibDeg * plan0.lee
  while (t <= total + 1e-9) {
    const f = (t - LEAD_IN_S) / turnS, heading = norm360(h0 + delta * smooth(f)), turning = f > 0 && f < 1
    const twa = angleDiff(twdFrom, heading), aw = apparentWind(twdFrom, tws, heading, v)
    const plan = sailPlan({ tws, twa, aws: aw.aws, awa: aw.awa }, boat, settings)
    // Target speed: the polar at this angle (nil in irons), less rudder drag while turning.
    const rate = turning ? Math.abs(delta) * 6 * f * (1 - f) / turnS : 0
    const target = steady(twa) * (1 - RUDDER_DRAG * Math.min(1, rate / boat.turnRateDegS))
    // Sails: luffing near head to wind (centred), otherwise to leeward; the boom's swing is rate-limited - quick in a
    // gybe when the wind gets behind it, gentle when tacking.
    const boomTarget = plan.luffing ? 0 : plan.boomDeg * plan.lee, jibTarget = plan.luffing ? 0 : plan.jibDeg * plan.lee
    const boomRate = k === 'gybe' ? 70 : 20
    boom += Math.max(-boomRate * dt, Math.min(boomRate * dt, boomTarget - boom))
    jib += Math.max(-40 * dt, Math.min(40 * dt, jibTarget - jib))
    phase = f <= 0 ? 'approach' : turning ? (k === 'tack' ? (Math.abs(twa) < 30 ? 'head to wind' : 'turning') : (Math.abs(twa) > 165 ? 'stern to wind' : 'turning')) : recoveredAt === null ? 'accelerating' : 'steady'
    samples.push({ t, headingDeg: heading, twa, awa: aw.awa, aws: aw.aws, speedKt: v, boomDeg: boom, jibDeg: jib, heelDeg: plan.heelDeg * plan.lee, luffing: plan.luffing, x, y, phase, pointOfSail: pointOfSail(Math.abs(twa)), sails: plan })
    minSpeed = Math.min(minSpeed, v)
    if (f >= 1 && recoveredAt === null && v >= RECOVERY_FRACTION * steady(twa1)) recoveredAt = t
    if (recoveredAt !== null && t >= recoveredAt + 2) break
    // Integrate: speed lag, position (metres; x east, y north).
    // Slowing is quicker than speeding up (drag of the hull and the flogging sails against the drive of filled ones).
    v += (target - v) * (1 - Math.exp(-dt / (target < v ? tau * DECEL_FACTOR : tau)))
    x += v * MS_PER_KT * Math.sin(heading * Math.PI / 180) * dt; y += v * MS_PER_KT * Math.cos(heading * Math.PI / 180) * dt
    t += dt
  }
  const end = samples.at(-1), T = end.t
  const made = end.x * ax + end.y * ay                              // progress along the axis (m)
  const lostM = Math.max(0, refVmgKt * MS_PER_KT * T - made), lostS = lostM / (refVmgKt * MS_PER_KT)
  const warnings = []
  if (k === 'gybe' && tws >= STRONG_GYBE_KT) warnings.push(`Gybing in ${Math.round(tws)} kt: the boom crosses violently - sheet the main in first, or tack round instead (a "chicken gybe")`)
  if (k === 'tack' && hs >= ROUGH_TACK_HS) warnings.push(`Tacking in ${hs.toFixed(1)} m seas: build speed, then tack on a flat spot between waves`)
  if (k === 'tack' && minSpeed < 0.8) warnings.push('The boat nearly stops head to wind: bear away more before tacking, or back the jib to come round')
  return {
    kind: k, samples, warnings, fromHeadingDeg: h0, toHeadingDeg: h1, fromTwa: twa0, toTwa: twa1, deltaDeg: delta,
    summary: { durationS: T, turnS, recoveredS: recoveredAt, minSpeedKt: minSpeed, startSpeedKt: v0, endSpeedKt: end.speedKt, speedLossPct: (1 - minSpeed / Math.max(v0, 0.1)) * 100,
      distanceLostM: lostM, boatLengthsLost: lostM / boat.loaM, timeLostS: lostS, refVmgKt, vmg },
  }
}

// The time lost per tack and per gybe (s) in a true wind, for routing: simulated once per boat and whole knot.
const penaltyMemo = new Map()
export function manoeuvrePenalty(boat, twsKt) {
  const s = Math.max(4, Math.min(30, Math.round(twsKt))), key = `${boat.key}:${s}`
  if (!penaltyMemo.has(key)) {
    const vmg = optimalVmgCached(boat, s)
    const tack = simulateManoeuvre({ conditions: { twdFrom: 0, tws: s }, start: { headingDeg: norm360(-vmg.upTwa) }, kind: 'tack', boat, dt: 0.5 })
    const gybe = simulateManoeuvre({ conditions: { twdFrom: 0, tws: s }, start: { headingDeg: norm360(-vmg.downTwa) }, kind: 'gybe', boat, dt: 0.5 })
    penaltyMemo.set(key, { tackS: tack.summary.timeLostS, gybeS: gybe.summary.timeLostS })
  }
  return penaltyMemo.get(key)
}

// The state of a simulated manoeuvre at time t (s), interpolated between samples (for the animations).
export function manoeuvreAt(sim, t) {
  const s = sim.samples, dt = s.length > 1 ? s[1].t - s[0].t : 1
  const k = Math.max(0, Math.min(s.length - 1, Math.floor(t / dt))), a = s[k], b = s[Math.min(s.length - 1, k + 1)], f = Math.max(0, Math.min(1, (t - a.t) / dt))
  const lerp = (p, q) => p + (q - p) * f
  return { ...a, t, headingDeg: norm360(a.headingDeg + angleDiff(b.headingDeg, a.headingDeg) * f), speedKt: lerp(a.speedKt, b.speedKt), boomDeg: lerp(a.boomDeg, b.boomDeg), jibDeg: lerp(a.jibDeg, b.jibDeg), heelDeg: lerp(a.heelDeg, b.heelDeg), x: lerp(a.x, b.x), y: lerp(a.y, b.y), done: t >= s.at(-1).t }
}
