// The weather along a planned flight route (flightPlan.js): every sample of the route (about half a grid cell) read at
// the time the aircraft passes it - the surface (wind, weather, visibility, cloud and ceiling, QNH, flight category,
// GAFOR) and the air at the altitude flown (wind, temperature, in cloud, icing, turbulence, thunderstorms, freezing
// level, the VFR band) - grouped into stretches of similar weather (the route forecast, in the order flown) and
// summarised under the headings of the ICAO low-level area forecast (GAMET, Annex 3 App. 5).
// Pure functions. Requirements: docs/weather/route-weather-and-sail-routing.md (F18).
import { FT_PER_M, HPA, surfaceWind, pressures, weatherWords, compass16 } from './station.js'
import { flightConditions, CATEGORIES } from './aviation.js'
import { weatherSymbol, solarElevation } from './weatherLayers.js'
import { vfrAt, GAFOR } from './gafor.js'

// Symbols from the most to the least significant (a merged stretch shows its most significant one).
export const SYMBOL_RANK = ['hail', 'thunder', 'snow', 'sleet', 'showers', 'rain', 'drizzle', 'fog', 'mist', 'overcast', 'cloudy', 'partly', 'few', 'clear']
const symbolRank = s => { const k = SYMBOL_RANK.indexOf(s); return k < 0 ? SYMBOL_RANK.length : k }
const CATEGORY_RANK = { VFR: 0, MVFR: 1, IFR: 2, LIFR: 3, '—': -1 }
const worseCategory = (a, b) => ((CATEGORY_RANK[b] ?? -1) > (CATEGORY_RANK[a] ?? -1) ? b : a)
const worseGafor = (a, b) => ((GAFOR[b]?.rank ?? -1) > (GAFOR[a]?.rank ?? -1) ? b : a)
const HAZARD_ORDER = ['thunderstorm', 'cyclone', 'terrain', 'icing', 'turbulence', 'cloud', 'mtobsc', 'outside']
export const GAMET_SFC_WIND_KT = 30
export const GAMET_SFC_VIS_M = 5000
export const GAMET_SIG_CLD_FT = 1000
export const MAX_STRETCHES = 12

// One route sample: the segment of the plan with the surface weather of its frame added.
function sampleOf(plan, seg, frame) {
  const c = flightConditions(frame, seg.i), sw = surfaceWind(frame, seg.i), p = pressures(frame, seg.i)
  let vfr = null; try { vfr = vfrAt(frame, seg.i) } catch { vfr = null }
  const layer = c.cloud.layer, ceilingAglFt = c.ceilingFt
  const ceilingAmslFt = Number.isFinite(ceilingAglFt) ? seg.elevFt + ceilingAglFt : ceilingAglFt
  const mid = (seg.fromNm + seg.toNm) / 2
  const phase = seg.cruise ? 'cruise' : mid < plan.lengthNm / 2 ? 'climb' : 'descent'
  const hazards = [...seg.hazards]
  // Mountains obscured: a broken or overcast base at or below the highest terrain of the corridor.
  if (Number.isFinite(ceilingAmslFt) && seg.terrainFt > 0 && ceilingAmslFt <= seg.terrainFt) hazards.push('mtobsc')
  return {
    k: seg.k, fromNm: seg.fromNm, toNm: seg.toNm, distNm: mid, i: seg.i, lat: seg.lat, lon: seg.lon, startMs: seg.startMs, night: solarElevation(seg.lat, seg.lon, seg.startMs) < -0.833, endMs: seg.startMs + seg.hours * 3.6e6, frameIndex: seg.frameIndex, phase,
    altFt: seg.altFt, elevFt: seg.elevFt, terrainFt: seg.terrainFt,
    surface: {
      wind: { fromDeg: sw.fromDeg, kt: sw.kt }, symbol: weatherSymbol(frame, seg.i), words: weatherWords(c.wx, c.vis), wx: c.wx,
      visibilityM: c.vis ? c.vis.metres : null, visCause: c.vis?.obscuration ?? (c.vis?.cause === 'precipitation' ? 'precipitation' : null),
      cloud: layer ? { cover: layer.cover, genus: layer.genus, abbreviation: layer.abbreviation, convective: layer.convective, baseAglFt: layer.base * FT_PER_M, baseFt: seg.elevFt + layer.base * FT_PER_M, topFt: layer.top === null ? null : seg.elevFt + layer.top * FT_PER_M } : null,
      cloudModelled: c.cloud.modelled, ceilingAglFt, ceilingAmslFt, qnhHpa: p.qnh === null ? null : p.qnh / HPA, category: c.category.name, gafor: vfr?.category.key ?? seg.gafor ?? '?',
    },
    aloft: { wind: seg.wind, tempC: seg.tempC, isaDevC: seg.isaDevC, inCloud: seg.hazards.includes('cloud'), shearKtPer1000ft: seg.shearKtPer1000ft ?? null },
    icing: seg.icing ?? null, freezingFt: seg.freezingFt ?? null, cloud: seg.cloud ?? null,
    vfrBand: vfr ? { floorFt: Math.max(vfr.space.bottomFt, seg.terrainFt + 500), topFt: vfr.space.ok ? vfr.space.topFt : null } : null,
    hazards: HAZARD_ORDER.filter(h => hazards.includes(h)),
  }
}

