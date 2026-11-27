// Sailing route planning through the forecast: weather routing by isochrones, or the direct route (the legs as planned,
// beating and running in tacks and gybes where a leg is too close to the wind), with the weather along the track at the
// time of passage, the manoeuvres, the sail plan, and a state at any time for the 3D sail-through.
// Pure functions. Requirements: docs/weather/route-weather-and-sail-routing.md (S18-S21).
//
// Method:
//   - geometry in the grid's plane (east/north metres about its anchor, the engine's projection; geo.js); winds are
//     the engine's grid-frame components, so headings relative to the wind are exact there and are converted to true
//     headings for the log (the grid's convergence at each point);
//   - wind: bilinear between cell centres, linear in time between frames (held at the last frame beyond the run); the
//     tropical-cyclone wind where stronger (station.js surfaceWind); current: the ocean model's where present; waves:
//     the viewer's wind-sea estimate (waves.js) of the nearest frame;
//   - boat: polar speed (polars.js) times the wave factor; the engine when the speed under sail is below a threshold;
//     a time penalty per tack and gybe from the manoeuvre simulation (manoeuvre.js);
//   - isochrones (James 1957, Hagiwara 1989): from the start, every heading (10 degrees apart plus the best VMG
//     headings and the bearing to the mark) for one time step; in each 3-degree sector (bearing from the start) the
//     point farthest from the start is kept (counted up to the mark's distance, less a tenth of the distance to go); land, and (if asked) winds and seas above the limits, are excluded; the mark is reached when a
//     point can fetch it (or beat or run to it on the best VMG) within a step; the route is traced back.
import { KT_PER_MS, M_PER_NM, angleDiff, norm360, validTime, formatLatLon, surfaceWind, beaufort, compass16, inverseProject, cellLocation, weatherWords, presentWeather, relativeHumidity, estimateVisibility, cloudLayer } from './station.js'
import { project, distanceNm, initialCourse } from './geo.js'
import { polarSpeed, optimalVmgCached, waveFactor, boatOf } from './polars.js'
import { apparentWind, pointOfSail, tackOf, marineWarning, WARNINGS } from './sailing.js'
import { sailPlan, sailShort } from './sailTrim.js'
import { manoeuvrePenalty, simulateManoeuvre, manoeuvreAt, LEAD_IN_S } from './manoeuvre.js'
import { weatherSymbol, solarElevation } from './weatherLayers.js'

const MS_PER_KT = 1 / KT_PER_MS, RAD = Math.PI / 180
export const DEFAULT_ROUTE_SETTINGS = { boat: 'cruiser40', mode: 'routed', motor: true, motorBelowKt: 3, motorKt: 5.5, avoidLimits: true, maxWindKt: 22, maxSeasM: 2, reefKt: 18, gennaker: true, headingStepDeg: 10, sectorDeg: 3, stepMinutes: null }
export const ROUTE_MODES = { routed: 'Weather-routed (isochrones)', direct: 'Direct (the legs as planned, tacking where needed)' }
export const MAX_STEPS = 400
// Colours of the points of sail (track, strip and legends).
export const POINT_OF_SAIL_COLOURS = { 'in irons (no-go zone)': '#8a96a0', 'close-hauled': '#ff6b6b', 'close reach': '#ffb15c', 'beam reach': '#ffd166', 'broad reach': '#3ddc84', running: '#4a9dff', motoring: '#b0b8bf' }

// ---- The weather the boat meets ---------------------------------------------------------------------------------------
// Accessors over the run: conditions at (east, north) in metres and a time in ms.
// currents: a current model (currents.js) for the current at any place and time; without it the run's ocean model.
export function weatherModel(frames, { sea = null, wavesFor = null, currents = null } = {}) {
  const g = frames[0].forecast.grid, nx = g.nx, ny = g.ny, n = nx * ny, times = frames.map(validTime)
  const winds = new WeakMap()
  const windOf = frame => { if (!winds.has(frame)) { const u = new Float64Array(n), v = new Float64Array(n); for (let i = 0; i < n; i++) { const w = surfaceWind(frame, i); u[i] = w.u; v[i] = w.v } winds.set(frame, { u, v }) } return winds.get(frame) }
  const cellAt = (east, north) => { const cx = (east - g.origin_east_m) / g.spacing_east_m, cy = (north - g.origin_north_m) / g.spacing_north_m; if (!(cx >= 0 && cx < nx && cy >= 0 && cy < ny)) return -1; return Math.floor(cy) * nx + Math.floor(cx) }
  const inside = (east, north) => cellAt(east, north) >= 0
  const isSea = (east, north) => { const i = cellAt(east, north); return i >= 0 && (!sea || !!sea[i]) }
  const bilinear = (arr, east, north) => {
    const fx = Math.max(0, Math.min(nx - 1, (east - g.origin_east_m) / g.spacing_east_m - 0.5)), fy = Math.max(0, Math.min(ny - 1, (north - g.origin_north_m) / g.spacing_north_m - 0.5))
    const x0 = Math.min(nx - 2, Math.floor(fx)), y0 = Math.min(ny - 2, Math.floor(fy)), ax = nx > 1 ? fx - x0 : 0, ay = ny > 1 ? fy - y0 : 0
    if (nx < 2 || ny < 2) return arr[Math.min(n - 1, Math.round(fy) * nx + Math.round(fx))]
    const i = y0 * nx + x0
    return (arr[i] * (1 - ax) + arr[i + 1] * ax) * (1 - ay) + (arr[i + nx] * (1 - ax) + arr[i + nx + 1] * ax) * ay
  }
  const bracket = ms => { if (ms <= times[0]) return [0, 0, 0]; if (ms >= times.at(-1)) return [times.length - 1, times.length - 1, 0]; let k = 0; while (times[k + 1] < ms) k++; return [k, k + 1, (ms - times[k]) / (times[k + 1] - times[k])] }
  const nearest = ms => { const [a, b, f] = bracket(ms); return f < 0.5 ? a : b }
  function at(east, north, ms) {
    const [a, b, f] = bracket(ms), wa = windOf(frames[a]), wb = windOf(frames[b])
    const u = bilinear(wa.u, east, north) * (1 - f) + bilinear(wb.u, east, north) * f, v = bilinear(wa.v, east, north) * (1 - f) + bilinear(wb.v, east, north) * f
    const speed = Math.hypot(u, v), k = nearest(ms), frame = frames[k], i = Math.max(0, cellAt(east, north))
    const o = frame.ocean, cm = currents ? currents.at(east, north, ms) : null
    const current = currents ? (cm ? { u: cm.u, v: cm.v, source: cm.source } : null) : o && o.mask?.[i] && Number.isFinite(o.u?.[i]) ? { u: o.u[i], v: o.v[i], source: 'ocean' } : null
    const waves = wavesFor ? wavesFor(frame) : null, onSea = !sea || !!sea[i]
    const hs = waves && onSea ? waves.hs[i] : 0
    const wavesFromDeg = waves && onSea && hs > 0.05 ? norm360(Math.atan2(waves.dirEast[i], waves.dirNorth[i]) / RAD + 180) : null
    return { u, v, tws: speed * KT_PER_MS, twdFrom: speed > 1e-9 ? norm360(Math.atan2(-u, -v) / RAD) : 0, current, hs, wavesFromDeg, frameIndex: k, i, beyond: ms > times.at(-1) + 1 }
  }
  return { grid: g, at, isSea, inside, cellAt, times, frames }
}

