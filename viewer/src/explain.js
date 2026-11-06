// Plain-language explanations of a data point: what the number means on an everyday scale, how it compares with the
// rest of the map, how it changes around the point and over time, and what else is going on there. The inspector
// overlay types these sentences out; the hover tip uses the short form. Pure functions: no Vue, no three.js.

// ---- The meaning of each layer ---------------------------------------------------------------------------------------
// more / less: comparatives ("warmer than 82 % of the map"); peak / trough: a point higher / lower than all its
// neighbours; scale: everyday classes as [upper limit, label] pairs (the last limit is Infinity); note(v): a unit hint.
const steps = (...pairs) => pairs
const temperatureScale = steps([-10, 'severe frost'], [0, 'freezing'], [8, 'cold'], [14, 'cool'], [20, 'mild'], [26, 'warm'], [32, 'hot'], [Infinity, 'very hot'])
const waterTemperatureScale = steps([0, 'freezing sea water'], [8, 'cold water'], [15, 'cool water'], [22, 'mild water'], [27, 'warm water'], [Infinity, 'very warm water (hurricane fuel)'])
const BEAUFORT = [[0.5, 'calm'], [1.6, 'light air'], [3.4, 'light breeze'], [5.5, 'gentle breeze'], [8, 'moderate breeze'], [10.8, 'fresh breeze'], [13.9, 'strong breeze'], [17.2, 'near gale'], [20.8, 'gale'], [24.5, 'strong gale'], [28.5, 'storm'], [32.7, 'violent storm'], [Infinity, 'hurricane force']]
// Notes are whole sentences about the value (without the final full stop).
const kmh = v => `${formatNumber(v)} m/s is about ${Math.round(v * 3.6)} km/h`
const fahrenheit = v => `${formatNumber(v)} °C is ${Math.round(v * 9 / 5 + 32)} °F`
const litres = () => '1 mm of water is 1 litre on every square metre'