// Contiguous ranges (NM) of the samples that pass a test, with the samples in each.
export function rangesOf(samples, test) {
  const out = []; let cur = null
  for (const s of samples) {
    if (test(s)) { if (cur) { cur.toNm = s.toNm; cur.items.push(s) } else cur = { fromNm: s.fromNm, toNm: s.toNm, items: [s] } }
    else if (cur) { out.push(cur); cur = null }
  }
  if (cur) out.push(cur)
  return out
}

const minOf = (list, f) => list.reduce((m, x) => { const v = f(x); return Number.isFinite(v) && (m === null || v < m) ? v : m }, null)
const maxOf = (list, f) => list.reduce((m, x) => { const v = f(x); return Number.isFinite(v) && (m === null || v > m) ? v : m }, null)
// Mean direction (vector mean of unit vectors weighted by speed) and the speed range of a list of winds.
function windSpan(winds) {
  let e = 0, n = 0
  for (const w of winds) { e += -Math.sin(w.fromDeg * Math.PI / 180) * w.kt; n += -Math.cos(w.fromDeg * Math.PI / 180) * w.kt }
  const fromDeg = ((Math.atan2(-e, -n) * 180 / Math.PI) + 360) % 360
  return { fromDeg, minKt: Math.min(...winds.map(w => w.kt)), maxKt: Math.max(...winds.map(w => w.kt)), meanKt: Math.hypot(e, n) / (winds.length || 1) }
}

