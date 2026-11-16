import test from 'node:test'
import assert from 'node:assert/strict'
import { LAYERS } from './layers.js'
import { SENSE, senseOf, classify, unitNote, formatNumber, withUnit, percentile, compass8, windAt, gradientAt, explainCell, whereWords, lineText } from './explain.js'

const grid = { nx: 3, ny: 3, spacing_east_m: 1000, spacing_north_m: 1000 }
const text = content => [content.verdict, ...content.sections.flatMap(s => s.lines)].map(lineText).join(' | ')

test('every described layer exists, and every scale is ordered and ends open', () => {
  const keys = new Set(LAYERS.map(l => l.key))
  for (const [key, sense] of Object.entries(SENSE)) {
    assert.ok(keys.has(key), `${key} is a layer`)
    assert.ok(sense.more && sense.less && sense.peak && sense.trough, `${key}: words`)
    if (sense.scale) {
      assert.ok(sense.scale.every(([limit], k) => k === 0 || limit > sense.scale[k - 1][0]), `${key}: ascending`)
      assert.equal(sense.scale.at(-1)[0], Infinity, `${key}: open top`)
    }
  }
  assert.deepEqual(senseOf('nothing-known'), { more: 'higher', less: 'lower', peak: 'a local high', trough: 'a local low' })
})

test('values get everyday classes and unit hints', () => {
  assert.equal(classify('temperature', 21.4), 'warm')
  assert.equal(classify('temperature', -3), 'freezing')
  assert.equal(classify('wind', 4.2), 'gentle breeze')
  assert.equal(classify('wind', 40), 'hurricane force')
  assert.equal(classify('precip', 12), 'heavy rain')
  assert.equal(classify('stability', 0.8), 'likely to slide')
  assert.equal(classify('bottomo2', 40), 'hypoxic: a dead zone')
  assert.equal(classify('terrain', null), null)
  assert.equal(classify('unknown', 3), null)
  assert.equal(unitNote('wind', 10), '10 m/s is about 36 km/h')
  assert.equal(unitNote('temperature', 20), '20 °C is 68 °F')
  assert.equal(unitNote('rainfall', 3), '1 mm of water is 1 litre on every square metre')
  assert.equal(unitNote('gpi', 1), null)
})

test('numbers carry useful precision only', () => {
  assert.equal(formatNumber(21.4321), '21.4')
  assert.equal(formatNumber(0.1934), '0.19')
  assert.equal(formatNumber(1013.25), '1,013')
  assert.equal(formatNumber(3), '3')
  assert.equal(formatNumber(2.5), '2.5')
  assert.equal(formatNumber(0), '0')
  assert.equal(formatNumber(null), '—')
  assert.equal(withUnit(21.43, '°C'), '21.4 °C')
  assert.equal(withUnit(82, '%'), '82%')
  assert.equal(withUnit(3, ''), '3')
})

test('percentiles, compass points and wind directions', () => {
  assert.equal(percentile([1, 2, 3, 4, null], 3), 62.5)
  assert.equal(percentile([], 3), null)
  assert.equal(compass8(0), 'north'); assert.equal(compass8(44), 'north-east'); assert.equal(compass8(-90), 'west'); assert.equal(compass8(359), 'north')
  const w = windAt(3, 3)   // blowing towards the north-east
  assert.equal(w.from, 'south-west')
  assert.ok(Math.abs(w.speed - Math.hypot(3, 3)) < 1e-9)
  assert.equal(w.label, 'gentle breeze')
  assert.equal(windAt(0, -5).from, 'north')
})

test('the local gradient points uphill and finds peaks and troughs', () => {
  const eastward = [0, 1, 2, 0, 1, 2, 0, 1, 2]
  const g = gradientAt(eastward, 3, 3, 4, 1000, 1000)
  assert.equal(compass8(g.towardDeg), 'east')
  assert.ok(Math.abs(g.east - 0.001) < 1e-12 && g.north === 0)
  const northward = [0, 0, 0, 1, 1, 1, 2, 2, 2]
  assert.equal(compass8(gradientAt(northward, 3, 3, 4).towardDeg), 'north', 'row index grows to the north')
  assert.equal(gradientAt([0, 0, 0, 0, 5, 0, 0, 0, 0], 3, 3, 4).peak, true)
  assert.equal(gradientAt([5, 5, 5, 5, 1, 5, 5, 5, 5], 3, 3, 4).trough, true)
  const edge = gradientAt([null, 1, 2, null, 1, 2, null, 1, 2], 3, 3, 4)
  assert.ok(edge.east > 0, 'one-sided difference next to missing cells')
})