// The grid's convergence at a point: the true bearing of grid north (degrees), for grid-to-true headings.
export function gridNorthTrue(grid, east, north) {
  const a = inverseProject(grid, east, north), b = inverseProject(grid, east, north + 1000)
  return initialCourse(a, b)
}

// ---- The boat on one heading ----------------------------------------------------------------------------------------
// Speed through the water (kt) and whether the engine is used, on a grid-frame heading in the conditions c. The engine
// is for light wind: when the boat would make less than the threshold even on a beam reach (not to motor to windward
// in a sailing breeze).
export const lightWind = (boat, opts, c) => opts.motor && polarSpeed(boat, c.tws, 90) * waveFactor(boat, c.hs, 90) < opts.motorBelowKt
function boatSpeed(boat, opts, c, headingDeg) {
  const twa = angleDiff(c.twdFrom, headingDeg)
  if (lightWind(boat, opts, c)) return { twa, stw: opts.motorKt, motoring: true }
  return { twa, stw: polarSpeed(boat, c.tws, twa) * waveFactor(boat, c.hs, twa), motoring: false }
}
// Velocity over the ground (m/s, east and north) through the water on a heading plus the current.
const overGround = (stwKt, headingDeg, c) => [stwKt * MS_PER_KT * Math.sin(headingDeg * RAD) + (c.current?.u ?? 0), stwKt * MS_PER_KT * Math.cos(headingDeg * RAD) + (c.current?.v ?? 0)]
// Making good a course over the ground across a current: the heading that cancels the cross-current (the current
// triangle, iterated as the polar speed changes with the heading). { headingDeg, stw, motoring, twa, along (m/s) } or
// null when the boat cannot stem the current on that course.
function makeGood(boat, opts, c, courseDeg) {
  const cu = c.current?.u ?? 0, cv = c.current?.v ?? 0, ux = Math.sin(courseDeg * RAD), uy = Math.cos(courseDeg * RAD)
  const cross = cu * uy - cv * ux, withCourse = cu * ux + cv * uy   // current to the right of the course, and along it
  let h = courseDeg, sp = boatSpeed(boat, opts, c, h)
  for (let k = 0; k < 3 && (cross || k === 0); k++) {
    const v = sp.stw * MS_PER_KT; if (!(v > 0.05) || Math.abs(cross) >= v) return null
    h = norm360(courseDeg - Math.asin(cross / v) / RAD); sp = boatSpeed(boat, opts, c, h)
  }
  const v = sp.stw * MS_PER_KT, along = v * Math.cos((h - courseDeg) * RAD) + withCourse
  return along > 0.05 ? { headingDeg: h, stw: sp.stw, motoring: sp.motoring, twa: sp.twa, along } : null
}
const within = (c, opts) => !opts.avoidLimits || (c.tws <= opts.maxWindKt && c.hs <= opts.maxSeasM)
// A straight segment stays on the water: sampled every ~quarter cell.
function clearWater(model, e0, n0, e1, n1) {
  const g = model.grid, len = Math.hypot(e1 - e0, n1 - n0), k = Math.max(2, Math.ceil(len / (Math.min(g.spacing_east_m, g.spacing_north_m) / 4)))
  for (let j = 1; j <= k; j++) { const f = j / k; if (!model.isSea(e0 + (e1 - e0) * f, n0 + (n1 - n0) * f)) return false }
  return true
}

// To a mark that cannot be fetched: on the two headings at the best VMG angle (both tacks or both gybes), the turn at
// the layline. Returns the two legs { headingDeg, distanceM } (in the order sailed: the current side first) or null.
export function lay(vectorE, vectorN, twdFrom, angleDeg, firstSign) {
  const hA = norm360(twdFrom - firstSign * angleDeg), hB = norm360(twdFrom + firstSign * angleDeg)
  const ax = Math.sin(hA * RAD), ay = Math.cos(hA * RAD), bx = Math.sin(hB * RAD), by = Math.cos(hB * RAD), det = ax * by - ay * bx
  if (Math.abs(det) < 1e-9) return null
  const a = (vectorE * by - vectorN * bx) / det, b = (ax * vectorN - ay * vectorE) / det
  if (a < -1e-6 || b < -1e-6) return null
  return [{ headingDeg: hA, distanceM: Math.max(0, a) }, { headingDeg: hB, distanceM: Math.max(0, b) }]
}

