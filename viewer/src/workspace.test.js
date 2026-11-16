import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { WINDOW_COLOURS, MAX_WINDOWS, openWindow, raiseWindow, minimiseWindow, closeWindow, nextFocus, taskbarAction, linkArrow, edgePoint, clockParts } from './workspace.js'

test('windows: one per cell, a colour each, the least recently used closes beyond the maximum', () => {
  let list = [], z = 0, id = 0
  const open = cell => { const r = openWindow(list, cell, { id: id + 1, z: ++z }); if (r.id === id + 1) id++; list = r.windows; return r }
  open(10); open(20)
  assert.deepEqual(list.map(w => w.cell), [10, 20])
  assert.notEqual(list[0].colour, list[1].colour)
  const again = open(10)
  assert.equal(again.id, 1, 'the same cell brings back its window'); assert.equal(list.length, 2)
  assert.ok(list.find(w => w.id === 1).z > list.find(w => w.id === 2).z, 'and raises it')
  for (let c = 30; c < 30 + MAX_WINDOWS; c++) open(c)
  assert.equal(list.length, MAX_WINDOWS)
  assert.ok(!list.some(w => w.cell === 20), 'the least recently used window closed first')
  assert.ok(new Set(list.map(w => w.colour)).size === Math.min(MAX_WINDOWS, WINDOW_COLOURS.length), 'colours stay distinct')
})

test('focus, minimise, restore and the taskbar click', () => {
  let list = [{ id: 1, cell: 1, z: 1, minimised: false }, { id: 2, cell: 2, z: 3, minimised: false }, { id: 3, cell: 3, z: 2, minimised: false }]
  assert.equal(nextFocus(list, 2), 3, 'the top-most other open window')
  list = minimiseWindow(list, 3)
  assert.equal(nextFocus(list, 2), 1, 'minimised windows are skipped')
  assert.equal(taskbarAction(list[2], 2), 'restore')
  assert.equal(taskbarAction(list[1], 2), 'minimise')
  assert.equal(taskbarAction(list[0], 2), 'focus')
  list = raiseWindow(list, 3, 9)
  assert.equal(list[2].minimised, false); assert.equal(list[2].z, 9)
  assert.deepEqual(closeWindow(list, 2).map(w => w.id), [1, 3])
  assert.equal(nextFocus([{ id: 1, minimised: true, z: 1 }], 5), null)
})

test('the arrow from a window to its cell ends in a head just outside the marker', () => {
  const a = linkArrow({ x: 0, y: 100 }, { x: 200, y: 100 }, { ring: 7, head: 9 })
  assert.match(a.path, /^M0\.0,100\.0 Q100\.0,\d/)
  assert.ok(Math.abs(Math.hypot(a.tip.x - 200, a.tip.y - 100) - 7) < 1e-6, 'the tip stops at the ring')
  assert.equal(a.head.split(' ').length, 3)
  assert.equal(linkArrow({ x: 0, y: 0 }, { x: 5, y: 5 }), null, 'too short to draw')
  assert.deepEqual(edgePoint({ x: 10, y: 10, w: 100, h: 50 }, { x: 300, y: 30 }), { x: 110, y: 30, inside: false })
  assert.equal(edgePoint({ x: 10, y: 10, w: 100, h: 50 }, { x: 50, y: 30 }).inside, true)
})

test('the clock shows local time with seconds, the date, and UTC', () => {
  const d = new Date(2026, 10, 6, 9, 5, 7)
  const c = clockParts(d)
  assert.equal(c.time, '09:05'); assert.equal(c.seconds, '07'); assert.equal(c.date, 'Fri 6 Nov')
  assert.match(c.utc, /^\d\d:\d\d UTC · 2026-11-0[67]$/)
})

test('the app runs a workspace: windows per point, a taskbar, a clock in both top bars', () => {
  const app = readFileSync(new URL('./App.vue', import.meta.url), 'utf8')
  const overlay = readFileSync(new URL('./InspectOverlay.vue', import.meta.url), 'utf8')
  assert.match(app, /<template v-for="v in windowViews" :key="v\.w\.id"><InspectOverlay/)
  assert.match(app, /<Taskbar v-if="immersive\|\|inspectWindows\.length"/)
  assert.match(app, /<WorkspaceClock class="header-clock"\/>/)
  assert.match(app, /<WorkspaceClock v-if="immersive" class="hud-clock"\/>/)
  assert.match(app, /watch\(snapshots,\(\)=>closeAllWindows\(\)\)/)
  assert.match(app, /taskbar:true/)
  assert.match(overlay, /emit\('minimise'\)/)
  assert.match(overlay, /linkArrow\(from, a\)/)
})

test('a new window goes beside its point and beside the windows already open', async () => {
  const { placeWindow } = await import('./workspace.js')
  const bounds = { left: 0, top: 0, right: 1200, bottom: 800 }
  const first = placeWindow({ anchor: { x: 300, y: 400 }, bounds, w: 360, h: 400 })
  assert.equal(first.x, 340, 'to the right of its point')
  const second = placeWindow({ anchor: { x: 320, y: 420 }, bounds, w: 360, h: 400, avoid: [{ ...first, w: 360, h: 400 }] })
  const overlapX = Math.max(0, Math.min(second.x + 360, first.x + 360) - Math.max(second.x, first.x)), overlapY = Math.max(0, Math.min(second.y + 400, first.y + 400) - Math.max(second.y, first.y))
  assert.ok(overlapX * overlapY < 360 * 400 * 0.5, 'it covers less than half of the first window')
  assert.ok(!(320 > second.x && 320 < second.x + 360 && 420 > second.y && 420 < second.y + 400), 'and not its own point')
  const edge = placeWindow({ anchor: { x: 1150, y: 400 }, bounds, w: 360, h: 400 })
  assert.ok(edge.x + 360 <= 1192, 'inside the bounds')
})
