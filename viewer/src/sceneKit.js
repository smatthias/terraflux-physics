// Small pieces shared by 3D scenes outside the observatory (the Coupled Earth maps): labels as sprites, a soft floor,
// the lights of the realistic look, and disposal. (The observatory keeps its own copies inside App.vue.)
import * as THREE from 'three'

export function glyphSprite(text, color, size = 1.1) {
  const canvas = document.createElement('canvas'); canvas.width = 96; canvas.height = 96
  const ctx = canvas.getContext('2d'); ctx.font = 'bold 64px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillStyle = color; ctx.fillText(text, 48, 52)
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(canvas), depthTest: false, transparent: true }))
  sprite.scale.set(size, size, 1)
  return sprite
}

export function textSprite(text, color, size = 1) {
  const font = 'bold 40px sans-serif', probe = document.createElement('canvas').getContext('2d')
  probe.font = font
  const w = Math.max(256, Math.ceil(probe.measureText(text).width) + 28), canvas = document.createElement('canvas')
  canvas.width = w; canvas.height = 64
  const ctx = canvas.getContext('2d'); ctx.font = font; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'
  ctx.shadowColor = 'rgba(4,12,18,.9)'; ctx.shadowBlur = 8; ctx.fillStyle = color; ctx.fillText(text, w / 2, 34)
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(canvas), depthTest: false, transparent: true }))
  sprite.scale.set(size * 4 * w / 256, size, 1)
  return sprite
}

export function softFloor(radius = 48) {
  const canvas = document.createElement('canvas'); canvas.width = canvas.height = 256
  const ctx = canvas.getContext('2d'), grad = ctx.createRadialGradient(128, 128, 0, 128, 128, 128)
  grad.addColorStop(0, 'rgba(64,120,140,.34)'); grad.addColorStop(0.45, 'rgba(30,66,82,.18)'); grad.addColorStop(1, 'rgba(9,21,29,0)')
  ctx.fillStyle = grad; ctx.fillRect(0, 0, 256, 256)
  const floor = new THREE.Mesh(new THREE.CircleGeometry(radius, 96), new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(canvas), transparent: true, depthWrite: false }))
  floor.rotation.x = -Math.PI / 2; floor.position.y = -0.06; floor.renderOrder = -1
  return floor
}

// Ambient, sky and rim light plus a sun that can cast soft shadows (the observatory's realistic-terrain lighting).
export function sceneLights(scene) {
  scene.add(new THREE.AmbientLight(0xd6efff, 1.3))
  scene.add(new THREE.HemisphereLight(0xdff3ff, 0x0b1820, 0.25))
  const rim = new THREE.DirectionalLight(0x7fe0ff, 0.35); rim.position.set(-22, 14, -26); scene.add(rim)
  const sun = new THREE.DirectionalLight(0xffffff, 2.6); sun.position.set(-30, 19, 10)
  const sh = sun.shadow; sh.mapSize.set(2048, 2048); Object.assign(sh.camera, { left: -26, right: 26, top: 26, bottom: -26, near: 1, far: 110 }); sh.bias = -0.0004; sh.normalBias = 0.035; sh.radius = 3
  scene.add(sun); scene.add(sun.target)
  return { sun }
}

export function disposeTree(object) {
  object?.traverse(o => {
    o.geometry?.dispose()
    if (o.material) { for (const m of [].concat(o.material)) { m.map?.dispose(); m.dispose() } }
    if (o.isInstancedMesh) o.dispose()
  })
}

// A crisp text label exactly as wide as its text, `height` scene units tall. align: 'left' anchors the left edge at
// the position, 'right' the right edge, 'center' the middle.
export function labelSprite(text, color, height = 0.3, { align = 'left', weight = 600, background = null } = {}) {
  const px = 44, font = `${weight} ${px}px "DM Sans", "Helvetica Neue", Arial, sans-serif`, probe = document.createElement('canvas').getContext('2d')
  probe.font = font
  const pad = background ? 14 : 6, w = Math.ceil(probe.measureText(text).width) + pad * 2, h = Math.round(px * 1.45)
  const canvas = document.createElement('canvas'); canvas.width = w; canvas.height = h
  const ctx = canvas.getContext('2d')
  if (background) { ctx.fillStyle = background; ctx.beginPath(); ctx.roundRect?.(0, 0, w, h, h / 3); ctx.fill() }
  ctx.font = font; ctx.textBaseline = 'middle'; ctx.shadowColor = 'rgba(4,10,16,.95)'; ctx.shadowBlur = background ? 0 : 8
  ctx.fillStyle = color; ctx.fillText(text, pad, h / 2 + 2)
  const texture = new THREE.CanvasTexture(canvas); texture.colorSpace = THREE.SRGBColorSpace; texture.anisotropy = 4
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, depthTest: false, transparent: true }))
  sprite.scale.set(height * w / h, height, 1)
  sprite.center.set(align === 'left' ? 0 : align === 'right' ? 1 : 0.5, 0.5)
  sprite.renderOrder = 10
  return sprite
}
