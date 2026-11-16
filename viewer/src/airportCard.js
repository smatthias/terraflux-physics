// The weather card that floats over each aerodrome in the flight weather scene: a compact station plot from its
// flight report (aviation.js). cardContent is pure (tested); drawAirportCard paints it on a 2D canvas context with the
// wind barb (barbs.js). The scene (App.vue) turns the canvas into a sprite above the windsock.
import { barbSvg } from './barbs.js'
import { FT_PER_M } from './station.js'

// The headline of a hazard sentence, short enough for the card: up to the first colon or bracket, at most 30 characters.
export function shortHazard(text) {
  if (!text) return null
  const head = text.split(/[:(;]/)[0].trim()
  return head.length > 30 ? `${head.slice(0, 29).trimEnd()}…` : head
}

const dir3 = d => String(Math.round(d / 10) * 10 || 360).padStart(3, '0')

// What the card shows: identifier, name, runway in use, category, wind, visibility and weather, cloud and ceiling,
// temperature / dew point and QNH. Missing elements say so briefly rather than inventing values.
export function cardContent(report, airport, { selected = false, symbol = null } = {}) {
  const r = report, v = r.visibility, c = r.cloud
  const visText = !v ? 'vis —' : v.metres >= 10000 ? 'vis 10 km+' : v.metres >= 5000 ? `vis ${Math.round(v.metres / 1000)} km` : `vis ${Math.round(v.metres / 50) * 50} m`
  const wx = r.weather.groups?.length ? ` ${r.weather.groups.join(' ')}` : ''
  const cloudText = !c.modelled ? 'cloud —' : c.layer ? `${c.layer.cover}${String(Math.floor(c.layer.base * FT_PER_M / 100)).padStart(3, '0')}${c.layer.convective ?? ''}` : 'clear'
  const ceiling = c.ceilingFt === null ? '' : c.ceilingFt === Infinity ? ' · no ceiling' : ` · ceiling ${Math.round(c.ceilingFt / 100) * 100} ft`
  const t = v2 => (v2 === null ? '—' : `${Math.round(v2)}`)
  return {
    id: airport.id, name: airport.name, runway: `RWY ${r.runway.designator}`, category: r.category.name, colour: r.category.colour, selected, symbol,
    wind: r.wind.kt < 0.5 ? 'calm' : `${dir3(r.wind.fromDeg)}° ${Math.round(r.wind.kt)} kt`, windKt: r.wind.kt, windFrom: r.wind.fromDeg,
    lines: [`${visText}${wx}`, `${cloudText}${ceiling}`, `${t(r.temperature.c)}/${t(r.temperature.dewC)} °C · ${r.pressure.qnhHpa === null ? 'QNH —' : `Q${String(Math.floor(r.pressure.qnhHpa)).padStart(4, '0')}`}`],
    hazard: shortHazard(r.hazards.find(h => h.level === 'severe')?.text ?? null),
  }
}

// A wind barb in a size x size box at (ox, oy).
function paintBarb(ctx, kt, fromDeg, size, ox, oy, northernHemisphere) {
  const g = barbSvg(kt, fromDeg, { size, northernHemisphere })
  ctx.strokeStyle = '#e8f6ff'; ctx.fillStyle = '#e8f6ff'; ctx.lineWidth = 4; ctx.lineCap = 'round'
  if (g.calm) { ctx.beginPath(); ctx.arc(ox + g.circle.cx, oy + g.circle.cy, g.circle.r, 0, Math.PI * 2); ctx.stroke() }
  for (const l of g.lines) { ctx.beginPath(); ctx.moveTo(ox + l.x1, oy + l.y1); ctx.lineTo(ox + l.x2, oy + l.y2); ctx.stroke() }
  for (const p of g.polygons) { const pts = p.split(' ').map(q => q.split(',').map(Number)); ctx.beginPath(); pts.forEach(([x, y], j) => (j ? ctx.lineTo(ox + x, oy + y) : ctx.moveTo(ox + x, oy + y))); ctx.closePath(); ctx.fill() }
  ctx.beginPath(); ctx.arc(ox + size / 2, oy + size / 2, 5, 0, Math.PI * 2); ctx.fill()
}

// Paint the card on ctx (width w, height h in pixels). The caller sizes the canvas.
// drawIcon(ctx, symbol, size, x, y): the weather symbol painter (weatherIcons.js), passed in so that this stays testable.
export function drawAirportCard(ctx, content, w, h, { northernHemisphere = true, drawIcon = null } = {}) {
  const k = content, r = 18
  ctx.clearRect(0, 0, w, h)
  ctx.fillStyle = 'rgba(6,17,24,0.9)'; ctx.strokeStyle = k.colour; ctx.lineWidth = k.selected ? 8 : 4
  ctx.beginPath(); ctx.moveTo(r, 4); ctx.arcTo(w - 4, 4, w - 4, h - 4, r); ctx.arcTo(w - 4, h - 4, 4, h - 4, r); ctx.arcTo(4, h - 4, 4, 4, r); ctx.arcTo(4, 4, w - 4, 4, r); ctx.closePath(); ctx.fill(); ctx.stroke()
  // Header: category badge, identifier, runway.
  ctx.fillStyle = k.colour; ctx.fillRect(22, 22, 118, 54)
  ctx.fillStyle = '#09151d'; ctx.font = 'bold 36px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(k.category, 81, 50)
  ctx.textAlign = 'left'; ctx.fillStyle = '#eef6fa'; ctx.font = 'bold 40px sans-serif'; ctx.fillText(k.id, 156, 42)
  ctx.fillStyle = '#9fb6c0'; ctx.font = '24px sans-serif'; ctx.fillText(`${k.name} · ${k.runway}`, 156, 72)
  // Wind barb in a box on the left, the wind in words beside it.
  paintBarb(ctx, k.windKt, k.windFrom, 96, 30, 96, northernHemisphere)
  ctx.fillStyle = '#eef6fa'; ctx.font = 'bold 32px sans-serif'; ctx.fillText(k.wind, 142, 118)
  ctx.font = '27px sans-serif'; ctx.fillStyle = '#d5e3ea'
  k.lines.forEach((line, j) => ctx.fillText(line, 142, 156 + j * 34))
  if (k.symbol && drawIcon) drawIcon(ctx, k.symbol, 104, w - 118, 8)
  if (k.hazard) { ctx.fillStyle = '#ff6b6b'; ctx.font = 'bold 25px sans-serif'; ctx.fillText(`⚠ ${k.hazard}`, 30, h - 26) }
}

// The card over a clicked point in the sailing view: the point read-out of sailing.js (pointReadout) — Beaufort force
// and wind, waves, current, water, visibility — with the distance and bearing from the boat as its header.
export function probeCardContent(p) {
  const nm = p.fromBoat ? (p.fromBoat.nm < 10 ? p.fromBoat.nm.toFixed(1) : `${Math.round(p.fromBoat.nm)}`) : null
  const head = p.fromBoat && p.fromBoat.nm >= 0.05 ? `${nm} NM · ${String(Math.round(p.fromBoat.bearingDeg) % 360).padStart(3, '0')}° from ⛵` : p.fromBoat ? 'at the boat' : 'point'
  const lines = []
  if (p.onSea === false) lines.push('land')
  else if (p.sea) lines.push(`waves ${p.sea.hs.toFixed(1)} m${p.sea.hs >= 0.05 ? ` · ${p.sea.tp.toFixed(0)} s` : ''}`)
  const extra = [p.current ? `current ${p.current.driftKt.toFixed(1)} kt` : null, p.water ? `water ${p.water.c.toFixed(0)} °C` : null].filter(Boolean)
  if (extra.length) lines.push(extra.join(' · '))
  const v = p.visibility
  if (v) lines.push(`vis ${v.metres >= 10000 ? '10 km+' : v.metres >= 1000 ? `${Math.round(v.metres / 1000)} km` : `${Math.round(v.metres / 50) * 50} m`}${p.qnhHpa !== null ? ` · ${Math.round(p.qnhHpa)} hPa` : ''}`)
  return {
    head, force: `F${p.wind.beaufort.force}`, colour: p.warning.colour, windKt: p.wind.kt, windFrom: p.wind.fromDeg,
    wind: p.wind.kt < 1 ? 'calm' : `${p.wind.from16} ${Math.round(p.wind.kt)} kt`, lines: lines.slice(0, 3),
  }
}

export function drawProbeCard(ctx, content, w, h, { northernHemisphere = true } = {}) {
  const k = content, r = 18
  ctx.clearRect(0, 0, w, h)
  ctx.fillStyle = 'rgba(6,17,24,0.9)'; ctx.strokeStyle = k.colour; ctx.lineWidth = 6
  ctx.beginPath(); ctx.moveTo(r, 4); ctx.arcTo(w - 4, 4, w - 4, h - 4, r); ctx.arcTo(w - 4, h - 4, 4, h - 4, r); ctx.arcTo(4, h - 4, 4, 4, r); ctx.arcTo(4, 4, w - 4, 4, r); ctx.closePath(); ctx.fill(); ctx.stroke()
  ctx.fillStyle = k.colour; ctx.fillRect(22, 22, 96, 54)
  ctx.fillStyle = '#09151d'; ctx.font = 'bold 38px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(k.force, 70, 50)
  ctx.textAlign = 'left'; ctx.fillStyle = '#ffe3a0'; ctx.font = 'bold 32px sans-serif'; ctx.fillText(k.head, 134, 50)
  paintBarb(ctx, k.windKt, k.windFrom, 96, 22, 92, northernHemisphere)
  ctx.fillStyle = '#eef6fa'; ctx.font = 'bold 32px sans-serif'; ctx.fillText(k.wind, 134, 112)
  ctx.font = '27px sans-serif'; ctx.fillStyle = '#d5e3ea'
  k.lines.forEach((line, j) => ctx.fillText(line, 134, 150 + j * 34))
}
