import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence, demo } from './data.js'
import { LAYER_GROUPS, LAYERS, EXAMPLE_GROUPS, groupOfLayer, layerAvailable, layerSubtitle } from './layers.js'

const here = dirname(fileURLToPath(import.meta.url))
const app = readFileSync(join(here, 'App.vue'), 'utf8')
const fixture = name => JSON.parse(readFileSync(join(here, '..', 'public', name, 'engine-timeline.json'), 'utf8'))

test('every layer has a unique key, a group, a subtitle and a tooltip, and the viewer draws it', () => {
  const keys = LAYERS.map(l => l.key)
  assert.equal(new Set(keys).size, keys.length, 'unique keys')
  assert.ok(LAYER_GROUPS.length >= 8 && LAYER_GROUPS.every(g => g.layers.length > 0 && g.tooltip.length > 20))
  for (const l of LAYERS) {
    assert.ok(l.title && l.subtitle && l.tooltip.length >= 30, `${l.key}: title, subtitle and a tooltip that explains it`)
    assert.equal(groupOfLayer(l.key), l.group)
    assert.ok(app.includes(`'${l.key}'`), `${l.key}: the viewer handles the layer`)
  }
  assert.equal(groupOfLayer('nope'), null)
})

test('availability follows the loaded data', () => {
  const empty = {}
  assert.deepEqual(LAYERS.filter(l => layerAvailable(l.key, empty)).map(l => l.key), ['temperature', 'wind'])
  const coast = validateSequence(fixture('coast'))[0]
  const available = LAYERS.filter(l => layerAvailable(l.key, coast)).map(l => l.key)
  assert.ok(LAYER_GROUPS.find(g => g.id === 'coast').layers.every(l => available.includes(l.key)), 'every coast layer with a coast block')
  assert.ok(!available.includes('temperature'), 'the coast timeline\'s placeholder temperature is not offered')
  assert.ok(!available.includes('current') && !available.includes('discharge'))
  const polarOcean = validateSequence(fixture('polar-ocean'))[0]
  assert.ok(['seaice', 'oxygen', 'icethickness', 'current', 'oceansst'].every(k => layerAvailable(k, polarOcean)))
  assert.equal(layerAvailable('unknown', polarOcean), false)
  const carbon = validateSequence(fixture('ocean-carbon'))[0]
  assert.ok(LAYER_GROUPS.find(g => g.id === 'carbon').layers.every(l => layerAvailable(l.key, carbon)), 'every carbon layer with a carbon block')
  assert.ok(['current', 'oceansst', 'mld'].every(k => layerAvailable(k, carbon)), 'the ocean layers of the same run')
  assert.ok(!layerAvailable('co2air', polarOcean) && !layerAvailable('bottomo2', coast), 'carbon layers need the carbon block')
})

test('subtitles describe the source of the loaded data', () => {
  const wind = LAYERS.find(l => l.key === 'wind')
  assert.equal(layerSubtitle(wind, {}), 'Prescribed field · m/s')
  assert.equal(layerSubtitle(wind, { coupled: {} }), 'Pressure · Coriolis · friction · m/s')
  assert.equal(layerSubtitle(LAYERS.find(l => l.key === 'temperature'), { atmosphere: {} }), 'Synthetic regional field · °C')
  assert.ok(validateSequence(demo()).length === 1)
})

test('every example has a tooltip and a loader the viewer knows', () => {
  const actions = EXAMPLE_GROUPS.flatMap(g => g.examples.map(e => e.action))
  assert.equal(new Set(actions).size, actions.length)
  const runner = app.slice(app.indexOf('function runExample('), app.indexOf('\n', app.indexOf('function runExample(')))
  for (const g of EXAMPLE_GROUPS) for (const e of g.examples) {
    assert.ok(e.tooltip.length >= 30 && e.label, `${e.action}: label and tooltip`)
    assert.ok(runner.includes(e.action) && new RegExp(`function ${e.action}\\(`).test(app), `${e.action}: wired to a loader`)
  }
})
