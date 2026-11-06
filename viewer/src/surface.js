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

// ---- Noise for the terrain's fine relief ---------------------------------------------------------------------------
// Deterministic 2D gradient noise (Perlin-like, about -1..1) and a ridged fractal of it: sharp crests and rough
// corners where a mountain is, calm where it is not. Seeded, so the same terrain always gets the same rocks.
function hash(ix, iy, seed) {
  let h = Math.imul(ix, 374761393) ^ Math.imul(iy, 668265263) ^ Math.imul(seed + 1, 982451653)
  h = Math.imul(h ^ (h >>> 13), 1274126177)
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296
}
function corner(ix, iy, seed, dx, dy) { const a = hash(ix, iy, seed) * 6.283185307179586; return Math.cos(a) * dx + Math.sin(a) * dy }
export function noise2(x, y, seed = 0) {
  const ix = Math.floor(x), iy = Math.floor(y), fx = x - ix, fy = y - iy
  const u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
  const a = corner(ix, iy, seed, fx, fy), b = corner(ix + 1, iy, seed, fx - 1, fy), c = corner(ix, iy + 1, seed, fx, fy - 1), d = corner(ix + 1, iy + 1, seed, fx - 1, fy - 1)
  return (a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v) * 1.4142
}
// Ridged multifractal, 0..1: each octave is 1 - |noise| squared, weighted by the octave before, so detail gathers on
// the crests (as in real eroded rock) instead of spreading evenly.
export function ridged(x, y, octaves = 4, seed = 0) {
  let sum = 0, amp = 0.5, freq = 1, norm = 0, weight = 1
  for (let o = 0; o < octaves; o++) {
    let n = 1 - Math.abs(noise2(x * freq, y * freq, seed + o * 17))
    n *= n
    sum += n * amp * weight; norm += amp
    weight = Math.min(1, Math.max(0, n * 1.6))
    amp *= 0.5; freq *= 2.07
  }
  return sum / norm
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
// extras: { name: per-cell Float32Array } sampled bilinearly into vertex attributes of that name.
// detail: { amplitude: per-cell Float32Array (scene units), seed } adds ridged fine relief on top of the interpolation.
function lattice({ nx, ny, s, heights, colors, values, wrapX, place, extras = null, detail = null }) {
  const ax = latticeAxis(nx, s), ay = latticeAxis(ny, s), cols = ax.length, rows = ay.length, n = cols * rows
  const X = axisTable(ax, nx, wrapX), Y = axisTable(ay, ny, false)
  const position = new Float32Array(n * 3), color = new Float32Array(n * 3), uv = new Float32Array(n * 2), value = new Float32Array(n), height = new Float32Array(n)
  const { weighted, valid } = valueFields(values, nx * ny)
  const extraNames = extras ? Object.keys(extras) : [], extraOut = extraNames.map(() => new Float32Array(n))
  const amplitude = detail?.amplitude ?? null, seed = detail?.seed ?? 7
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
      const fx = ax[c], fy = ay[r]
      if (amplitude) {
        const amp = sampleLinear(amplitude, 1, 0, nx, ny, fx, fy, wrapX)
        // From spurs and gullies a few cells long down to crags a fraction of a cell wide.
        if (amp > 1e-6) h += amp * (ridged(fx * 0.38, fy * 0.38, 6, seed) - 0.4) * 2
      }
      for (let e = 0; e < extraNames.length; e++) extraOut[e][k] = sampleLinear(extras[extraNames[e]], 1, 0, nx, ny, fx, fy, wrapX)
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
  const extra = {}
  extraNames.forEach((name, e) => { extra[name] = extraOut[e] })
  return { cols, rows, position, color, uv, value, index, height, extra, s }
}

// Ambient occlusion from cavities: a vertex below the average of its surroundings (a gully, a valley floor, the foot of
// a cliff) sees less sky. Separable box blur of the lattice heights; 1 = open, lower = occluded.
export function cavityOcclusion(height, cols, rows, radius, scale) {
  const blurX = new Float32Array(height.length), blur = new Float32Array(height.length), ao = new Float32Array(height.length)
  for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
    let sum = 0, n = 0
    for (let d = -radius; d <= radius; d++) { const cc = c + d; if (cc >= 0 && cc < cols) { sum += height[r * cols + cc]; n++ } }
    blurX[r * cols + c] = sum / n
  }
  for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
    let sum = 0, n = 0
    for (let d = -radius; d <= radius; d++) { const rr = r + d; if (rr >= 0 && rr < rows) { sum += blurX[rr * cols + c]; n++ } }
    blur[r * cols + c] = sum / n
  }
  for (let k = 0; k < height.length; k++) {
    const cavity = (blur[k] - height[k]) / (scale || 1)
    ao[k] = Math.min(1.08, Math.max(0.35, 1 - cavity))
  }
  return ao
}