// From (e, n) at time t (ms) to the mark: the legs that get there (straight if the mark can be fetched, else beating or
// running on the best VMG angles), each { headingDeg, seconds, e, n (end) }; null if it cannot be done (land, limits).
function finish(model, boat, opts, node, mark, { maxSeconds = Infinity, sideSign = 0 } = {}) {
  const c = model.at(node.e, node.n, node.t), dE = mark.e - node.e, dN = mark.n - node.n, d = Math.hypot(dE, dN)
  if (d < 1) return { legs: [], seconds: 0 }
  if (!within(c, opts)) return null
  const bearing = norm360(Math.atan2(dE, dN) / RAD), twa = angleDiff(c.twdFrom, bearing), vmg = optimalVmgCached(boat, c.tws)
  const straight = () => {
    const s = makeGood(boat, opts, c, bearing)
    return s ? [{ headingDeg: s.headingDeg, seconds: d / s.along, e: mark.e, n: mark.n, motoring: s.motoring }] : null
  }
  let legs = null
  const upwind = Math.abs(twa) < vmg.upTwa, downwind = Math.abs(twa) > vmg.downTwa
  if ((upwind || downwind) && !lightWind(boat, opts, c)) {
    const angle = upwind ? vmg.upTwa : vmg.downTwa, first = sideSign || Math.sign(twa) || 1
    const parts = lay(dE, dN, c.twdFrom, angle, first)
    if (parts) {
      legs = []; let e = node.e, n = node.n
      for (const p of parts) {
        if (p.distanceM < 1) continue
        // Each tack as a course over the ground (the laylines ignore the current; the heading corrects for it).
        const s = makeGood(boat, opts, c, p.headingDeg)
        const e1 = e + Math.sin(p.headingDeg * RAD) * p.distanceM, n1 = n + Math.cos(p.headingDeg * RAD) * p.distanceM
        if (!s || !clearWater(model, e, n, e1, n1)) { legs = null; break }
        legs.push({ headingDeg: s.headingDeg, seconds: p.distanceM / s.along, e: e1, n: n1, motoring: s.motoring }); e = e1; n = n1
      }
      if (legs && legs.length === 2) legs[1].seconds += (upwind ? manoeuvrePenalty(boat, c.tws).tackS : manoeuvrePenalty(boat, c.tws).gybeS)
    }
  }
  if (!legs) { legs = straight(); if (legs && !clearWater(model, node.e, node.n, mark.e, mark.n)) legs = null }
  if (!legs) return null
  const seconds = legs.reduce((a, l) => a + l.seconds, 0)
  return seconds <= maxSeconds ? { legs, seconds } : null
}

// ---- One leg by isochrones -------------------------------------------------------------------------------------------
function isochroneLeg(model, boat, opts, from, mark, t0) {
  const D0 = Math.hypot(mark.e - from.e, mark.n - from.n)
  const stepS = (opts.stepMinutes ?? Math.max(15, Math.min(180, Math.round(D0 / (5 * MS_PER_KT) / 30 / 60 / 5) * 5 || 15))) * 60
  let nodes = [{ e: from.e, n: from.n, t: t0, parent: null, headingDeg: null, sign: from.sign ?? 0, motoring: false }]
  const isochrones = [], headingsBase = []
  for (let h = 0; h < 360; h += opts.headingStepDeg) headingsBase.push(h)
  let best = null
  for (let step = 0; step < MAX_STEPS; step++) {
    // Can any point of the isochrone reach the mark within this step? (Only points within a step's reach are tried.)
    const reach = (Math.max(opts.motorKt, 14 * Math.sqrt(boat.lwlM / 11)) + 4) * MS_PER_KT * stepS
    for (const node of nodes) {
      if (Math.hypot(mark.e - node.e, mark.n - node.n) > reach) continue
      const f = finish(model, boat, opts, node, mark, { maxSeconds: stepS, sideSign: node.sign })
      if (f && (!best || node.t + f.seconds * 1000 < best.t)) best = { node, finish: f, t: node.t + f.seconds * 1000 }
    }
    if (best) break
    const next = new Map(), sector = opts.sectorDeg
    for (const node of nodes) {
      const c = model.at(node.e, node.n, node.t)
      if (!within(c, opts)) continue
      const vmg = optimalVmgCached(boat, c.tws), toMark = norm360(Math.atan2(mark.e - node.e, mark.n - node.n) / RAD)
      const headings = [...headingsBase, toMark, norm360(c.twdFrom - vmg.upTwa), norm360(c.twdFrom + vmg.upTwa), norm360(c.twdFrom - vmg.downTwa), norm360(c.twdFrom + vmg.downTwa)]
      for (const h of headings) {
        const s = boatSpeed(boat, opts, c, h)
        if (!(s.stw > 0.05)) continue
        const sign = s.motoring ? node.sign : Math.sign(s.twa) || node.sign
        let dt = stepS
        if (!s.motoring && node.sign && sign !== node.sign && !node.motoring) { const p = manoeuvrePenalty(boat, c.tws); dt -= Math.abs(s.twa) < 90 ? p.tackS : p.gybeS }
        const [ve, vn] = overGround(s.stw, h, c), e = node.e + ve * dt, n = node.n + vn * dt
        if (!clearWater(model, node.e, node.n, e, n)) continue
        // A point beyond the limits is not kept (it would take the sector and never be expanded).
        if (opts.avoidLimits && !within(model.at(e, n, node.t + stepS * 1000), opts)) continue
        // The point's sector (bearing from the start) and its score: the distance from the start, counted only up to
        // the mark's distance (beyond it, being farther out is no better), less a tenth of the distance still to go -
        // the frontier spreads out as the isochrone method requires, and the points nearest the mark win past it.
        const toGo = Math.hypot(mark.e - e, mark.n - n)
        // Do not wander off: points much farther from the mark than the start are dropped.
        if (toGo > D0 * 1.6 + 20000) continue
        const r = Math.min(Math.hypot(e - from.e, n - from.n), D0) - 0.1 * toGo, key = Math.round(Math.atan2(e - from.e, n - from.n) / RAD / sector)
        const prev = next.get(key)
        if (!prev || r > prev.r) next.set(key, { e, n, t: node.t + stepS * 1000, parent: node, headingDeg: h, sign, motoring: s.motoring, r })
      }
    }
    nodes = [...next.values()]
    if (!nodes.length) return { ok: false, reason: opts.avoidLimits ? 'No route: land, or winds and seas above the limits set, block every way forward.' : 'No route: land blocks every way forward.', isochrones }
    isochrones.push(nodes.map(p => ({ e: p.e, n: p.n })).sort((p, q) => Math.atan2(p.e - from.e, p.n - from.n) - Math.atan2(q.e - from.e, q.n - from.n)))
    if (nodes[0].t - t0 > (model.times.at(-1) - t0) + 72 * 3.6e6) return { ok: false, reason: 'No route within three days past the end of the run.', isochrones }
  }
  if (!best) return { ok: false, reason: 'No route found within the step limit.', isochrones }
  // Trace back: the headings sailed, then the finish legs.
  const chain = []; for (let p = best.node; p; p = p.parent) chain.unshift(p)
  const path = chain.map(p => ({ e: p.e, n: p.n, t: p.t }))
  const segs = chain.slice(1).map((p, k) => ({ from: path[k], to: path[k + 1], headingDeg: p.headingDeg, motoring: p.motoring }))
  let e = best.node.e, n = best.node.n, t = best.node.t
  for (const l of best.finish.legs) { const to = { e: l.e, n: l.n, t: t + l.seconds * 1000 }; segs.push({ from: { e, n, t }, to, headingDeg: l.headingDeg, motoring: l.motoring }); e = l.e; n = l.n; t = to.t }
  return { ok: true, segs: smooth(model, boat, opts, segs, stepS, from.sign ?? 0), isochrones, stepS }
}

