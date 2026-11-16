import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { trackLines, windField, forecastGrid, projectToGrid } from './coupledEarth.js'
import { cloudForms, isobars, trackAt, galeRadius, levelMask, fluxArrows, globeDir, nextFrame } from './earthScene.js'

const fdoc = JSON.parse(readFileSync(new URL('../public/coupled-forecast/engine-timeline.json', import.meta.url), 'utf8')), f = fdoc.earth_forecast
const climate = JSON.parse(readFileSync(new URL('../public/coupled-climate/engine-timeline.json', import.meta.url), 'utf8')).earth_climate

test('cloud forms follow the forecast cover, rain and thunder chance', () => {
  const forms = cloudForms({ cover: [10, 30, 70, 90, 90, 60], precip: [0, 0, 0, 0, 5, 0], thunder: [0, 0, 0, 0, 0, 55] })
  assert.deepEqual(forms.map(c => c?.type ?? null), [null, 'cumulus', 'stratocumulus', 'stratocumulus', 'nimbostratus', 'cumulonimbus'])
  assert.ok(forms.filter(Boolean).every(c => c.top > c.base && c.fraction > 0 && c.fraction <= 1))
  const real = cloudForms({ cover: f.frames[4].cloud_pct, precip: f.frames[4].precip_mm, thunder: f.frames[4].p_thunder })
  assert.equal(real.length, f.frames[4].cloud_pct.length); assert.ok(real.some(Boolean), 'the forecast has clouds')
})

test('isobars from the forecast pressure, major lines every other level', () => {
  const g = forecastGrid(fdoc), lines = isobars(f.frames[0].pressure_hpa, g.nx, g.ny, 4)
  assert.ok(lines.length > 2 && lines.every(l => l.level % 4 === 0 && l.segments.length > 0))
  assert.ok(lines.every(l => l.major === (l.level % 8 === 0)))
})

test('the hurricane at any hour lies on its track, between the reported points', () => {
  const truth = trackLines(f).find(t => t.truth), p = truth.points
  assert.deepEqual(trackAt(p, p[1].hour), p[1])
  const mid = trackAt(p, (p[1].hour + p[2].hour) / 2)
  assert.ok(Math.abs(mid.lat - (p[1].lat + p[2].lat) / 2) < 1e-9 && mid.category === p[1].category)
  assert.equal(trackAt(p, p.at(-1).hour + 1), null); assert.equal(trackAt([], 3), null)
})

test('the gale radius comes from the forecast wind around the storm', () => {
  const g = forecastGrid(fdoc), truth = trackLines(f).find(t => t.truth), p = truth.points.find(q => q.wind >= 33)
  const frame = fdoc.frames.findIndex(fr => Math.round(fr.elapsed_seconds / 3600) === p.hour)
  assert.ok(frame >= 0, 'a wind frame at a hurricane hour')
  const at = projectToGrid(p.lat, p.lon, g), r = galeRadius(windField(fdoc, frame).speed, g.nx, g.ny, at.x, at.y)
  assert.ok(r > 0 && r <= 5, `gale radius ${r} cells`)
  assert.equal(galeRadius(new Array(100).fill(5), 10, 10, 5, 5), 0, 'no gale, no radius')
})

test('warnings, CO2 fluxes, globe directions and looping frames', () => {
  assert.deepEqual(Array.from(levelMask([0, 1, 2, 3], 2)), [0, 0, 1, 1])
  const net = climate.co2.net_t_km2_yr, nx = 360 / climate.map.cell_degrees[0], ny = 180 / climate.map.cell_degrees[1]
  const arrows = fluxArrows(net, nx, ny, { stride: 2, min: 20 })
  assert.ok(arrows.some(a => a.up) && arrows.some(a => !a.up), 'sources rise and sinks sink')
  assert.ok(arrows.every(a => a.strength > 0 && a.strength <= 1 && (a.up === net[a.i] > 0)))
  const n = globeDir(90, 0), e = globeDir(0, 90), z = globeDir(0, 0)
  assert.ok(Math.abs(n[1] - 1) < 1e-12 && Math.abs(e[0] - 1) < 1e-12 && Math.abs(z[2] - 1) < 1e-12)
  assert.equal(nextFrame(3, 4), 0); assert.equal(nextFrame(1, 4), 2); assert.equal(nextFrame(0, 0), 0)
})

