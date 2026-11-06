// Fullscreen ("immersive") mode of the observatory scene: the keyboard shortcuts and the HUD geometry.
// In fullscreen the WebGL scene fills the screen, the control panel floats over its left side and the data stream over
// its right side. The numbers here are the single source of truth for the CSS insets and for the camera offset that
// keeps the scene centred in the part of the screen the panels leave free.

export const SHORTCUTS = [
  { keys: ['F'], label: 'Enter or leave fullscreen', scope: 'always' },
  { keys: ['Esc'], label: 'Leave fullscreen', scope: 'fullscreen' },
  { keys: ['['], label: 'Show or hide the control panel', scope: 'fullscreen' },
  { keys: [']'], label: 'Show or hide the data stream', scope: 'fullscreen' },
  { keys: ['Space'], label: 'Play or pause the timeline', scope: 'fullscreen' },
  { keys: ['←', '→'], label: 'Previous or next frame', scope: 'fullscreen' },
  { keys: ['R'], label: 'Reset the camera', scope: 'fullscreen' },
  { keys: ['?'], label: 'Show or hide this list', scope: 'fullscreen' },
]

const TEXT_INPUTS = new Set(['text', 'search', 'email', 'number', 'password', 'tel', 'url', 'date', 'datetime-local', 'month', 'time', 'week'])

// Typing targets swallow every shortcut: a letter typed into a form field is text, not a command.
export function isEditable(target) {
  if (!target) return false
  if (target.isContentEditable) return true
  const tag = String(target.tagName ?? '').toUpperCase()
  if (tag === 'TEXTAREA' || tag === 'SELECT') return true
  return tag === 'INPUT' && TEXT_INPUTS.has(String(target.type ?? 'text').toLowerCase())
}

// Focused controls own Space and the arrow keys (a button clicks on Space, a slider moves on the arrows).
export function isInteractive(target) {
  if (!target) return false
  if (isEditable(target)) return true
  return ['BUTTON', 'INPUT', 'SUMMARY', 'A'].includes(String(target.tagName ?? '').toUpperCase())
}

// The action of a keydown, or null. Only F works outside fullscreen; everything else is a fullscreen shortcut so that the
// normal page keeps its scrolling and its native keyboard behaviour.
export function shortcutAction(event, { immersive = false } = {}) {
  if (!event || event.defaultPrevented || event.isComposing) return null
  if (event.ctrlKey || event.metaKey || event.altKey) return null
  const target = event.target
  if (isEditable(target)) return null
  const key = event.key
  const repeatable = key === 'ArrowLeft' || key === 'ArrowRight'
  if (event.repeat && !repeatable) return null
  if (key === 'f' || key === 'F') return 'toggle'
  if (!immersive) return null
  if (key === 'Escape') return 'exit'
  if (key === '[') return 'controls'
  if (key === ']') return 'stream'
  if (key === '?') return 'help'
  if (key === 'r' || key === 'R') return 'reset'
  if (isInteractive(target)) return null
  if (key === ' ' || key === 'Spacebar') return 'play'
  if (key === 'ArrowLeft') return 'previous'
  if (key === 'ArrowRight') return 'next'
  return null
}

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v))

// The HUD frame for a screen of width x height. Wide screens reserve room for the open panels (the scene is framed
// between them); narrow screens let the panels slide over the scene like drawers, so nothing is reserved for them.
export function hudLayout({ width = 1600, height = 900, controls = true, stream = true, timeline = false } = {}) {
  const compact = width < 760
  const gap = compact ? 10 : 14
  const bar = compact ? 52 : 60
  const panelLeft = Math.round(clamp(width * 0.21, 272, 340))
  const panelRight = Math.round(clamp(width * 0.2, 260, 330))
  const docked = width >= 1100
  const timelineHeight = timeline ? 74 : 0
  const top = bar + gap * 2
  const left = docked && controls ? panelLeft + gap : 0
  const right = docked && stream ? panelRight + gap : 0
  const bottom = timeline ? timelineHeight + gap * 2 : 0
  return { width, height, gap, bar, panelLeft, panelRight, docked, compact, timelineHeight, top, left, right, bottom }
}

// Panels shown when fullscreen opens: both on wide screens, only the controls on medium screens, none on phones.
export function defaultPanels(width) {
  return { controls: width >= 760, stream: width >= 1100 }
}

// The camera's view offset in pixels that moves the scene's centre to the centre of the free area (positive x: right,
// positive y: down). setViewOffset takes the negated values.
export function sceneOffset(layout) {
  if (!layout) return { x: 0, y: 0 }
  return { x: (layout.left - layout.right) / 2, y: (layout.top - layout.bottom) / 2 }
}

// Camera zoom that frames the scene in the free area as the page frames it in its viewport (about 1.8:1): the scene is
// scaled to the largest box of that shape that fits between the panels, kept within [min, max] so that it never gets
// tiny on small screens nor blown up on large ones. Closing the panels frees room, so the scene grows.
export function fitZoom(layout, { aspect = 1.8, min = 0.75, max = 1 } = {}) {
  if (!layout || !(layout.height > 0)) return 1
  const freeWidth = layout.width - layout.left - layout.right, freeHeight = layout.height - layout.top - layout.bottom
  const fit = Math.min(freeHeight, freeWidth / aspect) / layout.height
  return clamp(fit, min, max)
}

// Exponential approach of current towards target over dt seconds (time constant tau): frame-rate independent easing.
export function approach(current, target, dt, tau = 0.12, snap = 0.05) {
  if (!(dt > 0)) return current
  const next = target + (current - target) * Math.exp(-dt / tau)
  return Math.abs(next - target) < snap ? target : next
}

// CSS custom properties for the stage element, in pixels.
export function hudStyle(layout) {
  const px = v => `${Math.round(v)}px`
  return {
    '--hud-gap': px(layout.gap), '--hud-bar': px(layout.bar), '--hud-panel-left': px(layout.panelLeft), '--hud-panel-right': px(layout.panelRight),
    '--hud-top': px(layout.top), '--hud-left': px(layout.left), '--hud-right': px(layout.right), '--hud-bottom': px(layout.bottom), '--hud-timeline': px(layout.timelineHeight),
  }
}
