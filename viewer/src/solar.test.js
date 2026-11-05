import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { validateSolar, summarizeSolar, locate, cycleAt, rScaleShares, chartWindow, monthlyPoints, polyline, band, sunspotChart, cycleTable, uvSites, flareYears,
  butterflyImage, diskGeometry, regionPosition, contrastPct, brightnessColor, contrastColor, cycleColor, flareLabel, fmtYear, monthIndex, monthLabel, monthYear, R_SCALE } from './solar.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'solar', 'engine-timeline.json'), 'utf8'))
const frameAt = (frames, label) => frames.find(f => f.solar.disk.month >= label)

test('solar fixture validates: disk frames, the record and the forecast', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].solar.block
  assert.equal(frames.length, b.disk.frames.length)
  assert.equal(frames[0].solar.synthetic, false)
  assert.equal(b.record_kind, 'cycle_table')
  assert.match(b.data_origin, /not observations/, 'the synthesised record says what it is')
  assert.equal(monthIndex(b.forecast.first_month), monthIndex(b.record.first_month) + b.record.months, 'the forecast starts where the record ends')
  assert.ok(frames.some(f => f.solar.source === 'record') && frames.some(f => f.solar.source === 'forecast'))
  assert.ok(frames.every(f => f.solar.brightness.length === f.forecast.grid.nx * f.forecast.grid.ny))
  assert.equal(frames[0].terrain, undefined, 'no terrain: the grid is the disk')
})

test('validation rejects broken solar blocks', () => {
  const bad = (change, pattern) => { const t = fixture(); change(t.solar); assert.throws(() => validateSequence(t), pattern) }
  bad(s => { s.schema_version = 'terraflux.solar.cycle/v0' }, /Unsupported solar contract/)
  bad(s => { s.record.sunspot_number[5] = -1 }, /Invalid sunspot record/)
  bad(s => { s.forecast.sunspot.p5[3] = s.forecast.sunspot.p95[3] + 1 }, /ordered/)
  bad(s => { s.disk.frames.pop() }, /match the timeline grid and frames/)
  bad(s => { s.disk.frames[2].brightness_k[0] = 5000 }, /match the grid and the disk/)
  bad(s => { s.forecast.first_month = '1999-01' }, /start where the record ends/)
  bad(s => { s.cycles[2].number += 5 }, /Invalid solar cycles/)
  const withTerrain = fixture()
  withTerrain.terrain = { schema_version: 'terraflux.terrain.grid/v1', grid: withTerrain.frames[0].grid, units: 'm', vertical_reference: 'x', source_id: 'x', values: new Array(withTerrain.frames[0].grid.nx ** 2).fill(0) }
  assert.throws(() => validateSequence(withTerrain), /not a terrain grid/)
})

test('metrics: record months before the issue date, the forecast median and its range after', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].solar.block
  const past = summarizeSolar(frames[0]), future = summarizeSolar(frames.at(-1))
  assert.equal(past.source, 'record')
  assert.equal(past.sunspot, b.record.sunspot_number[locate(b, frames[0].solar.month).k])
  assert.equal(past.sunspot_p5, null)
  assert.equal(future.source, 'forecast')
  assert.ok(future.sunspot_p5 <= future.sunspot && future.sunspot <= future.sunspot_p95)
  assert.ok(future.tsi_p5 <= future.tsi_w_m2 && future.tsi_w_m2 <= future.tsi_p95)
  // UV bands come sorted by their upper wavelength; the shorter ones vary more, band after band.
  const m = summarizeSolar(frameAt(frames, '2014-01'))
  assert.deepEqual(m.uv.map(u => u.to_nm), [...m.uv.map(u => u.to_nm)].sort((x, y) => x - y))
  assert.ok(m.uv.every((u, k) => k === 0 || u.change_pct < m.uv[k - 1].change_pct) && m.uv.at(-1).change_pct > 0)
  assert.ok(m.tsi_change_w_m2 > 0.3, 'the Sun is brighter at maximum')
  assert.ok(m.uv_index_change_pct < 0 && m.uv_index_change_pct > -3, 'surface UV index barely moves (ozone compensates)')
  assert.ok(m.flares.p_x1 > 0.2 && m.flares.x1 > 0.2, 'X flares are likely every month at maximum')
  assert.equal(m.flares.r_scale.length, R_SCALE.length)
  assert.ok(m.flares.r_scale.every((l, k) => k === 0 || l.expected < m.flares.r_scale[k - 1].expected), 'stronger blackouts are rarer')
  assert.equal(m.flares.r_scale[2].expected, m.flares.x1, 'R3 = X1')
  assert.ok(m.disk.spot_ppm <= 0 && m.disk.facular_ppm >= 0)
  assert.ok(['rising', 'declining', 'near maximum', 'near minimum'].includes(m.trend))
  const minimum = summarizeSolar(frameAt(frames, '2008-12'))
  assert.ok(minimum.sunspot < 30 && minimum.flares.x1 < m.flares.x1 / 5, 'few flares at the minimum')
})

