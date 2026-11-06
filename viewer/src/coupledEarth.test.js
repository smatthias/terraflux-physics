import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import {
  EARTH_VIEWS, HORIZONS, HAZARDS, earthDocumentKind, validateEarthForecast, validateEarthClimate, forecastGrid, projectToGrid, climateGrid, geoToClimateCell,
  cityForecast, cellForecast, noonFrame, dayLabel, skillSeries, usefulLead, windFrames, windField, directionFrom, compass, beaufort, cityWindTable, meteogram,
  warningMatrix, warningLevels, warningList, warningCounts, peakValueLabel, trackLines, trackSpread, driverBars, scenarioChart, forcingByGroup, impactRows,
  feedbackRows, co2Budget, couplingGraph, edgePath, keyAnswers, contourLines, fluxColor, temperatureColor, precipColor, levelColor, symbolInfo, formatImpact,
} from './coupledEarth.js'

const here = dirname(fileURLToPath(import.meta.url))
const fixture = name => JSON.parse(readFileSync(join(here, '..', 'public', name, 'engine-timeline.json'), 'utf8'))
const forecastDoc = fixture('coupled-forecast'), climateDoc = fixture('coupled-climate')
const f = validateEarthForecast(forecastDoc), c = validateEarthClimate(climateDoc)

test('both coupled runs validate, and other documents are told apart', () => {
  assert.equal(earthDocumentKind(forecastDoc), 'forecast')
  assert.equal(earthDocumentKind(climateDoc), 'climate')
  assert.equal(earthDocumentKind({ schema_version: 'terraflux.weather.timeline/v1' }), null)
  assert.throws(() => validateEarthForecast(climateDoc), /earth.forecast/)
  assert.throws(() => validateEarthClimate(forecastDoc), /earth.climate/)
  const broken = structuredClone(forecastDoc)
  broken.earth_forecast.days[0].warnings.wind[0] = 7
  assert.throws(() => validateEarthForecast(broken), /levels 0-3/)
  const short = structuredClone(climateDoc)
  short.earth_climate.co2.impact_score.pop()
  assert.throws(() => validateEarthClimate(short), /impact_score/)
  assert.ok(EARTH_VIEWS.length === 6 && EARTH_VIEWS.every(v => ['climate', 'forecast'].includes(v.needs) && v.tooltip.length > 30))
})

test('the projection puts every cell centre back on its cell', () => {
  const g = forecastGrid(forecastDoc)
  const centre = projectToGrid(g.lat0, g.lon0, g)
  assert.ok(Math.abs(centre.x - g.nx / 2) < 1e-9 && Math.abs(centre.y - g.ny / 2) < 1e-9, 'the anchor is the grid centre')
  for (const k of [0, 77, 800, g.nx * g.ny - 1]) {
    const p = projectToGrid(f.static.latitude_deg[k], f.static.longitude_deg[k], g)
    assert.ok(Math.abs(p.x - (k % g.nx + 0.5)) < 0.02 && Math.abs(p.y - (Math.floor(k / g.nx) + 0.5)) < 0.02, `cell ${k}`)
  }
  const cg = climateGrid(c)
  assert.equal(cg.nx * cg.ny, c.map.land_mask.length)
  assert.deepEqual(geoToClimateCell(-90, -180, cg), { x: 0, y: 0 })
  for (const city of f.cities.filter(x => x.inside)) {
    const p = projectToGrid(city.latitude_deg, city.longitude_deg, g)
    assert.equal(Math.floor(p.y) * g.nx + Math.floor(p.x), city.cell, `${city.name} lies in its cell`)
  }
})

