import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { SHORTCUTS, isEditable, isInteractive, shortcutAction, hudLayout, defaultPanels, sceneOffset, fitZoom, approach, hudStyle } from './immersive.js'

const here = dirname(fileURLToPath(import.meta.url))
const app = readFileSync(join(here, 'App.vue'), 'utf8')
const key = (k, extra = {}) => ({ key: k, target: { tagName: 'BODY' }, ...extra })

test('F toggles fullscreen everywhere; the other shortcuts only act in fullscreen', () => {
  assert.equal(shortcutAction(key('f')), 'toggle')
  assert.equal(shortcutAction(key('F')), 'toggle')
  for (const k of ['Escape', '[', ']', '?', 'r', ' ', 'ArrowLeft', 'ArrowRight']) assert.equal(shortcutAction(key(k)), null, `${k} is inert outside fullscreen`)
  const on = { immersive: true }
  assert.equal(shortcutAction(key('f'), on), 'toggle')
  assert.equal(shortcutAction(key('Escape'), on), 'exit')
  assert.equal(shortcutAction(key('['), on), 'controls')
  assert.equal(shortcutAction(key(']'), on), 'stream')
  assert.equal(shortcutAction(key('?'), on), 'help')
  assert.equal(shortcutAction(key('R'), on), 'reset')
  assert.equal(shortcutAction(key(' '), on), 'play')
  assert.equal(shortcutAction(key('ArrowLeft'), on), 'previous')
  assert.equal(shortcutAction(key('ArrowRight'), on), 'next')
  assert.equal(shortcutAction(key('x'), on), null)
})

test('shortcuts never fire while typing, with modifiers, on key repeat or when already handled', () => {
  const on = { immersive: true }
  for (const target of [{ tagName: 'INPUT', type: 'text' }, { tagName: 'INPUT', type: 'number' }, { tagName: 'TEXTAREA' }, { tagName: 'SELECT' }, { tagName: 'DIV', isContentEditable: true }]) {
    assert.equal(isEditable(target), true)
    assert.equal(shortcutAction(key('f', { target }), on), null, `${target.tagName} swallows F`)
    assert.equal(shortcutAction(key('Escape', { target }), on), null)
  }
  assert.equal(shortcutAction(key('f', { ctrlKey: true })), null, 'Ctrl+F stays the browser search')
  assert.equal(shortcutAction(key('f', { metaKey: true })), null)
  assert.equal(shortcutAction(key('f', { altKey: true })), null)
  assert.equal(shortcutAction(key('f', { repeat: true })), null, 'holding F does not flicker')
  assert.equal(shortcutAction(key('ArrowRight', { repeat: true }), on), 'next', 'holding an arrow scrubs')
  assert.equal(shortcutAction(key('f', { defaultPrevented: true })), null)
  assert.equal(shortcutAction(key('f', { isComposing: true })), null)
  assert.equal(shortcutAction(null), null)
})

test('focused controls keep Space and the arrows; checkboxes and sliders still allow F and Esc', () => {
  const on = { immersive: true }
  for (const target of [{ tagName: 'BUTTON' }, { tagName: 'INPUT', type: 'range' }, { tagName: 'INPUT', type: 'checkbox' }, { tagName: 'SUMMARY' }, { tagName: 'A' }]) {
    assert.equal(isEditable(target), false)
    assert.equal(isInteractive(target), true)
    assert.equal(shortcutAction(key(' ', { target }), on), null)
    assert.equal(shortcutAction(key('ArrowLeft', { target }), on), null)
    assert.equal(shortcutAction(key('f', { target }), on), 'toggle')
    assert.equal(shortcutAction(key('Escape', { target }), on), 'exit')
  }
  assert.equal(isInteractive(null), false)
  assert.equal(isEditable(undefined), false)
})

test('every documented shortcut has a label, and F is the only global one', () => {
  assert.ok(SHORTCUTS.every(s => s.keys.length && s.label.length > 8))
  assert.deepEqual(SHORTCUTS.filter(s => s.scope === 'always').map(s => s.keys[0]), ['F'])
})

