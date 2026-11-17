import test from 'node:test'
import assert from 'node:assert/strict'
import { polarSpeed, optimalVmg, optimalVmgCached, waveFactor, hullSpeedKt, speedScale, polarCurves, boatOf, BOATS, REFERENCE_POLAR, POLAR_TWA, POLAR_TWS } from './polars.js'
import { sailPlan, sailPlanFor, effectiveWind, sailShort, REEF_AREA, GALE_KT, STORM_KT } from './sailTrim.js'
import { simulateManoeuvre, mirroredHeading, manoeuvreKind, manoeuvrePenalty, manoeuvreAt, STRONG_GYBE_KT } from './manoeuvre.js'
import { apparentWind } from './sailing.js'

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
const B40 = BOATS.cruiser40

// ---- The polar ----------------------------------------------------------------------------------------------------------
test('the polar: nothing head to wind, the table at its points, bilinear between them, the same on both tacks', () => {
  assert.equal(polarSpeed(B40, 12, 0), 0); assert.equal(polarSpeed(B40, 12, 25), 0, 'in irons'); assert.equal(polarSpeed(B40, 0, 90), 0, 'no wind')
  POLAR_TWA.forEach((a, i) => POLAR_TWS.forEach((t, j) => near(polarSpeed(B40, t, a), REFERENCE_POLAR[i][j], 1e-12, `${t} kt at ${a}°`)))
  near(polarSpeed(B40, 11, 90), (6.7 + 7.1) / 2, 1e-12, 'between 10 and 12 kt'); near(polarSpeed(B40, 12, 100), (7.1 + 7.3) / 2, 1e-12, 'between 90 and 110 degrees')
  near(polarSpeed(B40, 14, -60), polarSpeed(B40, 14, 60), 1e-12, 'port and starboard alike'); near(polarSpeed(B40, 14, 300), polarSpeed(B40, 14, 60), 1e-12)
  near(polarSpeed(B40, 40, 90), polarSpeed(B40, 35, 90), 1e-12, 'held above 35 kt')
  // Froude scaling and the hull speed.
  near(polarSpeed(BOATS.cruiser30, 12, 90) / polarSpeed(B40, 12, 90), Math.sqrt(8.2 / 11), 1e-12)
  near(speedScale(BOATS.performance45), Math.sqrt(12.6 / 11) * 1.06, 1e-12); near(hullSpeedKt(B40), 1.34 * Math.sqrt(11 * 3.2808399), 1e-12)
  for (const b of Object.values(BOATS)) assert.ok(Math.max(...POLAR_TWA.map(a => polarSpeed(b, 20, a))) < hullSpeedKt(b) * 1.15, `${b.key}: about the hull speed at most`)
  assert.equal(boatOf('nope').key, 'cruiser40')
})

test('best VMG: upwind at 40-50 degrees, downwind deeper as the wind rises', () => {
  let prevDown = 0
  for (const tws of [6, 10, 14, 20, 25]) {
    const v = optimalVmg(B40, tws)
    assert.ok(v.upTwa >= 40 && v.upTwa <= 50, `${tws} kt: upwind at ${v.upTwa}`); assert.ok(v.downTwa >= 140 && v.downTwa <= 180, `${tws} kt: downwind at ${v.downTwa}`)
    near(v.upVmg, v.upSpeed * Math.cos(v.upTwa * Math.PI / 180), 1e-9); near(v.downVmg, -v.downSpeed * Math.cos(v.downTwa * Math.PI / 180), 1e-9)
    for (let a = 25; a <= 90; a += 1) assert.ok(polarSpeed(B40, tws, a) * Math.cos(a * Math.PI / 180) <= v.upVmg + 1e-9, 'no better angle upwind')
    assert.ok(v.downTwa >= prevDown - 1e-9); prevDown = v.downTwa
  }
  assert.deepEqual(optimalVmgCached(B40, 12.2), optimalVmg(B40, 12))
})

test('waves from ahead slow the boat, following seas do not; small boats suffer more', () => {
  assert.equal(waveFactor(B40, 0, 0), 1); near(waveFactor(B40, 2, 180), 1, 1e-12, 'following seas')
  near(waveFactor(B40, 2, 0), 1 - 0.06 * 2, 1e-12, 'head seas: 6 % per metre'); near(waveFactor(B40, 2, 90), 1 - 0.06, 1e-12)
  assert.ok(waveFactor(BOATS.cruiser30, 2, 40) < waveFactor(B40, 2, 40)); assert.equal(waveFactor(B40, 30, 0), 0.5, 'at least half')
  const c = polarCurves(B40, [10]); assert.equal(c[0].points.length, 31); assert.equal(c[0].points[0].twa, 30); assert.equal(c[0].points.at(-1).twa, 180)
})

