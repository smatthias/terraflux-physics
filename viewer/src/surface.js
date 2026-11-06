// The continuous surface of the scene. Instead of one box per grid cell, the field is drawn as one smooth, shaded sheet
// that passes exactly through the top centre of every cell (so the overlays that sit on the cells keep sitting on the
// surface), with colours blended between the cells. A shader on top lights up the data point under the pointer, draws a
// glowing line through every place with the same value and rings the selected point; hover never rebuilds the scene.
import * as THREE from 'three'

// Subdivisions per cell so that the surface stays within a vertex budget: fine on small grids, modest on large ones.
// Always even (or 1), so that there is a vertex exactly on every cell centre and the surface really reaches each value.
export function surfaceSubdivisions(nx, ny, budget = 60000, max = 8) {
  const s = Math.min(max, Math.floor(Math.sqrt(budget / Math.max(1, nx * ny))))
  return s >= 2 ? s - (s % 2) : 1
}

// Lattice coordinates along one axis in cell units (cell centres at integers, the domain from -0.5 to n - 0.5): s
// steps per cell, or with s = 1 the centres themselves plus the two outer edges.
export function latticeAxis(n, s) {
  if (s <= 1) return [-0.5, ...Array.from({ length: n }, (_, i) => i), n - 0.5]
  return Array.from({ length: n * s + 1 }, (_, c) => -0.5 + c / s)
}

const clampIndex = (i, n) => (i < 0 ? 0 : i >= n ? n - 1 : i)
const wrapIndex = (i, n) => ((i % n) + n) % n

function catmullRom(p0, p1, p2, p3, t) {
  const t2 = t * t, t3 = t2 * t
  return 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (3 * p1 - p0 - 3 * p2 + p3) * t3)
}

// Bicubic Catmull-Rom through the cell centres (cell x sits at fx = x), clamped to the range of the four surrounding
// centres: smooth where the field is smooth, and no overshoot (ringing) at steps such as coasts or class boundaries.
export function sampleCubic(field, nx, ny, fx, fy, wrapX = false) {
  const x0 = Math.floor(fx), y0 = Math.floor(fy), tx = fx - x0, ty = fy - y0
  const X = k => (wrapX ? wrapIndex(x0 + k, nx) : clampIndex(x0 + k, nx))
  const Y = k => clampIndex(y0 + k, ny)
  const row = j => { const r = Y(j) * nx; return catmullRom(field[r + X(-1)], field[r + X(0)], field[r + X(1)], field[r + X(2)], tx) }
  const v = catmullRom(row(-1), row(0), row(1), row(2), ty)
  const a = field[Y(0) * nx + X(0)], b = field[Y(0) * nx + X(1)], c = field[Y(1) * nx + X(0)], d = field[Y(1) * nx + X(1)]
  const lo = Math.min(a, b, c, d), hi = Math.max(a, b, c, d)
  return v < lo ? lo : v > hi ? hi : v
}

// Bilinear sample of component k of a field with `stride` components per cell.
export function sampleLinear(field, stride, k, nx, ny, fx, fy, wrapX = false) {
  const x0 = Math.floor(fx), y0 = Math.floor(fy), tx = fx - x0, ty = fy - y0
  const X = i => (wrapX ? wrapIndex(i, nx) : clampIndex(i, nx)), Y = j => clampIndex(j, ny)
  const at = (x, y) => field[(Y(y) * nx + X(x)) * stride + k]
  const top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * tx
  const bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * tx
  return top + (bottom - top) * ty
}

// The grid cell under a surface uv (u across x, v across y, both 0..1 over the whole domain).
export function cellAtUv(u, v, nx, ny) {
  const x = clampIndex(Math.floor(u * nx), nx), y = clampIndex(Math.floor(v * ny), ny)
  return y * nx + x
}