function geometryOf(l) {
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(l.position, 3))
  geometry.setAttribute('color', new THREE.BufferAttribute(l.color, 3))
  geometry.setAttribute('uv', new THREE.BufferAttribute(l.uv, 2))
  geometry.setAttribute('aValue', new THREE.BufferAttribute(l.value, 1))
  geometry.setIndex(new THREE.BufferAttribute(l.index, 1))
  for (const [name, array] of Object.entries(l.extra ?? {})) geometry.setAttribute(name, new THREE.BufferAttribute(array, 1))
  geometry.userData.lattice = { cols: l.cols, rows: l.rows, s: l.s }
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
export function planeSurface({ nx, ny, dx, dz, heights, colors, values, s = surfaceSubdivisions(nx, ny), extras = null, detail = null, occlusion = 0 }) {
  const l = lattice({ nx, ny, s, heights, colors, values, wrapX: false, extras, detail, place: (fx, fy, h, k, out) => { out[k * 3] = (fx + 0.5 - nx / 2) * dx; out[k * 3 + 1] = h; out[k * 3 + 2] = -(fy + 0.5 - ny / 2) * dz } })
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
  if (occlusion > 0) geometry.setAttribute('aAO', new THREE.BufferAttribute(cavityOcclusion(H, cols, rows, Math.max(2, s), occlusion), 1))
  geometry.userData.height = H
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
export function surfaceSkirt(geometry, floor = 0, { iceDepth = 0 } = {}) {
  const { cols, rows } = geometry.userData.lattice, pos = geometry.attributes.position, col = geometry.attributes.color
  const ice = geometry.attributes.aIce ?? null, waterAttr = geometry.attributes.aWater ?? null
  const loop = []
  for (let c = 0; c < cols; c++) loop.push(c)
  for (let r = 1; r < rows; r++) loop.push(r * cols + cols - 1)
  for (let c = cols - 2; c >= 0; c--) loop.push((rows - 1) * cols + c)
  for (let r = rows - 2; r >= 0; r--) loop.push(r * cols)
  const n = loop.length, position = new Float32Array(n * 6), color = new Float32Array(n * 6), top = new Float32Array(n * 2), iceCol = new Float32Array(n * 2), water = new Float32Array(n * 2), index = []
  loop.forEach((k, j) => {
    const x = pos.getX(k), y = pos.getY(k), z = pos.getZ(k)
    position.set([x, y, z, x, Math.min(floor, y), z], j * 6)
    top[j * 2] = top[j * 2 + 1] = y
    iceCol[j * 2] = iceCol[j * 2 + 1] = (ice ? ice.getX(k) : 0) * iceDepth
    water[j * 2] = water[j * 2 + 1] = waterAttr ? waterAttr.getX(k) : 0
    color.set([col.getX(k) * 0.55, col.getY(k) * 0.55, col.getZ(k) * 0.55, col.getX(k) * 0.16, col.getY(k) * 0.16, col.getZ(k) * 0.16], j * 6)
    if (j < n - 1) { const t = j * 2, b = t + 2; index.push(t, t + 1, b, b, t + 1, b + 1) }
  })
  const skirt = new THREE.BufferGeometry()
  skirt.setAttribute('position', new THREE.BufferAttribute(position, 3))
  skirt.setAttribute('color', new THREE.BufferAttribute(color, 3))
  skirt.setAttribute('aTop', new THREE.BufferAttribute(top, 1))
  skirt.setAttribute('aIceDepth', new THREE.BufferAttribute(iceCol, 1))
  skirt.setAttribute('aWater', new THREE.BufferAttribute(water, 1))
  skirt.setIndex(index)
  skirt.computeVertexNormals()
  return skirt
}

// The surface material: a standard lit material with vertex colours, extended by a few uniforms.
//   uHover / uHoverOn   the data point under the pointer (cell coordinates) and how lit it is (0..1, eased)
//   uSel / uSelOn       the selected data point, ringed
//   uIso / uIsoOn       the hovered value (0..1): a glowing line through every place with that value
//   uContours           faint contour lines of the value, so the shape of the field reads like a map
// GLSL helpers shared by the terrain chunks: sRGB colours to linear, a cheap value-noise fbm for surface texture.
const TERRAIN_GLSL = `
vec3 srgb(vec3 c){ return pow(c, vec3(2.2)); }
float thash(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float tnoise(vec2 p){ vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(thash(i), thash(i + vec2(1.0, 0.0)), u.x), mix(thash(i + vec2(0.0, 1.0)), thash(i + vec2(1.0, 1.0)), u.x), u.y); }
float tfbm(vec2 p){ float s = 0.0, a = 0.5; for (int o = 0; o < 4; o++) { s += a * tnoise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; } return s; }
`

// The realistic ground: what the land looks like at this altitude and steepness. Meadow and forest below the tree line,
// scrub above it, bare rock on steep slopes and high up (in layered strata, so cliffs read as rock), snow above the snow
// line on slopes gentle enough to hold it (or wherever the data has snow), glacier ice where the data has ice, sand at
// the shore, water that darkens with depth. Cavities are occluded. The data colour is then laid over it at uDataMix.
const TERRAIN_FRAGMENT = `
#ifdef TF_TERRAIN
{
  vec3 dataCol = diffuseColor.rgb;
  float slope = 1.0 - clamp(normalize(vTerrainNormal).y, 0.0, 1.0);
  vec2 q = vTerrainPos.xz;
  float n1 = tfbm(q * 0.7), n2 = tfbm(q * 3.1 + 7.1), n3 = tfbm(q * 13.0 + 2.3);
  float e = vElev + (n1 - 0.5) * 240.0;
  vec3 meadow = srgb(vec3(0.44, 0.53, 0.27)), forest = srgb(vec3(0.19, 0.30, 0.16)), scrub = srgb(vec3(0.50, 0.47, 0.34)), sand = srgb(vec3(0.80, 0.74, 0.57));
  // Forest on the slopes below the tree line (in broad stands, not speckles), meadow on the flat valley floors.
  float stands = smoothstep(0.36, 0.64, tfbm(q * 0.42 + 3.0));
  float forestAmount = clamp(smoothstep(0.05, 0.18, slope) * (1.0 - smoothstep(0.48, 0.6, slope)) * 0.75 + stands * 0.4, 0.0, 1.0);
  vec3 g = mix(meadow, forest, forestAmount * 0.85 * (1.0 - smoothstep(uTreeline - 300.0, uTreeline, e)));
  g = mix(g, scrub, smoothstep(uTreeline - 100.0, uTreeline + 300.0, e));
  g *= 0.9 + 0.2 * n3;
  // The shoreline: the blended water share cut at a noisy threshold, so coasts are irregular rather than cell-shaped,
  // with a sandy fringe on low ground.
  float wet = smoothstep(0.36, 0.64, vWater + (n2 - 0.5) * 0.5 + (n3 - 0.5) * 0.12);
  float shore = (1.0 - smoothstep(3.0, 25.0, vElev)) * smoothstep(0.05, 0.35, vWater + (n2 - 0.5) * 0.3) * (1.0 - wet);
  g = mix(g, sand, shore * 0.85);
  float rockAmount = smoothstep(0.26, 0.46, slope + (n2 - 0.5) * 0.24);
  rockAmount = max(rockAmount, smoothstep(uSnowline + 200.0, uSnowline + 800.0, e) * smoothstep(0.18, 0.32, slope));
  float band = e / uStrata + (n1 - 0.5) * 1.4 + n3 * 0.25;
  float layer = fract(band);
  vec3 rock = mix(srgb(vec3(0.46, 0.42, 0.38)), srgb(vec3(0.63, 0.58, 0.51)), smoothstep(0.30, 0.50, layer) - smoothstep(0.72, 0.88, layer));
  rock = mix(rock, srgb(vec3(0.30, 0.27, 0.26)), step(0.9, fract(band * 0.37 + 0.21)) * 0.85);
  rock *= 0.74 + 0.46 * n3;
  g = mix(g, rock, rockAmount);
  float snow = smoothstep(uSnowline - 120.0, uSnowline + 180.0, e) * (1.0 - smoothstep(0.40, 0.60, slope));
  snow = max(snow, clamp(vSnow, 0.0, 1.0) * (1.0 - smoothstep(0.55, 0.75, slope)));
  g = mix(g, srgb(vec3(0.94, 0.96, 0.99)) * (0.93 + 0.08 * n3), clamp(snow, 0.0, 1.0));
  vec3 ice = mix(srgb(vec3(0.82, 0.90, 0.95)), srgb(vec3(0.66, 0.80, 0.91)), smoothstep(0.3, 0.7, n2) * 0.5 + smoothstep(0.4, 0.8, n3) * 0.15);
  g = mix(g, ice, clamp(vIce, 0.0, 1.0) * (1.0 - 0.6 * smoothstep(0.5, 0.7, slope)));
  vec3 water = mix(srgb(vec3(0.12, 0.38, 0.47)), srgb(vec3(0.03, 0.12, 0.23)), smoothstep(0.0, 900.0, -vElev)) * (0.92 + 0.12 * n2);
  g = mix(g, water, wet);
  g *= mix(1.0, clamp(vAO, 0.0, 1.1), uAO);
  diffuseColor.rgb = mix(g, dataCol, uDataMix);
}
#endif`

export function surfaceMaterial({ nx, ny, wrap = false, contours = 0, terrain = null }) {
  const uniforms = {
    uGrid: { value: new THREE.Vector2(nx, ny) }, uWrap: { value: wrap ? 1 : 0 },
    uHover: { value: new THREE.Vector2(-100, -100) }, uHoverOn: { value: 0 },
    uSel: { value: new THREE.Vector2(-100, -100) }, uSelOn: { value: 0 },
    uIso: { value: -1 }, uIsoOn: { value: 0 }, uContours: { value: contours }, uTime: { value: 0 },
    // Realistic terrain (used with terrain: {...}): data colour share, snow and tree line (m), strata spacing (m), occlusion.
    uDataMix: { value: terrain?.dataMix ?? 0.35 }, uSnowline: { value: terrain?.snowline ?? 2800 }, uTreeline: { value: terrain?.treeline ?? 1900 },
    uStrata: { value: terrain?.strata ?? 60 }, uAO: { value: terrain ? 0.85 : 0 },
  }
  const material = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: terrain ? 0.9 : 0.82, metalness: 0.02 })
  // Add to the material's own defines (STANDARD), never replace them.
  if (terrain) material.defines = { ...material.defines, TF_TERRAIN: '' }
  material.onBeforeCompile = shader => {
    Object.assign(shader.uniforms, uniforms)
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', `#include <common>
attribute float aValue; uniform vec2 uGrid; varying vec2 vCell; varying float vVal;
#ifdef TF_TERRAIN
attribute float aElev; attribute float aIce; attribute float aSnow; attribute float aWater; attribute float aAO;
varying float vElev; varying float vIce; varying float vSnow; varying float vWater; varying float vAO; varying vec3 vTerrainPos; varying vec3 vTerrainNormal;
#endif`)
      .replace('#include <uv_vertex>', '#include <uv_vertex>\nvCell = uv * uGrid - 0.5;\nvVal = aValue;')
      .replace('#include <begin_vertex>', `#include <begin_vertex>
#ifdef TF_TERRAIN
vElev = aElev; vIce = aIce; vSnow = aSnow; vWater = aWater; vAO = aAO;
vTerrainPos = (modelMatrix * vec4(transformed, 1.0)).xyz; vTerrainNormal = normalize(mat3(modelMatrix) * objectNormal);
#endif`)
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
uniform vec2 uGrid; uniform float uWrap; uniform vec2 uHover; uniform float uHoverOn; uniform vec2 uSel; uniform float uSelOn;
uniform float uIso; uniform float uIsoOn; uniform float uContours; uniform float uTime;
varying vec2 vCell; varying float vVal;
#ifdef TF_TERRAIN
uniform float uDataMix; uniform float uSnowline; uniform float uTreeline; uniform float uStrata; uniform float uAO;
varying float vElev; varying float vIce; varying float vSnow; varying float vWater; varying float vAO; varying vec3 vTerrainPos; varying vec3 vTerrainNormal;
${TERRAIN_GLSL}
#endif
vec2 cellDelta(vec2 a, vec2 b){ vec2 d = a - b; if (uWrap > 0.5) d.x = mod(d.x + 0.5 * uGrid.x, uGrid.x) - 0.5 * uGrid.x; return d; }
float footprint(vec2 d){ vec2 q = abs(d); return 1.0 - smoothstep(0.38, 0.62, pow(pow(q.x, 4.0) + pow(q.y, 4.0), 0.25)); }`)
      .replace('#include <color_fragment>', `#include <color_fragment>
${TERRAIN_FRAGMENT}`)
      .replace('#include <roughnessmap_fragment>', `#include <roughnessmap_fragment>
#ifdef TF_TERRAIN
roughnessFactor = mix(roughnessFactor, 0.18, clamp(vWater, 0.0, 1.0) * (1.0 - uDataMix));
roughnessFactor = mix(roughnessFactor, 0.42, clamp(vIce + vSnow, 0.0, 1.0) * (1.0 - uDataMix));
#endif`)
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
  material.customProgramCacheKey = () => (terrain ? 'terraflux-terrain' : 'terraflux-surface')
  return material
}