export const SENSE = {
  temperature: { more: 'warmer', less: 'colder', peak: 'a warm spot', trough: 'a cold spot', scale: temperatureScale, note: fahrenheit },
  wind: { more: 'windier', less: 'calmer', peak: 'a wind maximum', trough: 'a calm spot', scale: BEAUFORT, note: kmh },
  stormwind: { more: 'windier', less: 'calmer', peak: 'the strongest storm wind nearby', trough: 'a calmer spot', scale: BEAUFORT, note: kmh },
  mpi: { more: 'higher', less: 'lower', peak: 'a local maximum', trough: 'a local minimum', scale: steps([18, 'too little energy for a hurricane'], [33, 'enough for a tropical storm'], [50, 'enough for a major hurricane'], [Infinity, 'enough for an extreme hurricane']), note: kmh },
  pressure: { more: 'higher', less: 'lower', peak: 'a high-pressure centre', trough: 'a low-pressure centre', scale: steps([980, 'a deep low: stormy'], [1000, 'low pressure: unsettled weather'], [1009, 'slightly low'], [1018, 'near average'], [1030, 'high pressure: settled weather'], [Infinity, 'a strong high']), note: () => 'average sea-level pressure is 1013 hPa' },
  humidity: { more: 'more humid', less: 'drier', peak: 'a moist spot', trough: 'a dry spot', scale: steps([30, 'dry air'], [60, 'comfortable'], [85, 'humid'], [97, 'very humid, close to saturation'], [Infinity, 'saturated: cloud or fog']) },
  density: { more: 'denser', less: 'lighter', peak: 'a pocket of heavy air', trough: 'a pocket of light air', note: () => 'air at sea level weighs about 1.2 kg per cubic metre' },
  moisture: { more: 'moister', less: 'drier', peak: 'a moist spot', trough: 'a dry spot', scale: steps([4, 'dry'], [10, 'moderately moist'], [16, 'moist'], [Infinity, 'tropical moisture']), note: () => 'grams of water vapour in each kilogram of air' },
  cloud: { more: 'cloudier', less: 'clearer', peak: 'a cloud mass', trough: 'a clear patch', scale: steps([10, 'clear sky'], [40, 'partly cloudy'], [75, 'mostly cloudy'], [Infinity, 'overcast']) },
  rainfall: { more: 'wetter', less: 'drier', peak: 'a rain maximum', trough: 'a dry spot', scale: steps([0.2, 'practically dry'], [2, 'a light shower'], [10, 'a rainy spell'], [30, 'a lot of rain'], [60, 'heavy rain'], [Infinity, 'extreme rainfall']), note: litres },
  accum: { more: 'wetter', less: 'drier', peak: 'a rain maximum', trough: 'a dry spot', scale: steps([0.2, 'practically dry'], [2, 'a light shower'], [10, 'a rainy spell'], [30, 'a lot of rain'], [Infinity, 'heavy rain']), note: litres },
  precip: { more: 'wetter', less: 'drier', peak: 'the core of a shower', trough: 'a dry gap', scale: steps([0.1, 'not raining'], [2.5, 'light rain'], [7.6, 'moderate rain'], [50, 'heavy rain'], [Infinity, 'violent rain']), note: () => 'millimetres of water falling per hour' },
  oroprecip: { more: 'wetter', less: 'drier', peak: 'the wettest slope nearby', trough: 'a rain shadow', scale: steps([0.1, 'no mountain rain'], [2.5, 'light'], [7.6, 'moderate'], [Infinity, 'heavy']), note: () => 'rain squeezed out of air forced up the slopes, per hour' },
  searain: { more: 'wetter', less: 'drier', peak: 'a rain core', trough: 'a dry gap', scale: steps([0.1, 'not raining'], [2.5, 'light rain'], [7.6, 'moderate rain'], [Infinity, 'heavy rain']) },
  cape: { more: 'more unstable', less: 'more stable', peak: 'the most unstable air nearby', trough: 'a stable pocket', scale: steps([300, 'stable: thunderstorms unlikely'], [1000, 'marginal: weak showers at most'], [2500, 'moderate: thunderstorms possible'], [4000, 'strong: severe storms possible'], [Infinity, 'extreme instability']), note: () => 'energy a rising air bubble gains; more means stronger updrafts' },
  cloudbase: { more: 'higher', less: 'lower', peak: 'high cloud bases', trough: 'low cloud bases', scale: steps([500, 'very low clouds'], [1500, 'low clouds'], [3000, 'mid-level cloud base'], [Infinity, 'high cloud base']) },
  cloudtop: { more: 'taller', less: 'shallower', peak: 'the tallest clouds nearby', trough: 'shallow clouds', scale: steps([2000, 'shallow clouds'], [6000, 'towering clouds'], [10000, 'deep storm clouds'], [Infinity, 'clouds reaching the tropopause']) },
  gpi: { more: 'more favourable', less: 'less favourable', peak: 'the most favourable spot', trough: 'an unfavourable spot', scale: steps([0.5, 'storms unlikely to form'], [2, 'some chance of a new storm'], [Infinity, 'favourable for a new storm']) },
  surge: { more: 'higher', less: 'lower', peak: 'the highest surge nearby', trough: 'a calmer stretch of coast', scale: steps([0.3, 'negligible'], [1, 'minor flooding of low ground'], [2.5, 'dangerous flooding'], [Infinity, 'life-threatening surge']) },
  stp: { more: 'more tornado-prone', less: 'less tornado-prone', peak: 'the highest tornado risk nearby', trough: 'a low-risk spot', scale: steps([0.5, 'tornadoes unlikely'], [1, 'weak tornadoes possible'], [4, 'strong tornadoes possible'], [Infinity, 'violent tornadoes possible']) },
  helicity: { more: 'more spin', less: 'less spin', peak: 'the strongest spin nearby', trough: 'little spin', scale: steps([100, 'little spin'], [250, 'enough for rotating storms'], [Infinity, 'strong spin: supercells likely']) },
  terrain: { more: 'higher', less: 'lower', peak: 'a summit', trough: 'a hollow', scale: steps([0, 'below the reference level'], [200, 'lowland'], [600, 'hills'], [1500, 'uplands'], [Infinity, 'mountains']) },
  slope: { more: 'steeper', less: 'gentler', peak: 'the steepest ground nearby', trough: 'a flat patch', scale: steps([3, 'flat'], [10, 'gentle'], [20, 'moderate'], [35, 'steep'], [Infinity, 'very steep']) },
  lifting: { more: 'rising faster', less: 'sinking more', peak: 'the strongest uplift nearby', trough: 'the strongest descent nearby', scale: steps([-0.3, 'air pushed down the slope (drying, warming)'], [-0.02, 'gently sinking'], [0.02, 'no vertical push'], [0.3, 'gently lifted'], [Infinity, 'air forced up the slope (clouds, rain)']) },
  blocked: { more: 'more blocked', less: 'freer', peak: 'the most blocked flow nearby', trough: 'free flow', scale: steps([0.1, 'the air flows over the mountain'], [0.5, 'partly blocked'], [Infinity, 'mostly blocked: the air goes around']) },
  foehn: { more: 'warmer (foehn)', less: 'less foehn', peak: 'the strongest foehn nearby', trough: 'no foehn', scale: steps([0.5, 'no foehn'], [3, 'mild foehn'], [Infinity, 'strong foehn']) },
  coldpool: { more: 'colder pool', less: 'milder', peak: 'the deepest cold pool nearby', trough: 'no cold pool', scale: steps([0.5, 'no cold pool'], [3, 'a shallow cold pool'], [Infinity, 'a strong cold pool']) },
  valley: { more: 'deeper', less: 'shallower', peak: 'the deepest hollow nearby', trough: 'open ground' },
  water: { more: 'wetter', less: 'drier', peak: 'a puddle', trough: 'a dry spot', note: litres },
  soil: { more: 'wetter', less: 'drier', peak: 'the wettest soil nearby', trough: 'dry soil', note: litres },
  surfacewater: { more: 'wetter', less: 'drier', peak: 'standing water', trough: 'a dry spot', scale: steps([1, 'dry ground'], [50, 'puddles'], [300, 'shallow flooding'], [Infinity, 'deep flooding']), note: litres },
  discharge: { more: 'more water', less: 'less water', peak: 'the biggest flow nearby', trough: 'little flow', scale: steps([0.1, 'a trickle'], [5, 'a stream'], [100, 'a river'], [Infinity, 'a large river']), note: v => `${formatNumber(v)} m³/s is about ${Math.round(v * 1000).toLocaleString('en-US')} litres per second` },
  saturation: { more: 'wetter', less: 'drier', peak: 'the wettest soil nearby', trough: 'the driest soil nearby', scale: steps([0.3, 'dry soil'], [0.7, 'moist soil'], [0.95, 'wet soil'], [Infinity, 'saturated: rain runs off']) },
  watertable: { more: 'deeper', less: 'shallower', peak: 'deep groundwater', trough: 'shallow groundwater', scale: steps([1, 'groundwater at the surface'], [5, 'shallow groundwater'], [Infinity, 'deep groundwater']) },
  snowpack: { more: 'snowier', less: 'less snow', peak: 'a snow maximum', trough: 'little snow', scale: steps([1, 'no snow'], [50, 'a thin snow cover'], [300, 'a solid snowpack'], [Infinity, 'deep snow']) },
  erosion: { more: 'more eroded', less: 'less eroded', peak: 'the worst erosion nearby', trough: 'intact ground' },
  deposition: { more: 'more sediment', less: 'less sediment', peak: 'the thickest deposit nearby', trough: 'no deposit' },
  stability: { more: 'safer', less: 'less stable', peak: 'the safest slope nearby', trough: 'the weakest slope nearby', scale: steps([1, 'likely to slide'], [1.3, 'marginal: may slide when wet'], [2, 'stable'], [Infinity, 'very stable']), note: () => 'resisting force divided by driving force; below 1 the slope fails' },
  floodclass: { more: 'more dangerous', less: 'safer', peak: 'the most dangerous water nearby', trough: 'safe ground', scale: steps([0.5, 'no flood hazard'], [1.5, 'low hazard'], [2.5, 'moderate: dangerous for children'], [3.5, 'high: dangerous for everyone'], [Infinity, 'extreme: buildings at risk']) },
  current: { more: 'faster', less: 'slower', peak: 'the core of a current', trough: 'still water', scale: steps([0.05, 'almost still'], [0.3, 'a gentle drift'], [1, 'a strong current'], [Infinity, 'a very fast jet']), note: kmh },
  sst: { more: 'warmer', less: 'colder', peak: 'a warm eddy', trough: 'a cold eddy', scale: waterTemperatureScale, note: fahrenheit },
  oceansst: { more: 'warmer', less: 'colder', peak: 'a warm eddy', trough: 'a cold eddy', scale: waterTemperatureScale, note: fahrenheit },
  salinity: { more: 'saltier', less: 'fresher', peak: 'a salty patch', trough: 'a fresh patch', scale: steps([30, 'brackish: diluted by fresh water'], [34, 'slightly fresh'], [36, 'typical ocean'], [Infinity, 'very salty']), note: () => 'grams of salt in each kilogram of sea water' },
  surfacesalinity: { more: 'saltier', less: 'fresher', peak: 'a salty patch', trough: 'a fresh patch', scale: steps([30, 'brackish: diluted by fresh water'], [34, 'slightly fresh'], [36, 'typical ocean'], [Infinity, 'very salty']), note: () => 'grams of salt in each kilogram of sea water' },
  seaice: { more: 'icier', less: 'more open water', peak: 'the thickest pack nearby', trough: 'open water', scale: steps([15, 'open water'], [50, 'loose ice'], [85, 'close pack'], [Infinity, 'solid pack ice']) },
  oxygen: { more: 'more oxygen', less: 'less oxygen', peak: 'well-oxygenated water', trough: 'oxygen-poor water', scale: steps([60, 'hypoxic: most animals flee or die'], [150, 'low oxygen'], [Infinity, 'healthy oxygen']) },
  bottomo2: { more: 'more oxygen', less: 'less oxygen', peak: 'well-oxygenated bottom water', trough: 'the worst dead-zone water nearby', scale: steps([5, 'anoxic: no oxygen'], [60, 'hypoxic: a dead zone'], [150, 'low oxygen'], [Infinity, 'healthy oxygen']) },
  ph: { more: 'less acidic', less: 'more acidic', peak: 'the least acidified water nearby', trough: 'the most acidified water nearby', scale: steps([7.7, 'strongly acidified'], [7.95, 'acidified'], [8.15, 'typical of today'], [Infinity, 'pre-industrial-like']) },
  omega: { more: 'kinder to shells', less: 'harsher on shells', peak: 'the best shell-building water nearby', trough: 'the most corrosive water nearby', scale: steps([1, 'corrosive: shells dissolve'], [2, 'stressful for shell builders'], [Infinity, 'good for shells and corals']) },
  omegabottom: { more: 'kinder to shells', less: 'harsher on shells', peak: 'the best shell-building water nearby', trough: 'the most corrosive water nearby', scale: steps([1, 'corrosive: shells dissolve'], [2, 'stressful for shell builders'], [Infinity, 'good for shells and corals']) },
  habitat: { more: 'worse', less: 'better', peak: 'the worst water nearby', trough: 'the best water nearby', scale: steps([0.5, 'habitable'], [1.5, 'stressed'], [2.5, 'hypoxic'], [Infinity, 'anoxic']) },
  co2air: { more: 'more CO₂', less: 'less CO₂', peak: 'a CO₂ plume', trough: 'clean air', note: () => 'parts per million; about 420 ppm was the global average in the mid-2020s' },
  coastlost: { more: 'more land lost', less: 'less land lost', peak: 'the worst loss nearby', trough: 'safe land', scale: steps([1, 'no land lost'], [50, 'partly lost'], [Infinity, 'mostly under water']) },
  coastpopulation: { more: 'more crowded', less: 'emptier', peak: 'a town', trough: 'open country' },
  sunbrightness: { more: 'brighter', less: 'darker', peak: 'a bright facula', trough: 'a dark sunspot' },
  suncontrast: { more: 'brighter', less: 'darker', peak: 'a bright facula', trough: 'a dark sunspot', scale: steps([-5, 'a sunspot'], [-0.5, 'slightly darker'], [0.5, 'quiet Sun'], [Infinity, 'a bright facula']) },
  windaurora: { more: 'more aurora', less: 'less aurora', peak: 'the brightest aurora nearby', trough: 'no aurora', scale: steps([0.5, 'no visible aurora'], [3, 'faint aurora'], [10, 'bright aurora'], [Infinity, 'a brilliant auroral storm']) },
  winddose: { more: 'more radiation', less: 'less radiation', peak: 'the highest dose nearby', trough: 'the best-shielded spot', note: () => 'a chest X-ray is about 20 µSv' },
}