test('ensembleStorm averages the members and ignores the truth run', async () => {
  const { ensembleStorm, nearestFrame } = await import('./earthScene.js')
  const tracks = [
    { truth: false, points: [{ hour: 0, lat: 20, lon: -60, wind: 30, category: 'tropical_storm' }, { hour: 6, lat: 22, lon: -62, wind: 40, category: 'category_1' }] },
    { truth: false, points: [{ hour: 0, lat: 22, lon: -62, wind: 34, category: 'category_1' }, { hour: 6, lat: 24, lon: -64, wind: 44, category: 'category_1' }] },
    { truth: true, points: [{ hour: 0, lat: 40, lon: 0, wind: 90, category: 'category_5' }, { hour: 6, lat: 40, lon: 0, wind: 90, category: 'category_5' }] },
  ]
  const s = ensembleStorm(tracks, 3)
  assert.equal(s.members, 2)
  assert.ok(Math.abs(s.lat - 22) < 1e-9 && Math.abs(s.lon + 62) < 1e-9 && Math.abs(s.wind - 37) < 1e-9)
  assert.equal(ensembleStorm(tracks, 6).category, 'category_1')
  assert.equal(ensembleStorm(tracks, 12), null)
  assert.equal(nearestFrame([0, 3, 6, 12, 24], 10), 3)
})

test('the Coupled Earth maps are drawn in 3D, animate and go fullscreen on their own', () => {
  const earth = readFileSync(new URL('./CoupledEarth.vue', import.meta.url), 'utf8')
  const scene = readFileSync(new URL('./EarthScene.vue', import.meta.url), 'utf8')
  const app = readFileSync(new URL('./App.vue', import.meta.url), 'utf8')
  assert.match(earth, /import EarthScene from '\.\/EarthScene\.vue'/)
  assert.match(earth, /<EarthScene v-if="sceneReady"/)
  assert.doesNotMatch(earth, /<canvas ref="canvas"/)
  assert.doesNotMatch(earth, /getContext\('2d'\)/)
  for (const v of ['weather', 'wind', 'severe', 'drivers']) assert.match(earth, new RegExp(`${v}: \\d+`), `a play step for ${v}`)
  assert.match(earth, /@toggle-play="togglePlay"/)
  assert.match(earth, /class="earth-scene-hud"/)
  // The scene recolours in place while playing and rebuilds the terrain only when the grid or relief changes.
  assert.match(scene, /watch\(\(\) => props\.colors, \(\) => schedule\('colours'\)\)/)
  assert.match(scene, /watch\(\(\) => props\.overlays, \(\) => schedule\('overlays'\)\)/)
  assert.match(scene, /requestFullscreen/)
  // The observatory neither renders while hidden nor throws the map out of fullscreen.
  assert.match(app, /if\(mode\.value!=='observatory'\)return;controls\.update\(\)/)
  assert.match(app, /document\.fullscreenElement===stage\.value\)syncNative\(\)/)
})

test('cloudDrawn thins the clouds by cover, deterministically', async () => {
  const { cloudDrawn } = await import('./earthScene.js')
  const share = cover => Array.from({ length: 2000 }, (_, i) => cloudDrawn(i, cover)).filter(Boolean).length / 2000
  assert.equal(share(30), 0)
  assert.ok(share(60) > 0.35 && share(60) < 0.65, String(share(60)))
  assert.equal(share(95), 1)
  assert.equal(cloudDrawn(17, 70), cloudDrawn(17, 70))
})