// Normalised values (0..1, null for missing) prepared for blending: missing cells do not drag their neighbours.
function valueFields(values, n) {
  const weighted = new Float32Array(n), valid = new Float32Array(n)
  for (let i = 0; i < n; i++) { const v = values?.[i]; if (v !== null && v !== undefined && Number.isFinite(v)) { weighted[i] = v; valid[i] = 1 } }
  return { weighted, valid }
}

// Catmull-Rom weights and neighbour indices along one axis, for every lattice coordinate.
function axisTable(coords, n, wrap) {
  const m = coords.length, index = new Int32Array(m * 4), weight = new Float32Array(m * 4), t = new Float32Array(m)
  for (let k = 0; k < m; k++) {
    const f = coords[k], x0 = Math.floor(f), u = f - x0, u2 = u * u, u3 = u2 * u
    t[k] = u
    for (let j = 0; j < 4; j++) index[k * 4 + j] = wrap ? wrapIndex(x0 - 1 + j, n) : clampIndex(x0 - 1 + j, n)
    weight[k * 4] = 0.5 * (-u + 2 * u2 - u3); weight[k * 4 + 1] = 0.5 * (2 - 5 * u2 + 3 * u3)
    weight[k * 4 + 2] = 0.5 * (u + 4 * u2 - 3 * u3); weight[k * 4 + 3] = 0.5 * (u3 - u2)
  }
  return { index, weight, t }
}

// Vertices on a regular lattice over the whole domain: fx, fy from -0.5 to n - 0.5 in cell units (cell centres at
// integers), so the surface covers the same footprint as the cells did. place(fx, fy, h, k, out) writes the position.
// The interpolation is separable: every grid row is first interpolated along x at the lattice columns, then each vertex
// only combines four rows. This gives the same values as sampleCubic / sampleLinear, an order of magnitude faster.
function lattice({ nx, ny, s, heights, colors, values, wrapX, place }) {
  const ax = latticeAxis(nx, s), ay = latticeAxis(ny, s), cols = ax.length, rows = ay.length, n = cols * rows
  const X = axisTable(ax, nx, wrapX), Y = axisTable(ay, ny, false)
  const position = new Float32Array(n * 3), color = new Float32Array(n * 3), uv = new Float32Array(n * 2), value = new Float32Array(n), height = new Float32Array(n)
  const { weighted, valid } = valueFields(values, nx * ny)
  // Row pass: per grid row and lattice column, the cubic height, the linear colour and the linear value weights.
  const hx = new Float32Array(ny * cols), lo = new Float32Array(ny * cols), hi = new Float32Array(ny * cols)
  const cx = new Float32Array(ny * cols * 3), wx = new Float32Array(ny * cols), vx = new Float32Array(ny * cols)
  for (let y = 0; y < ny; y++) {
    const r = y * nx
    for (let c = 0; c < cols; c++) {
      const i = c * 4, a = r + X.index[i + 1], b = r + X.index[i + 2], t = X.t[c], o = y * cols + c
      hx[o] = X.weight[i] * heights[r + X.index[i]] + X.weight[i + 1] * heights[a] + X.weight[i + 2] * heights[b] + X.weight[i + 3] * heights[r + X.index[i + 3]]
      lo[o] = Math.min(heights[a], heights[b]); hi[o] = Math.max(heights[a], heights[b])
      for (let j = 0; j < 3; j++) cx[o * 3 + j] = colors[a * 3 + j] + (colors[b * 3 + j] - colors[a * 3 + j]) * t
      wx[o] = valid[a] + (valid[b] - valid[a]) * t; vx[o] = weighted[a] + (weighted[b] - weighted[a]) * t
    }
  }
  // Column pass: every vertex combines four interpolated rows (cubic) or two (linear), clamped against ringing.
  for (let r = 0; r < rows; r++) {
    const i = r * 4, r0 = Y.index[i] * cols, r1 = Y.index[i + 1] * cols, r2 = Y.index[i + 2] * cols, r3 = Y.index[i + 3] * cols
    const w0 = Y.weight[i], w1 = Y.weight[i + 1], w2 = Y.weight[i + 2], w3 = Y.weight[i + 3], t = Y.t[r]
    for (let c = 0; c < cols; c++) {
      const k = r * cols + c, a = r1 + c, b = r2 + c
      let h = w0 * hx[r0 + c] + w1 * hx[a] + w2 * hx[b] + w3 * hx[r3 + c]
      const low = Math.min(lo[a], lo[b]), high = Math.max(hi[a], hi[b])
      h = h < low ? low : h > high ? high : h
      height[k] = h
      place(ax[c], ay[r], h, k, position)
      for (let j = 0; j < 3; j++) color[k * 3 + j] = cx[a * 3 + j] + (cx[b * 3 + j] - cx[a * 3 + j]) * t
      uv[k * 2] = (ax[c] + 0.5) / nx; uv[k * 2 + 1] = (ay[r] + 0.5) / ny
      const w = wx[a] + (wx[b] - wx[a]) * t
      value[k] = w > 0.5 ? (vx[a] + (vx[b] - vx[a]) * t) / w : -1
    }
  }
  const index = new Uint32Array((cols - 1) * (rows - 1) * 6)
  let q = 0
  for (let r = 0; r < rows - 1; r++) for (let c = 0; c < cols - 1; c++) {
    const a = r * cols + c, b = a + 1, d = a + cols, e = d + 1
    index[q++] = a; index[q++] = b; index[q++] = d
    index[q++] = b; index[q++] = e; index[q++] = d
  }
  return { cols, rows, position, color, uv, value, index, height }
}