const GENERIC = { more: 'higher', less: 'lower', peak: 'a local high', trough: 'a local low' }
export function senseOf(key) { return { ...GENERIC, ...(SENSE[key] ?? {}) } }

// The everyday class of a value, or null if the layer has no scale.
export function classify(key, value) {
  const scale = SENSE[key]?.scale
  if (!scale || value === null || value === undefined || !Number.isFinite(value)) return null
  const hit = scale.find(([limit]) => value < limit)
  return hit ? hit[1] : scale.at(-1)[1]
}

export function unitNote(key, value) {
  const note = SENSE[key]?.note
  return note && Number.isFinite(value) ? note(value) : null
}

// ---- Numbers ---------------------------------------------------------------------------------------------------------
// Enough digits to be useful and no more: 3 significant figures, at most 2 decimals, thousands separated.
export function formatNumber(v, digits = 3) {
  if (v === null || v === undefined || !Number.isFinite(v)) return '—'
  const a = Math.abs(v)
  if (a === 0) return '0'
  if (a >= 1e6 || a < 1e-3) return v.toExponential(Math.max(0, digits - 1)).replace('e+', 'e')
  if (a >= 100) return Math.round(v).toLocaleString('en-US')
  const decimals = Math.min(2, Math.max(0, digits - 1 - Math.floor(Math.log10(a))))
  return v.toFixed(decimals).replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '')
}
export const withUnit = (v, unit) => `${formatNumber(v)}${unit ? (unit === '%' || unit === '°' ? unit : ` ${unit}`) : ''}`

