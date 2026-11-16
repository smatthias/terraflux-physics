// The report stations in the 3D scene: a windsock with a runway for the flight weather report, a sailing boat for the
// sailing report. Each marker is a THREE.Group centred on its cell with userData.update(time) for its animation
// (the sock flutters, the boat rides the waves). Scene frame: east +x, north -z, up +y.
// No DOM: labels are added by the scene (App.vue), so these builders run in Node tests.
import * as THREE from 'three'

const rad = d => d * Math.PI / 180
// Yaw that turns a marker built with its bow along -z (north) to a heading (degrees true, clockwise from north).
export const headingYaw = deg => -rad(deg)
// Unit vector (scene x, z) of a compass bearing.
export const bearingVector = deg => [Math.sin(rad(deg)), -Math.cos(rad(deg))]

// How far a windsock droops below the horizontal (degrees) at a wind of kt knots: hanging straight down in calm air,
// fully extended (horizontal) from 15 kt, the standard full-extension speed of an aviation windsock.
export const sockDroop = kt => 88 * (1 - Math.min(1, Math.max(0, kt) / 15))

// The sheet angle of the sail (degrees off the centreline, to leeward): about half the apparent wind angle, between a
// close-hauled 8 degrees and a squared-out 85 degrees.
export const sheetAngle = awa => Math.min(85, Math.max(8, Math.abs(awa) / 2))

function ring(radius, colour, opacity = 0.85) {
  const mesh = new THREE.Mesh(new THREE.RingGeometry(radius * 0.82, radius, 48), new THREE.MeshBasicMaterial({ color: colour, transparent: true, opacity, side: THREE.DoubleSide, depthWrite: false }))
  mesh.rotation.x = -Math.PI / 2
  mesh.renderOrder = 3
  return mesh
}

// Windsock and runway. fromDeg: wind direction (from, true); kt: wind speed; runwayDeg: runway heading; colour: the
// flight-category colour (ring and mast band); size: scene units across.
export function windsockMarker({ fromDeg = 0, kt = 0, runwayDeg = 360, colour = '#3ddc84', size = 1.6 } = {}) {
  const group = new THREE.Group(); group.name = 'flight-station'
  group.add(ring(size * 0.62, colour))
  // The runway: a dark strip along the set heading with a dashed white centreline and threshold bars.
  // Lifted a little so that on sloping relief (the cell's centre height) the strip is not buried in the hillside.
  const runway = new THREE.Group(); runway.name = 'runway'; runway.rotation.y = headingYaw(runwayDeg); runway.position.y = size * 0.05
  const length = size * 1.5, width = size * 0.16
  const strip = new THREE.Mesh(new THREE.BoxGeometry(width, 0.025, length), new THREE.MeshStandardMaterial({ color: 0x2b3136, roughness: 0.9 }))
  strip.position.y = 0.0125; strip.receiveShadow = true; runway.add(strip)
  const paint = new THREE.MeshBasicMaterial({ color: 0xe8eef0 })
  for (let k = -3; k <= 3; k++) { const dash = new THREE.Mesh(new THREE.BoxGeometry(width * 0.08, 0.03, length * 0.07), paint); dash.position.set(0, 0.016, k * length * 0.12); runway.add(dash) }
  for (const end of [-1, 1]) { const bar = new THREE.Mesh(new THREE.BoxGeometry(width * 0.8, 0.03, length * 0.02), paint); bar.position.set(0, 0.016, end * length * 0.46); runway.add(bar) }
  group.add(runway)
  // The mast beside the runway (to its right) with the sock at the top.
  const side = bearingVector(runwayDeg + 90), mastHeight = size * 0.7
  const mast = new THREE.Mesh(new THREE.CylinderGeometry(size * 0.012, size * 0.016, mastHeight, 8), new THREE.MeshStandardMaterial({ color: 0xd8dee2, metalness: 0.3, roughness: 0.5 }))
  mast.position.set(side[0] * width * 2.2, mastHeight / 2, side[1] * width * 2.2); mast.castShadow = true; group.add(mast)
  const band = new THREE.Mesh(new THREE.CylinderGeometry(size * 0.02, size * 0.02, size * 0.05, 10), new THREE.MeshBasicMaterial({ color: colour }))
  band.position.set(mast.position.x, mastHeight * 0.92, mast.position.z); group.add(band)
  // The sock: five alternating orange and white segments tapering away from the mast, built along +z of its pivot.
  const pivot = new THREE.Group(); pivot.name = 'sock'; pivot.rotation.order = 'YXZ'
  pivot.position.set(mast.position.x, mastHeight, mast.position.z)
  const sockLength = size * 0.42, r0 = size * 0.06, r1 = size * 0.028
  for (let k = 0; k < 5; k++) {
    const a = r0 + (r1 - r0) * k / 5, b = r0 + (r1 - r0) * (k + 1) / 5
    const seg = new THREE.Mesh(new THREE.CylinderGeometry(a, b, sockLength / 5, 14, 1, true), new THREE.MeshStandardMaterial({ color: k % 2 ? 0xf4f6f7 : 0xff6d1f, side: THREE.DoubleSide, roughness: 0.8 }))
    seg.geometry.rotateX(Math.PI / 2); seg.position.z = sockLength * (k + 0.5) / 5; seg.castShadow = true; pivot.add(seg)
  }
  group.add(pivot)
  // Point the sock downwind: yaw so that +z runs toward fromDeg + 180, then droop by the wind speed.
  const towards = fromDeg + 180, baseYaw = Math.PI - rad(towards), droop = rad(sockDroop(kt))
  pivot.rotation.set(droop, baseYaw, 0)
  const flutter = Math.min(1, kt / 20)
  group.userData = { kind: 'flight', towardsDeg: ((towards % 360) + 360) % 360, droopDeg: sockDroop(kt), runwayDeg, update: time => { pivot.rotation.y = baseYaw + rad(3) * flutter * Math.sin(time * 6.1) + rad(1.5) * flutter * Math.sin(time * 13.7); pivot.rotation.x = droop + rad(2) * flutter * Math.sin(time * 8.3) } }
  return group
}

