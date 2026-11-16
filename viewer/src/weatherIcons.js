// Weather symbols drawn with canvas paths (no emoji fonts needed): the same icons on the 3D map, on the aerodrome
// cards and in the reports. drawWeatherIcon(ctx, key, size, x, y) paints the symbol of weatherLayers.js into a
// size x size box at (x, y).
// night: a moon in place of the sun (clear, few, partly cloudy, showers).
export function drawWeatherIcon(ctx, key, size = 128, x = 0, y = 0, { night = false } = {}) {
  const s = size / 128
  ctx.save(); ctx.translate(x, y); ctx.scale(s, s); ctx.lineCap = 'round'; ctx.lineJoin = 'round'
  const sun = (cx, cy, r) => {
    if (night) { ctx.fillStyle = '#e9eef6'; ctx.beginPath(); ctx.arc(cx, cy, r * 1.15, 0, Math.PI * 2); ctx.arc(cx + r * 0.65, cy - r * 0.4, r * 1.0, 0, Math.PI * 2, true); ctx.fill('evenodd'); return }
    ctx.strokeStyle = '#ffc845'; ctx.lineWidth = 7
    for (let k = 0; k < 8; k++) { const a = k * Math.PI / 4; ctx.beginPath(); ctx.moveTo(cx + Math.cos(a) * r * 1.35, cy + Math.sin(a) * r * 1.35); ctx.lineTo(cx + Math.cos(a) * r * 1.75, cy + Math.sin(a) * r * 1.75); ctx.stroke() }
    ctx.fillStyle = '#ffd34d'; ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill()
  }
  const cloud = (cx, cy, w, fill = '#eef3f6', edge = 'rgba(20,40,55,0.55)') => {
    const r = w / 4
    ctx.fillStyle = fill; ctx.strokeStyle = edge; ctx.lineWidth = 4
    ctx.beginPath(); ctx.arc(cx - r * 1.1, cy + r * 0.2, r * 0.95, Math.PI * 0.5, Math.PI * 1.5); ctx.arc(cx - r * 0.2, cy - r * 0.55, r * 1.2, Math.PI * 1.05, Math.PI * 1.9)
    ctx.arc(cx + r * 1.05, cy - r * 0.05, r * 1.0, Math.PI * 1.35, Math.PI * 0.5); ctx.closePath(); ctx.fill(); ctx.stroke()
  }
  const drops = (cx, cy, n, colour = '#4aa8ff', len = 16) => { ctx.strokeStyle = colour; ctx.lineWidth = 6; for (let k = 0; k < n; k++) { const dx = (k - (n - 1) / 2) * 18; ctx.beginPath(); ctx.moveTo(cx + dx + 4, cy); ctx.lineTo(cx + dx - 3, cy + len); ctx.stroke() } }
  const dots = (cx, cy, n, colour = '#7cc4ff') => { ctx.fillStyle = colour; for (let k = 0; k < n; k++) { ctx.beginPath(); ctx.arc(cx + (k - (n - 1) / 2) * 16, cy + (k % 2) * 8, 4.5, 0, Math.PI * 2); ctx.fill() } }
  const flakes = (cx, cy, n) => { ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 4; for (let k = 0; k < n; k++) { const fx = cx + (k - (n - 1) / 2) * 22, fy = cy + (k % 2) * 9; for (let a = 0; a < 3; a++) { const t = a * Math.PI / 3; ctx.beginPath(); ctx.moveTo(fx - Math.cos(t) * 8, fy - Math.sin(t) * 8); ctx.lineTo(fx + Math.cos(t) * 8, fy + Math.sin(t) * 8); ctx.stroke() } } }
  const bolt = (cx, cy) => { ctx.fillStyle = '#ffd34d'; ctx.strokeStyle = '#b37a00'; ctx.lineWidth = 2.5; ctx.beginPath(); ctx.moveTo(cx + 6, cy - 4); ctx.lineTo(cx - 12, cy + 22); ctx.lineTo(cx - 1, cy + 22); ctx.lineTo(cx - 8, cy + 44); ctx.lineTo(cx + 16, cy + 14); ctx.lineTo(cx + 4, cy + 14); ctx.lineTo(cx + 12, cy - 4); ctx.closePath(); ctx.fill(); ctx.stroke() }
  const bars = (cy, n, colour) => { ctx.strokeStyle = colour; ctx.lineWidth = 8; for (let k = 0; k < n; k++) { const o = k % 2 ? 10 : 0; ctx.beginPath(); ctx.moveTo(20 + o, cy + k * 17); ctx.lineTo(108 - (k % 2 ? 0 : 10), cy + k * 17); ctx.stroke() } }
  switch (key) {
    case 'clear': sun(64, 64, 24); break
    case 'few': sun(48, 46, 20); cloud(74, 78, 62); break
    case 'partly': sun(44, 42, 20); cloud(70, 74, 86); break
    case 'cloudy': cloud(64, 66, 100); break
    case 'overcast': cloud(50, 52, 76, '#b8c4cc'); cloud(72, 72, 92, '#8d9ba5'); break
    case 'drizzle': cloud(64, 52, 96, '#c9d3da'); dots(64, 96, 4); break
    case 'rain': cloud(64, 48, 96, '#aebbc5'); drops(64, 86, 4); break
    case 'showers': sun(40, 36, 17); cloud(70, 56, 86, '#c5d0d8'); drops(70, 90, 3); break
    case 'thunder': case 'hail': cloud(64, 44, 100, '#6c7a86'); bolt(64, 66); if (key === 'hail') { ctx.fillStyle = '#ffffff'; for (const [hx, hy] of [[30, 98], [98, 92], [90, 112]]) { ctx.beginPath(); ctx.arc(hx, hy, 6, 0, Math.PI * 2); ctx.fill() } } else drops(64, 96, 2, '#4aa8ff', 12); break
    case 'snow': cloud(64, 46, 96, '#d9e3ea'); flakes(64, 94, 3); break
    case 'sleet': cloud(64, 46, 96, '#c4cfd7'); drops(46, 86, 2); flakes(88, 96, 1); break
    case 'fog': cloud(64, 40, 80, '#b4c0c8'); bars(78, 3, '#c9d3da'); break
    case 'mist': bars(50, 4, 'rgba(215,225,232,0.85)'); break
    default: ctx.fillStyle = 'rgba(200,215,225,0.6)'; ctx.font = 'bold 72px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText('?', 64, 68)
  }
  ctx.restore()
}