// Share of the map's valid cells below v (ties count half), 0..100.
export function percentile(values, v) {
  let below = 0, equal = 0, n = 0
  for (const x of values ?? []) { if (x === null || x === undefined || !Number.isFinite(x)) continue; n++; if (x < v) below++; else if (x === v) equal++ }
  return n ? (below + equal / 2) / n * 100 : null
}

const POINTS = ['north', 'north-east', 'east', 'south-east', 'south', 'south-west', 'west', 'north-west']
export function compass8(deg) { return POINTS[Math.round((((deg % 360) + 360) % 360) / 45) % 8] }

// Wind from its east/north components: the direction it blows FROM, its speed and Beaufort class.
export function windAt(u, v) {
  const speed = Math.hypot(u, v)
  const from = (Math.atan2(-u, -v) * 180 / Math.PI + 360) % 360
  return { speed, fromDeg: from, from: compass8(from), toDeg: (from + 180) % 360, label: classify('wind', speed) }
}

// The local slope of the field at cell i from its neighbours (central differences where possible), and whether the
// cell is a local peak or trough among its eight neighbours. Gradients are per metre of the grid spacing.
export function gradientAt(values, nx, ny, i, spacingEast = 1, spacingNorth = 1) {
  const x = i % nx, y = Math.floor(i / nx), v = values[i]
  const at = (a, b) => (a < 0 || b < 0 || a >= nx || b >= ny ? null : values[b * nx + a] ?? null)
  const ok = w => w !== null && Number.isFinite(w)
  const diff = (lo, hi, span) => (ok(lo) && ok(hi) ? (hi - lo) / (2 * span) : ok(hi) && ok(v) ? (hi - v) / span : ok(lo) && ok(v) ? (v - lo) / span : 0)
  const east = diff(at(x - 1, y), at(x + 1, y), spacingEast), north = diff(at(x, y - 1), at(x, y + 1), spacingNorth)
  const around = []
  for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) if (a || b) { const w = at(x + a, y + b); if (ok(w)) around.push(w) }
  const peak = ok(v) && around.length >= 3 && around.every(w => w < v)
  const trough = ok(v) && around.length >= 3 && around.every(w => w > v)
  const magnitude = Math.hypot(east, north)
  return { east, north, magnitude, towardDeg: (Math.atan2(east, north) * 180 / Math.PI + 360) % 360, peak, trough, neighbours: around }
}