// Sailing boat on its heading, the sail sheeted to leeward of the apparent wind, a masthead wind arrow (pointing into
// the apparent wind, like a Windex) and a ring in the warning colour. awa: apparent wind angle, signed (positive: from
// starboard); awdFrom: apparent wind direction (from, true); hs, tp: the sea (for the bobbing); size: scene units.
export function boatMarker({ headingDeg = 0, awa = 45, awdFrom = 45, colour = '#3ddc84', hs = 0, tp = 0, size = 1.4 } = {}) {
  const group = new THREE.Group(); group.name = 'sailing-station'
  group.add(ring(size * 0.62, colour))
  const boat = new THREE.Group(); boat.name = 'boat'; boat.rotation.y = headingYaw(headingDeg); group.add(boat)
  // Hull: a pointed outline extruded upward; the bow along -z.
  const L = size * 0.62, W = size * 0.2, shape = new THREE.Shape()
  shape.moveTo(0, L / 2); shape.quadraticCurveTo(W / 2, L * 0.18, W / 2, -L * 0.2); shape.lineTo(W * 0.42, -L / 2); shape.lineTo(-W * 0.42, -L / 2); shape.lineTo(-W / 2, -L * 0.2); shape.quadraticCurveTo(-W / 2, L * 0.18, 0, L / 2)
  const hullGeometry = new THREE.ExtrudeGeometry(shape, { depth: size * 0.07, bevelEnabled: false }); hullGeometry.rotateX(-Math.PI / 2)
  const hull = new THREE.Mesh(hullGeometry, new THREE.MeshStandardMaterial({ color: 0xf2f5f7, roughness: 0.45 })); hull.castShadow = true; hull.name = 'hull'; boat.add(hull)
  const deck = new THREE.Mesh(new THREE.BoxGeometry(W * 0.5, size * 0.012, L * 0.35), new THREE.MeshStandardMaterial({ color: 0x9c7a54, roughness: 0.8 }))
  deck.position.set(0, size * 0.075, L * 0.08); boat.add(deck)
  // Mast a third of the way aft of the bow.
  const mastZ = -L * 0.12, mastHeight = size * 0.85
  const mast = new THREE.Mesh(new THREE.CylinderGeometry(size * 0.008, size * 0.011, mastHeight, 8), new THREE.MeshStandardMaterial({ color: 0xc9d1d6, metalness: 0.4, roughness: 0.4 }))
  mast.position.set(0, size * 0.07 + mastHeight / 2, mastZ); mast.castShadow = true; boat.add(mast)
  // Mainsail: a triangle from the tack (mast foot) to the head (mast top) and the clew (boom end), swung to leeward.
  // Facing the bow (-z), starboard is +x. Wind over starboard (awa > 0) puts the sail to port (-x): the clew, built
  // aft along +z, turns by a yaw whose sine has the sign of the side it goes to.
  const sheet = sheetAngle(awa), side = awa >= 0 ? -1 : 1
  const sailPivot = new THREE.Group(); sailPivot.name = 'sail'; sailPivot.position.set(0, size * 0.1, mastZ); sailPivot.rotation.y = side * rad(sheet)
  const boom = L * 0.55, sailGeometry = new THREE.BufferGeometry()
  sailGeometry.setAttribute('position', new THREE.Float32BufferAttribute([0, 0, 0, 0, mastHeight * 0.92, 0, 0, 0, boom], 3)); sailGeometry.computeVertexNormals()
  const sail = new THREE.Mesh(sailGeometry, new THREE.MeshStandardMaterial({ color: 0xfbfbf6, side: THREE.DoubleSide, roughness: 0.7, transparent: true, opacity: 0.95 }))
  sail.castShadow = true; sailPivot.add(sail); boat.add(sailPivot)
  // Masthead arrow pointing into the apparent wind (world frame: it does not turn with the hull).
  const arrowDir = bearingVector(awdFrom), arrow = new THREE.ArrowHelper(new THREE.Vector3(arrowDir[0], 0, arrowDir[1]), new THREE.Vector3(0, 0, 0), size * 0.3, 0x7fe0ff, size * 0.1, size * 0.06)
  arrow.name = 'apparent-wind'
  const top = new THREE.Vector3(0, size * 0.07 + mastHeight, mastZ).applyAxisAngle(new THREE.Vector3(0, 1, 0), boat.rotation.y)
  arrow.position.copy(top).addScaledVector(new THREE.Vector3(arrowDir[0], 0, arrowDir[1]), -size * 0.15); group.add(arrow)
  // Riding the sea: heave with the wave period, a little roll and pitch with the height.
  const heave = Math.min(size * 0.05, 0.02 + 0.015 * hs), period = tp > 0 ? Math.max(2.5, Math.min(8, tp * 0.6)) : 4, roll = rad(Math.min(12, 2 + 2.5 * hs))
  group.userData = { kind: 'sailing', sheetDeg: sheet, leeward: side < 0 ? 'port' : 'starboard', headingDeg, update: time => { const w = 2 * Math.PI * time / period; boat.position.y = heave * Math.sin(w); boat.rotation.z = roll * Math.sin(w + 0.9); boat.rotation.x = roll * 0.5 * Math.sin(w + 2.1) } }
  return group
}

