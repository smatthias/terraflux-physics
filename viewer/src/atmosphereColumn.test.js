import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { validateSequence } from './data.js'
import { atmosphereColumn, environmentTemperature, columnLayers, freezingLevel, relativeHumidity, metres, temperatureColour, CEILING_COVER, DEFAULT_COLUMN } from './atmosphereColumn.js'

const frames = validateSequence(JSON.parse(readFileSync(new URL('../public/clouds/engine-timeline.json', import.meta.url), 'utf8')))

test('the environment follows the column model: lapse rate, tropopause and the capping inversion', () => {
  const c = { ...DEFAULT_COLUMN }
  assert.equal(environmentTemperature(c, 290, 0), 290)
  assert.ok(Math.abs(environmentTemperature(c, 290, 1000) - 283.5) < 1e-9)
  assert.equal(environmentTemperature(c, 290, 15000), environmentTemperature(c, 290, 11000), 'isothermal above the tropopause')
  const cap = { ...c, inversion_base_m: 1500, inversion_depth_m: 500, inversion_strength_k: 1, lapse_rate_above_inversion_k_m: 0.0085 }
  assert.ok(environmentTemperature(cap, 300, 2000) > environmentTemperature(cap, 300, 1500), 'warmer through the inversion')
  assert.ok(Math.abs(environmentTemperature(cap, 300, 3000) - (300 - 0.0065 * 1500 + 1 - 0.0085 * 1000)) < 1e-9, 'steeper lapse above the cap')
  const layers = columnLayers(c, { surfaceK: 290, surfaceQ: 0.009, surfacePa: 101000 })
  assert.equal(layers.length, 24); assert.equal(layers.at(-1).top, 12000)
  assert.ok(layers.every((l, k) => k === 0 || l.pressureHpa < layers[k - 1].pressureHpa), 'pressure falls with height')
  assert.ok(Math.abs(layers[0].pressureHpa - 101000 / 100 * Math.exp(-9.80665 * 250 / (287.04 * 289.2))) < 1.5, 'hydrostatic')
  assert.ok(Math.abs(freezingLevel(c, 283.15) - 10 / 0.0065) < 10)
  assert.equal(freezingLevel(c, 270), null, 'already freezing at the ground')
  assert.ok(Math.abs(relativeHumidity(0.0076, 293.15, 101325) - 52) < 3, 'about half saturated at 20 °C and 7.6 g/kg')
  assert.equal(metres(11000), '11\u2009000 m'); assert.equal(metres(862.4), '860 m')
  assert.match(temperatureColour(-60), /^rgb\(40,70,160\)$/); assert.match(temperatureColour(0), /^rgb\(215,235,245\)$/)
})

test('a thunderstorm column: genus, base, top, thickness, ceiling, levels and the build-up from the ground up', () => {
  const f = frames[1], i = f.clouds.type.indexOf('cumulonimbus'), c = atmosphereColumn(f, i)
  assert.equal(c.cloud.key, 'cumulonimbus'); assert.equal(c.title, 'Cumulonimbus (thunderstorm)')
  assert.equal(c.cloud.base, f.clouds.base[i]); assert.equal(c.cloud.top, f.clouds.top[i])
  assert.equal(c.cloud.thickness, f.clouds.top[i] - f.clouds.base[i])
  assert.ok(f.clouds.fraction[i] >= CEILING_COVER && c.cloud.ceiling === c.cloud.base, 'a 90 % cover makes a ceiling at the base')
  assert.equal(c.freezing.derived, false, 'the engine freezing level is used')
  assert.equal(c.freezing.height, f.precipitation.freezingLevel[i])
  // Levels in height order, the column diagnostics labelled as those of the unheated air (the cloud grew from heated air).
  assert.deepEqual(c.levels.map(l => l.height), [...c.levels.map(l => l.height)].sort((a, b) => a - b))
  assert.ok(c.levels.some(l => l.key === 'lcl' && /unheated/.test(l.label)))
  // Steps: from the ground to the top, then what falls out.
  const keys = c.steps.map(s => s.key)
  assert.equal(keys[0], 'ground'); assert.equal(keys.at(-1), 'precipitation')
  for (const k of ['rise', 'base', 'buoyant', 'freezing', 'top', 'tropopause']) assert.ok(keys.includes(k), k)
  const heights = c.steps.filter(s => s.key !== 'precipitation').map(s => s.height)
  assert.deepEqual(heights, [...heights].sort((a, b) => a - b))
  assert.match(c.steps.find(s => s.key === 'top').text, /anvil/)
  assert.match(c.steps.find(s => s.key === 'ground').text, /6\.0 K/)
  assert.ok(c.top >= c.cloud.top + 1000)
})

test('layered and clear columns, and data without vertical information', () => {
  const f = frames[3], ns = f.clouds.type.indexOf('nimbostratus'), c = atmosphereColumn(f, ns)
  assert.equal(c.cloud.mechanism, 'frontal')
  assert.ok(!c.steps.some(s => s.key === 'buoyant' || s.key === 'lfc'), 'no free convection in a frontal layer')
  assert.match(c.steps.find(s => s.key === 'top').text, /lifted layer/)
  const clear = atmosphereColumn(frames[0], 5)
  assert.equal(clear.cloud, null); assert.equal(clear.title, 'Clear sky')
  assert.ok(clear.steps.some(s => s.key === 'lcl'))
  const sc = f.clouds.type.indexOf('stratocumulus'), stc = atmosphereColumn(f, sc)
  assert.equal(stc.cloud.ceiling, null, 'a 60 % cover is no ceiling')
  assert.equal(atmosphereColumn({ forecast: { fields: { air_temperature: { values: [290] } } } }, 0), null)
})

test('the inspector opens on the Atmosphere tab, and clicking a cloud picks its cell', () => {
  const overlay = readFileSync(new URL('./InspectOverlay.vue', import.meta.url), 'utf8')
  const app = readFileSync(new URL('./App.vue', import.meta.url), 'utf8')
  const column = readFileSync(new URL('./AtmosphereColumn.vue', import.meta.url), 'utf8')
  assert.match(overlay, /const tab = ref\(props\.column \? 'atmosphere' : 'explain'\)/)
  assert.match(overlay, /<AtmosphereColumn :column="column"/)
  assert.match(app, /:column="v\.view\.column"/)
  assert.match(app, /hit\?\.object\.userData\.cells&&hit\.instanceId!==undefined/)
  assert.match(column, /OrbitControls/)
  assert.match(column, /addLabel\(metres\(km \* 1000\), 'ruler'/)
})