// ---- The explanation of one data point ---------------------------------------------------------------------------
// A line is an array of tokens: strings are prose, { v } are values (the gaps the typing fills in).
const V = (v, tone) => ({ v, tone })

export function explainCell(ctx) {
  if (ctx.categories) return explainCategory(ctx)
  const { key, title, tooltip, unit, value, values, i, grid } = ctx, domain = ctx.domain ?? 'the map'
  const sense = senseOf(key), fmt = v => withUnit(v, unit)
  const valid = (values ?? []).filter(v => v !== null && v !== undefined && Number.isFinite(v))
  const missing = value === null || value === undefined || !Number.isFinite(value)
  const cls = missing ? null : classify(key, value)
  const mean = valid.length ? valid.reduce((a, b) => a + b, 0) / valid.length : null
  const lo = valid.length ? Math.min(...valid) : null, hi = valid.length ? Math.max(...valid) : null
  const pct = missing ? null : percentile(values, value)
  const sections = []
  let around = null

  // Verdict: the everyday class first, then the comparison in one short sentence.
  const verdict = []
  if (missing) verdict.push('No data here: this point is outside the covered area or was not measured.')
  else {
    // After the class the comparison continues in lower case; on its own it starts the sentence.
    const open = w => (cls ? w : cap(w))
    if (cls) verdict.push(V(cap(cls), 'class'), ' · ')
    if (hi !== null && hi - lo < 1e-12) verdict.push(open(`the same everywhere on ${domain}.`))
    else if (value >= hi) verdict.push(open('the '), V('highest'), ` value on ${domain}.`)
    else if (value <= lo) verdict.push(open('the '), V('lowest'), ` value on ${domain}.`)
    else if (pct >= 50) verdict.push(open(sense.more), ' than ', V(`${Math.round(pct)} %`), ` of ${domain}.`)
    else verdict.push(open(sense.less), ' than ', V(`${Math.round(100 - pct)} %`), ` of ${domain}.`)
  }

  if (tooltip) sections.push({ id: 'what', heading: 'What this shows', lines: [[tooltip]] })

  if (!missing && valid.length > 1) {
    const lines = [[`Across ${domain} it ranges from `, V(fmt(lo)), ' to ', V(fmt(hi)), ', averaging ', V(fmt(mean)), '.']]
    const d = value - mean
    if (Math.abs(d) > 1e-9 && hi > lo) {
      if (Math.abs(d) / (hi - lo) < 0.05) lines.push(['Close to the average: ', V(fmt(Math.abs(d))), d > 0 ? ' above it.' : ' below it.'])
      else lines.push(['This point is ', V(fmt(Math.abs(d))), d > 0 ? ' above the average.' : ' below the average.'])
    }
    const note = unitNote(key, value)
    if (note) lines.push([cap(note), '.'])
    sections.push({ id: 'compare', heading: `Compared with ${domain}`, lines, graphic: 'distribution' })
  }

  if (!missing && grid) {
    const g = gradientAt(values, grid.nx, grid.ny, i, grid.spacing_east_m, grid.spacing_north_m)
    const perKm = grid.spacing_east_m >= 20000 ? 100 : 1, per = perKm === 100 ? 'per 100 km' : 'per km'
    const change = g.magnitude * 1000 * perKm, span = hi - lo || 1
    const lines = []
    if (g.peak) lines.push([cap(sense.peak), ': every neighbouring point is ', sense.less, '.'])
    else if (g.trough) lines.push([cap(sense.trough), ': every neighbouring point is ', sense.more, '.'])
    const flat = g.magnitude * Math.max(grid.spacing_east_m, grid.spacing_north_m) < span * 0.01
    around = { towardDeg: g.towardDeg, flat, peak: g.peak, trough: g.trough, more: sense.more }
    if (flat) lines.push(['Nearly the same all around: a flat patch of the field.'])
    else if (!g.peak && !g.trough) lines.push(['It gets ', sense.more, ' towards the ', V(compass8(g.towardDeg)), ', by ', V(`${formatNumber(change)}${unit ? ` ${unit}` : ''}`), ` ${per}.`])
    sections.push({ id: 'around', heading: 'Around this point', lines, graphic: 'neighbourhood' })
  }

  const series = (ctx.series ?? []).filter(p => p.value !== null && p.value !== undefined && Number.isFinite(p.value))
  if (!missing && series.length > 1) {
    const lines = [], prev = ctx.previous
    if (prev !== null && prev !== undefined && Number.isFinite(prev)) {
      const d = value - prev, span = (Math.max(...series.map(p => p.value)) - Math.min(...series.map(p => p.value))) || 1
      if (Math.abs(d) < span * 0.01) lines.push(['Steady since the previous frame.'])
      else lines.push([d > 0 ? 'Up ' : 'Down ', V(fmt(Math.abs(d))), ` since the previous frame${ctx.previousLabel ? ` (${ctx.previousLabel})` : ''}.`])
    } else lines.push(['This is the first frame of the run.'])
    const top = series.reduce((a, b) => (b.value > a.value ? b : a)), bottom = series.reduce((a, b) => (b.value < a.value ? b : a))
    lines.push(['Over the run it goes from ', V(fmt(bottom.value)), ' to ', V(fmt(top.value)), ' here; the peak comes at ', V(top.label), '.'])
    sections.push({ id: 'time', heading: 'Over time', lines, graphic: 'series' })
  }

  const also = (ctx.also ?? []).filter(a => a.value !== null && a.value !== undefined && Number.isFinite(a.value))
  const lines = []
  if (ctx.wind && ctx.wind.speed >= 0.05) lines.push(['Wind ', V(withUnit(ctx.wind.speed, 'm/s')), ' from the ', V(ctx.wind.from), `: ${ctx.wind.label}.`])
  // Waves (estimated from the wind in the viewer, waves.js): height, sea state, period, spacing, direction and speed.
  if (ctx.waves) {
    const w = ctx.waves
    if (!(w.hs >= 0.05)) lines.push([w.regime === 'damped by sea ice' ? 'No waves: sea ice damps them.' : 'A calm sea: no wind waves here.'])
    else lines.push(['Waves ', V(withUnit(w.hs, 'm')), ` high (${w.state}), every `, V(`${formatNumber(w.tp)} s`), ', ', V(`${formatNumber(w.length)} m`), ' apart, running towards the ', V(compass8(w.towardDeg)), ' at ', V(withUnit(w.speed, 'm/s')),
      `: ${w.regime === 'fully developed' ? 'a fully developed sea' : w.regime === 'depth-limited' ? 'limited by the shallow water' : w.regime === 'damped by sea ice' ? 'damped by sea ice' : `still growing over ${formatNumber(w.fetchKm)} km of open water`} (estimated from the wind).`])
  }
  for (const a of also.slice(0, 4)) { const c = classify(a.key, a.value); lines.push([`${a.title} `, V(withUnit(a.value, a.unit)), c ? ` (${c}).` : '.']) }
  if (lines.length) sections.push({ id: 'also', heading: 'Also at this point', lines, graphic: ctx.wind && ctx.wind.speed >= 0.05 ? 'compass' : null })

  if (ctx.where) sections.push({ id: 'where', heading: 'Where', lines: [ctx.where] })

  return {
    title, value: missing ? '—' : formatNumber(value), unit: missing ? '' : unit, verdict, sections, around,
    distribution: missing || hi === null || hi <= lo ? null : { lo, hi, value, mean, pct, less: sense.less, more: sense.more },
    short: missing ? 'no data' : cls ?? (pct === null ? '' : pct >= 50 ? `${sense.more} than ${Math.round(pct)} % of ${domain}` : `${sense.less} than ${Math.round(100 - pct)} % of ${domain}`),
  }
}