// A small airliner for the fly-through, built nose along -z (north) so that headingYaw turns it: fuselage, swept
// wings, tailplane and fin, a red light on the left wingtip, green on the right and a flashing beacon. set(state)
// places it: position, heading (the nose: it crabs into the wind while the path follows the track) and pitch.
export function aircraftMarker({ size = 0.5, colour = '#f4f7f9' } = {}) {
  const group = new THREE.Group(); group.name = 'aircraft'; group.rotation.order = 'YXZ'
  const skin = new THREE.MeshStandardMaterial({ color: colour, roughness: 0.35, metalness: 0.25 }), trim = new THREE.MeshStandardMaterial({ color: 0x2f7fd6, roughness: 0.5 })
  const body = new THREE.Mesh(new THREE.CapsuleGeometry(size * 0.07, size * 0.75, 6, 12), skin); body.rotation.x = Math.PI / 2; body.castShadow = true; group.add(body)
  const wing = new THREE.Shape(); wing.moveTo(0, 0.12); wing.lineTo(0.5, -0.12); wing.lineTo(0.5, -0.2); wing.lineTo(0, -0.06); wing.lineTo(-0.5, -0.2); wing.lineTo(-0.5, -0.12); wing.closePath()
  const wingGeometry = new THREE.ExtrudeGeometry(wing, { depth: 0.012, bevelEnabled: false }); wingGeometry.rotateX(Math.PI / 2); wingGeometry.scale(size, 1, size)
  const wings = new THREE.Mesh(wingGeometry, skin); wings.position.set(0, -size * 0.02, -size * 0.02); wings.castShadow = true; group.add(wings)
  const tail = new THREE.Shape(); tail.moveTo(0, 0.05); tail.lineTo(0.18, -0.05); tail.lineTo(0.18, -0.09); tail.lineTo(0, -0.04); tail.lineTo(-0.18, -0.09); tail.lineTo(-0.18, -0.05); tail.closePath()
  const tailGeometry = new THREE.ExtrudeGeometry(tail, { depth: 0.01, bevelEnabled: false }); tailGeometry.rotateX(Math.PI / 2); tailGeometry.scale(size, 1, size)
  const tailplane = new THREE.Mesh(tailGeometry, skin); tailplane.position.set(0, size * 0.02, size * 0.36); group.add(tailplane)
  const fin = new THREE.Mesh(new THREE.BoxGeometry(size * 0.012, size * 0.16, size * 0.14), trim); fin.position.set(0, size * 0.1, size * 0.38); group.add(fin)
  const light = (c, x) => { const m = new THREE.Mesh(new THREE.SphereGeometry(size * 0.025, 8, 6), new THREE.MeshBasicMaterial({ color: c })); m.position.set(x, -size * 0.01, size * 0.16); group.add(m); return m }
  // Looking along the nose (-z), the left wing is -x: red on the left, green on the right.
  light(0xff3b30, -size * 0.5); light(0x34c759, size * 0.5)
  const beacon = new THREE.Mesh(new THREE.SphereGeometry(size * 0.03, 8, 6), new THREE.MeshBasicMaterial({ color: 0xff5252 })); beacon.position.set(0, size * 0.08, 0); group.add(beacon)
  group.userData = {
    kind: 'aircraft',
    set(position, headingDeg, pitchDeg = 0) { group.position.copy(position); group.rotation.y = headingYaw(headingDeg); group.rotation.x = rad(pitchDeg) },
    update: time => { beacon.visible = (time * 1.2) % 1 < 0.15 },
  }
  return group
}
