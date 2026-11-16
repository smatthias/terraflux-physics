import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { validateSequence } from './data.js'
import { tornadoWatch, maskOutline, supercells, hurricaneVisual, pathSince, alongPath, watchBulletin, supercellSpin, SUPERCELL_HELICITY, GALE_M_S } from './stormWatch.js'

const fixture = name => validateSequence(JSON.parse(readFileSync(new URL(`../public/${name}/engine-timeline.json`, import.meta.url), 'utf8')))
const supercell = fixture('supercell'), hurricane = fixture('hurricane')

test('the supercell fixture is engine output of the tornado-outbreak request, hourly', () => {
  assert.equal(supercell.length, 25)
  assert.ok(supercell.every(f => f.severe && f.clouds), 'severe convection and clouds in every frame')
  const request = JSON.parse(readFileSync(new URL('../../examples/supercell-outbreak-request.json', import.meta.url), 'utf8'))
  const outbreak = JSON.parse(readFileSync(new URL('../../examples/tornado-outbreak-request.json', import.meta.url), 'utf8'))
  assert.equal(request.snapshot_every_steps * request.time.timestep_seconds, 3600, 'hourly snapshots')
  assert.deepEqual({ ...request, snapshot_every_steps: 0 }, { ...outbreak, snapshot_every_steps: 0 }, 'otherwise the same request as the tested outbreak')
})

test("tornado watches cover the engine's tornado-possible cells and their favourable surroundings, nothing else", () => {
  const { nx, ny } = supercell[0].forecast.grid
  let watched = 0
  for (const f of supercell) {
    const w = tornadoWatch(f.severe, nx, ny)
    for (let i = 0; i < nx * ny; i++) {
      if (f.severe.possible[i]) assert.equal(w.watch[i], 1, 'every possible cell is watched')
      if (w.watch[i] && !f.severe.possible[i]) assert.ok(['moderate', 'significant'].includes(f.severe.risk[i]), 'surroundings only where the environment is favourable')
    }
    if (!f.severe.possibleCells) assert.equal(w.cells, 0, 'no tornado possible, no watch')
    assert.equal(w.regions.reduce((a, r) => a + r.cells.length, 0), w.cells)
    for (const r of w.regions) assert.ok(r.possible > 0 && r.maxStp === Math.max(...r.cells.map(c => f.severe.stp[c])))
    watched += w.cells
  }
  assert.ok(watched > 0, 'the outbreak has watches')
  assert.equal(tornadoWatch(supercell[20].severe, nx, ny).cells, 0, 'quiet in the evening of the run')
})

test('outlines run along the edges between watched and other cells', () => {
  const mask = Uint8Array.from([0, 0, 0, 0, 1, 1, 0, 0, 0], v => v)   // 3 x 3, two cells in the middle row
  const seg = maskOutline(mask, 3, 3)
  assert.equal(seg.length, 6, 'a 2 x 1 block has six unit edges')
  assert.deepEqual(maskOutline(new Uint8Array(4), 2, 2), [])
})

test('supercells: cumulonimbus, cap broken, strong helicity, moving with the storm motion', () => {
  const peak = supercell[3], cells = supercells(peak)
  assert.ok(cells.length > 0)
  for (const s of cells) {
    assert.equal(peak.clouds.type[s.i], 'cumulonimbus'); assert.ok(peak.severe.capBroken[s.i]); assert.ok(Math.abs(s.helicity) >= SUPERCELL_HELICITY)
    assert.equal(s.motionEast, peak.severe.motionEast[s.i]); assert.ok(s.speed > 0)
  }
  assert.ok(cells.every((s, k) => k === 0 || s.stp <= cells[k - 1].stp), 'strongest first')
  assert.ok(cells.every(s => s.cyclonic), 'northern hemisphere: positive helicity, cyclonic rotation')
  assert.deepEqual(supercells(supercell[15]), [], 'none after the outbreak')
  assert.ok(supercellSpin(250) > supercellSpin(120), 'more helicity, faster rotation')
})

test("hurricanes: spin the northern way, a core and a gale radius from the engine's wind field", () => {
  const f = hurricane[1], g = f.forecast.grid, storm = f.storms.list[0]
  const h = hurricaneVisual(storm, f.storms.env.storm_wind_speed_m_s, g, { northern: true, others: f.storms.list })
  assert.ok(h.spin > 0, 'counter-clockwise seen from above in the north')
  assert.ok(hurricaneVisual(storm, null, g, { northern: false }).spin < 0)
  assert.ok(h.gale > h.core && h.gale <= storm.radius_max_wind_m * 12 && h.eye < h.core)
  assert.equal(h.east, storm.east_m); assert.equal(h.category, storm.category)
  // The gale radius is where the engine's wind still reaches gale.
  const far = f.storms.env.storm_wind_speed_m_s.map((w, i) => ({ w, r: Math.hypot(g.origin_east_m + (i % g.nx + 0.5) * g.spacing_east_m - storm.east_m, g.origin_north_m + (Math.floor(i / g.nx) + 0.5) * g.spacing_north_m - storm.north_m) })).filter(c => c.w >= GALE_M_S)
  if (far.length) assert.ok(Math.abs(h.gale - Math.min(Math.max(storm.radius_max_wind_m * 2, ...far.map(c => c.r)), storm.radius_max_wind_m * 12)) < 1e-6)
})

