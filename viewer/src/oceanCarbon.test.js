import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { validateSequence } from './data.js'
import { summarizeCarbon, carbonSeries, carbonRunSummary, carbonColumn, oxygenSection, deadZonePatches, worstCell, carbonBudget, habitatInfo,
  oxygenLevelColor, ppmColor, fluxColor, phColor, phRange, omegaColor, productionColor, nitrateColor, HABITAT_CLASSES, CARBON_BASES, MT_PER_MOL } from './oceanCarbon.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const fixture = name => JSON.parse(readFileSync(join(root, 'viewer', 'public', name, 'engine-timeline.json'), 'utf8'))
const today = validateSequence(fixture('ocean-carbon'))
const future = validateSequence(fixture('ocean-carbon-2100'))
const july = frames => frames.find(f => f.carbon.year >= Math.floor(f.carbon.year) + 0.5 && f.carbon.year < Math.floor(frames[0].carbon.year) + 0.6) ?? frames[7]

test('carbon fixtures validate: a year of monthly frames with the air, the sea and its habitat', () => {
  for (const frames of [today, future]) {
    assert.equal(frames.length, 13)
    const c = frames[0].carbon
    assert.ok(c && frames.every(f => f.carbon && f.ocean), 'every frame carries the ocean and its carbon')
    assert.equal(c.levels.length, 12)
    assert.deepEqual(c.sources.map(s => s.name), ['Delta City', 'Bayou Refineries', 'Coastal Port', 'Peninsula City'])
    assert.deepEqual(c.rivers.map(r => r.name), ['Great Delta River', 'Western River'])
    assert.equal(c.rivers[0].chemistry.nitrate_umol_kg, 110)
    assert.ok(frames.every((f, k) => k === 0 || f.carbon.year > frames[k - 1].carbon.year))
  }
  assert.ok(Math.floor(today[0].carbon.year) === 2025 && Math.floor(future[0].carbon.year) === 2099)
})

test('the validator rejects broken budgets, misplaced dead zones and land chemistry', () => {
  const broken = [
    t => { t.ocean.frames[3].carbon.ocean.budget.air_sea_carbon_mol += 1e12 },
    t => { t.ocean.frames[2].carbon.ocean.dead_zone_cells.push(0) },
    t => { t.ocean.frames[1].carbon.ocean.surface_ph[0] = 8.1 },
    t => { t.ocean.frames[4].carbon.atmosphere.emitted_mt_co2 += 50 },
    t => { const o = t.ocean.frames[5].carbon.ocean; const i = o.dead_zone_cells[0] ?? t.ocean.ocean_mask.indexOf(true); o.column_class[i] = 0 },
    t => { t.ocean.carbon.sources[0].mt_co2_per_year *= 2 },
    t => { t.ocean.processes = t.ocean.processes.filter(p => p !== 'atmosphere_co2') },
  ]
  for (const [k, breakIt] of broken.entries()) {
    const t = fixture('ocean-carbon')
    breakIt(t)
    assert.throws(() => validateSequence(t), Error, `case ${k}`)
  }
})

test('the air: the background rises through the year; the sources raise their surroundings; the sea takes it up', () => {
  const first = summarizeCarbon(today[0]), last = summarizeCarbon(today.at(-1))
  assert.ok(last.background_ppm > first.background_ppm && first.background_ppm > 420 && first.background_ppm < 432)
  for (const f of today) {
    const m = summarizeCarbon(f)
    assert.ok(m.max_excess_ppm > 1, 'the sources stand out above the background')
    assert.equal(m.emissions_mt_yr, 180)
  }
  const refinery = today[0].carbon.sources.find(s => s.name === 'Bayou Refineries')
  const ppm = today.at(-1).carbon.ppm, sorted = [...ppm].sort((a, b) => b - a)
  assert.ok(ppm[refinery.peak_cell] >= sorted[Math.floor(ppm.length * 0.05)], 'the refinery belt is among the highest 5 % of the map')
  const f = future[0].carbon
  assert.ok(f.background > 1000 && summarizeCarbon(future[0]).uptake_mt_co2_yr > 0, 'under 1000 ppm the sea takes up CO2')
})

test('acidification: 2100 is about 0.3 pH units more acid with half the aragonite saturation margin', () => {
  const a = carbonRunSummary(carbonSeries(today)), b = carbonRunSummary(carbonSeries(future))
  assert.ok(a.mean_ph > 8.0 && a.mean_ph < 8.1, `today pH ${a.mean_ph}`)
  assert.ok(b.mean_ph < a.mean_ph - 0.2 && b.mean_ph > 7.6, `2100 pH ${b.mean_ph}`)
  assert.ok(b.mean_omega < a.mean_omega - 0.8 && b.mean_omega > 1.5)
  assert.ok(b.mean_uptake_mt_co2_yr > a.mean_uptake_mt_co2_yr + 10, 'a rising atmosphere drives more uptake')
  assert.ok(b.max_dead_zone_km2 > a.max_dead_zone_km2, 'a warmer sea holds less oxygen: the dead zone grows')
  assert.ok(b.max_corrosive_km2 >= a.max_corrosive_km2)
})