// ---- The sail plan and trim ------------------------------------------------------------------------------------------
const plan = (aws, awa, tws = aws, twa = awa, settings = {}) => sailPlan({ tws, twa, aws, awa }, B40, settings)

test('reefs by the apparent wind upwind and the true wind downwind, relative to the user\'s "reef at"', () => {
  assert.equal(effectiveWind({ tws: 15, aws: 20, awa: 30 }), 20); assert.equal(effectiveWind({ tws: 20, aws: 14, awa: 150 }), 20)
  const reefs = [17, 18, 24.9, 25, 31.9, 32, 33.9].map(k => plan(k, 30, k - 4, 42).reef)
  assert.deepEqual(reefs, [0, 1, 1, 2, 2, 3, 3], 'at 18, 25 and 32 kt with the default reef at 18 kt')
  assert.deepEqual([15, 16].map(k => plan(k, 30, k - 4, 42, { reefKt: 16 }).reef), [0, 1], 'the user\'s own reef point')
  const gale = plan(GALE_KT, 30, 30, 42); assert.equal(gale.main.kind, 'trysail'); assert.equal(gale.headsail.kind, 'storm'); assert.equal(gale.level, 'severe')
  const storm = plan(40, 30, STORM_KT, 42); assert.equal(storm.sailing, false); assert.equal(storm.main.areaM2, 0); assert.match(storm.text, /bare poles/)
  near(plan(20, 30).main.areaM2, B40.mainM2 * REEF_AREA[1], 1e-9, 'the 1st reef area')
  // Running in 20 kt true: the apparent wind is only ~13 kt, but the reef follows the true wind.
  assert.equal(plan(13, 165, 20, 170).reef, 1)
})

test('the headsail: genoa, furled, jib, gennaker off the wind in moderate winds, goose-winged on a dead run', () => {
  assert.equal(plan(10, 30).headsail.kind, 'genoa'); assert.equal(plan(10, 30).headsail.furl, 0)
  assert.equal(plan(16, 30).headsail.label, 'Genoa furled to 75 %'); assert.equal(plan(22, 30).headsail.kind, 'jib'); assert.equal(plan(30, 30).headsail.label, 'Small jib (genoa furled to 40 %)')
  assert.equal(plan(8, 120, 12, 140).headsail.kind, 'gennaker'); assert.equal(plan(8, 120, 12, 140, { gennaker: false }).headsail.kind, 'genoa')
  assert.notEqual(plan(12, 150, 18, 160).headsail.kind, 'gennaker', 'not above 16 kt'); assert.notEqual(plan(12, 60, 12, 80).headsail.kind, 'gennaker', 'not on a reach')
  const run = plan(9, 172, 14, 175, { gennaker: false }); assert.equal(run.gooseWing, true); assert.match(run.text, /goose-winged/); assert.equal(run.jibDeg, 80)
})

test('sheets to leeward, eased as the wind goes aft; heel from the side force; luffing head to wind', () => {
  const s = plan(12, 40), p = plan(12, -40)
  assert.equal(s.leeward, 'port'); assert.equal(s.lee, -1); assert.equal(p.leeward, 'starboard'); assert.equal(p.lee, 1)
  let prev = 0
  for (const awa of [20, 30, 45, 60, 90, 120, 150, 180]) { const b = plan(12, awa).boomDeg; assert.ok(b >= prev && b >= 3 && b <= 85, `boom at ${awa}: ${b}`); prev = b }
  near(plan(12, 60).boomDeg, 0.75 * (60 - 18), 1e-12); assert.equal(plan(12, 170).boomDeg, 85, 'against the shrouds'); assert.ok(plan(12, 60).jibDeg <= 40)
  assert.ok(plan(16, 30).heelDeg > plan(16, 90).heelDeg && plan(16, 90).heelDeg > plan(16, 160).heelDeg, 'most heel close-hauled')
  assert.ok(plan(20, 30, 16, 45, { reefKt: 30 }).heelDeg > plan(20, 30).heelDeg, 'a reef reduces heel')
  assert.ok(plan(40, 30, 33, 40).heelDeg <= 30)
  assert.equal(plan(12, 15).luffing, true); assert.match(plan(12, 15).trim[0], /luffing/)
  assert.ok(plan(25, 30).trim.some(t => /Traveller down/.test(t))); assert.ok(plan(6, 30).trim.some(t => /light air/.test(t)))
  const f = sailPlanFor({ twdFrom: 0, tws: 14, headingDeg: 45, speedKt: 6 }, B40), aw = apparentWind(0, 14, 45, 6)
  near(f.awa, aw.awa, 1e-12); assert.equal(f.leeward, 'starboard', 'wind over port: sails to starboard')
  assert.equal(sailShort(plan(20, 30)), '1st reef · genoa 75 %'); assert.equal(sailShort(plan(8, 120, 12, 140)), 'full main · gennaker'); assert.equal(sailShort(plan(40, 30, 50, 40)), 'bare poles')
})

