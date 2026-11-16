// Wind barbs in the 3D scene: a whole layer of barbs as one mesh, lying flat at its altitude (scene frame: east +x,
// north -z, up +y). The staff and the barbs are thin ribbons (WebGL lines are one pixel wide), the pennants filled
// triangles, each barb coloured by its speed. Geometry from barbs.js.
import * as THREE from 'three'
import { barbGlyph, barbColour, BARB } from './barbs.js'

// items: [{ x, z, kt, fromDeg, colour?, y? }] (scene units; an item's y overrides the layer's height y, so a surface
// layer can follow the relief); length: staff length (scene units);
// width: ribbon width (scene units). Returns a THREE.Mesh (userData.count: the barbs drawn).
export function barbLayer(items, { y = 0, length = 1, width = 0.05, northernHemisphere = true, opacity = 1 } = {}) {
  const positions = [], colours = [], c = new THREE.Color()
  let h = y
  const push = (x, z, col) => { positions.push(x, h, z); colours.push(col.r, col.g, col.b) }
  // Map (east, north) of the glyph to the scene: x = east, z = -north.
  const ribbon = (ax, az, bx, bz, col) => {
    const dx = bx - ax, dz = bz - az, l = Math.hypot(dx, dz) || 1, px = -dz / l * width / 2, pz = dx / l * width / 2
    push(ax + px, az + pz, col); push(bx + px, bz + pz, col); push(bx - px, bz - pz, col)
    push(ax + px, az + pz, col); push(bx - px, bz - pz, col); push(ax - px, az - pz, col)
  }
  for (const it of items) {
    c.set(it.colour ?? barbColour(it.kt)); h = it.y ?? y
    const g = barbGlyph(it.kt, it.fromDeg, { northernHemisphere })
    if (g.calm) {
      // A small ring for calm.
      const r = BARB.calmRadius * length, steps = 12
      for (let k = 0; k < steps; k++) {
        const a0 = k / steps * Math.PI * 2, a1 = (k + 1) / steps * Math.PI * 2
        ribbon(it.x + r * Math.cos(a0), it.z + r * Math.sin(a0), it.x + r * Math.cos(a1), it.z + r * Math.sin(a1), c)
      }
      continue
    }
    for (const [e0, n0, e1, n1] of g.lines) ribbon(it.x + e0 * length, it.z - n0 * length, it.x + e1 * length, it.z - n1 * length, c)
    for (const t of g.triangles) for (let k = 0; k < 6; k += 2) push(it.x + t[k] * length, it.z - t[k + 1] * length, c)
  }
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3))
  geometry.setAttribute('color', new THREE.Float32BufferAttribute(colours, 3))
  const mesh = new THREE.Mesh(geometry, new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide, transparent: opacity < 1, opacity, depthWrite: opacity >= 1 }))
  mesh.userData.count = items.length
  mesh.name = 'barb-layer'
  return mesh
}

// A translucent sheet marking a flight level over the map, with a thin outline.
export function levelSheet(width, depth, y, { colour = '#7fe0ff', opacity = 0.07 } = {}) {
  const group = new THREE.Group(); group.name = 'level-sheet'
  const sheet = new THREE.Mesh(new THREE.PlaneGeometry(width, depth), new THREE.MeshBasicMaterial({ color: colour, transparent: true, opacity, side: THREE.DoubleSide, depthWrite: false }))
  sheet.rotation.x = -Math.PI / 2; sheet.position.y = y; group.add(sheet)
  const w = width / 2, d = depth / 2
  const outline = new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints([[-w, -d], [w, -d], [w, d], [-w, d]].map(([x, z]) => new THREE.Vector3(x, y, z))), new THREE.LineBasicMaterial({ color: colour, transparent: true, opacity: 0.45 }))
  group.add(outline)
  return group
}