// Classes instead of amounts (cloud type, habitat, flood class, magnetic region): name the class, how common it is,
// what it borders and how it changes. ctx.categories[v] = { label, note?, color? } for class v.
function explainCategory(ctx) {
  const { title, tooltip, value, values, i, grid, categories } = ctx, domain = ctx.domain ?? 'the map'
  const info = v => categories[v] ?? null, label = v => info(v)?.label ?? String(v)
  const missing = value === null || value === undefined || !info(value)
  const valid = (values ?? []).filter(v => v !== null && v !== undefined && info(v))
  const counts = new Map()
  for (const v of valid) counts.set(v, (counts.get(v) ?? 0) + 1)
  const share = v => (valid.length ? (counts.get(v) ?? 0) / valid.length * 100 : 0)
  const ranked = [...counts.keys()].sort((a, b) => counts.get(b) - counts.get(a))
  const sections = [], verdict = []
  if (missing) verdict.push('No data here: this point is outside the covered area or was not classified.')
  // The class itself is the headline value; the verdict says how common it is.
  else if (share(value) >= 99.5) verdict.push(cap(`all of ${domain} is like this.`))
  else verdict.push(V(`${Math.round(share(value))} %`), ` of ${domain} is like this.`)
  if (tooltip) sections.push({ id: 'what', heading: 'What this shows', lines: [[tooltip]] })
  if (!missing && info(value).note) sections.push({ id: 'class', heading: cap(label(value)), lines: [[info(value).note]] })
  if (valid.length) {
    const top = ranked.slice(0, 3).map((v, k) => [k ? (k === ranked.slice(0, 3).length - 1 ? ' and ' : ', ') : '', V(label(v)), ` (${Math.round(share(v))} %)`]).flat()
    sections.push({ id: 'compare', heading: `Across ${domain}`, lines: [['Most common: ', ...top, '.']], graphic: 'categories' })
  }
  if (!missing && grid) {
    const x = i % grid.nx, y = Math.floor(i / grid.nx), near = new Set()
    for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) {
      if ((!a && !b) || x + a < 0 || y + b < 0 || x + a >= grid.nx || y + b >= grid.ny) continue
      const w = values[(y + b) * grid.nx + x + a]; if (w !== null && w !== undefined && info(w) && w !== value) near.add(w)
    }
    const lines = near.size ? [['It borders ', ...[...near].map((v, k) => [k ? ', ' : '', V(label(v))]).flat(), '.']] : [['All neighbouring points are ', V(label(value)), ' too.']]
    sections.push({ id: 'around', heading: 'Around this point', lines, graphic: 'neighbourhood' })
  }
  const series = ctx.series ?? []
  if (!missing && series.length > 1) {
    const lines = [], prev = ctx.previous
    if (prev !== null && prev !== undefined && info(prev)) lines.push(prev === value ? ['Unchanged since the previous frame.'] : ['Changed from ', V(label(prev)), ' since the previous frame.'])
    let changes = 0
    for (let k = 1; k < series.length; k++) if (series[k].value !== series[k - 1].value) changes++
    lines.push(changes ? ['Over the run it changes class ', V(String(changes)), ` time${changes === 1 ? '' : 's'} here.`] : ['It keeps this class for the whole run.'])
    sections.push({ id: 'time', heading: 'Over time', lines })
  }
  if (ctx.where) sections.push({ id: 'where', heading: 'Where', lines: [ctx.where] })
  return {
    title, value: missing ? '—' : label(value), unit: '', verdict, sections, around: null, distribution: null,
    categories: valid.length ? ranked.map(v => ({ label: label(v), color: info(v).color ?? null, share: share(v), current: v === value })) : null,
    short: missing ? 'no data' : `${Math.round(share(value))} % of ${domain} is like this`,
  }
}

function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : s }

// Where a cell lies, in words: geographic for global grids, kilometres from the south-west corner otherwise.
export function whereWords(grid, i, { geographic = false } = {}) {
  const x = i % grid.nx, y = Math.floor(i / grid.nx)
  if (geographic) {
    const lat = -90 + (y + 0.5) * 180 / grid.ny, lon = -180 + (x + 0.5) * 360 / grid.nx
    return [V(`${Math.abs(lat).toFixed(1)}° ${lat < 0 ? 'S' : 'N'}`), ', ', V(`${Math.abs(lon).toFixed(1)}° ${lon < 0 ? 'W' : 'E'}`), ` · column ${x + 1}, row ${y + 1} of ${grid.nx} × ${grid.ny}.`]
  }
  const east = (x + 0.5) * grid.spacing_east_m / 1000, north = (y + 0.5) * grid.spacing_north_m / 1000
  return [V(`${formatNumber(east)} km`), ' east and ', V(`${formatNumber(north)} km`), ` north of the map's south-west corner · column ${x + 1}, row ${y + 1} of ${grid.nx} × ${grid.ny}.`]
}

// Plain text of a line (for accessibility and tests).
export const lineText = line => line.map(t => (typeof t === 'string' ? t : t.v)).join('')