test('a data point explains itself in sentences', () => {
  const values = [10, 12, 14, 11, 21.4, 15, 12, 13, 16]
  const content = explainCell({ key: 'temperature', title: 'Air temperature', tooltip: 'Air temperature near the ground.', unit: '°C', value: 21.4, values, i: 4, grid,
    series: [{ value: 18, label: '+0 min' }, { value: 20, label: '+10 min' }, { value: 21.4, label: '+20 min' }], previous: 20, previousLabel: '+10 min',
    wind: windAt(3, 3), also: [{ key: 'rainfall', title: 'Rain so far', unit: 'mm', value: 1.2 }], where: whereWords({ ...grid, nx: 3, ny: 3 }, 4) })
  const all = text(content)
  assert.equal(content.value, '21.4'); assert.equal(content.unit, '°C')
  assert.equal(lineText(content.verdict), 'Warm · the highest value on the map.')
  for (const phrase of ['Air temperature near the ground.', 'ranges from 10 °C to 21.4 °C, averaging 13.8 °C', 'above the average', '21.4 °C is 71 °F.',
    'A warm spot: every neighbouring point is colder.', 'Up 1.4 °C since the previous frame (+10 min).', 'the peak comes at +20 min',
    'Wind 4.24 m/s from the south-west: gentle breeze.', 'Rain so far 1.2 mm (a light shower).', '1.5 km east and 1.5 km north']) {
    assert.ok(all.includes(phrase), `missing: ${phrase}\n${all}`)
  }
  assert.deepEqual(content.sections.map(s => s.id), ['what', 'compare', 'around', 'time', 'also', 'where'])
  assert.deepEqual(content.sections.map(s => s.graphic ?? null), [null, 'distribution', 'neighbourhood', 'series', 'compass', null])
  assert.ok(content.distribution.pct > 80)
  assert.equal(content.short, 'warm')
  assert.equal(content.around.peak, true)
  // Values are the gaps that the typing fills in.
  assert.ok(content.sections[1].lines[0].some(t => typeof t === 'object' && t.v === '10 °C'))
})

test('ordinary points compare by share, gradients read as directions, and missing data says so', () => {
  const values = [0, 1, 2, 0, 1, 2, 0, 1, 2]
  const c = explainCell({ key: 'wind', title: 'Wind speed', unit: 'm/s', value: 1, values, i: 4, grid })
  assert.equal(lineText(c.verdict), 'Light air · windier than 50 % of the map.')
  assert.ok(text(c).includes('It gets windier towards the east, by 1 m/s per km.'))
  assert.equal(compass8(c.around.towardDeg), 'east')
  assert.ok(!c.sections.some(s => s.id === 'time'), 'no time section for a single frame')
  const lower = explainCell({ key: 'terrain', title: 'Terrain', unit: 'm', value: 0.5, values: [0, 0.5, 1, 2, 3], i: 1, grid: null })
  assert.equal(lineText(lower.verdict), 'Lowland · lower than 70 % of the map.')
  const none = explainCell({ key: 'rainfall', title: 'Rainfall', unit: 'mm', value: null, values: [1, null], i: 1, grid })
  assert.equal(none.value, '—'); assert.equal(none.short, 'no data'); assert.equal(none.distribution, null)
  assert.ok(lineText(none.verdict).startsWith('No data here'))
  const plain = explainCell({ key: 'valley', title: 'Valley depth', unit: 'm', value: 0.1, values: [0, 0.1, 0.2, 0.3], i: 1, grid: null })
  assert.equal(lineText(plain.verdict), 'Shallower than 63 % of the map.', 'without a class the sentence starts with a capital')
  const flat = explainCell({ key: 'cape', title: 'CAPE', unit: 'J/kg', value: 5, values: [5, 5, 5, 5, 5, 5, 5, 5, 5], i: 4, grid })
  assert.ok(lineText(flat.verdict).includes('the same everywhere'))
})

