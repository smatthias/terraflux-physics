import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { cloudFormationDemo, summarizeClouds, validateCloudFormation, CLOUD_TYPES, cloudTypeIndex, cloudTypeInfo } from './cloudFormation.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'clouds', 'engine-timeline.json'), 'utf8'))

test('engine cloud fixture validates and exposes fronts and clouds per frame', () => {
  const frames = validateSequence(fixture())
  assert.equal(frames.length, 7)
  const count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  for (const frame of frames) {
    assert.equal(frame.clouds.type.length, count)
    assert.equal(frame.clouds.synthetic, false)
    assert.ok(frame.clouds.fronts >= 0 && frame.clouds.cloudy >= 0)
  }
  const day = summarizeClouds(frames[1]), night = summarizeClouds(frames[3])
  assert.ok(day.thunderstorm_cells > 0 && day.surface_heating_k > 5, 'afternoon thunderstorms')
  assert.equal(night.thunderstorm_cells, 0)
  assert.ok(night.frontal_cells > 0 && night.convective_cells === 0, 'nocturnal cloud is frontal only')
  assert.ok(day.cold_front_cells > 0 && day.warm_front_cells > 0)
  assert.ok(day.cloud_cover_pct > night.cloud_cover_pct)
  assert.ok(night.mean_base_m > 0 && night.mean_top_m > night.mean_base_m)
})

test('synthetic mock follows the documented format and is labelled synthetic', () => {
  const demo = cloudFormationDemo()
  const frames = validateSequence(demo)
  assert.equal(frames.length, 6)
  assert.equal(frames[0].clouds.synthetic, true)
  assert.equal(frames[0].coupled.synthetic, true)
  const metrics = frames.map(summarizeClouds)
  assert.ok(metrics.every(m => m.warm_front_cells > 0 && m.cold_front_cells > 0), 'every mock frame carries both fronts')
  assert.ok(metrics.some(m => m.thunderstorm_cells > 0) && metrics.some(m => m.thunderstorm_cells === 0), 'storms come and go with heating')
  assert.ok(frames[0].clouds.type.includes('nimbostratus') && frames[0].clouds.type.includes('altostratus'), 'warm-front layer sequence')
  assert.equal(frames[0].clouds.config.warm_front_slope, 1 / 200)
})

test('categorical palette covers every type', () => {
  assert.equal(CLOUD_TYPES.length, 11)
  assert.equal(cloudTypeIndex('none'), 0)
  assert.equal(cloudTypeInfo('cumulonimbus').abbreviation, 'Cb')
  assert.equal(cloudTypeInfo('unknown').key, 'none')
  assert.ok(new Set(CLOUD_TYPES.map(t => t.color)).size === CLOUD_TYPES.length, 'distinct colours')
})

test('invalid cloud blocks are rejected', () => {
  const wrongTotals = fixture(); wrongTotals.coupled_atmosphere.clouds.frames[1].cloudy_cells += 1
  assert.throws(() => validateSequence(wrongTotals), /totals/)
  const badType = fixture(); badType.coupled_atmosphere.clouds.frames[0].cloud_type[0] = 'fog'
  assert.throws(() => validateSequence(badType), /Unknown cloud type/)
  const badStorm = fixture(); const f = badStorm.coupled_atmosphere.clouds.frames[1]; const k = f.cloud_type.indexOf('cumulonimbus'); f.thunderstorm[k] = false
  assert.throws(() => validateSequence(badStorm), /Thunderstorm flag/)
  const badBase = fixture(); const g = badBase.coupled_atmosphere.clouds.frames[1]; const m = g.cloud_type.findIndex(t => t !== 'none'); g.base_m[m] = g.top_m[m] + 1
  assert.throws(() => validateSequence(badBase), /base < top/)
  const badSource = fixture(); badSource.coupled_atmosphere.clouds.source_kind = 'observed'
  assert.throws(() => validateSequence(badSource), /engine_output or synthetic/)
  const shortFrames = fixture(); shortFrames.coupled_atmosphere.clouds.frames.pop()
  assert.throws(() => validateSequence(shortFrames), /frame count/)
  const frontSide = fixture(); const h = frontSide.coupled_atmosphere.clouds.frames[1]; const fi = h.front_type.findIndex(t => t !== 'none'); h.side[fi] = 1
  assert.throws(() => validateCloudFormation(frontSide.coupled_atmosphere.clouds, validateSequence({ schema_version: 'terraflux.weather.timeline/v1', frames: frontSide.frames })), /no side/)
})