test('forecast products: horizons, days, cities, the inspector and the skill', () => {
  assert.deepEqual(HORIZONS, [3, 5, 7, 10])
  assert.ok(f.forecast_days >= 10)
  for (const h of HORIZONS) {
    const rows = cityForecast(f, h)
    assert.equal(rows.length, f.cities.filter(x => x.inside).length)
    assert.ok(rows.every(r => r.days.length === h && r.days.every(d => d.tmax >= d.tmin - 0.05 && d.rainChance >= 0 && d.rainChance <= 100 && d.level >= 0 && d.level <= 3)))
  }
  for (let day = 1; day <= f.forecast_days; day++) assert.equal(f.frame_hours[noonFrame(f, day)], 24 * (day - 1) + 12, `noon of day ${day}`)
  assert.match(dayLabel(f, 1), /^(Mon|Tue|Wed|Thu|Fri|Sat|Sun) \d+\.\d+\.$/)
  const cell = f.cities.find(x => x.inside).cell, inspected = cellForecast(f, cell, 5)
  assert.equal(inspected.length, 5)
  assert.equal(inspected[0].tmax, f.days[0].tmax_c[cell])
  const s = skillSeries(f)
  assert.equal(s.length, f.frames.length)
  assert.ok(s[0].pressureAcc > 0.9 && s.at(-1).pressureRmse > s[1].pressureRmse, 'skill is lost with the lead time')
  const lead = usefulLead(f)
  assert.ok(lead.hours >= 24 && lead.hours <= 24 * f.forecast_days)
})

test('wind: the first three days hour by hour, city tables and meteograms', () => {
  const frames = windFrames(f)
  assert.equal(frames.at(-1).hour, 72)
  assert.ok(frames.every(x => f.frame_hours[x.index] === x.hour))
  const w = windField(forecastDoc, frames[4].index)
  assert.equal(w.speed.length, f.static.land_mask.length)
  assert.ok(w.speed.every(v => v >= 0 && v < 90))
  assert.equal(directionFrom(0, -5), 0, 'a wind blowing south comes from the north')
  assert.equal(directionFrom(-5, 0), 90, 'a wind blowing west comes from the east')
  assert.equal(compass(225), 'SW')
  assert.equal(beaufort(0.1), 0)
  assert.equal(beaufort(40), 12)
  assert.equal(beaufort(9), 5)
  const table = cityWindTable(f)
  assert.ok(table.length > 10)
  for (const row of table) {
    const city = f.cities.find(x => x.name === row.name)
    for (const d of row.days.filter(Boolean)) {
      assert.ok(d.hour > 24 * (d.day - 1) && d.hour <= 24 * d.day)
      const k = city.hourly.hour.indexOf(d.hour)
      assert.equal(d.gust, Math.max(...city.hourly.gust_m_s.filter((_, j) => city.hourly.hour[j] > 24 * (d.day - 1) && city.hourly.hour[j] <= 24 * d.day)))
      assert.equal(city.hourly.gust_m_s[k], d.gust)
    }
  }
  const m = meteogram(f.cities.find(x => x.inside && x.hourly))
  assert.equal(m.wind.split(' ').length, 72)
  assert.ok(m.max >= 10 && m.arrows.length === 12)
})

test('severe weather: the warning matrix, levels, areas and the hurricane plume', () => {
  const matrix = warningMatrix(f)
  assert.equal(matrix.length, f.forecast_days)
  for (const row of matrix) {
    const d = f.days[row.day - 1]
    for (const h of row.hazards) {
      assert.equal(h.level, Math.max(...d.warnings[h.key]))
      assert.equal(h.cells, d.warnings[h.key].filter(x => x > 0).length)
    }
    const all = warningLevels(f, row.day)
    assert.ok(all.every((l, i) => l === Math.max(...HAZARDS.map(hz => d.warnings[hz.key][i]))), 'all hazards: the highest level per cell')
    const areas = warningList(f, row.day)
    assert.equal(areas.reduce((a, r) => a + r.cells, 0), HAZARDS.reduce((a, hz) => a + d.warnings[hz.key].filter(x => x > 0).length, 0), 'the areas cover the warned cells')
    assert.ok(areas.every((r, k) => k === 0 || areas[k - 1].level >= r.level), 'highest level first')
    const counts = warningCounts(f, row.day)
    assert.equal(counts[1] + counts[2] + counts[3], areas.length)
  }
  assert.match(peakValueLabel({ hazard: 'wind', peak_value: 33.4 }), /gusts 33 m\/s/)
  assert.match(peakValueLabel({ hazard: 'tornado', peak_value: 0.3 }), /30 % of the members/)
  const tracks = trackLines(f)
  assert.equal(tracks.filter(t => !t.truth).length, f.members * f.tracks.members[0].length)
  assert.ok(tracks.some(t => t.truth) && tracks.every(t => t.points.length > 2 && t.points.every(p => p.wind > 0)))
  const s12 = trackSpread(f, 12), s72 = trackSpread(f, 72)
  assert.ok(s12.members === f.members && s72.spreadKm > s12.spreadKm, 'the plume widens')
  assert.equal(levelColor(0), null)
  assert.ok(levelColor(3).startsWith('#'))
})