test('wide screens reserve the open panels; narrow screens overlay them as drawers', () => {
  const wide = hudLayout({ width: 1600, height: 900, controls: true, stream: true, timeline: true })
  assert.equal(wide.docked, true)
  assert.equal(wide.left, wide.panelLeft + wide.gap)
  assert.equal(wide.right, wide.panelRight + wide.gap)
  assert.equal(wide.top, wide.bar + 2 * wide.gap)
  assert.equal(wide.bottom, wide.timelineHeight + 2 * wide.gap)
  assert.ok(wide.panelLeft >= 272 && wide.panelLeft <= 340 && wide.panelRight >= 260 && wide.panelRight <= 330)
  const closed = hudLayout({ width: 1600, height: 900, controls: false, stream: false })
  assert.equal(closed.left, 0); assert.equal(closed.right, 0); assert.equal(closed.bottom, 0)
  const narrow = hudLayout({ width: 900, height: 700, controls: true, stream: true })
  assert.equal(narrow.docked, false)
  assert.equal(narrow.left, 0); assert.equal(narrow.right, 0)
  assert.equal(hudLayout({ width: 500 }).compact, true)
  assert.deepEqual(defaultPanels(1600), { controls: true, stream: true })
  assert.deepEqual(defaultPanels(900), { controls: true, stream: false })
  assert.deepEqual(defaultPanels(500), { controls: false, stream: false })
})

test('the camera offset centres the scene in the free area and eases there frame-rate independently', () => {
  const both = hudLayout({ width: 1600, controls: true, stream: true })
  const leftOnly = hudLayout({ width: 1600, controls: true, stream: false })
  assert.equal(sceneOffset(both).x, (both.left - both.right) / 2)
  assert.ok(sceneOffset(leftOnly).x > 0, 'only the control panel open: the scene moves right')
  assert.ok(sceneOffset(hudLayout({ width: 1600, controls: false, stream: true })).x < 0)
  assert.deepEqual(sceneOffset(null), { x: 0, y: 0 })
  // Two half steps equal one full step, so the easing does not depend on the frame rate.
  const once = approach(0, 100, 0.1), twice = approach(approach(0, 100, 0.05), 100, 0.05)
  assert.ok(Math.abs(once - twice) < 1e-9)
  assert.ok(once > 0 && once < 100)
  assert.equal(approach(99.97, 100, 0.016), 100, 'snaps when within a twentieth of a pixel')
  assert.equal(approach(5, 100, 0), 5)
})

test('hudStyle exposes the layout as pixel custom properties', () => {
  const style = hudStyle(hudLayout({ width: 1600, controls: true, stream: false, timeline: true }))
  for (const k of ['--hud-gap', '--hud-bar', '--hud-top', '--hud-left', '--hud-right', '--hud-bottom', '--hud-panel-left', '--hud-panel-right', '--hud-timeline']) assert.match(style[k], /^\d+px$/, k)
  assert.equal(style['--hud-right'], '0px')
})

test('the app wires the shortcuts, the Fullscreen API and the HUD into the stage', () => {
  for (const needle of ["addEventListener('keydown'", "addEventListener('fullscreenchange'", 'requestFullscreen', 'exitFullscreen', 'setViewOffset', 'clearViewOffset', '<DataStream', 'ref="stage"', ':style="stageStyle"', ':colors="streamColors"', 'fieldColor(value)']) {
    assert.ok(app.includes(needle), `App.vue: ${needle}`)
  }
  assert.ok(app.includes("removeEventListener('keydown'") && app.includes("removeEventListener('fullscreenchange'"), 'listeners are removed on unmount')
})

test('the zoom frames the scene between the panels and grows when they close', () => {
  const open = fitZoom(hudLayout({ width: 1600, height: 900, controls: true, stream: true, timeline: true }))
  const closed = fitZoom(hudLayout({ width: 1600, height: 900, controls: false, stream: false, timeline: true }))
  assert.ok(open >= 0.75 && open <= 1 && closed <= 1)
  assert.ok(closed > open, 'closing both panels enlarges the scene')
  assert.equal(fitZoom(hudLayout({ width: 700, height: 900 })), 0.75, 'a tall narrow screen hits the floor')
  assert.equal(fitZoom(null), 1)
})