function geometryOf(l) {
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(l.position, 3))
  geometry.setAttribute('color', new THREE.BufferAttribute(l.color, 3))
  geometry.setAttribute('uv', new THREE.BufferAttribute(l.uv, 2))
  geometry.setAttribute('aValue', new THREE.BufferAttribute(l.value, 1))
  geometry.setIndex(new THREE.BufferAttribute(l.index, 1))
  geometry.userData.lattice = { cols: l.cols, rows: l.rows }
  return geometry
}

// Turn the triangles outward: compare the winding of a triangle near the middle with the wanted direction there.
function orient(geometry, outward) {
  const pos = geometry.attributes.position, idx = geometry.index.array, { cols, rows } = geometry.userData.lattice
  const r = Math.floor((rows - 1) / 2), c = Math.floor((cols - 1) / 2), t = (r * (cols - 1) + c) * 6
  const A = new THREE.Vector3().fromBufferAttribute(pos, idx[t]), B = new THREE.Vector3().fromBufferAttribute(pos, idx[t + 1]), C = new THREE.Vector3().fromBufferAttribute(pos, idx[t + 2])
  const normal = B.clone().sub(A).cross(C.clone().sub(A))
  if (normal.dot(outward(A)) < 0) for (let i = 0; i < idx.length; i += 3) { const x = idx[i + 1]; idx[i + 1] = idx[i + 2]; idx[i + 2] = x }
}

// A flat domain: x east, z south (north is -z), heights up. Cell i's top centre is at ((x+.5-nx/2)dx, h, -(y+.5-ny/2)dz).
export function planeSurface({ nx, ny, dx, dz, heights, colors, values, s = surfaceSubdivisions(nx, ny) }) {
  const l = lattice({ nx, ny, s, heights, colors, values, wrapX: false, place: (fx, fy, h, k, out) => { out[k * 3] = (fx + 0.5 - nx / 2) * dx; out[k * 3 + 1] = h; out[k * 3 + 2] = -(fy + 0.5 - ny / 2) * dz } })
  const geometry = geometryOf(l)
  orient(geometry, () => new THREE.Vector3(0, 1, 0))
  // The normal of a height field y = h(x, z) is (-dh/dx, 1, -dh/dz), from central differences on the lattice.
  const { cols, rows, position: P, height: H } = l, normal = new Float32Array(cols * rows * 3)
  for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
    const k = r * cols + c, c0 = c > 0 ? k - 1 : k, c1 = c < cols - 1 ? k + 1 : k, r0 = r > 0 ? k - cols : k, r1 = r < rows - 1 ? k + cols : k
    const hx = (H[c1] - H[c0]) / (P[c1 * 3] - P[c0 * 3]), hz = (H[r1] - H[r0]) / (P[r1 * 3 + 2] - P[r0 * 3 + 2])
    const len = Math.hypot(hx, 1, hz)
    normal[k * 3] = -hx / len; normal[k * 3 + 1] = 1 / len; normal[k * 3 + 2] = -hz / len
  }
  geometry.setAttribute('normal', new THREE.BufferAttribute(normal, 3))
  geometry.userData.kind = 'plane'
  return geometry
}

