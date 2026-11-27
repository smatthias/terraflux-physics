// Boat performance: the polar diagram (boat speed through the water by true wind speed and true wind angle), the best
// VMG up- and downwind, the loss in waves from ahead, and the boats on offer. Pure functions.
// Requirements and limits: docs/weather/route-weather-and-sail-routing.md (S15).
//
// The reference polar is a generic, illustrative polar of a 40 ft cruising yacht (waterline 11 m), shaped like the
// published VPP polars of such boats (ORC certificates): no speed in the no-go zone, the best VMG upwind at about 42-45
// degrees true, the fastest on a broad reach, running deeper as the wind rises, and the sail plan reduced in a gale.
// It is not a certificate of any real boat. Other sizes are scaled by Froude (speed ~ sqrt of the waterline length).

// True wind speeds (kt) and angles (deg) of the table; speeds in kt.
export const POLAR_TWS = [0, 4, 6, 8, 10, 12, 14, 16, 20, 25, 30, 35]
export const POLAR_TWA = [0, 25, 32, 40, 45, 52, 60, 75, 90, 110, 120, 135, 150, 165, 180]
const Z = Array(12).fill(0)
export const REFERENCE_POLAR = [
  Z,                                                                   // 0: head to wind
  Z,                                                                   // 25: in irons
  [0, 0.9, 1.5, 2.0, 2.4, 2.6, 2.8, 2.9, 2.9, 2.7, 2.3, 1.8],          // 32: pinching, sails luffing
  [0, 2.2, 3.4, 4.4, 5.1, 5.5, 5.8, 5.9, 5.9, 5.6, 5.0, 4.2],          // 40
  [0, 2.6, 3.9, 4.9, 5.6, 6.0, 6.2, 6.3, 6.4, 6.1, 5.6, 4.8],          // 45
  [0, 2.9, 4.3, 5.3, 6.0, 6.4, 6.6, 6.7, 6.8, 6.6, 6.1, 5.3],          // 52
  [0, 3.2, 4.6, 5.6, 6.3, 6.7, 6.9, 7.0, 7.2, 7.0, 6.6, 5.9],          // 60
  [0, 3.4, 4.9, 5.9, 6.6, 7.0, 7.3, 7.4, 7.6, 7.6, 7.2, 6.5],          // 75
  [0, 3.5, 5.0, 6.0, 6.7, 7.1, 7.4, 7.6, 7.9, 8.0, 7.7, 7.0],          // 90
  [0, 3.4, 5.0, 6.0, 6.8, 7.3, 7.6, 7.9, 8.3, 8.6, 8.4, 7.8],          // 110
  [0, 3.2, 4.8, 5.9, 6.7, 7.2, 7.6, 7.9, 8.4, 8.8, 8.7, 8.2],          // 120
  [0, 2.8, 4.3, 5.4, 6.3, 6.9, 7.3, 7.7, 8.3, 8.9, 9.0, 8.6],          // 135
  [0, 2.3, 3.6, 4.7, 5.6, 6.3, 6.8, 7.2, 7.9, 8.6, 8.9, 8.6],          // 150
  [0, 2.0, 3.1, 4.1, 5.0, 5.7, 6.3, 6.7, 7.4, 8.1, 8.5, 8.3],          // 165
  [0, 1.8, 2.9, 3.8, 4.7, 5.4, 6.0, 6.4, 7.1, 7.8, 8.2, 8.0],          // 180: dead run
]
export const REFERENCE_LWL_M = 11

// The boats: size, sails (m2) and handling. turnRateDegS: heading change in a tack or gybe; accelTauS: the time
// constant of the speed's response (first order); waveLossPerM: the speed lost per metre of head seas.
export const BOATS = {
  cruiser30: { key: 'cruiser30', label: '30 ft cruiser', loaM: 9.2, lwlM: 8.2, beamM: 3.2, mastM: 13, mainM2: 22, genoaM2: 30, jibM2: 18, stormJibM2: 5, trysailM2: 7, gennakerM2: 55, turnRateDegS: 9, accelTauS: 6 },
  cruiser40: { key: 'cruiser40', label: '40 ft cruiser', loaM: 12.2, lwlM: 11, beamM: 3.9, mastM: 17.5, mainM2: 38, genoaM2: 52, jibM2: 32, stormJibM2: 8, trysailM2: 11, gennakerM2: 95, turnRateDegS: 7, accelTauS: 8 },
  performance45: { key: 'performance45', label: '45 ft performance cruiser', loaM: 13.7, lwlM: 12.6, beamM: 4.2, mastM: 20.5, mainM2: 52, genoaM2: 64, jibM2: 42, stormJibM2: 10, trysailM2: 14, gennakerM2: 140, turnRateDegS: 8, accelTauS: 7, boost: 1.06 },
}
export const DEFAULT_BOAT = 'cruiser40'
export const boatOf = key => BOATS[key] ?? BOATS[DEFAULT_BOAT]
// Froude scale of a boat's speeds against the reference polar (and a performance boat's extra speed).
export const speedScale = boat => Math.sqrt(boat.lwlM / REFERENCE_LWL_M) * (boat.boost ?? 1)
// Hull speed (kt): 1.34 sqrt(LWL in ft), the customary displacement limit.
export const hullSpeedKt = boat => 1.34 * Math.sqrt(boat.lwlM * 3.2808399)