// A stretch from its samples: everything the route forecast says about it.
function stretchOf(items) {
  const worst = items.reduce((w, s) => (symbolRank(s.surface.symbol) < symbolRank(w.surface.symbol) ? s : w), items[0])
  const phases = [...new Set(items.map(s => s.phase))]
  return {
    fromNm: items[0].fromNm, toNm: items.at(-1).toNm, startMs: items[0].startMs, endMs: items.at(-1).endMs, phases, count: items.length,
    symbol: worst.surface.symbol, night: worst.night, words: worst.surface.words,
    category: items.reduce((c, s) => worseCategory(c, s.surface.category), '—'), gafor: items.reduce((g, s) => worseGafor(g, s.surface.gafor), '?'),
    visibilityMinM: minOf(items, s => s.surface.visibilityM), ceilingMinAglFt: minOf(items, s => s.surface.ceilingAglFt), cloudBaseMinFt: minOf(items, s => s.surface.cloud?.baseFt), cloudTopMaxFt: maxOf(items, s => s.surface.cloud?.topFt),
    cover: items.map(s => s.surface.cloud?.cover).filter(Boolean).sort((a, b) => ['FEW', 'SCT', 'BKN', 'OVC'].indexOf(b) - ['FEW', 'SCT', 'BKN', 'OVC'].indexOf(a))[0] ?? null,
    genus: [...new Set(items.map(s => s.surface.cloud?.abbreviation).filter(Boolean))],
    altFt: { min: minOf(items, s => s.altFt), max: maxOf(items, s => s.altFt) },
    windAloft: windSpan(items.map(s => s.aloft.wind)), tempC: { min: minOf(items, s => s.aloft.tempC), max: maxOf(items, s => s.aloft.tempC) },
    surfaceWind: windSpan(items.map(s => s.surface.wind)), freezingMinFt: minOf(items, s => s.freezingFt), qnhMinHpa: minOf(items, s => s.surface.qnhHpa),
    vfrTopMinFt: minOf(items, s => s.vfrBand?.topFt ?? NaN),
    hazards: HAZARD_ORDER.filter(h => items.some(s => s.hazards.includes(h))),
  }
}
const stretchKey = s => [s.surface.symbol, s.surface.category, s.surface.gafor, s.hazards.filter(h => h !== 'outside').join(',')].join('|')

