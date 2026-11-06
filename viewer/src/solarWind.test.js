import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { tableAt, cellDose, cellSep, windLayerValues, windLayerRange, summarizeWind, magneticPosition, magneticLocalTime, ovalBoundary, ovalRing, vectorToLatLon,
  sunFixedRotation, geographicVector, magnetosphereSketch, displayRadius, polarDial, escapeRows, hydrogenShares, annualMeans, chartLines, windColor, sig, REGIONS, WIND_LAYER_KEYS } from './solarWind.js'
import { LAYERS, layerAvailable } from './layers.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'solar-wind', 'engine-timeline.json'), 'utf8'))
const frames = validateSequence(fixture())

test('solar wind fixture validates: one map per storm frame, with static magnetic coordinates', () => {
  const block = frames[0].solarWind.block
  assert.equal(frames.length, block.storm.hours / block.storm.frame_every_hours + 1)
  assert.ok(frames.every((f, k) => f.solarWind.index === k && f.solarWind.aurora.length === f.forecast.grid.nx * f.forecast.grid.ny))
  for (const key of WIND_LAYER_KEYS) assert.ok(layerAvailable(key, frames[0]), key)
  assert.ok(!layerAvailable('temperature', frames[0]) && !layerAvailable('wind', frames[0]), 'no air temperature or wind layers for the solar wind')
  assert.ok(LAYERS.filter(l => l.key.startsWith('wind') && l.key !== 'wind').every(l => l.group === 'sun'))
})

test('validation rejects broken solar wind blocks', () => {
  const broken = mutate => { const doc = fixture(); mutate(doc.solar_wind, doc); return () => validateSequence(doc) }
  assert.throws(broken(b => { b.schema_version = 'terraflux.solar.wind/v0' }), /Unsupported solar wind/)
  assert.throws(broken(b => { b.storm.frames[3].region[10] = 7 }), /regions/)
  assert.throws(broken(b => { b.storm.frames[0].dose_by_cutoff_usv_h.pop() }), /radiation tables/)
  assert.throws(broken(b => { b.static.cutoff_gv.pop() }), /static maps/)
  assert.throws(broken(b => { b.storm.frames.pop() }), /match the timeline/)
  assert.throws(broken(b => { b.storm.series.kp.pop() }), /storm series/)
})

test('the browser geometry agrees with the engine: magnetic coordinates, the oval and the regions', () => {
  const f = frames[20], w = f.solarWind, s = w.static, nx = f.forecast.grid.nx, ny = f.forecast.grid.ny
  for (const i of [0, 123, 2000, 3000, nx * ny - 1]) {
    const lat = -90 + (Math.floor(i / nx) + 0.5) * 180 / ny, lon = -180 + (i % nx + 0.5) * 360 / nx
    const m = magneticPosition(lat, lon, w.dipole)
    assert.ok(Math.abs(m.lat - s.magnetic_latitude_deg[i]) < 0.01 && Math.abs(((m.lon - s.magnetic_longitude_deg[i] + 540) % 360) - 180) < 0.01, `cell ${i}`)
  }
  let checked = 0
  s.magnetic_latitude_deg.forEach((mlat, i) => {
    const colat = 90 - Math.abs(mlat), mlt = magneticLocalTime(s.magnetic_longitude_deg[i], w.map.subsolar.magnetic_longitude_deg)
    const region = w.region[i], pole = ovalBoundary(w.map.oval, mlt, 'poleward'), eq = ovalBoundary(w.map.oval, mlt, 'equatorward')
    if (region === 2) { assert.ok(colat < pole + 1e-6); checked++ }
    if (region === 0) assert.ok(colat > eq - 0.05)
    if (region === 1) assert.ok(colat >= pole - 1e-6 && colat <= eq + 0.05)
  })
  assert.ok(checked > 100)
  // The ring lies on the boundary.
  const ring = ovalRing(w, 1, 'equatorward', 24)
  ring.forEach((v, k) => {
    const { lat, lon } = vectorToLatLon(v), m = magneticPosition(lat, lon, w.dipole)
    assert.ok(Math.abs((90 - m.lat) - ovalBoundary(w.map.oval, k, 'equatorward')) < 1e-6)
  })
})

