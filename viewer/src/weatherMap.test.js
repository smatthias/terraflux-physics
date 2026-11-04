import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { cloudFormationDemo } from './cloudFormation.js'
import { isobars, rateColor, intensityClass, summarizePrecipitation, validatePrecipitation, PRECIPITATION_TYPES, precipitationTypeInfo } from './weatherMap.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'clouds', 'engine-timeline.json'), 'utf8'))

test('engine precipitation block validates and summarises day/night outcomes', () => {
  const frames = validateSequence(fixture())
  const day = summarizePrecipitation(frames[1]), night = summarizePrecipitation(frames[3]), clear = summarizePrecipitation(frames[0])
  assert.equal(clear.precipitating_pct, 0)
  assert.ok(day.lightning_cells > 0 && day.hail_cells > 0 && day.max_hail_cm > 0.5 && day.max_rate_mm_h > 10, 'afternoon storms with hail')
  assert.equal(night.lightning_cells, 0)
  assert.ok(night.type_counts.rain > 0 && night.hazard_counts.prolonged_rain > 0, 'nocturnal frontal rain is prolonged')
  assert.ok(night.type_counts.drizzle > 0)
  assert.equal(frames[1].precipitation.synthetic, false)
  assert.ok(frames[1].precipitation.config.interval_seconds === 28800)
})

test('synthetic mock carries a precipitation block in the same format', () => {
  const frames = validateSequence(cloudFormationDemo())
  assert.equal(frames[0].precipitation.synthetic, true)
  const metrics = frames.map(summarizePrecipitation)
  assert.ok(metrics.some(m => m.lightning_cells > 0) && metrics.some(m => m.snow_cells > 0))
  assert.ok(metrics.every(m => m.precipitating_pct > 0))
})

test('precipitation styling helpers', () => {
  assert.equal(PRECIPITATION_TYPES.length, 8)
  assert.equal(precipitationTypeInfo('snow').symbol, '✱')
  assert.equal(precipitationTypeInfo('unknown').key, 'none')
  assert.equal(rateColor(0), null)
  assert.match(rateColor(1), /^rgb\(/)
  assert.notEqual(rateColor(1), rateColor(30))
  assert.deepEqual([0, 1, 5, 20, 70].map(intensityClass), ['none', 'light', 'moderate', 'heavy', 'violent'])
})

test('isobars contour a linear field into straight lines and a low into closed rings', () => {
  const grid = { nx: 10, ny: 6 }
  const linear = []
  for (let y = 0; y < 6; y++) for (let x = 0; x < 10; x++) linear.push(100000 + 50 * x)
  const lines = isobars(linear, grid, 100)
  assert.ok(lines.length >= 4)
  for (const level of lines) {
    assert.equal(level.lines.length, 1, 'one polyline per level')
    const us = level.lines[0].map(p => p[0])
    assert.ok(Math.max(...us) - Math.min(...us) < 1e-9, 'vertical isobar for an east-west gradient')
    assert.equal(level.lines[0].length, 6, 'spans every row')
    const expectedU = ((level.level_pa - 100000) / 50 + 0.5) / 10
    assert.ok(Math.abs(us[0] - expectedU) < 1e-9)
  }
  const low = []
  for (let y = 0; y < 20; y++) for (let x = 0; x < 20; x++) low.push(101500 - 1000 * Math.exp(-((x - 9.5) ** 2 + (y - 9.5) ** 2) / 20))
  const rings = isobars(low, { nx: 20, ny: 20 }, 200)
  assert.ok(rings.length >= 3)
  const inner = rings[0].lines[0]
  assert.ok(Math.hypot(inner[0][0] - inner[inner.length - 1][0], inner[0][1] - inner[inner.length - 1][1]) < 1e-9, 'innermost isobar closes on itself')
  assert.throws(() => isobars([1, 2], grid, 100), /finite pressure field/)
})

test('invalid precipitation blocks are rejected', () => {
  const badIntensity = fixture(); const f = badIntensity.coupled_atmosphere.precipitation.frames[1]; const k = f.rate_mm_h.findIndex(r => r > 10); f.intensity[k] = 'light'
  assert.throws(() => validateSequence(badIntensity), /Intensity class/)
  const hailNoBolt = fixture(); const g = hailNoBolt.coupled_atmosphere.precipitation.frames[1]; const m = g.hail_max_diameter_cm.findIndex(h => h > 0); g.lightning[m] = false
  assert.throws(() => validateSequence(hailNoBolt), /Hail requires lightning|Hazards must match/)
  const totals = fixture(); totals.coupled_atmosphere.precipitation.frames[1].precipitating_cells += 1
  assert.throws(() => validateSequence(totals), /totals/)
  const orphan = fixture(); delete orphan.coupled_atmosphere.clouds
  assert.throws(() => validateSequence(orphan), /require the clouds block/)
  const timeline = validateSequence({ schema_version: 'terraflux.weather.timeline/v1', frames: fixture().frames })
  const shifted = fixture().coupled_atmosphere.precipitation; shifted.frames[2].elapsed_seconds += 1
  assert.throws(() => validatePrecipitation(shifted, timeline), /times must match/)
})