// ---- Smoothing the traced route -----------------------------------------------------------------------------------------
// The isochrone route zig-zags by the heading resolution (10 degrees). Consecutive steps on the same side of the wind
// are merged into straight legs where that is clear water, stays on that side and is no slower; the result is sailed
// again from the start (resail) and kept only if it arrives no later.
// Sail a polyline straight leg by leg (pieces of at most stepS), each piece with the conditions at its start; null if a
// piece cannot be sailed (in the no-go zone, or the limits are set and exceeded).
function resail(model, boat, opts, points, t0, stepS, sign0) {
  const segs = []; let t = t0, sign = sign0
  for (let k = 1; k < points.length; k++) {
    const a = points[k - 1], b = points[k], L = Math.hypot(b.e - a.e, b.n - a.n), h = norm360(Math.atan2(b.e - a.e, b.n - a.n) / RAD)
    if (L < 1) continue
    let done = 0
    while (done < L - 1e-6) {
      const e = a.e + (b.e - a.e) * done / L, n = a.n + (b.n - a.n) * done / L, c = model.at(e, n, t)
      if (!within(c, opts)) return null
      const sp = makeGood(boat, opts, c, h)
      if (!sp || (!sp.motoring && polarSpeed(boat, c.tws, sp.twa) < 0.5 * polarSpeed(boat, c.tws, 90))) return null   // the current, pinching or in irons
      const along = sp.along
      const nextSign = sp.motoring ? sign : Math.sign(sp.twa) || sign
      let penalty = 0
      if (!sp.motoring && sign && nextSign !== sign) { const p = manoeuvrePenalty(boat, c.tws); penalty = Math.abs(sp.twa) < 90 ? p.tackS : p.gybeS }
      const piece = Math.min(L - done, along * stepS), dt = piece / along + penalty
      segs.push({ from: { e, n, t }, to: { e: a.e + (b.e - a.e) * (done + piece) / L, n: a.n + (b.n - a.n) * (done + piece) / L, t: t + dt * 1000 }, headingDeg: sp.headingDeg, motoring: sp.motoring })
      done += piece; t += dt * 1000; sign = nextSign
    }
  }
  return segs
}
function smooth(model, boat, opts, segs, stepS, sign0) {
  if (segs.length < 3) return segs
  const V = [segs[0].from, ...segs.map(x => x.to)], sides = segs.map(x => (x.motoring ? 0 : Math.sign(angleDiff(model.at(x.from.e, x.from.n, x.from.t).twdFrom, x.headingDeg)))), out = [V[0]]
  let i = 0
  while (i < V.length - 1) {
    let j = i + 1
    for (let k = i + 2; k < V.length; k++) {
      if (sides.slice(i, k).some(v => v !== sides[i])) break
      if (!clearWater(model, V[i].e, V[i].n, V[k].e, V[k].n)) break
      const piece = resail(model, boat, opts, [V[i], V[k]], V[i].t, stepS, sides[i] || sign0)
      if (!piece || piece.at(-1).to.t > V[k].t + 1000 * Math.max(1, 0.002 * (V[k].t - V[i].t) / 1000)) break
      j = k
    }
    out.push(V[j]); i = j
  }
  if (out.length === V.length) return segs
  const again = resail(model, boat, opts, out, V[0].t, stepS, sign0)
  return again && again.at(-1).to.t <= segs.at(-1).to.t + 60000 ? again : segs
}