// A globe from a latitude-longitude grid (rows south to north from -90°, columns from -180° east): dir(lat, lon) gives
// the unit direction in the scene, heights lift the surface outward from radius R around centre. The seam column is
// closed and the poles get radial normals, so the shading shows neither.
export function globeSurface({ nx, ny, heights, colors, values, R, centre, dir, s = surfaceSubdivisions(nx, ny) }) {
  const v = new THREE.Vector3()
  const l = lattice({ nx, ny, s, heights, colors, values, wrapX: true, place: (fx, fy, h, k, out) => {
    const lat = -90 + (fy + 0.5) * 180 / ny, lon = -180 + (fx + 0.5) * 360 / nx
    const d = dir(lat, lon), radius = R + h
    out[k * 3] = centre.x + d[0] * radius; out[k * 3 + 1] = centre.y + d[1] * radius; out[k * 3 + 2] = centre.z + d[2] * radius
  } })
  const geometry = geometryOf(l)
  orient(geometry, a => v.copy(a).sub(centre))
  geometry.computeVertexNormals()
  const normal = geometry.attributes.normal, pos = geometry.attributes.position, { cols, rows } = l
  for (let r = 0; r < rows; r++) {
    const a = r * cols, b = a + cols - 1
    const avg = new THREE.Vector3().fromBufferAttribute(normal, a).add(new THREE.Vector3().fromBufferAttribute(normal, b)).normalize()
    normal.setXYZ(a, avg.x, avg.y, avg.z); normal.setXYZ(b, avg.x, avg.y, avg.z)
  }
  for (const r of [0, rows - 1]) for (let c = 0; c < cols; c++) {
    const k = r * cols + c, radial = new THREE.Vector3().fromBufferAttribute(pos, k).sub(centre).normalize()
    normal.setXYZ(k, radial.x, radial.y, radial.z)
  }
  normal.needsUpdate = true
  geometry.userData.kind = 'globe'
  return geometry
}

// The sides of a flat surface down to the floor, shaded darker towards the bottom: the field reads as a solid slab.
export function surfaceSkirt(geometry, floor = 0) {
  const { cols, rows } = geometry.userData.lattice, pos = geometry.attributes.position, col = geometry.attributes.color
  const loop = []
  for (let c = 0; c < cols; c++) loop.push(c)
  for (let r = 1; r < rows; r++) loop.push(r * cols + cols - 1)
  for (let c = cols - 2; c >= 0; c--) loop.push((rows - 1) * cols + c)
  for (let r = rows - 2; r >= 0; r--) loop.push(r * cols)
  const n = loop.length, position = new Float32Array(n * 6), color = new Float32Array(n * 6), index = []
  loop.forEach((k, j) => {
    const x = pos.getX(k), y = pos.getY(k), z = pos.getZ(k)
    position.set([x, y, z, x, Math.min(floor, y), z], j * 6)
    color.set([col.getX(k) * 0.55, col.getY(k) * 0.55, col.getZ(k) * 0.55, col.getX(k) * 0.16, col.getY(k) * 0.16, col.getZ(k) * 0.16], j * 6)
    if (j < n - 1) { const t = j * 2, b = t + 2; index.push(t, t + 1, b, b, t + 1, b + 1) }
  })
  const skirt = new THREE.BufferGeometry()
  skirt.setAttribute('position', new THREE.BufferAttribute(position, 3))
  skirt.setAttribute('color', new THREE.BufferAttribute(color, 3))
  skirt.setIndex(index)
  skirt.computeVertexNormals()
  return skirt
}