test('cycles: dates resolve to cycle numbers, also in the forecast', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].solar.block
  const c24 = b.cycles.find(c => c.number === 24)
  assert.equal(cycleAt(b, c24.minimum_year + 1).cycle, 24)
  const next = b.forecast.turning_points.find(t => t.kind === 'minimum')
  assert.equal(cycleAt(b, next.year_median + 2).cycle, next.cycle)
  assert.ok(Math.abs(cycleAt(b, next.year_median + 2).phase_years - 2) < 1e-9)
  const rows = cycleTable(b)
  assert.ok(rows.filter(r => !r.predicted).length === b.cycles.length && rows.some(r => r.predicted))
  const predicted = rows.find(r => r.predicted)
  assert.equal(predicted.cycle, b.cycles.at(-1).number + 1)
  assert.ok(predicted.minimum.year_p5 <= predicted.minimum.year_median && predicted.maximum.value_p5 < predicted.maximum.value_p95)
  assert.ok(rows.find(r => r.in_progress).fit.in_progress)
})

test('charts: windows, thinned points, polylines and the forecast band', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].solar.block
  const [a0, a1] = chartWindow(b, 'all'), [f0, f1] = chartWindow(b, 'focus')
  assert.ok(a0 < f0 && a1 === f1 && Math.abs(f0 - (b.forecast.issue_year - 22)) < 1e-9)
  const c = sunspotChart(b, 'focus')
  assert.ok(c.monthly.every(([y]) => y >= c.x[0] && y <= c.x[1]) && c.forecast.p50.length === b.forecast.months)
  assert.ok(c.y[1] >= Math.max(...c.forecast.p95.map(p => p[1])))
  assert.ok(c.extrema.some(e => e.predicted && e.kind === 'maximum') && c.extrema.some(e => !e.predicted && e.kind === 'minimum'))
  assert.ok(sunspotChart(b, 'all').monthly.length < b.record.months, 'long windows are thinned')
  assert.deepEqual(monthlyPoints('2000-01', [1, null, 3]), [[monthYear(24000), 1], [monthYear(24002), 3]])
  assert.equal(polyline([[0, 0], [10, 10]], [0, 10], [0, 10]), '0.00,47.00 100.00,3.00')
  assert.equal(band([[0, 0], [10, 0]], [[0, 10], [10, 10]], [0, 10], [0, 10]), '0.00,47.00 100.00,47.00 100.00,3.00 0.00,3.00')
})

test('UV sites, flare years and the R scale come from the engine numbers', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].solar.block
  const frame = frameAt(frames, '2014-06')
  const sites = uvSites(frame)
  assert.equal(sites.length, b.atmosphere.sites.length)
  const north = sites.find(s => s.latitude_deg > 40)
  assert.ok(north.quiet > 4 && Math.abs(north.now / north.quiet - 1 - north.change_pct / 100) < 1e-12)
  const shares = rScaleShares(b)
  assert.ok(shares.m1 > 10 && shares.x10 < 0.1 && shares.x20 < shares.x10)
  const { past, future } = flareYears(b, 2000, 2040)
  assert.ok(past.length > 10 && future.length > 5 && past.every(y => y.year >= 1999 && y.year <= 2040))
  assert.ok(future.every(y => y.x1_expected_p5 <= y.x1_expected_p50 && y.x1_expected_p50 <= y.x1_expected_p95))
  assert.equal(flareLabel(2.34e-4), 'X2.3')
  assert.equal(flareLabel(9.96e-6), 'M1.0')
  assert.equal(fmtYear(monthYear(monthIndex('2030-07'))), '2030-07')
  assert.equal(monthLabel(monthIndex('1999-12')), '1999-12')
})