// ---- One leg directly: along the rhumb line, tacking or gybing inside a corridor where the mark cannot be fetched ----
function directLeg(model, boat, opts, from, mark, t0) {
  const D0 = Math.hypot(mark.e - from.e, mark.n - from.n), corridor = Math.max(M_PER_NM, Math.min(0.15 * D0, 10 * M_PER_NM))
  const stepS = Math.max(120, Math.min(1200, D0 / (5 * MS_PER_KT) / 120))
  const ux = (mark.e - from.e) / (D0 || 1), uy = (mark.n - from.n) / (D0 || 1)
  const segs = []
  let e = from.e, n = from.n, t = t0, sign = from.sign ?? 0, motoringPrev = false
  for (let k = 0; k < 20000; k++) {
    const node = { e, n, t, sign }
    const f = finish(model, boat, { ...opts, avoidLimits: false }, node, mark, { maxSeconds: stepS, sideSign: sign })
    if (f) { for (const l of f.legs) { const to = { e: l.e, n: l.n, t: t + l.seconds * 1000 }; segs.push({ from: { e, n, t }, to, headingDeg: l.headingDeg, motoring: l.motoring }); e = l.e; n = l.n; t = to.t } return { ok: true, segs, stepS } }
    const c = model.at(e, n, t), vmg = optimalVmgCached(boat, c.tws), bearing = norm360(Math.atan2(mark.e - e, mark.n - n) / RAD), twa = angleDiff(c.twdFrom, bearing)
    let h = bearing
    const cross = (e - from.e) * uy - (n - from.n) * ux            // right of the rhumb line positive
    const beat = Math.abs(twa) < vmg.upTwa, run = Math.abs(twa) > vmg.downTwa
    const motorOnly = lightWind(boat, opts, c)
    if ((beat || run) && !motorOnly) {
      const angle = beat ? vmg.upTwa : vmg.downTwa
      let s = sign || (Math.sign(twa) || 1)
      const heading = sg => norm360(c.twdFrom - sg * angle)
      // The side a heading takes the boat to (right of the rhumb line: positive).
      const sideOf = sg => { const hh = heading(sg) * RAD; return Math.sin(hh) * uy - Math.cos(hh) * ux }
      if (Math.abs(cross) > corridor && Math.sign(sideOf(s)) === Math.sign(cross)) s = -s
      h = heading(s)
    }
    const sp = boatSpeed(boat, opts, c, h), [ve, vn] = overGround(sp.stw, h, c)
    let e1 = e + ve * stepS, n1 = n + vn * stepS
    if (!clearWater(model, e, n, e1, n1)) { return { ok: false, reason: `The direct route runs onto land near ${formatLatLon(inverseProject(model.grid, e1, n1))}: add a waypoint around it, or use weather routing.`, segs } }
    const nextSign = sp.motoring ? sign : Math.sign(sp.twa) || sign
    let dt = stepS
    if (!sp.motoring && !motoringPrev && sign && nextSign !== sign) { const p = manoeuvrePenalty(boat, c.tws); const lost = Math.abs(sp.twa) < 90 ? p.tackS : p.gybeS; e1 = e + ve * (stepS - lost); n1 = n + vn * (stepS - lost) }
    segs.push({ from: { e, n, t }, to: { e: e1, n: n1, t: t + dt * 1000 }, headingDeg: h, motoring: sp.motoring })
    e = e1; n = n1; t += dt * 1000; sign = nextSign; motoringPrev = sp.motoring
    if (t - t0 > (model.times.at(-1) - t0) + 72 * 3.6e6) return { ok: false, reason: 'The direct route does not arrive within three days past the end of the run.', segs }
  }
  return { ok: false, reason: 'The direct route did not converge.', segs }
}

// ---- The route --------------------------------------------------------------------------------------------------------
// frames: the run; startIndex: departure frame; points: [{ name, lat, lon }] (start first, destination last); sea: the
// land/sea mask (1 = sea; null = unknown); wavesFor(frame): the frame's wave field or null; settings: see
// DEFAULT_ROUTE_SETTINGS; departMs: an exact departure time (defaults to the frame's).
export function sailRoute({ frames, startIndex = 0, points, sea = null, wavesFor = null, currents = null, settings = {}, departMs = null, model = null, compare = true }) {
  const opts = { ...DEFAULT_ROUTE_SETTINGS, ...settings }, boat = boatOf(opts.boat)
  if (!points || points.length < 2) return { ok: false, reason: 'Choose a start and a destination.' }
  const m = model ?? weatherModel(frames, { sea, wavesFor, currents }), g = m.grid
  const marks = []
  for (const [k, p] of points.entries()) {
    const { east, north } = project(g, p)
    if (!m.inside(east, north)) return { ok: false, reason: `${p.name || `Point ${k + 1}`} lies outside the simulated area: there is no weather there.` }
    if (!m.isSea(east, north)) return { ok: false, reason: `${p.name || `Point ${k + 1}`} is on land (${p.lat.toFixed(3)}, ${p.lon.toFixed(3)}): choose a point on the water.` }
    marks.push({ e: east, n: north, name: p.name || (k === 0 ? 'Start' : k === points.length - 1 ? 'Destination' : `WP${k}`), lat: p.lat, lon: p.lon })
  }
  for (let k = 1; k < marks.length; k++) if (Math.hypot(marks[k].e - marks[k - 1].e, marks[k].n - marks[k - 1].n) < 200) return { ok: false, reason: `${marks[k - 1].name} and ${marks[k].name} are the same place.` }
  const t0 = departMs ?? validTime(frames[startIndex])
  let segs = [], isochrones = [], t = t0, sign = 0, stepS = null, relaxed = false
  for (let k = 1; k < marks.length; k++) {
    const from = { ...marks[k - 1], e: segs.length ? segs.at(-1).to.e : marks[k - 1].e, n: segs.length ? segs.at(-1).to.n : marks[k - 1].n, sign }
    let leg = opts.mode === 'direct' ? directLeg(m, boat, opts, from, marks[k], t) : isochroneLeg(m, boat, opts, from, marks[k], t)
    // Limits that cannot be kept: route anyway and say so.
    if (!leg.ok && opts.mode !== 'direct' && opts.avoidLimits) { const again = isochroneLeg(m, boat, { ...opts, avoidLimits: false }, from, marks[k], t); if (again.ok) { leg = again; relaxed = true } }
    if (!leg.ok) return { ok: false, reason: `${marks[k - 1].name} → ${marks[k].name}: ${leg.reason}`, isochrones: [...isochrones, ...(leg.isochrones ?? [])] }
    for (const s of leg.segs) s.leg = k - 1
    segs = segs.concat(leg.segs); isochrones = isochrones.concat(leg.isochrones ?? []); stepS = stepS ?? leg.stepS
    t = segs.at(-1).to.t
    const last = segs.filter(s => !s.motoring).at(-1); if (last) { const c = m.at(last.from.e, last.from.n, last.from.t); sign = Math.sign(angleDiff(c.twdFrom, last.headingDeg)) || sign }
  }
  const route = buildRoute(m, boat, opts, marks, segs, { t0, isochrones, stepS, relaxed })
  // Weather routing is compared with the direct route (the legs as planned), sailed from the same time.
  if (opts.mode === 'routed' && compare) {
    const d = sailRoute({ frames, startIndex, points, sea, wavesFor, currents, settings: { ...opts, mode: 'direct' }, departMs: t0, model: m, compare: false })
    route.compare = d.ok ? { ok: true, hours: d.hours, sailedNm: d.sailedNm, tacks: d.tacks, gybes: d.gybes, maxTws: d.maxTws, savedHours: d.hours - route.hours } : { ok: false, reason: d.reason }
  }
  return route
}