// The surface material: a standard lit material with vertex colours, extended by a few uniforms.
//   uHover / uHoverOn   the data point under the pointer (cell coordinates) and how lit it is (0..1, eased)
//   uSel / uSelOn       the selected data point, ringed
//   uIso / uIsoOn       the hovered value (0..1): a glowing line through every place with that value
//   uContours           faint contour lines of the value, so the shape of the field reads like a map
export function surfaceMaterial({ nx, ny, wrap = false, contours = 0 }) {
  const uniforms = {
    uGrid: { value: new THREE.Vector2(nx, ny) }, uWrap: { value: wrap ? 1 : 0 },
    uHover: { value: new THREE.Vector2(-100, -100) }, uHoverOn: { value: 0 },
    uSel: { value: new THREE.Vector2(-100, -100) }, uSelOn: { value: 0 },
    uIso: { value: -1 }, uIsoOn: { value: 0 }, uContours: { value: contours }, uTime: { value: 0 },
  }
  const material = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.82, metalness: 0.02 })
  material.onBeforeCompile = shader => {
    Object.assign(shader.uniforms, uniforms)
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\nattribute float aValue;\nuniform vec2 uGrid;\nvarying vec2 vCell;\nvarying float vVal;')
      .replace('#include <uv_vertex>', '#include <uv_vertex>\nvCell = uv * uGrid - 0.5;\nvVal = aValue;')
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
uniform vec2 uGrid; uniform float uWrap; uniform vec2 uHover; uniform float uHoverOn; uniform vec2 uSel; uniform float uSelOn;
uniform float uIso; uniform float uIsoOn; uniform float uContours; uniform float uTime;
varying vec2 vCell; varying float vVal;
vec2 cellDelta(vec2 a, vec2 b){ vec2 d = a - b; if (uWrap > 0.5) d.x = mod(d.x + 0.5 * uGrid.x, uGrid.x) - 0.5 * uGrid.x; return d; }
float footprint(vec2 d){ vec2 q = abs(d); return 1.0 - smoothstep(0.38, 0.62, pow(pow(q.x, 4.0) + pow(q.y, 4.0), 0.25)); }`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
{
  vec2 dh = cellDelta(vCell, uHover); float rh = length(dh);
  totalEmissiveRadiance += vec3(0.62, 0.93, 1.0) * uHoverOn * (footprint(dh) * 0.5 + exp(-rh * rh * 0.9) * 0.22);
  vec2 ds = cellDelta(vCell, uSel); float rs = length(ds);
  float ring = 1.0 - smoothstep(0.0, 0.09, abs(rs - 0.62 - 0.05 * sin(uTime * 3.0)));
  totalEmissiveRadiance += vec3(1.0, 0.82, 0.4) * uSelOn * (ring * 0.95 + footprint(ds) * 0.28);
  if (vVal >= 0.0) {
    float w = max(fwidth(vVal), 1e-4);
    if (uIso >= 0.0) totalEmissiveRadiance += vec3(0.62, 0.93, 1.0) * uIsoOn * (1.0 - smoothstep(0.6 * w, 1.9 * w, abs(vVal - uIso))) * 0.55;
    if (uContours > 0.0) { float f = abs(fract(vVal * 10.0 + 0.5) - 0.5); diffuseColor.rgb *= 1.0 - uContours * (1.0 - smoothstep(0.0, 1.3 * fwidth(vVal * 10.0), f)); }
  }
}`)
  }
  material.userData.uniforms = uniforms
  material.customProgramCacheKey = () => 'terraflux-surface'
  return material
}