// The sides of the terrain block as a geological cut: a soil band under the surface, rock in strata below it, an ice
// layer on top where the land is under ice and a water column where it is sea. Without terrain: darkened data colours.
export function skirtMaterial({ terrain = false, strata = 3.2 } = {}) {
  const material = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.92, metalness: 0, side: THREE.DoubleSide })
  if (!terrain) return material
  material.defines = { ...material.defines, TF_TERRAIN: '' }
  material.onBeforeCompile = shader => {
    shader.uniforms.uSkirtStrata = { value: strata }
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\nattribute float aTop; attribute float aIceDepth; attribute float aWater;\nvarying float vTop; varying float vIceDepth; varying float vWaterSide; varying vec3 vSidePos;')
      .replace('#include <begin_vertex>', '#include <begin_vertex>\nvTop = aTop; vIceDepth = aIceDepth; vWaterSide = aWater; vSidePos = (modelMatrix * vec4(transformed, 1.0)).xyz;')
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
uniform float uSkirtStrata; varying float vTop; varying float vIceDepth; varying float vWaterSide; varying vec3 vSidePos;
${TERRAIN_GLSL}`)
      .replace('#include <color_fragment>', `#include <color_fragment>
{
  float depth = vTop - vSidePos.y;
  float n = tfbm(vec2(vSidePos.x + vSidePos.z, vSidePos.y * 2.0) * 2.2);
  float band = fract(vSidePos.y * uSkirtStrata + (n - 0.5) * 0.9);
  vec3 c = mix(srgb(vec3(0.43, 0.38, 0.33)), srgb(vec3(0.60, 0.54, 0.46)), smoothstep(0.28, 0.48, band) - smoothstep(0.70, 0.86, band));
  c = mix(c, srgb(vec3(0.27, 0.24, 0.23)), step(0.92, fract(vSidePos.y * uSkirtStrata * 0.41 + 0.3 + n * 0.2)) * 0.8);
  c *= 0.8 + 0.35 * tfbm(vSidePos.xy * 9.0 + vSidePos.zy * 9.0);
  c = mix(srgb(vec3(0.30, 0.22, 0.15)) * (0.85 + 0.3 * n), c, smoothstep(0.04, 0.14 + 0.06 * n, depth));
  c = mix(c, srgb(vec3(0.70, 0.84, 0.93)) * (0.9 + 0.15 * n), step(depth, vIceDepth) * step(0.001, vIceDepth));
  c = mix(c, srgb(vec3(0.05, 0.20, 0.30)), step(0.5, vWaterSide));
  c *= 0.45 + 0.55 * smoothstep(0.0, max(0.5, vTop), vSidePos.y + 0.2);
  diffuseColor.rgb = c;
}`)
  }
  material.customProgramCacheKey = () => 'terraflux-skirt-terrain'
  return material
}

// The surface height over every cell centre, after the fine relief: overlays that sit on the cells (rivers, labels,
// markers) use it so that they rest on the rocks instead of inside them.
export function centreHeights(geometry, nx, ny) {
  const { cols, s } = geometry.userData.lattice, H = geometry.userData.height ?? null, pos = geometry.attributes.position
  const column = x => (s <= 1 ? x + 1 : Math.round((x + 0.5) * s)), out = new Float32Array(nx * ny)
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) { const k = column(y) * cols + column(x); out[y * nx + x] = H ? H[k] : pos.getY(k) }
  return out
}