// The track, log, manoeuvres, stretches and summary from the segments sailed.
function buildRoute(m, boat, opts, marks, rawSegs, { t0, isochrones, stepS, relaxed }) {
  const g = m.grid, frames = m.frames
  // Slivers (under 150 m, e.g. the last metres to a mark) join the segment before them: no phantom turns.
  const segs = []
  for (const s of rawSegs) {
    const len = Math.hypot(s.to.e - s.from.e, s.to.n - s.from.n), prev = segs.at(-1)
    if (len < 150 && prev && prev.leg === s.leg) { prev.to = s.to; continue }
    segs.push({ ...s })
  }
  if (segs.length > 1 && Math.hypot(segs[0].to.e - segs[0].from.e, segs[0].to.n - segs[0].from.n) < 150 && segs[1].leg === segs[0].leg) { segs[1].from = segs[0].from; segs.shift() }
  const point = (e, n) => inverseProject(g, e, n)
  let dist = 0
  const track = segs.map((s, k) => {
    const c = m.at(s.from.e, s.from.n, s.from.t), sp = boatSpeed(boat, opts, c, s.headingDeg), conv = gridNorthTrue(g, s.from.e, s.from.n)
    const seconds = (s.to.t - s.from.t) / 1000, overM = Math.hypot(s.to.e - s.from.e, s.to.n - s.from.n)
    const stw = s.motoring ? opts.motorKt : sp.stw, aw = apparentWind(c.twdFrom, c.tws, s.headingDeg, stw)
    const plan = s.motoring ? null : sailPlan({ tws: c.tws, twa: aw.twa, aws: aw.aws, awa: aw.awa }, boat, opts)
    const frame = frames[c.frameIndex], i = c.i, pos = point(s.from.e, s.from.n)
    const wx = presentWeather(frame, i), vis = estimateVisibility({ rh: relativeHumidity(frame, i), weather: wx, cloudBase: cloudLayer(frame, i).layer?.base ?? null })
    const row = {
      k, leg: s.leg, ms: s.from.t, endMs: s.to.t, hours: seconds / 3600, e: s.from.e, n: s.from.n, e1: s.to.e, n1: s.to.n, lat: pos.lat, lon: pos.lon, i, frameIndex: c.frameIndex, beyond: c.beyond,
      gridHeadingDeg: s.headingDeg, headingDeg: norm360(s.headingDeg + conv), cogDeg: norm360(Math.atan2(s.to.e - s.from.e, s.to.n - s.from.n) / RAD + conv), stwKt: stw, sogKt: seconds > 0 ? overM / seconds * KT_PER_MS : 0,
      fromNm: dist / M_PER_NM, distNm: overM / M_PER_NM,
      tws: c.tws, twdFrom: norm360(c.twdFrom + conv), gridTwdFrom: c.twdFrom, twa: aw.twa, aws: aw.aws, awa: aw.awa, tack: tackOf(aw.twa), pointOfSail: s.motoring ? 'motoring' : pointOfSail(Math.abs(aw.twa)),
      motoring: !!s.motoring, sails: plan, sailShort: plan ? sailShort(plan) : 'engine', beaufort: beaufort(c.tws / KT_PER_MS), warning: marineWarning(c.tws, c.hs, { scaKt: opts.maxWindKt, scaSeasM: opts.maxSeasM }),
      hs: c.hs, wavesFromDeg: c.wavesFromDeg === null ? null : norm360(c.wavesFromDeg + conv), current: c.current ? { setDeg: norm360(Math.atan2(c.current.u, c.current.v) / RAD + conv), driftKt: Math.hypot(c.current.u, c.current.v) * KT_PER_MS, source: c.current.source ?? 'ocean' } : null,
      symbol: weatherSymbol(frame, i), weather: weatherWords(wx, vis), visibilityM: vis?.metres ?? null, thunder: wx.thunder, night: solarElevation(pos.lat, pos.lon, s.from.t) < -0.833,
    }
    dist += overM
    return row
  })
  // Manoeuvres: where the wind changes sides under sail.
  const manoeuvres = []
  for (let k = 1; k < track.length; k++) {
    const a = track[k - 1], b = track[k]
    if (a.motoring || b.motoring || Math.sign(a.twa) === Math.sign(b.twa) || !a.twa || !b.twa) continue
    const kind = Math.abs(b.twa) < 90 ? 'tack' : 'gybe'
    const sim = simulateManoeuvre({ conditions: { twdFrom: b.gridTwdFrom, tws: b.tws, hs: b.hs }, start: { headingDeg: a.gridHeadingDeg, speedKt: a.stwKt }, kind, targetTwa: Math.abs(b.twa), boat, settings: opts, dt: 0.25 })
    manoeuvres.push({ index: k, kind, ms: b.ms, lat: b.lat, lon: b.lon, e: b.e, n: b.n, fromHeadingDeg: a.headingDeg, toHeadingDeg: b.headingDeg, tws: b.tws, twdFrom: b.twdFrom, hs: b.hs, sim, warnings: sim.warnings })
  }
  // Stretches of similar sailing (point of sail, sails, force).
  const keyOf = r => `${r.pointOfSail}|${r.sailShort}|${r.beaufort.force}`
  let groups = []
  for (const r of track) { const last = groups.at(-1); if (last && last.key === keyOf(r)) last.rows.push(r); else groups.push({ key: keyOf(r), rows: [r] }) }
  while (groups.length > 12) {
    let k = 0; const hrs = gr => gr.rows.reduce((a, r) => a + r.hours, 0)
    for (let j = 1; j < groups.length; j++) if (hrs(groups[j]) < hrs(groups[k])) k = j
    const into = k === 0 ? 1 : k === groups.length - 1 ? k - 1 : (hrs(groups[k - 1]) <= hrs(groups[k + 1]) ? k - 1 : k + 1), a = Math.min(k, into)
    groups.splice(a, 2, { key: groups[a].key, rows: [...groups[a].rows, ...groups[a + 1].rows] })
  }
  const stretches = groups.map(gr => {
    const rows = gr.rows, hrs = rows.reduce((a, r) => a + r.hours, 0), main = rows.reduce((w, r) => (r.hours > w.hours ? r : w), rows[0])
    return { fromMs: rows[0].ms, toMs: rows.at(-1).endMs, fromNm: rows[0].fromNm, toNm: rows.at(-1).fromNm + rows.at(-1).distNm, hours: hrs, pointOfSail: main.pointOfSail, tack: main.tack, sailShort: main.sailShort, sails: main.sails, motoring: main.motoring,
      twsMin: Math.min(...rows.map(r => r.tws)), twsMax: Math.max(...rows.map(r => r.tws)), twdFrom: main.twdFrom, headingDeg: main.headingDeg, sogKt: rows.reduce((a, r) => a + r.distNm, 0) / (hrs || 1), hsMax: Math.max(...rows.map(r => r.hs)), forceMax: Math.max(...rows.map(r => r.beaufort.force)),
      warning: rows.reduce((w, r) => (['none', 'sca', 'gale', 'storm', 'hurricane'].indexOf(r.warning.key) > ['none', 'sca', 'gale', 'storm', 'hurricane'].indexOf(w.key) ? r.warning : w), WARNINGS.none),
      symbol: main.symbol, night: main.night, weather: main.weather, manoeuvres: manoeuvres.filter(x => x.ms >= rows[0].ms && x.ms <= rows.at(-1).endMs).length }
  })
  const hours = (segs.at(-1).to.t - t0) / 3.6e6, sailedNm = dist / M_PER_NM
  const directNm = marks.slice(1).reduce((a, p, k) => a + distanceNm(marks[k], p), 0)
  const maxTws = Math.max(...track.map(r => r.tws)), maxHs = Math.max(...track.map(r => r.hs))
  const motoringHours = track.filter(r => r.motoring).reduce((a, r) => a + r.hours, 0)
  const over = track.filter(r => r.tws > opts.maxWindKt || r.hs > opts.maxSeasM)
  const checks = [], add = (level, text) => checks.push({ level, text })
  const worst = track.reduce((w, r) => (['none', 'sca', 'gale', 'storm', 'hurricane'].indexOf(r.warning.key) > ['none', 'sca', 'gale', 'storm', 'hurricane'].indexOf(w.key) ? r.warning : w), WARNINGS.none)
  if (['gale', 'storm', 'hurricane'].includes(worst.key)) add('severe', `${worst.label} conditions on the route (up to ${Math.round(maxTws)} kt): do not sail`)
  if (relaxed) add('warning', `The limits set (${opts.maxWindKt} kt, ${opts.maxSeasM} m) cannot be kept on any route: this route goes beyond them`)
  if (over.length) add('warning', `Beyond your limits for ${over.reduce((a, r) => a + r.hours, 0).toFixed(1)} h: wind up to ${Math.round(maxTws)} kt, seas up to ${maxHs.toFixed(1)} m`)
  if (track.some(r => r.thunder)) add('severe', 'Thunderstorms on the route: squalls and sudden wind shifts')
  for (const x of manoeuvres) for (const w of x.warnings) add('caution', `${x.kind === 'tack' ? 'Tack' : 'Gybe'} at ${new Date(x.ms).toISOString().slice(11, 16)}Z: ${w}`)
  if (motoringHours > 0.05) add('info', `Motoring for ${motoringHours.toFixed(1)} h where the boat makes less than ${opts.motorBelowKt} kt under sail`)
  if (track.some(r => r.beyond)) add('info', 'The passage runs past the last frame of the run: the wind there is held at the last frame')
  add('info', 'Simulated weather and a generic polar: not for navigation')
  const tacks = manoeuvres.filter(x => x.kind === 'tack').length, gybes = manoeuvres.length - tacks
  // The isochrones as lat/lon lines (every one, for the 3D view; thinned there).
  const lines = isochrones.map(line => line.map(p => ({ e: p.e, n: p.n })))
  return {
    ok: true, mode: opts.mode, boat, settings: opts, marks, departMs: t0, etaMs: segs.at(-1).to.t, hours, sailedNm, directNm, avgSogKt: sailedNm / (hours || 1), vmcKt: directNm / (hours || 1),
    track, manoeuvres, tacks, gybes, stretches, motoringHours, maxTws, maxHs, worstWarning: worst, checks, isochrones: lines, stepMinutes: stepS ? stepS / 60 : null, relaxed,
    end: { e: segs.at(-1).to.e, n: segs.at(-1).to.n, ...point(segs.at(-1).to.e, segs.at(-1).to.n) },
  }
}