test('places are described geographically or in kilometres', () => {
  assert.equal(lineText(whereWords({ nx: 72, ny: 36, spacing_east_m: 5e5, spacing_north_m: 5e5 }, 18 * 72 + 36, { geographic: true })).split(' · ')[0], '2.5° N, 2.5° E')
  assert.ok(lineText(whereWords({ nx: 12, ny: 10, spacing_east_m: 1000, spacing_north_m: 1000 }, 0)).startsWith('0.5 km east and 0.5 km north'))
})

test('classes are named, counted and followed, not treated as amounts', () => {
  const categories = [{ label: 'clear', color: '#111' }, { label: 'cumulus', color: '#eee', note: 'Fair-weather heaps of cloud.' }, { label: 'stratus', color: '#888' }]
  const values = [0, 0, 0, 1, 1, 2, 0, 1, 0]
  const c = explainCell({ key: 'clouds', title: 'Cloud type', tooltip: 'Cloud genus.', value: 1, values, i: 4, grid, categories, domain: 'the region',
    series: [{ value: 0, label: 'a' }, { value: 1, label: 'b' }, { value: 1, label: 'c' }], previous: 1 })
  assert.equal(c.value, 'cumulus'); assert.equal(c.unit, '')
  assert.equal(lineText(c.verdict), '33 % of the region is like this.')
  const all = text(c)
  for (const phrase of ['Fair-weather heaps of cloud.', 'Most common: clear (56 %), cumulus (33 %) and stratus (11 %).', 'It borders clear, stratus.', 'Unchanged since the previous frame.', 'changes class 1 time here.']) assert.ok(all.includes(phrase), `missing: ${phrase}\n${all}`)
  assert.deepEqual(c.categories.map(k => [k.label, k.current]), [['clear', false], ['cumulus', true], ['stratus', false]])
  assert.equal(c.short, '33 % of the region is like this')
  assert.equal(c.distribution, null)
  const lone = explainCell({ key: 'clouds', title: 'Cloud type', value: 0, values: [0, 0, 0, 0], i: 0, grid: { nx: 2, ny: 2 }, categories })
  assert.ok(text(lone).includes('All neighbouring points are clear too.'))
  assert.equal(lineText(lone.verdict), 'All of the map is like this.')
})

test('the domain is named in the comparison', () => {
  const c = explainCell({ key: 'sunbrightness', title: 'Brightness', unit: 'K', value: 5900, values: [5000, 5500, 5900, 6100], i: 2, grid: null, domain: 'the disk' })
  assert.equal(lineText(c.verdict), 'Brighter than 63 % of the disk.')
})

test('waves at a point are told as height, sea state, period, spacing, direction and speed', () => {
  const base = { key: 'current', title: 'Current', unit: 'm/s', value: 0.2, values: [0.1, 0.2, 0.3], i: 1, grid: null }
  const c = explainCell({ ...base, waves: { hs: 3.2, tp: 8.1, length: 102, speed: 12.6, towardDeg: 45, state: 'rough', regime: 'fetch-limited', fetchKm: 180 } })
  assert.ok(text(c).includes('Waves 3.2 m high (rough), every 8.1 s, 102 m apart, running towards the north-east at 12.6 m/s: still growing over 180 km of open water (estimated from the wind).'), text(c))
  assert.ok(text(explainCell({ ...base, waves: { hs: 0, regime: 'calm' } })).includes('A calm sea: no wind waves here.'))
  assert.ok(text(explainCell({ ...base, waves: { hs: 0, regime: 'damped by sea ice' } })).includes('No waves: sea ice damps them.'))
  assert.ok(text(explainCell({ ...base, waves: { hs: 9, tp: 13, length: 260, speed: 20, towardDeg: 0, state: 'high', regime: 'fully developed', fetchKm: 900 } })).includes('a fully developed sea'))
})
