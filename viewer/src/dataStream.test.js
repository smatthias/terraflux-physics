import test from 'node:test'
import assert from 'node:assert/strict'
import { validateSequence, demo } from './data.js'
import { LAYERS } from './layers.js'
import { VIEW_LABELS, channelLabel, frameChannels, fieldStats, histogram, rowSignal, signalPaths, rampRgb, overlayChips, formatSignal, clockLabel, describeChanges, appendEntries } from './dataStream.js'

const layer = key => LAYERS.find(l => l.key === key)
const snap = (over = {}) => ({ source: 'Synthetic demonstration', frames: 1, nx: 32, ny: 24, variant: 'baseline', view: 'field', viewLabel: 'Field explorer', layer: 'temperature', layerTitle: 'Air temperature', unit: '°C', frame: 0, frameLabel: '+60 min', overlays: ['wind vectors'], selected: null, selectedLabel: '', selectedValue: null, ...over })

test('every viewer view has a stream label', () => {
  for (const v of ['field', 'map', 'severe', 'water', 'hazards', 'ocean', 'polar', 'coast', 'carbon', 'solar', 'dust', 'solarwind']) assert.ok(VIEW_LABELS[v], v)
})

test('channels list the frame blocks and mark the ones the layer cannot do without', () => {
  const frame = validateSequence(demo())[0]
  const channels = frameChannels(frame, layer('rainfall'))
  const rain = channels.find(c => c.key === 'rain')
  assert.ok(rain, 'the demo frame carries rainfall')
  assert.equal(rain.active, true, 'rainfall accumulation needs the rain block')
  assert.equal(rain.label, 'Radar rainfall')
  assert.equal(channels.find(c => c.key === 'forecast').base, true)
  assert.equal(channels.find(c => c.key === 'forecast').active, false)
  // Temperature needs no particular block, so nothing is active.
  assert.ok(frameChannels(frame, layer('temperature')).every(c => !c.active))
  // A layer the frame cannot show marks nothing active rather than everything.
  assert.ok(frameChannels(frame, layer('discharge')).every(c => !c.active))
  assert.deepEqual(frameChannels(null, layer('temperature')), [])
  assert.ok(frameChannels({ forecast: {}, values: null, count: 3 }, null).every(c => c.key === 'forecast'), 'only object blocks are channels')
  assert.equal(frameChannels({ forecast: {} }, { needs: () => { throw new Error('x') } })[0].active, false, 'a throwing test is treated as unusable')
})

test('channel labels fall back to readable words for unknown blocks', () => {
  assert.equal(channelLabel('solarWind'), 'Solar wind')
  assert.equal(channelLabel('someNewBlock'), 'Some new block')
  assert.equal(channelLabel('snake_case_key'), 'Snake case key')
})

test('field statistics skip missing cells', () => {
  assert.deepEqual(fieldStats([1, null, 3, NaN, 5]), { cells: 5, valid: 3, min: 1, max: 5, mean: 3 })
  assert.deepEqual(fieldStats([null, null]), { cells: 2, valid: 0, min: null, max: null, mean: null })
  assert.deepEqual(fieldStats(undefined), { cells: 0, valid: 0, min: null, max: null, mean: null })
})

test('the histogram is normalised to its fullest bin and clamps outliers into the end bins', () => {
  const h = histogram([0, 0, 0, 5, 10, null, -3, 42], 0, 10, 4)
  assert.deepEqual(h, [1, 0, 0.25, 0.5], 'four cells at or below 0 (one clamped), one at 5, two at or above 10 (one clamped)')
  assert.deepEqual(histogram([], 0, 1, 3), [0, 0, 0])
  assert.deepEqual(histogram([2, 2], 2, 2, 3), [0, 1, 0], 'a flat field lands in the middle')
})

test('row signals read one grid row and break the scope trace at missing cells', () => {
  const values = [0, 1, 2, 3, null, 5]
  assert.deepEqual(rowSignal(values, 3, 1), [3, null, 5])
  assert.deepEqual(rowSignal(values, 0, 0), [])
  const paths = signalPaths([0, 10, null, 10], 0, 10, 30, 10, 0)
  assert.equal(paths.length, 2)
  assert.equal(paths[0], '0.00,10.00 10.00,0.00')
  assert.equal(paths[1], '30.00,0.00 30.00,0.00', 'a lone point becomes a visible dot')
  assert.deepEqual(signalPaths([null, null], 0, 1), [])
})