test('dead zones are seasonal and sit on the shelf; the worst lies off the river mouths', () => {
  const series = carbonSeries(today)
  const winter = series[0].dead_zone_km2, summer = Math.max(...series.map(p => p.dead_zone_km2))
  assert.ok(summer > 20000 && summer > 5 * Math.max(winter, 1000), `a summer dead zone (${summer} km² vs ${winter} in January)`)
  const peak = today[series.findIndex(p => p.dead_zone_km2 === summer)]
  const patches = deadZonePatches(peak)
  assert.ok(patches.length >= 1 && patches.every((p, k) => k === 0 || p.area_km2 <= patches[k - 1].area_km2))
  const cells = patches.flatMap(p => p.cells)
  assert.equal(cells.length, peak.carbon.deadCells.length)
  assert.ok(cells.every(i => peak.carbon.seafloor[i] < 150), 'dead zones form on the shelf, not over the deep basin')
  assert.ok(patches.some(p => p.nearest_river === 'Great Delta River' && p.river_distance_km < 120), 'one lies off the delta')
  assert.equal(patches[0].nearest_river, 'Great Delta River', 'the largest is fed by the delta river (downstream, along the coastal current)')
  const m = summarizeCarbon(peak)
  assert.equal(m.dead_zone_km2, peak.carbon.totals.dead_zone_area_km2)
  assert.ok(m.min_bottom_oxygen_umol_kg < peak.carbon.habitat.hypoxia_umol_kg)
  assert.ok(m.habitable_volume_pct > 90 && m.habitable_volume_pct < 100, 'most of the volume stays habitable')
})

test('water column: levels from the surface to the seafloor with the habitat of each', () => {
  const cell = worstCell(today)
  assert.ok(cell !== null && today[0].carbon.mask[cell])
  const peak = today.reduce((best, f) => f.carbon.o2Bottom[cell] < best.carbon.o2Bottom[cell] ? f : best, today[0])
  const p = carbonColumn(peak, cell)
  assert.ok(p.shelf && p.levels.length >= 2 && p.levels[0].top === 0)
  assert.ok(Math.abs(p.bottom_m - p.seafloor_m) < 1e-9, 'a shelf column reaches the seafloor')
  assert.ok(p.levels.every((l, k) => l.bottom > l.top && (k === 0 || l.top === p.levels[k - 1].bottom)))
  assert.ok(p.levels[0].oxygen_umol_kg > p.levels.at(-1).oxygen_umol_kg + 50, 'oxygen-rich at the surface, starved at the bottom')
  assert.ok(p.levels.at(-1).cls >= 2 && p.dead_zone && p.hypoxic_m > 0)
  assert.equal(carbonColumn(peak, peak.carbon.mask.indexOf(false)), null, 'no column on land')
  const deep = peak.carbon.mask.findIndex((m, i) => m && peak.carbon.seafloor[i] > 1000)
  const open = carbonColumn(peak, deep)
  assert.ok(!open.shelf && open.levels.length === 12 && open.bottom_m === 600)
  const section = oxygenSection(today, cell)
  assert.ok(section.rects.length >= 13 * 2 && section.max_depth_m === p.bottom_m && section.end > section.start)
})

test('budgets in Mt CO2 add up', () => {
  const b = carbonBudget(today.at(-1))
  assert.ok(Math.abs(b.ocean.reduce((s, x) => s + x.value, 0) - b.ocean_change) < 1e-6 * Math.abs(today.at(-1).carbon.budget.carbon_inventory_mol * MT_PER_MOL))
  assert.ok(Math.abs(b.air.reduce((s, x) => s + x.value, 0) - b.air_change) < 0.05)
  assert.ok(b.river_kt_n > 0 && b.ocean.find(x => x.key === 'river').value > 0)
})

test('colours', () => {
  const rgb = s => s.match(/\d+/g).map(Number)
  assert.ok(rgb(oxygenLevelColor(0))[0] < 60 && rgb(oxygenLevelColor(40))[0] > 180 && rgb(oxygenLevelColor(250))[2] > 150, 'black anoxic, red hypoxic, blue rich')
  assert.ok(rgb(ppmColor(10))[0] > rgb(ppmColor(10))[2] && rgb(ppmColor(-5))[1] > rgb(ppmColor(-5))[0])
  assert.ok(rgb(fluxColor(3))[2] > 150 && rgb(fluxColor(-3))[0] > 150, 'blue uptake, red outgassing')
  assert.ok(rgb(phColor(8.15))[2] > rgb(phColor(7.7))[2] && rgb(phColor(7.7))[0] > 150)
  assert.ok(rgb(omegaColor(0.7))[0] > 100 && rgb(omegaColor(0.7))[2] > 100 && rgb(omegaColor(4))[2] > 150, 'magenta corrosive, blue supersaturated')
  assert.ok(rgb(productionColor(3))[1] > rgb(productionColor(0))[1] && rgb(nitrateColor(40))[0] > rgb(nitrateColor(0))[0])
  const [lo, hi] = phRange(today), [lo2, hi2] = phRange(future)
  assert.ok(hi - lo >= 0.1 - 1e-12 && hi2 < lo, 'each run has its own pH range; 2100 lies below today')
  assert.equal(phColor(lo, lo, hi), phColor(7.6), 'the low end of the range takes the acid colour')
  assert.equal(HABITAT_CLASSES.length, 4)
  assert.equal(habitatInfo(-1).key, 'land')
  assert.ok(CARBON_BASES.every(b => b.key && b.label))
})