const bracket = (axis, v) => {
  if (v <= axis[0]) return [0, 0, 0]
  if (v >= axis.at(-1)) return [axis.length - 1, axis.length - 1, 0]
  let k = 0; while (axis[k + 1] < v) k++
  return [k, k + 1, (v - axis[k]) / (axis[k + 1] - axis[k])]
}
// Boat speed (kt) through the water at true wind speed twsKt and true wind angle twaDeg (either sign; 0-180 used),
// bilinear in the table, scaled for the boat. Above 35 kt the 35 kt column is held.
export function polarSpeed(boat, twsKt, twaDeg) {
  const a = Math.min(180, Math.abs(((twaDeg % 360) + 540) % 360 - 180)), s = Math.max(0, twsKt)
  const [i0, i1, fa] = bracket(POLAR_TWA, a), [j0, j1, fs] = bracket(POLAR_TWS, s)
  const row = k => REFERENCE_POLAR[k][j0] + (REFERENCE_POLAR[k][j1] - REFERENCE_POLAR[k][j0]) * fs
  return (row(i0) + (row(i1) - row(i0)) * fa) * speedScale(boat)
}

// Best VMG (velocity made good towards or away from the wind) at a wind speed, searched to 0.5 degree:
// { upTwa, upVmg, upSpeed, downTwa, downVmg, downSpeed }.
export function optimalVmg(boat, twsKt) {
  let up = { twa: 45, vmg: 0, speed: 0 }, down = { twa: 180, vmg: 0, speed: 0 }
  for (let a = 25; a <= 90; a += 0.5) { const v = polarSpeed(boat, twsKt, a), m = v * Math.cos(a * Math.PI / 180); if (m > up.vmg) up = { twa: a, vmg: m, speed: v } }
  for (let a = 90; a <= 180; a += 0.5) { const v = polarSpeed(boat, twsKt, a), m = -v * Math.cos(a * Math.PI / 180); if (m > down.vmg) down = { twa: a, vmg: m, speed: v } }
  return { upTwa: up.twa, upVmg: up.vmg, upSpeed: up.speed, downTwa: down.twa, downVmg: down.vmg, downSpeed: down.speed }
}
// Memoised by whole knots (routing asks for it at every step).
const vmgMemo = new Map()
export function optimalVmgCached(boat, twsKt) {
  const key = `${boat.key}:${Math.round(twsKt * 2) / 2}`
  if (!vmgMemo.has(key)) vmgMemo.set(key, optimalVmg(boat, Math.round(twsKt * 2) / 2))
  return vmgMemo.get(key)
}

// Speed factor in waves: 1 - k Hs (1 + cos TWA) / 2 (head seas cost most, following seas nothing), at least 0.5;
// k = 0.06 per metre for the 40 ft reference, inversely with the waterline (small boats suffer more).
export function waveFactor(boat, hsM, twaDeg) {
  if (!(hsM > 0)) return 1
  const k = 0.06 * REFERENCE_LWL_M / boat.lwlM, a = Math.abs(twaDeg) * Math.PI / 180
  return Math.max(0.5, 1 - k * hsM * (1 + Math.cos(a)) / 2)
}

// The polar as curves for a diagram: for each wind speed, points (twa, speed) every 5 degrees from 30 to 180 degrees,
// with the best VMG angles.
export function polarCurves(boat, speeds = [6, 10, 14, 20, 25]) {
  return speeds.map(tws => ({ tws, points: Array.from({ length: 31 }, (_, k) => { const a = 30 + k * 5; return { twa: a, speed: polarSpeed(boat, tws, a) } }), vmg: optimalVmg(boat, tws) }))
}