// The summary under the GAMET headings: each { key, label, level, text, ranges } (level 'nil' when nothing to say).
function gamet(samples) {
  const out = [], nm = r => `${Math.round(r.fromNm)}–${Math.round(r.toNm)} NM`, where = rs => rs.map(nm).join(', ')
  const add = (key, label, level, text, ranges = []) => out.push({ key, label, level, text, ranges: ranges.map(r => ({ fromNm: r.fromNm, toNm: r.toNm })) })
  const r100 = v => Math.round(v / 100) * 100, fl = v => `${r100(v).toLocaleString('en-US')} ft`
  // Section I: hazards.
  const wind = rangesOf(samples, s => s.surface.wind.kt >= GAMET_SFC_WIND_KT)
  add('SFC WIND', 'Surface wind', wind.length ? 'warning' : 'nil', wind.length ? `Mean surface wind ${GAMET_SFC_WIND_KT} kt or more at ${where(wind)} (max ${Math.round(maxOf(samples, s => s.surface.wind.kt))} kt)` : `Nil: surface wind below ${GAMET_SFC_WIND_KT} kt all along`, wind)
  const vis = rangesOf(samples, s => s.surface.visibilityM !== null && s.surface.visibilityM < GAMET_SFC_VIS_M)
  add('SFC VIS', 'Surface visibility', vis.length ? (minOf(samples, s => s.surface.visibilityM) < 1500 ? 'severe' : 'warning') : 'nil',
    vis.length ? vis.map(r => { const m = minOf(r.items, s => s.surface.visibilityM), causes = [...new Set(r.items.map(s => s.surface.visCause).filter(Boolean))]; return `${m < 1000 ? `${Math.round(m / 50) * 50} m` : `${(m / 1000).toFixed(1)} km`} at ${nm(r)}${causes.length ? ` (${causes.join(', ')})` : ''}` }).join('; ')
      : samples.some(s => s.surface.visibilityM !== null) ? `Nil: ${GAMET_SFC_VIS_M / 1000} km or more all along` : 'Not known: the run has no humidity or precipitation', vis)
  const ts = rangesOf(samples, s => s.surface.wx.thunder), heavy = rangesOf(samples, s => ['heavy', 'violent'].includes(s.surface.wx.intensity)), fz = rangesOf(samples, s => s.surface.wx.freezing)
  const sig = [...ts.map(r => `TS at ${nm(r)}${r.items.some(s => s.surface.wx.hail >= 0.5) ? ' with hail' : ''}`), ...heavy.map(r => `heavy ${r.items[0].surface.wx.kind ?? 'precipitation'} at ${nm(r)}`), ...fz.map(r => `freezing rain at ${nm(r)}`)]
  add('SIGWX', 'Significant weather', ts.length || fz.length ? 'severe' : heavy.length ? 'warning' : 'nil', sig.length ? sig.join('; ') : 'Nil', [...ts, ...heavy, ...fz])
  const mt = rangesOf(samples, s => s.hazards.includes('mtobsc'))
  add('MT OBSC', 'Mountains obscured', mt.length ? 'warning' : 'nil', mt.length ? `Cloud on the high ground at ${where(mt)}` : 'Nil', mt)
  const low = rangesOf(samples, s => s.surface.ceilingAglFt !== null && Number.isFinite(s.surface.ceilingAglFt) && s.surface.ceilingAglFt < GAMET_SIG_CLD_FT)
  const cb = rangesOf(samples, s => s.surface.cloud?.convective === 'CB'), tcu = rangesOf(samples, s => s.surface.cloud?.convective === 'TCU')
  const sc = [...low.map(r => `BKN/OVC below ${GAMET_SIG_CLD_FT} ft AGL at ${nm(r)} (down to ${r100(minOf(r.items, s => s.surface.ceilingAglFt))} ft)`), ...cb.map(r => `CB at ${nm(r)}`), ...tcu.map(r => `TCU at ${nm(r)}`)]
  add('SIG CLD', 'Significant cloud', cb.length ? 'severe' : low.length ? 'warning' : tcu.length ? 'caution' : 'nil', sc.length ? sc.join('; ') : 'Nil', [...low, ...cb, ...tcu])
  const ice = rangesOf(samples, s => !!s.icing)
  add('ICE', 'Icing', ice.some(r => r.items.some(s => s.hazards.includes('icing'))) ? 'warning' : ice.length ? 'caution' : 'nil',
    ice.length ? ice.map(r => `${r.items[0].icing.severity} ${fl(minOf(r.items, s => s.icing.fromFt))}–${fl(maxOf(r.items, s => s.icing.toFt))} at ${nm(r)}${r.items.some(s => s.hazards.includes('icing')) ? ' (at your altitude)' : ''}`).join('; ') : 'Nil', ice)
  const turb = rangesOf(samples, s => s.hazards.includes('turbulence'))
  add('TURB', 'Turbulence', ts.length ? 'severe' : turb.length ? 'caution' : 'nil', [...turb.map(r => `shear up to ${maxOf(r.items, s => s.aloft.shearKtPer1000ft).toFixed(0)} kt per 1 000 ft at your altitude at ${nm(r)}`), ...(ts.length ? ['severe in and near the thunderstorms'] : [])].join('; ') || 'Nil', [...turb, ...(ts.length ? ts : [])])
  // Section II: the route's weather.
  const w = windSpan(samples.map(s => s.aloft.wind)), tmin = minOf(samples, s => s.aloft.tempC), tmax = maxOf(samples, s => s.aloft.tempC)
  add('WIND/T', 'Wind and temperature at the altitude flown', 'info', `${String(Math.round(w.fromDeg / 10) * 10 || 360).padStart(3, '0')}° ${Math.round(w.minKt)}–${Math.round(w.maxKt)} kt, ${Math.round(tmin)} to ${Math.round(tmax)} °C`)
  const cloudy = samples.filter(s => s.surface.cloud)
  add('CLD', 'Cloud', 'info', cloudy.length ? `${[...new Set(cloudy.map(s => `${s.surface.cloud.cover} ${s.surface.cloud.abbreviation}`))].slice(0, 4).join(', ')}; lowest base ${fl(minOf(cloudy, s => s.surface.cloud.baseFt))}, highest top ${fl(maxOf(cloudy, s => s.surface.cloud.topFt ?? s.surface.cloud.baseFt))} AMSL` : samples.some(s => s.surface.cloudModelled) ? 'Nil (sky clear)' : 'Not known: the run has no cloud model')
  const z0 = minOf(samples, s => s.freezingFt), z1 = maxOf(samples, s => s.freezingFt)
  add('FZLVL', 'Freezing level', 'info', z0 === null ? 'Not known' : z0 <= 0 ? `At the surface${z1 > 0 ? ` to ${fl(z1)}` : ''}` : `${fl(z0)}${z1 - z0 >= 100 ? `–${fl(z1)}` : ''} AMSL`)
  const q = minOf(samples, s => s.surface.qnhHpa)
  add('MNM QNH', 'Lowest QNH', 'info', q === null ? 'Not known: the run has no pressure field' : `${Math.floor(q)} hPa`)
  return out
}