test('disk geometry, regions and colours', () => {
  const frames = validateSequence(fixture())
  const b = frames[0].solar.block, grid = frames[0].forecast.grid
  const geo = diskGeometry(b, grid)
  assert.equal(geo.length, grid.nx * grid.ny)
  geo.forEach((g, i) => assert.equal(g.mu === null, frames[0].solar.quiet[i] === null, 'the disk cells match the engine\'s disk'))
  const centre = geo.reduce((best, g) => g.mu !== null && g.mu > best.mu ? g : best, { mu: -1 })
  assert.ok(centre.mu > 0.99 && Math.abs(centre.latitude_deg) < 2 && Math.abs(centre.longitude_deg) < 2)
  const p = regionPosition({ latitude_deg: 30, longitude_deg: 0 })
  assert.ok(Math.abs(p.y - 0.5) < 1e-12 && Math.abs(p.x) < 1e-12 && Math.abs(p.mu - Math.sqrt(3) / 2) < 1e-12)
  assert.equal(contrastPct(null, 5000), null)
  assert.ok(Math.abs(contrastPct(5000 * 2 ** 0.25, 5000) - 100) < 1e-9)
  const withSpots = frames.find(f => f.solar.regions.length > 5)
  const dark = Math.min(...withSpots.solar.brightness.map((t, i) => contrastPct(t, withSpots.solar.quiet[i]) ?? 0))
  assert.ok(dark < -5, 'spots darken their cells')
  assert.equal(brightnessColor(null), '#05080b')
  assert.notEqual(contrastColor(-30), contrastColor(3))
  assert.equal(cycleColor(24, 25), '#6fa8ff')
  const img = butterflyImage(b, chartWindow(b, 'modern'), 120, 40)
  assert.equal(img.pixels.length, 120 * 40 * 4)
  assert.ok(img.pixels.some((v, i) => i % 4 === 0 && v > 200), 'the wings are bright')
})

test('long term: grand minima and maxima from 1000 to the horizon, validated and summarised', async () => {
  const { validateLongTerm, longTermWindow, yearlyPoints, longTermAt, epochBands, epochTable, dynamoExample, holoceneStrip, LONG_TERM_WINDOWS, EPOCH_COLORS } = await import('./solar.js')
  const frames = validateSequence(fixture())
  const lt = frames[0].solar.block.long_term
  assert.ok(lt && lt.start_year === 1000 && lt.horizon_year === 2100)
  assert.equal(lt.series.sunspot.length, 1101)
  // Windows and points.
  assert.deepEqual(longTermWindow(lt, 'maunder'), [1600, 1760])
  assert.deepEqual(longTermWindow(lt, 'all'), [1000, 2101])
  assert.ok(LONG_TERM_WINDOWS.every(w => longTermWindow(lt, w.key)[1] > longTermWindow(lt, w.key)[0]))
  const pts = yearlyPoints(lt, lt.series.sunspot, [1600, 1760])
  assert.equal(pts.length, 160)
  assert.equal(pts[0][0], 1600.5)
  // The Maunder Minimum: few spots, more cosmogenic isotopes, darker and (a little) colder than the Modern Maximum.
  const mm = longTermAt(lt, 1680), modern = longTermAt(lt, 1990)
  assert.ok(mm.sunspot < 25 && mm.source === 'envelope' && modern.source === 'record')
  assert.ok(mm.production > modern.production * 1.2 && mm.tsi < modern.tsi - 0.5 && mm.solar_k < modern.solar_k)
  assert.equal(longTermAt(lt, 900), null)
  // Epochs and their detection.
  const rows = epochTable(lt)
  const maunder = rows.find(r => r.name === 'Maunder Minimum')
  assert.ok(maunder.detected && maunder.color === EPOCH_COLORS.grand_minimum && maunder.cosmogenic_production_pct > 0)
  assert.ok(maunder.tsi_vs_reference_w_m2.high < maunder.tsi_vs_reference_w_m2.central && maunder.tsi_vs_reference_w_m2.central < maunder.tsi_vs_reference_w_m2.low)
  const dalton = rows.find(r => r.name === 'Dalton Minimum')
  assert.ok(dalton.temperature_k.volcanic < dalton.temperature_k.solar_central, 'the Dalton-era cold was volcanic in this model')
  const bands = epochBands(lt, [1600, 1760])
  assert.ok(bands.some(b => b.name === 'Maunder Minimum' && b.from === 1645 && b.to === 1715) && bands.every(b => b.from >= 1600 && b.to <= 1760))
  // The dynamo example: spots vanish inside the episode, a weak Babcock-Leighton source triggers it.
  const ex = dynamoExample(lt)
  assert.ok(ex.rows.filter(r => r.inside).every(r => r.low) && ex.rows.filter(r => r.inside).some(r => r.emergence < 0.2))
  assert.ok(ex.trigger && ex.trigger.efficiency < 0.5 && ex.recovery && !ex.recovery.low)
  const h = holoceneStrip(lt)
  assert.ok(h.years > 11000 && h.minima > 10 && h.bands.every(b => b.to > b.from))
  // Invalid long-term blocks are refused.
  const t = fixture()
  t.solar.long_term.series.sunspot.pop()
  assert.throws(() => validateSequence(t), /Invalid long-term series/)
  const u = fixture()
  u.solar.long_term.dynamo.outlook.cycles[3].grand_minimum_begun = 0
  assert.throws(() => validateSequence(u), /cumulative/)
  assert.doesNotThrow(() => validateLongTerm(lt))
})
