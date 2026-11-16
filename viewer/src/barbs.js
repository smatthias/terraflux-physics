// Wind barbs: the meteorological symbol that combines wind direction and speed. Pure geometry: the 3D barb layers
// (windBarbs.js) and the SVG barbs of the reports (WindBarb.vue) both draw from it.
//
// Convention (WMO plotting practice, as on aviation and marine charts):
//   - the staff runs from the station toward the direction the wind blows FROM;
//   - the speed, rounded to the nearest 5 kt, is drawn at the staff's outer end: a pennant (filled triangle) per 50 kt,
//     a full barb per 10 kt, a half barb for the remaining 5 kt; a lone half barb sits a little in from the end so it
//     is not read as a full one; calm (below 2.5 kt) is a circle;
//   - the barbs lean toward the outer end and stand on the clockwise side of the staff in the Northern Hemisphere
//     (toward low pressure), on the anticlockwise side in the Southern Hemisphere.

// Pennants, full and half barbs for a speed in knots (rounded to 5 kt).
export function barbParts(kt) {
  const rounded = Math.round(Math.max(0, kt) / 5) * 5
  if (rounded < 5) return { rounded: 0, calm: true, pennants: 0, full: 0, half: 0 }
  const pennants = Math.floor(rounded / 50), rest = rounded - pennants * 50
  return { rounded, calm: false, pennants, full: Math.floor(rest / 10), half: rest % 10 === 5 ? 1 : 0 }
}

// Shape constants in units of the staff length.
export const BARB = { feather: 0.42, angle: 60, spacing: 0.13, pennantWidth: 0.2, pennantGap: 0.05, calmRadius: 0.16 }

// The barb in map coordinates (east, north), staff length 1, station at the origin:
//   { calm, parts, lines: [[e0, n0, e1, n1], ...], triangles: [[e0, n0, e1, n1, e2, n2], ...] }
// lines are the staff and the barbs, triangles the pennants. A calm wind has no lines; draw a circle of
// BARB.calmRadius instead.
export function barbGlyph(kt, fromDeg, { northernHemisphere = true } = {}) {
  const parts = barbParts(kt)
  if (parts.calm) return { calm: true, parts, lines: [], triangles: [] }
  const a = fromDeg * Math.PI / 180, d = [Math.sin(a), Math.cos(a)]
  // The feather side: the staff direction turned 90 degrees clockwise (NH) or anticlockwise (SH).
  const c = northernHemisphere ? [d[1], -d[0]] : [-d[1], d[0]]
  const at = (s, n) => [s * d[0] + n * c[0], s * d[1] + n * c[1]]
  const slant = BARB.angle * Math.PI / 180, fe = Math.cos(slant), fn = Math.sin(slant)
  const lines = [[0, 0, ...at(1, 0)]], triangles = []
  let s = 1
  for (let k = 0; k < parts.pennants; k++) {
    triangles.push([...at(s, 0), ...at(s + BARB.feather * fe, BARB.feather * fn), ...at(s - BARB.pennantWidth, 0)])
    s -= BARB.pennantWidth + BARB.pennantGap
  }
  if (parts.pennants && !parts.full && !parts.half) s += BARB.pennantGap
  for (let k = 0; k < parts.full; k++) { lines.push([...at(s, 0), ...at(s + BARB.feather * fe, BARB.feather * fn)]); s -= BARB.spacing }
  if (parts.half) {
    if (!parts.pennants && !parts.full) s -= BARB.spacing      // a lone half barb sits in from the end
    lines.push([...at(s, 0), ...at(s + BARB.feather * fe / 2, BARB.feather * fn / 2)])
  }
  return { calm: false, parts, lines, triangles }
}

// The barb as SVG pieces in screen coordinates (x right, y down) inside a size x size box with the station at its
// centre; the staff is 0.45 of the box. { calm, circle: { cx, cy, r } | null, lines: [{x1, y1, x2, y2}], polygons: ['x,y x,y x,y'] }
export function barbSvg(kt, fromDeg, { size = 40, northernHemisphere = true } = {}) {
  const g = barbGlyph(kt, fromDeg, { northernHemisphere }), L = size * 0.45, C = size / 2
  const px = (e, n) => [C + e * L, C - n * L]
  if (g.calm) return { calm: true, circle: { cx: C, cy: C, r: BARB.calmRadius * L }, lines: [], polygons: [] }
  return {
    calm: false, circle: null,
    lines: g.lines.map(([e0, n0, e1, n1]) => { const [x1, y1] = px(e0, n0), [x2, y2] = px(e1, n1); return { x1, y1, x2, y2 } }),
    polygons: g.triangles.map(t => [px(t[0], t[1]), px(t[2], t[3]), px(t[4], t[5])].map(p => p.map(v => v.toFixed(2)).join(',')).join(' ')),
  }
}

// Colour classes by speed (kt): light, moderate, strong, very strong, jet-core.
export const BARB_COLOURS = [[20, '#cfe9f5'], [50, '#7fd1ff'], [80, '#ffd166'], [120, '#ff8c42'], [Infinity, '#ff4d8d']]
export const barbColour = kt => BARB_COLOURS.find(([limit]) => kt < limit)[1]