test('the ramp matches the legend ends and clamps', () => {
  assert.deepEqual(rampRgb(0), [0x28, 0x68, 0xcc])
  assert.deepEqual(rampRgb(1), [0xd0, 0x5b, 0x26])
  assert.deepEqual(rampRgb(-2), rampRgb(0))
  assert.deepEqual(rampRgb(7), rampRgb(1))
  assert.deepEqual(rampRgb(NaN), rampRgb(0))
})

test('overlay chips name the flags that are on and the chosen options', () => {
  assert.deepEqual(overlayChips({ isobars: true, fronts: false, base: 'temperature', isobarIntervalHpa: 2, scenario: '' }), ['isobars', 'base: temperature', 'isobar hPa: 2'])
  assert.deepEqual(overlayChips(null), [])
})

test('numbers are compact and clock labels carry milliseconds', () => {
  assert.equal(formatSignal(21.4321), '21.4')
  assert.equal(formatSignal(21.4321, 4), '21.43')
  assert.equal(formatSignal(123456), '123,456')
  assert.equal(formatSignal(0), '0')
  assert.equal(formatSignal(2.5e-5), '2.50e-5')
  assert.equal(formatSignal(null), '—')
  assert.equal(clockLabel(new Date(2026, 0, 1, 7, 5, 9, 42)), '07:05:09.042')
})

test('the change log names each change of a rebuild', () => {
  assert.equal(describeChanges(null, snap())[0].tag, 'INIT')
  assert.deepEqual(describeChanges(snap(), snap()), [{ tag: 'RENDER', text: 'scene rebuilt' }])
  assert.deepEqual(describeChanges(snap(), null), [])
  const frames = describeChanges(snap({ frames: 12, frame: 2 }), snap({ frames: 12, frame: 3, frameLabel: '+240 min' }))
  assert.deepEqual(frames, [{ tag: 'FRAME', text: '4/12 · +240 min' }])
  const loaded = describeChanges(snap(), snap({ source: 'Engine output · coupled wind', frames: 8, frame: 0, nx: 40 }))
  assert.equal(loaded[0].tag, 'DATA')
  assert.ok(loaded.every(e => e.tag !== 'FRAME'), 'a new dataset is not reported as a frame step')
  const tags = describeChanges(snap(), snap({ view: 'map', viewLabel: 'Weather map', layer: 'wind', layerTitle: 'Wind speed', unit: 'm/s', overlays: ['isobars'] })).map(e => `${e.tag}:${e.text}`)
  assert.deepEqual(tags, ['VIEW:Field explorer → Weather map', 'LAYER:Wind speed [m/s]', 'OVERLAY:+isobars −wind vectors'])
  assert.deepEqual(describeChanges(snap(), snap({ selected: 7, selectedLabel: '7,0', selectedValue: 21.4321 })), [{ tag: 'PROBE', text: 'cell 7,0 → 21.43 °C' }])
  assert.deepEqual(describeChanges(snap({ selected: 7 }), snap()), [{ tag: 'PROBE', text: 'cell probe cleared' }])
  assert.deepEqual(describeChanges(snap(), snap({ variant: 'Lower infiltration' })), [{ tag: 'DATA', text: 'showing Lower infiltration' }])
})

test('the log keeps the newest entries first with increasing ids', () => {
  const now = new Date(2026, 0, 1, 12, 0, 0, 0)
  let log = appendEntries([], [{ tag: 'INIT', text: 'a' }], { now })
  log = appendEntries(log, [{ tag: 'VIEW', text: 'b' }, { tag: 'LAYER', text: 'c' }], { now })
  assert.deepEqual(log.map(e => e.text), ['c', 'b', 'a'])
  assert.deepEqual(log.map(e => e.id), [3, 2, 1])
  assert.equal(log[0].time, '12:00:00.000')
  assert.equal(appendEntries(log, []), log, 'nothing to add keeps the same array')
  let long = []
  for (let i = 0; i < 30; i++) long = appendEntries(long, [{ tag: 'FRAME', text: String(i) }], { limit: 10, now })
  assert.equal(long.length, 10)
  assert.equal(long[0].text, '29')
  assert.equal(long[0].id, 30, 'ids keep growing after the cap')
})