test('climate: drivers add up, charts and impacts', () => {
  for (const s of c.scenarios) for (const period of ['today', 'y2050', 'y2100']) {
    const bars = driverBars(c, s.key, period)
    assert.ok(bars.rows.every(r => r.lo <= r.hi && bars.max >= Math.abs(r.warming_k)))
    const k = period === 'today' ? c.years.indexOf(c.today) : c.years.indexOf(period === 'y2050' ? 2050 : 2100)
    assert.ok(Math.abs(bars.total - c.series[s.key].global_k[k]) < 0.05, `${s.key} ${period}: the bars add up to the warming`)
  }
  assert.equal(driverBars(c, c.central_scenario, 'today').rows[0].key, 'co2')
  const chart = scenarioChart(c, 'global_k', { focus: c.central_scenario, band: ['global_low_k', 'global_high_k'] })
  assert.equal(chart.lines.length, c.scenarios.length)
  assert.ok(chart.band && chart.lo <= 0 && chart.ticks.some(t => t.year === 2000))
  const groups = forcingByGroup(c, c.central_scenario), total = c.series[c.central_scenario].total_forcing_w_m2, k = c.years.indexOf(c.today)
  assert.ok(Math.abs(groups.reduce((a, g) => a + g.values[k], 0) - total[k]) < 0.05, 'the groups add up to the total forcing')
  const impacts = impactRows(c)
  assert.ok(impacts.length >= 10 && impacts.every(i => typeof i.fmt(i.today) === 'string'))
  assert.equal(formatImpact({ key: 'x', unit: 'times as often' }, 8.1), '×8.1')
  assert.equal(formatImpact({ key: 'ph', unit: 'pH' }, 8.0234), '8.023')
  const fb = feedbackRows(c)
  assert.ok(Math.abs(fb.reduce((a, r) => a + r.today, 0) - c.climate.warming_today_k) < 0.02, 'the feedback shares add up to the warming')
})

test('CO2: the budget closes, the coupling graph and the key answers', () => {
  const b = co2Budget(c)
  const get = key => b.rows.find(r => r.key === key).value
  assert.ok(Math.abs(get('fossil') + get('land_use') + get('land_sink') + get('ocean_sink') - get('atmosphere')) < 0.2, 'sources + sinks = what stays in the air')
  assert.ok(b.airborne > 0.3 && b.airborne < 0.6)
  const graph = couplingGraph(c)
  assert.ok(graph.nodes.length >= 8 && graph.edges.length >= 10)
  assert.ok(graph.edges.every(e => e.weight > 0 && e.links.length >= 1 && graph.nodes.includes(e.from) && graph.nodes.includes(e.to)))
  assert.match(edgePath(graph.edges[0]).d, /^M[\d.]+,[\d.]+ Q[\d.-]+,[\d.-]+ [\d.]+,[\d.]+$/)
  const answers = keyAnswers(c, f)
  assert.deepEqual(answers.map(a => a.key), ['warming', 'driver', 'future', 'co2', 'lead', 'warnings'])
  assert.ok(answers.every(a => a.value && a.detail !== undefined))
  assert.deepEqual(keyAnswers(null, null), [])
})

test('contours and colours', () => {
  const nx = 5, ny = 4, values = []
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) values.push(i)
  const segs = contourLines(values, nx, ny, 2.5)
  assert.equal(segs.length, ny - 1)
  assert.ok(segs.every(s => s.every(p => Math.abs(p[0] - 3) < 1e-9)), 'the 2.5 line runs between columns 2 and 3 (centres at 2.5 and 3.5)')
  assert.equal(precipColor(0.05), null)
  assert.match(precipColor(20), /^rgb/)
  assert.notEqual(temperatureColor(-5), temperatureColor(30))
  assert.equal(fluxColor(0.3), 'rgb(247,247,247)')
  assert.notEqual(fluxColor(500), fluxColor(-500))
  assert.equal(symbolInfo(99).label, 'cloudy')
})