test('layer values: radiation from the cutoff tables, fixed colour ranges', () => {
  const f = frames[0], w = f.solarWind, s = w.static
  assert.equal(tableAt([0, 10, 20], 0.5, 0.25), 5)
  assert.equal(tableAt([0, 10, 20], 0.5, 5), 20)
  const pole = s.cutoff_gv.indexOf(Math.min(...s.cutoff_gv)), eq = s.cutoff_gv.indexOf(Math.max(...s.cutoff_gv))
  assert.ok(cellDose(w, pole) > 2 * cellDose(w, eq), 'the poles receive more than twice the dose')
  assert.ok(Math.abs(cellDose(w, pole) - w.map.dose_by_cutoff_usv_h[0]) < 0.05)
  assert.equal(cellSep(w, eq), 0)
  for (const key of WIND_LAYER_KEYS) {
    const v = windLayerValues(f, key), [lo, hi] = windLayerRange(frames, key)
    assert.equal(v.length, s.cutoff_gv.length, key)
    assert.ok(hi > lo && v.every(x => x >= lo - 1e-9 && x <= hi + 1e-9), key)
    assert.match(windColor(key, v[pole], lo, hi), /^#[0-9a-f]{6}$/)
  }
  const sepPeak = frames.reduce((a, fr) => Math.max(a, ...windLayerValues(fr, 'windsep')), 0)
  assert.ok(sepPeak > 1, 'solar protons ionise the polar caps during the event')
  assert.equal(REGIONS.length, 4)
})

test('metrics of the storm: the magnetopause inside geostationary orbit, Kp and Dst at the peak', () => {
  const peak = frames.reduce((best, f) => (summarizeWind(f).dst < summarizeWind(best).dst ? f : best), frames[0])
  const m = summarizeWind(peak), q = summarizeWind(frames[0])
  assert.ok(m.kp > 7 && m.dst < -150 && m.oval_mlat < q.oval_mlat - 10 && m.cap_mlat < q.cap_mlat)
  assert.ok(frames.some(f => summarizeWind(f).inside_geostationary), 'the magnetopause is pushed inside geostationary orbit')
  assert.ok(m.o_kg > 5 * q.o_kg && m.exo > q.exo + 150 && m.density_ratio > 2)
  assert.ok(m.neutron_pole < 0 && m.neutron_pole < m.neutron_equator, 'Forbush decrease, deepest at the pole')
})

test('the scene: the Sun along +x, the magnetosphere sketch around the engine magnetopause', () => {
  const f = frames[0], w = f.solarWind, rot = sunFixedRotation(w.map.subsolar)
  const sun = rot(geographicVector(w.map.subsolar.latitude_deg, w.map.subsolar.longitude_deg))
  assert.ok(Math.abs(sun[0] - 1) < 1e-9 && Math.abs(sun[1]) < 1e-9 && Math.abs(sun[2]) < 1e-9)
  const north = rot([0, 0, 1])
  assert.ok(Math.abs(north[1] - Math.cos(w.map.subsolar.latitude_deg * Math.PI / 180)) < 1e-9, 'north up, tilted by the season')
  const sketch = magnetosphereSketch(w, rot, 5)
  assert.ok(Math.abs(sketch.nose[0] - displayRadius(w.map.magnetopause.standoff_re, 5)) < 1e-9)
  assert.equal(sketch.open.length, 16)
  assert.ok(sketch.closed.length >= 12 && sketch.closed.every(l => l.points.length > 10))
  assert.ok(sketch.open.every(l => l.points.at(-1)[0] < -10), 'open field lines swept into the tail')
  const dial = polarDial(f, 1, windLayerValues(f, 'windaurora'))
  assert.ok(dial.length > 100 && dial.every(c => Math.hypot(c.x - 50, c.y - 50) <= 45.01))
})

test('escape, cycle and chart helpers', () => {
  const b = frames[0].solarWind.block, rows = escapeRows(b)
  const total = rows.reduce((s, r) => s + r.kg_s, 0)
  assert.ok(Math.abs(total - b.budgets.mean_escape.total_kg_s) < 1e-3 * total)
  assert.ok(rows[0].kg_s > rows[1].kg_s && rows[1].polar_share === 1 && rows[0].polar_share < 0.1, 'hydrogen dominates and leaves mostly globally; oxygen only at the poles')
  const shares = hydrogenShares(b.cycle)
  assert.ok(shares.every(s => Math.abs(s.jeans + s.exchange + s.polar - 1) < 1e-2))
  const years = annualMeans(b.cycle, 'sunspot_number')
  assert.equal(years[0].year, b.cycle.first_year)
  const chart = chartLines([0, 1, 2], [{ values: [1, 2, 3] }, { values: [3, null, 1] }])
  assert.equal(chart.lines[0].points, '0.00,48.15 50.00,25.00 100.00,1.85')
  assert.equal(chart.lo, 1)
  assert.equal(chart.hi, 3)
  assert.equal(chart.lines[1].points.split(' ').length, 2)
  assert.equal(sig(0.00123), '1.2e-3')
  assert.equal(sig(123456), '120,000')
})