// ---- The boat at a time into the passage (the 3D sail-through) -------------------------------------------------------
// Position (grid metres and lat/lon), headings, speeds, wind, sails and heel; inside a manoeuvre's window the simulated
// turn (heading, boom, headsail, heel, speed) replaces the steady values. hours: time since departure.
export function sailStateAt(route, hours) {
  const t = route.departMs + Math.max(0, Math.min(route.hours, hours)) * 3.6e6, tr = route.track
  let k = 0; while (k < tr.length - 1 && tr[k].endMs < t) k++
  const r = tr[k], f = r.endMs > r.ms ? Math.max(0, Math.min(1, (t - r.ms) / (r.endMs - r.ms))) : 0
  const e = r.e + (r.e1 - r.e) * f, n = r.n + (r.n1 - r.n) * f
  const base = {
    hours: (t - route.departMs) / 3.6e6, ms: t, done: t >= route.etaMs, k, e, n, frameIndex: r.frameIndex, gridHeadingDeg: r.gridHeadingDeg, headingDeg: r.headingDeg, cogDeg: r.cogDeg, stwKt: r.stwKt, sogKt: r.sogKt,
    tws: r.tws, twdFrom: r.twdFrom, twa: r.twa, aws: r.aws, awa: r.awa, pointOfSail: r.pointOfSail, tack: r.tack, motoring: r.motoring, sails: r.sails, sailShort: r.sailShort, hs: r.hs, beaufort: r.beaufort, warning: r.warning, current: r.current,
    boomDeg: r.sails ? r.sails.boomDeg * r.sails.lee : 0, jibDeg: r.sails ? r.sails.jibDeg * r.sails.lee : 0, heelDeg: r.sails ? r.sails.heelDeg * r.sails.lee : 0, luffing: false, manoeuvre: null,
  }
  // A manoeuvre in progress: its simulation is centred on the turn (lead-in before the change of heading).
  for (const mv of route.manoeuvres) {
    const start = mv.ms - (LEAD_IN_S + mv.sim.summary.turnS / 2) * 1000, end = start + mv.sim.summary.durationS * 1000
    if (t >= start && t <= end) {
      const s = manoeuvreAt(mv.sim, (t - start) / 1000), conv = r.headingDeg - r.gridHeadingDeg
      return { ...base, gridHeadingDeg: s.headingDeg, headingDeg: norm360(s.headingDeg + conv), stwKt: s.speedKt, twa: s.twa, awa: s.awa, aws: s.aws, boomDeg: s.boomDeg, jibDeg: s.jibDeg, heelDeg: s.heelDeg, luffing: s.luffing, pointOfSail: s.pointOfSail, manoeuvre: { kind: mv.kind, phase: s.phase, t: (t - start) / 1000, duration: mv.sim.summary.durationS, window: [start, end] } }
    }
  }
  const next = route.manoeuvres.find(x => x.ms > t)
  return { ...base, next: next ? { kind: next.kind, inMinutes: (next.ms - t) / 60000 } : null }
}
// The real-time windows (ms of the passage clock) in which manoeuvres are played: the sail-through slows to real time.
export const manoeuvreWindows = route => route.manoeuvres.map(mv => { const start = mv.ms - (LEAD_IN_S + mv.sim.summary.turnS / 2) * 1000; return [start, start + mv.sim.summary.durationS * 1000] })