test('between frames a hurricane glides along its computed track', () => {
  const before = hurricane[0].storms.list.find(s => s.id === 1), after = hurricane[1].storms.list.find(s => s.id === 1)
  const path = pathSince(after.track, [before.east_m, before.north_m])
  assert.ok(path.length > 10, 'the sub-steps between the two frames')
  assert.deepEqual(path.at(-1), after.track.at(-1))
  assert.deepEqual(alongPath(path, 0), path[0]); assert.deepEqual(alongPath(path, 1), path.at(-1))
  const mid = alongPath([[0, 0], [10, 0], [10, 10]], 0.75)
  assert.ok(Math.abs(mid[0] - 10) < 1e-9 && Math.abs(mid[1] - 5) < 1e-9, 'by length along the path')
  assert.deepEqual(pathSince([[1, 2]], null), [[1, 2]]); assert.deepEqual(pathSince([], [0, 0]), [])
})

test("the bulletin gathers the frame's watch, supercells and hurricanes", () => {
  const { nx, ny } = supercell[0].forecast.grid
  const b = watchBulletin(supercell[3], nx, ny, 2500)
  assert.ok(b.tornado.cells > 0 && b.tornado.areaKm2 === b.tornado.cells * 2500 && b.supercells.length > 0 && b.hurricanes.length === 0)
  assert.ok(b.storms.length > 0 && b.storms.length <= b.supercells.length, 'the drawn storms, grouped from the supercell cells')
  const hb = watchBulletin(hurricane[1], hurricane[1].forecast.grid.nx, hurricane[1].forecast.grid.ny, 3600)
  assert.ok(hb.hurricanes.length >= 1)
})

test('the map of the watch: engine fields only, with an automatic choice by what the frame has', async () => {
  const { SEVERE_BASES, severeBase, severeValue, severeColour } = await import('./stormWatch.js')
  assert.equal(severeBase(supercell[3], 'auto'), 'risk'); assert.equal(severeBase(hurricane[1], 'auto'), 'stormwind')
  assert.equal(severeBase(supercell[3], 'stormwind'), 'risk', 'a choice the frame cannot show falls back')
  assert.equal(severeBase(supercell[3], 'cape'), 'cape')
  const i = supercells(supercell[3])[0].i
  assert.equal(severeValue(supercell[3], 'stp', i), supercell[3].severe.stp[i])
  assert.equal(severeValue(supercell[3], 'helicity', i), Math.abs(supercell[3].severe.helicity[i]))
  assert.equal(severeValue(hurricane[1], 'stormwind', 0), hurricane[1].storms.env.storm_wind_speed_m_s[0])
  assert.equal(severeColour('stp', 0), '#16242d'); assert.equal(severeColour('stp', 99), '#c724b1'); assert.equal(severeColour('cap', 1e9), '#a8d8ff')
  assert.equal(severeColour('risk', 1), null); assert.equal(severeColour('stp', null), null)
  assert.ok(SEVERE_BASES.every(b => b.label))
})

test('discrete supercells: spaced storm centres in order of the tornado parameter, every supercell cell in one', async () => {
  const { discreteSupercells } = await import('./stormWatch.js')
  const { nx, ny } = supercell[0].forecast.grid
  for (const f of supercell) {
    const cells = supercells(f), storms = discreteSupercells(f, nx, ny)
    assert.equal(storms.reduce((a, s) => a + s.members.length, 0), cells.length, 'each supercell cell in exactly one storm')
    assert.equal(new Set(storms.flatMap(s => s.members)).size, cells.length)
    for (const [k, s] of storms.entries()) {
      assert.ok(s.members.includes(s.i) && s.maxStp === Math.max(...s.members.map(m => f.severe.stp[m])))
      for (const o of storms.slice(0, k)) assert.ok(Math.max(Math.abs(o.i % nx - s.i % nx), Math.abs(Math.floor(o.i / nx) - Math.floor(s.i / nx))) >= 2, 'centres at least two cells apart')
      assert.equal(s.possible, s.members.some(m => f.severe.possible[m]))
    }
    if (cells.length > 4) assert.ok(storms.length < cells.length, 'neighbouring cells merge into storms')
  }
  const peak = discreteSupercells(supercell[3], nx, ny)
  assert.ok(peak.length >= 3 && peak.some(s => s.possible), 'an outbreak of several storms, some tornado-possible')
  assert.ok(peak.every(s => s.members.length <= 9), 'no storm spans the whole cluster')
})

test('the absence of a watch is explained from the engine fields', async () => {
  const { noWatchReason } = await import('./stormWatch.js')
  assert.ok(noWatchReason(hurricane[2].severe).includes('too little low-level rotation'), 'the tropics: uncapped and unstable, but little helicity')
  assert.ok(noWatchReason(supercell[20].severe).includes('capping inversion holds'), 'the evening of the outbreak: capped again')
  const broken = { ...supercell[20].severe, capBrokenCells: 3 }
  assert.ok(noWatchReason(broken).startsWith('the cap is broken in 3 cells'))
})