// ---- Tacks and gybes ---------------------------------------------------------------------------------------------------
test('a tack: the bow through the wind, speed lost and regained, the sails cross, the heel changes sides', () => {
  assert.equal(mirroredHeading(0, 315), 45); assert.equal(mirroredHeading(0, 210), 150); assert.equal(mirroredHeading(90, 45, 40), 130)
  assert.equal(manoeuvreKind(40), 'tack'); assert.equal(manoeuvreKind(-150), 'gybe')
  const sim = simulateManoeuvre({ conditions: { twdFrom: 0, tws: 12 }, start: { headingDeg: 315 }, kind: 'tack', boat: B40 })
  assert.equal(sim.kind, 'tack'); near(sim.toHeadingDeg, 45, 1e-9); near(sim.deltaDeg, 90, 1e-9, 'clockwise through north')
  const s = sim.samples, first = s[0], last = s.at(-1)
  assert.ok(s.some(x => Math.abs(x.twa) < 5), 'head to wind on the way'); assert.ok(s.some(x => x.luffing), 'the sails luff')
  assert.ok(first.boomDeg < 0 && last.boomDeg > 0, 'wind over starboard, boom to port; after the tack to starboard'); assert.ok(first.heelDeg < 0 && last.heelDeg > 0, 'the heel changes sides')
  const S = sim.summary
  assert.ok(S.minSpeedKt < 0.6 * S.startSpeedKt, 'the boat slows in the turn'); assert.ok(last.speedKt >= 0.95 * S.startSpeedKt - 0.1, 'and is back to speed')
  assert.ok(S.boatLengthsLost > 0.5 && S.boatLengthsLost < 4, `${S.boatLengthsLost} boat lengths lost`); near(S.timeLostS, S.distanceLostM / (S.refVmgKt / 1.9438445), 1e-9)
  assert.ok(S.turnS >= 90 / B40.turnRateDegS - 1e-9); assert.deepEqual(sim.warnings, [])
  // Every sample a quarter of a second apart; the state between samples interpolates.
  near(s[1].t - s[0].t, 0.25, 1e-12); const mid = manoeuvreAt(sim, 0.125); near(mid.speedKt, (s[0].speedKt + s[1].speedKt) / 2, 1e-9); assert.equal(manoeuvreAt(sim, 1e6).done, true)
})

test('a gybe: the stern through the wind, the boom across quickly, little speed lost; warnings in strong wind and big seas', () => {
  const sim = simulateManoeuvre({ conditions: { twdFrom: 0, tws: 12 }, start: { headingDeg: 210 }, kind: 'gybe', boat: B40 })
  near(sim.toHeadingDeg, 150, 1e-9); near(sim.deltaDeg, -60, 1e-9, 'anticlockwise through south')
  assert.ok(sim.samples.some(x => Math.abs(x.twa) > 175), 'stern to wind on the way')
  const k = sim.samples.findIndex(x => x.boomDeg > 0), j = sim.samples.findIndex(x => x.boomDeg > 60)
  assert.ok(k > 0 && j > k && sim.samples[j].t - sim.samples[k - 1].t < 3, 'the boom crosses in a couple of seconds')
  const tack = simulateManoeuvre({ conditions: { twdFrom: 0, tws: 12 }, start: { headingDeg: 315 }, kind: 'tack', boat: B40 })
  assert.ok(sim.summary.speedLossPct < tack.summary.speedLossPct && sim.summary.timeLostS < tack.summary.timeLostS)
  assert.match(simulateManoeuvre({ conditions: { twdFrom: 0, tws: STRONG_GYBE_KT + 3 }, start: { headingDeg: 200 }, kind: 'gybe', boat: B40 }).warnings[0], /chicken gybe/)
  assert.match(simulateManoeuvre({ conditions: { twdFrom: 0, tws: 14, hs: 3 }, start: { headingDeg: 315 }, kind: 'tack', boat: B40 }).warnings[0], /flat spot/)
  const pen = manoeuvrePenalty(B40, 12); assert.ok(pen.tackS > pen.gybeS && pen.gybeS >= 0); assert.equal(manoeuvrePenalty(B40, 12.3), pen, 'memoised by the knot')
})