// ---- Departure planning ----------------------------------------------------------------------------------------------
// The route for each departure frame: duration, ETA, strongest wind and highest seas, tacks and gybes, and whether it
// stays within the limits. The best: the shortest within the limits (or the shortest of all when none is).
export function departureScan({ frames, indices, points, sea = null, wavesFor = null, currents = null, settings = {}, model = null }) {
  const m = model ?? weatherModel(frames, { sea, wavesFor, currents })
  const rows = indices.map(index => departureRow(sailRoute({ frames, startIndex: index, points, sea, wavesFor, settings, model: m, compare: false }), index, frames))
  return { rows, best: bestDeparture(rows) }
}
// One row of the scan from a route (or its failure).
export function departureRow(r, index, frames) {
  return r.ok ? { index, ok: true, departMs: r.departMs, etaMs: r.etaMs, hours: r.hours, maxTws: r.maxTws, maxHs: r.maxHs, tacks: r.tacks, gybes: r.gybes, motoringHours: r.motoringHours, within: r.maxTws <= r.settings.maxWindKt && r.maxHs <= r.settings.maxSeasM && !r.relaxed, warning: r.worstWarning }
    : { index, ok: false, departMs: validTime(frames[index]), reason: r.reason }
}
// The best departure: the shortest passage within the limits (or the shortest of all when none is within them).
export function bestDeparture(rows) {
  const ok = rows.filter(r => r.ok), pool = ok.filter(r => r.within).length ? ok.filter(r => r.within) : ok
  return pool.reduce((b, r) => (!b || r.hours < b.hours ? r : b), null)?.index ?? null
}

// Where a point of the grid is, for the route editor ("54.52° N 10.44° E").
export const cellPoint = (grid, i) => { const c = cellLocation(grid, i); return { lat: c.lat, lon: c.lon } }
export const compassOf = compass16