// The weather along the route. plan: planFlight(...) with ok; frames: the run (the plan's frame indices).
// { samples, stretches, gamet, worst: { visibilityM, ceilingAglFt, category, gafor, symbol } }
export function routeWeather(plan, frames) {
  if (!plan?.ok) return null
  const samples = plan.segments.map(seg => sampleOf(plan, seg, frames[seg.frameIndex]))
  // Stretches: consecutive samples with the same key, then the shortest merged into a neighbour until at most 12.
  let groups = []
  for (const s of samples) { const key = stretchKey(s), last = groups.at(-1); if (last && last.key === key) last.items.push(s); else groups.push({ key, items: [s] }) }
  while (groups.length > MAX_STRETCHES) {
    let k = 0
    for (let j = 1; j < groups.length; j++) { const len = g => g.items.at(-1).toNm - g.items[0].fromNm; if (len(groups[j]) < len(groups[k])) k = j }
    const into = k === 0 ? 1 : k === groups.length - 1 ? k - 1 : (groups[k - 1].items.length <= groups[k + 1].items.length ? k - 1 : k + 1)
    const a = Math.min(k, into), b = Math.max(k, into)
    groups.splice(a, 2, { key: groups[a].key, items: [...groups[a].items, ...groups[b].items] })
  }
  const stretches = groups.map(g => stretchOf(g.items))
  return {
    samples, stretches, gamet: gamet(samples),
    worst: {
      visibilityM: minOf(samples, s => s.surface.visibilityM), ceilingAglFt: minOf(samples, s => s.surface.ceilingAglFt) ?? (samples.some(s => s.surface.ceilingAglFt === Infinity) ? Infinity : null),
      category: samples.reduce((c, s) => worseCategory(c, s.surface.category), '—'), gafor: samples.reduce((g, s) => worseGafor(g, s.surface.gafor), '?'),
      symbol: samples.reduce((w, s) => (symbolRank(s.surface.symbol) < symbolRank(w) ? s.surface.symbol : w), 'clear'),
    },
  }
}

// A short line for a stretch ("BKN Cu 2 500–6 000 ft · vis 8 km · rain showers").
export function stretchLine(s) {
  const parts = []
  if (s.words && s.words !== 'dry') parts.push(s.words)
  if (s.cover) parts.push(`${s.cover} ${s.genus.join('/')} ${Math.round(s.cloudBaseMinFt / 100) * 100}${s.cloudTopMaxFt ? `–${Math.round(s.cloudTopMaxFt / 100) * 100}` : ''} ft`)
  else parts.push('no cloud')
  if (s.visibilityMinM !== null) parts.push(`vis ${s.visibilityMinM >= 10000 ? '10 km+' : s.visibilityMinM >= 1000 ? `${(s.visibilityMinM / 1000).toFixed(s.visibilityMinM < 5000 ? 1 : 0)} km` : `${Math.round(s.visibilityMinM / 50) * 50} m`}`)
  return parts.join(' · ')
}
export const categoryColour = name => CATEGORIES[name]?.colour ?? '#7e98a5'
export const windText = w => (w.maxKt < 2.5 ? 'calm' : `${compass16(w.fromDeg)} ${Math.round(w.minKt)}${Math.round(w.maxKt) !== Math.round(w.minKt) ? `–${Math.round(w.maxKt)}` : ''} kt`)
