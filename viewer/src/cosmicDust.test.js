import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { summarizeDust, fateShares, sizeRows, profileRows, budgetRows, dustRampColor, ratioLabel, localSolarTime, formatSig, DUST_STATES, SOURCE_SYMBOLS } from './cosmicDust.js'
import { LAYERS } from './layers.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'dust', 'engine-timeline.json'), 'utf8'))

test('cosmic dust fixture validates: one map per hour of the day', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].dust.block
  assert.equal(frames.length, 24)
  assert.equal(frames[0].dust.synthetic, false)
  assert.equal(frames[5].dust.hour, 5)
  assert.equal(frames[0].dust.influx.length, frames[0].forecast.grid.nx * frames[0].forecast.grid.ny)
  assert.ok(b.scenarios.low.influx_t_per_day === 30 && b.scenarios.high.influx_t_per_day === 100)
  // Layers: the dust layers are offered, the placeholder temperature and wind are not.
  const available = LAYERS.filter(l => l.needs(frames[0])).map(l => l.key)
  assert.ok(available.includes('dustinflux') && available.includes('dustdaily') && !available.includes('temperature') && !available.includes('wind'))
})

test('validation rejects broken cosmic dust blocks', () => {
  const bad = (change, pattern) => { const t = fixture(); change(t.cosmic_dust); assert.throws(() => validateSequence(t), pattern) }
  bad(d => { d.schema_version = 'terraflux.cosmic.dust/v0' }, /Unsupported cosmic dust contract/)
  bad(d => { d.fate.ablated_t_per_day += 10 }, /add up to the influx/)
  bad(d => { d.map.frames.pop() }, /match the timeline/)
  bad(d => { d.map.frames[3].influx_ug_m2_day[7] = -1 }, /Invalid dust map frame/)
  bad(d => { d.scenarios.low.influx_t_per_day = 500 }, /ordered/)
})

test('metrics, shares and scenarios', () => {
  const frames = validateSequence(fixture())
  const m = summarizeDust(frames[6]), high = summarizeDust(frames[6], 'high')
  assert.ok(m.contrast > 2 && m.max_ug_m2_day > m.mean_ug_m2_day && m.min_ug_m2_day < m.mean_ug_m2_day)
  assert.ok(Math.abs(high.mean_ug_m2_day / m.mean_ug_m2_day - 100 / 60) < 1e-9, 'the maps scale with the influx')
  const shares = fateShares(frames[0].dust.block)
  assert.deepEqual(shares.map(s => s.key), DUST_STATES.map(s => s.key))
  assert.ok(Math.abs(shares.reduce((a, s) => a + s.share, 0) - 1) < 0.01)
  assert.ok(shares[0].share > 0.5, 'most of the mass evaporates')
  const sizes = sizeRows(frames[0].dust.block)
  assert.ok(sizes[0].surviving > sizes.at(-1).surviving && sizes.every((r, k) => k === 0 || r.diameter_um > sizes[k - 1].diameter_um))
  const profile = profileRows(frames[0].dust.block)
  const peak = key => profile.reduce((best, r) => (r[key] > best[key] ? r : best))
  assert.ok(peak('ablation').altitude_km > 80 && peak('ablation').altitude_km < 100, 'ablation peaks where the metal layers are')
  assert.ok(peak('water').altitude_km > peak('ablation').altitude_km, 'the water leaves higher up')
})

test('budgets: negligible for water and climate, a net loss of mass', () => {
  const frames = validateSequence(fixture())
  const rows = budgetRows(frames[0].dust.block)
  const row = label => rows.find(r => r.label.startsWith(label))
  assert.ok(row('Arriving with the dust').ratio < 0.01, 'far less water than the escaping hydrogen carries away')
  assert.ok(row('Gain from dust').ratio < 1, 'the Earth loses more mass than it gains')
  assert.ok(row('Radiative forcing').ratio < 0.1 && row('Global temperature').ratio < 0.01)
  assert.ok(row('Into the oceans').ratio < 1e-12)
  assert.equal(ratioLabel(0.25), '25.0 %')
  assert.equal(ratioLabel(3e-6), '3.0e-6')
  assert.equal(localSolarTime(0, 90), 6, 'dawn 90 degrees west of the subsolar point')
  assert.notEqual(dustRampColor(0.05, 0, 0.3), dustRampColor(0.25, 0, 0.3))
  assert.equal(SOURCE_SYMBOLS.apex, '☄')
})

test('numbers keep two significant figures, with exponents for the very small and very large', () => {
  assert.equal(formatSig(6.196), '6.2')
  assert.equal(formatSig(0.0062), '6.2e-3')
  assert.equal(formatSig(10960), '11,000')
  assert.equal(formatSig(600000, 3), '600,000')
  assert.equal(formatSig(1.096e7, 3), '1.10e+7')
  assert.equal(formatSig(0), '0')
  assert.equal(formatSig(null), '—')
})
