<script setup>
// Coupled Earth: the climate drivers, the CO2 impact, the 3-10 day weather forecast, the 1-3 day wind forecast and the
// severe-weather warnings of the coupled runs (terraflux-run-earth-climate and terraflux-run-earth-forecast). The maps
// are flat: the forecast grid in its azimuthal frame, the climate on a global latitude-longitude grid.
import { ref, computed, watch, onMounted, onBeforeUnmount, nextTick } from 'vue'
import {
  EARTH_VIEWS, HORIZONS, HAZARDS, LEVELS, SYMBOLS, PERIODS, FORECAST_FIELDS, SEVERE_LAYERS, CO2_LAYERS, CLIMATE_LAYERS, CATEGORY_LABELS,
  earthDocumentKind, validateEarthForecast, validateEarthClimate, forecastGrid, projectToGrid, climateGrid, geoToClimateCell,
  symbolInfo, hazardInfo, fieldInfo, noonFrame, dayLabel, validTime, cityForecast, cellForecast, skillSeries, usefulLead,
  windFrames, windField, directionFrom, compass, beaufort, cityWindTable, meteogram,
  warningMatrix, warningLevels, warningList, warningCounts, peakValueLabel, trackLines, trackSpread, categoryColor,
  driverBars, scenarioChart, forcingByGroup, impactRows, feedbackRows, groupColor, scenarioColor,
  co2LayerInfo, co2Budget, couplingGraph, edgePath, strengthWeight, keyAnswers, contourLines,
  temperatureColor, windColor, probabilityColor, levelColor, warmingColor, localWarmingColor, fmt, signed,
} from './coupledEarth.js'

const props = defineProps({ document: { type: Object, default: null }, goto: { type: Object, default: null } })
const base = import.meta.env.BASE_URL
const view = ref('overview')
const forecastDoc = ref(null), climateDoc = ref(null)
const forecast = computed(() => forecastDoc.value?.earth_forecast ?? null)
const climate = computed(() => climateDoc.value?.earth_climate ?? null)
const loading = ref(''), error = ref('')
const sources = ref({ forecast: '', climate: '' })

function accept(doc, label) {
  const kind = earthDocumentKind(doc)
  if (kind === 'forecast') { validateEarthForecast(doc); forecastDoc.value = Object.freeze(doc); sources.value.forecast = label; resetForecastState() }
  else if (kind === 'climate') { validateEarthClimate(doc); climateDoc.value = Object.freeze(doc); sources.value.climate = label; resetClimateState() }
  else throw new Error('Expected a coupled forecast (earth_forecast) or coupled climate (earth_climate) timeline.')
  return kind
}
async function load(kind) {
  if ((kind === 'forecast' && forecastDoc.value) || (kind === 'climate' && climateDoc.value)) return
  loading.value = kind === 'forecast' ? 'Loading the ensemble forecast…' : 'Loading the climate run…'
  try {
    const response = await fetch(`${base}coupled-${kind}/engine-timeline.json`)
    if (!response.ok) throw new Error(`Could not load the coupled ${kind} example (${response.status}).`)
    accept(await response.json(), kind === 'forecast' ? 'Engine output · coupled ensemble forecast (synthetic situation)' : 'Engine output · coupled climate')
  } catch (err) { error.value = err.message } finally { loading.value = '' }
}
async function ensureData() {
  const needs = view.value === 'overview' ? ['climate', 'forecast'] : [EARTH_VIEWS.find(v => v.key === view.value).needs]
  for (const k of needs) await load(k)
}
async function upload(e) {
  error.value = ''
  const file = e.target.files[0]
  if (!file) return
  try {
    if (file.size > 30 * 1024 * 1024) throw new Error('Choose a JSON file smaller than 30 MB.')
    const kind = accept(JSON.parse(await file.text()), file.name)
    if (EARTH_VIEWS.find(v => v.key === view.value).needs !== kind) view.value = kind === 'forecast' ? 'weather' : 'drivers'
  } catch (err) { error.value = err.message } finally { e.target.value = '' }
}
watch(view, ensureData)
watch(() => props.goto, g => { if (g?.view) view.value = g.view })
watch(() => props.document, doc => { if (doc) { try { const kind = accept(doc, 'Opened file'); view.value = kind === 'forecast' ? 'weather' : 'drivers' } catch (err) { error.value = err.message } } })

// ---- Forecast state ------------------------------------------------------------------------------------------------
const horizon = ref(5), day = ref(1), weatherField = ref('tmax_c'), selectedCell = ref(null), hoverCell = ref(null)
const windIndex = ref(0), windLayer = ref('speed'), windCityName = ref(''), playing = ref(false)
const severeDay = ref(1), severeHazard = ref('all'), severeLayer = ref('warnings'), showTracks = ref(true), trackHour = ref(48)
function resetForecastState() {
  const f = forecastDoc.value.earth_forecast
  horizon.value = HORIZONS.filter(h => h <= f.forecast_days).includes(5) ? 5 : Math.min(f.forecast_days, 3)
  day.value = 1; selectedCell.value = null; windIndex.value = 0; severeDay.value = 1
  const firstRed = warningMatrix(f).find(d => d.hazards.some(h => h.level >= 3))
  if (firstRed) severeDay.value = firstRed.day
  windCityName.value = cityWindTable(f).reduce((a, b) => (b.peak && (!a || b.peak.gust > a.peak.gust) ? b : a), null)?.name ?? ''
  trackHour.value = Math.min(72, f.frame_hours.at(-1))
}
const fgrid = computed(() => (forecastDoc.value ? forecastGrid(forecastDoc.value) : null))
const horizons = computed(() => (forecast.value ? HORIZONS.filter(h => h <= forecast.value.forecast_days) : HORIZONS))
watch(horizon, h => { if (day.value > h) day.value = h })
const dayBlock = computed(() => forecast.value?.days[day.value - 1] ?? null)
const cities = computed(() => (forecast.value ? cityForecast(forecast.value, horizon.value) : []))
const skill = computed(() => (forecast.value ? skillSeries(forecast.value) : []))
const lead = computed(() => (forecast.value ? usefulLead(forecast.value) : null))
const inspectCell = computed(() => selectedCell.value ?? hoverCell.value)
const inspector = computed(() => {
  const f = forecast.value, c = inspectCell.value
  if (!f || c === null || !['weather', 'severe'].includes(view.value)) return null
  return { cell: c, lat: f.static.latitude_deg[c], lon: f.static.longitude_deg[c], land: f.static.land_mask[c], elevation: f.static.elevation_m[c], sst: f.static.sea_surface_temperature_c[c], days: cellForecast(f, c, view.value === 'severe' ? f.forecast_days : horizon.value) }
})
// Skill chart: pressure anomaly correlation and the temperature error against the spread, over the lead time.
const skillChart = computed(() => {
  const s = skill.value
  if (!s.length) return null
  const h1 = s.at(-1).hour, X = h => h / (h1 || 1) * 100
  const accY = v => 48 - Math.max(0, v) * 44
  const tMax = Math.max(0.5, ...s.map(p => Math.max(p.temperatureRmse, p.temperatureSpread, p.temperatureClimatology))) * 1.05
  const tY = v => 48 - v / tMax * 44
  const line = (key, Y) => s.filter(p => p[key] !== null).map(p => `${X(p.hour).toFixed(2)},${Y(p[key]).toFixed(2)}`).join(' ')
  return { acc: line('pressureAcc', accY), rain: line('rainSkill', accY), accLine: accY(0.6), tRmse: line('temperatureRmse', tY), tSpread: line('temperatureSpread', tY), tClim: line('temperatureClimatology', tY), tMax,
    ticks: [0, 24, 48, 72, 96, 120, 168, 240].filter(h => h <= h1).map(h => ({ h, x: X(h) })), useful: lead.value ? X(lead.value.hours) : null }
})

// ---- Wind ----------------------------------------------------------------------------------------------------------
const windList = computed(() => (forecast.value ? windFrames(forecast.value) : []))
const windFrame = computed(() => windList.value[Math.min(windIndex.value, windList.value.length - 1)] ?? null)
const windNow = computed(() => (forecastDoc.value && windFrame.value ? windField(forecastDoc.value, windFrame.value.index) : null))
const windCities = computed(() => (forecast.value ? cityWindTable(forecast.value) : []))
const windCity = computed(() => forecast.value?.cities.find(c => c.name === windCityName.value && c.hourly) ?? forecast.value?.cities.find(c => c.inside && c.hourly) ?? null)
const windGram = computed(() => (windCity.value ? meteogram(windCity.value) : null))
const windStats = computed(() => {
  const f = forecast.value, w = windNow.value, fr = windFrame.value
  if (!f || !w) return null
  const frame = f.frames[fr.index]
  let max = 0, at = 0, gust = 0, gustAt = 0
  w.speed.forEach((s, i) => { if (s > max) { max = s; at = i } })
  frame.gust_m_s.forEach((g, i) => { if (g > gust) { gust = g; gustAt = i } })
  const orange = frame.p_gust_orange.filter(p => p >= 30).length
  return { max, at, gust, gustAt, orange, lat: f.static.latitude_deg, lon: f.static.longitude_deg }
})
let timer = null
function togglePlay() {
  playing.value = !playing.value
  clearInterval(timer)
  if (playing.value) timer = setInterval(() => { windIndex.value = (windIndex.value + 1) % windList.value.length }, 700)
}
onBeforeUnmount(() => clearInterval(timer))

// ---- Severe --------------------------------------------------------------------------------------------------------
const matrix = computed(() => (forecast.value ? warningMatrix(forecast.value) : []))
const severeRegions = computed(() => (forecast.value ? warningList(forecast.value, severeDay.value, severeHazard.value) : []))
const severeCounts = computed(() => (forecast.value ? warningCounts(forecast.value, severeDay.value) : { 1: 0, 2: 0, 3: 0 }))
const tracks = computed(() => (forecast.value ? trackLines(forecast.value) : []))
const trackHours = computed(() => [...new Set(tracks.value.filter(t => !t.truth).flatMap(t => t.points.map(p => p.hour)))].sort((a, b) => a - b))
const spreadRows = computed(() => (forecast.value ? [12, 24, 48, 72, 96, 120].map(h => trackSpread(forecast.value, h)).filter(Boolean) : []))
const severeInfo = computed(() => SEVERE_LAYERS.find(l => l.key === severeLayer.value) ?? SEVERE_LAYERS[0])

// ---- Climate state -------------------------------------------------------------------------------------------------
const scenario = ref(''), period = ref('today'), climateLayer = ref('warming'), climateFrame = ref(0), co2Layer = ref('impact_score'), seriesKey = ref('global_k')
function resetClimateState() {
  const c = climateDoc.value.earth_climate
  scenario.value = c.central_scenario
  climateFrame.value = c.map.frames.length - 1
}
const cgrid = computed(() => (climate.value ? climateGrid(climate.value) : null))
const bars = computed(() => (climate.value && scenario.value ? driverBars(climate.value, scenario.value, period.value) : null))
const SERIES_CHOICES = [
  { key: 'global_k', label: 'Global warming (K)', band: ['global_low_k', 'global_high_k'] },
  { key: 'total_forcing_w_m2', label: 'Total forcing (W/m²)' },
  { key: 'co2_ppm', label: 'CO₂ (ppm)' },
  { key: 'arctic_k', label: 'Arctic warming (K)' },
  { key: 'heat_50yr_factor', label: '1-in-50-year heat: times as often' },
  { key: 'heavy_rain_factor', label: 'Heavy rain: times as often' },
  { key: 'september_ice_mkm2', label: 'Arctic September sea ice (million km²)' },
  { key: 'sea_level_since_1900_m', label: 'Sea level since 1900 (m)' },
  { key: 'ocean_heat_zj', label: 'Ocean heat (ZJ)' },
  { key: 'ph', label: 'Ocean surface pH' },
  { key: 'overturning_sv', label: 'Atlantic overturning (Sv)' },
]
const seriesChoice = computed(() => SERIES_CHOICES.find(s => s.key === seriesKey.value) ?? SERIES_CHOICES[0])
const warmingChart = computed(() => (climate.value ? scenarioChart(climate.value, seriesChoice.value.key, { focus: scenario.value, band: seriesChoice.value.band ?? null }) : null))
const groupChart = computed(() => {
  const c = climate.value
  if (!c || !scenario.value) return null
  const groups = forcingByGroup(c, scenario.value), years = c.years
  const all = groups.flatMap(g => g.values)
  const lo = Math.min(0, ...all) * 1.08, hi = Math.max(0, ...all) * 1.08
  const X = y => (y - years[0]) / (years.at(-1) - years[0]) * 100, Y = v => 48 - (v - lo) / ((hi - lo) || 1) * 44
  return { lo, hi, zero: Y(0), today: X(c.today), lines: groups.map(g => ({ ...g, points: g.values.map((v, k) => `${X(years[k]).toFixed(2)},${Y(v).toFixed(2)}`).join(' ') })) }
})
const impacts = computed(() => (climate.value ? impactRows(climate.value) : []))
const feedbacks = computed(() => (climate.value ? feedbackRows(climate.value) : []))
const climateYear = computed(() => climate.value?.map.frames[Math.min(climateFrame.value, climate.value.map.frames.length - 1)]?.year ?? null)
const budget = computed(() => (climate.value ? co2Budget(climate.value) : null))
const co2Chart = computed(() => (climate.value ? scenarioChart(climate.value, 'co2_ppm', { focus: scenario.value }) : null))
const phChart = computed(() => (climate.value ? scenarioChart(climate.value, 'ph', { focus: scenario.value }) : null))
const graph = computed(() => (climate.value ? couplingGraph(climate.value) : null))
const hoverEdge = ref(null)
const answers = computed(() => keyAnswers(climate.value, forecast.value))
const sensitivity = computed(() => {
  const runs = forecast.value?.couplings.runs ?? []
  const max = Math.max(1, ...runs.flatMap(r => [Math.abs(r.thunder_change_pct), Math.abs(r.precip_change_pct)]))
  return { max, runs }
})
const scenarioName = key => climate.value?.scenarios.find(s => s.key === key)?.name ?? key

// ---- Map drawing ---------------------------------------------------------------------------------------------------
const mapHost = ref(null), canvas = ref(null)
const mapKind = computed(() => (['weather', 'wind', 'severe'].includes(view.value) ? 'forecast' : ['drivers', 'co2'].includes(view.value) ? 'climate' : null))
const mapGrid = computed(() => (mapKind.value === 'forecast' ? fgrid.value : mapKind.value === 'climate' ? cgrid.value : null))
const mapSize = ref({ w: 800, h: 500 })
let resize = null
onMounted(() => {
  if (props.document) { try { const kind = accept(props.document, 'Opened file'); view.value = kind === 'forecast' ? 'weather' : 'drivers' } catch (err) { error.value = err.message } }
  if (props.goto?.view && !props.document) view.value = props.goto.view
  ensureData()
  resize = new ResizeObserver(() => layout())
  watch(mapHost, el => { if (el) { resize.observe(el); layout() } }, { immediate: true, flush: 'post' })
})
onBeforeUnmount(() => resize?.disconnect())
function layout() {
  const el = mapHost.value, g = mapGrid.value
  if (!el || !g) return
  const w = el.clientWidth, h = Math.round(w * g.ny / g.nx)
  mapSize.value = { w, h }
  nextTick(draw)
}
watch(mapGrid, () => nextTick(layout))

// The value and colour of a cell in the current view.
const cellValue = computed(() => {
  const f = forecast.value, c = climate.value
  if (mapKind.value === 'forecast' && f) {
    if (view.value === 'weather') {
      const info = fieldInfo(weatherField.value), values = dayBlock.value[weatherField.value]
      return { values, color: i => info.color(values[i]), label: info.label, unit: info.unit }
    }
    if (view.value === 'wind') {
      const frame = f.frames[windFrame.value.index]
      if (windLayer.value === 'gust') return { values: frame.gust_m_s, color: i => windColor(frame.gust_m_s[i]), label: 'Gusts (ensemble mean)', unit: 'm/s' }
      if (windLayer.value === 'p_gust_orange') return { values: frame.p_gust_orange, color: i => probabilityColor(frame.p_gust_orange[i]), label: `Chance of gusts ≥ ${f.warnings.thresholds.gust_m_s[1]} m/s`, unit: '%' }
      const s = windNow.value.speed
      return { values: s, color: i => windColor(s[i]), label: 'Wind at 10 m (ensemble mean)', unit: 'm/s' }
    }
    if (view.value === 'severe') {
      if (severeLayer.value === 'warnings') {
        const lv = warningLevels(f, severeDay.value, severeHazard.value)
        return { values: lv, color: i => levelColor(lv[i]), label: `Warnings · ${severeHazard.value === 'all' ? 'all hazards' : hazardInfo(severeHazard.value).label}`, unit: '', level: true }
      }
      const values = f.days[severeDay.value - 1][severeLayer.value], info = severeInfo.value
      return { values, color: i => info.color(values[i]), label: info.label, unit: info.unit }
    }
  }
  if (mapKind.value === 'climate' && c) {
    if (view.value === 'drivers') {
      const frame = c.map.frames[Math.min(climateFrame.value, c.map.frames.length - 1)]
      if (climateLayer.value === 'aerosol') return { values: frame.aerosol_k, color: i => localWarmingColor(frame.aerosol_k[i] * 4), label: `Aerosol cooling ${frame.year} · ${scenarioName(c.central_scenario)}`, unit: 'K_small' }
      if (climateLayer.value === 'overturning') return { values: frame.overturning_k, color: i => localWarmingColor(frame.overturning_k[i] * 4), label: `Cooling from a weaker overturning ${frame.year} · ${scenarioName(c.central_scenario)}`, unit: 'K_small' }
      if (climateLayer.value === 'pattern') return { values: c.map.pattern, color: i => warmingColor(c.map.pattern[i] - 1, 2), label: 'Warming pattern (local / global mean)', unit: '×' }
      const values = frame.local_warming_k[scenario.value]
      return { values, color: i => localWarmingColor(values[i]), label: `Local warming ${frame.year} · ${scenarioName(scenario.value)}`, unit: 'K' }
    }
    const info = co2LayerInfo(co2Layer.value), values = c.co2[co2Layer.value]
    return { values, color: i => info.color(values[i]), label: info.label, unit: info.unit }
  }
  return null
})
const mapTitle = computed(() => {
  const f = forecast.value
  if (view.value === 'weather' && f) return `${fieldInfo(weatherField.value).label} · day ${day.value} (${dayLabel(f, day.value)})`
  if (view.value === 'wind' && f && windFrame.value) return `Wind · +${windFrame.value.hour} h · ${validTime(f, windFrame.value.hour).toISOString().slice(0, 16).replace('T', ' ')} UTC`
  if (view.value === 'severe' && f) return `${cellValue.value?.label ?? ''} · day ${severeDay.value} (${dayLabel(f, severeDay.value)})`
  return cellValue.value?.label ?? ''
})

function draw() {
  const cv = canvas.value, g = mapGrid.value, cv2 = cellValue.value
  if (!cv || !g || !cv2) return
  const dpr = Math.min(window.devicePixelRatio || 1, 2), { w, h } = mapSize.value
  cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr)
  cv.style.width = `${w}px`; cv.style.height = `${h}px`
  const ctx = cv.getContext('2d')
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
  ctx.fillStyle = '#0b1a23'; ctx.fillRect(0, 0, w, h)
  const cw = w / g.nx, ch = h / g.ny
  const X = x => x * cw, Y = y => h - y * ch   // fractional cells from the south-west corner -> pixels
  const forecastMap = mapKind.value === 'forecast'
  const land = forecastMap ? forecast.value.static.land_mask : climate.value.map.land_mask
  // Cells: the base colour (land grey-green, sea dark blue), then the field.
  // Cell edges snapped to whole pixels: no seams between the cells.
  const px = i => Math.round(X(i)), py = j => Math.round(Y(j))
  for (let j = 0; j < g.ny; j++) for (let i = 0; i < g.nx; i++) {
    const k = j * g.nx + i, x0 = px(i), y0 = py(j + 1), cwk = px(i + 1) - x0, chk = py(j) - y0
    ctx.fillStyle = land[k] ? '#26352c' : '#0d2433'
    ctx.fillRect(x0, y0, cwk, chk)
    const col = cv2.color(k)
    if (col) {
      ctx.globalAlpha = cv2.level ? 0.85 : 1
      ctx.fillStyle = col; ctx.fillRect(x0, y0, cwk, chk)
      ctx.globalAlpha = 1
    }
  }
  // Coastlines from the land mask.
  ctx.strokeStyle = 'rgba(235,245,250,.75)'; ctx.lineWidth = 1; ctx.beginPath()
  for (let j = 0; j < g.ny; j++) for (let i = 0; i < g.nx; i++) {
    const k = j * g.nx + i
    if (!land[k]) continue
    if (i + 1 < g.nx && !land[k + 1]) { ctx.moveTo(X(i + 1), Y(j)); ctx.lineTo(X(i + 1), Y(j + 1)) }
    if (i > 0 && !land[k - 1]) { ctx.moveTo(X(i), Y(j)); ctx.lineTo(X(i), Y(j + 1)) }
    if (j + 1 < g.ny && !land[k + g.nx]) { ctx.moveTo(X(i), Y(j + 1)); ctx.lineTo(X(i + 1), Y(j + 1)) }
    if (j > 0 && !land[k - g.nx]) { ctx.moveTo(X(i), Y(j)); ctx.lineTo(X(i + 1), Y(j)) }
  }
  ctx.stroke()
  // Graticule.
  ctx.strokeStyle = 'rgba(200,220,230,.16)'; ctx.lineWidth = 0.8; ctx.fillStyle = 'rgba(200,220,230,.55)'; ctx.font = '10px DM Sans, sans-serif'
  if (forecastMap) {
    const st = forecast.value.static
    for (const [values, levels, suffix] of [[st.latitude_deg, [10, 20, 30, 40, 50, 60, 70], '°N'], [st.longitude_deg, [-120, -100, -80, -60, -40, -20, 0, 20, 40], '°']]) {
      for (const level of levels) {
        const segs = contourLines(values, g.nx, g.ny, level)
        if (!segs.length) continue
        ctx.beginPath()
        for (const [a, b] of segs) { ctx.moveTo(X(a[0]), Y(a[1])); ctx.lineTo(X(b[0]), Y(b[1])) }
        ctx.stroke()
        const labelAt = suffix === '°N' ? segs.reduce((m, s) => (s[0][0] < m[0][0] ? s : m)) : segs.reduce((m, s) => (s[0][1] < m[0][1] ? s : m))
        ctx.fillText(suffix === '°N' ? `${level}°N` : `${Math.abs(level)}°${level < 0 ? 'W' : level > 0 ? 'E' : ''}`, X(labelAt[0][0]) + 3, Y(labelAt[0][1]) - 3)
      }
    }
  } else {
    ctx.beginPath()
    for (let lat = -60; lat <= 60; lat += 30) { const y = Y((lat + 90) / g.dy); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.fillText(`${Math.abs(lat)}°${lat < 0 ? 'S' : lat > 0 ? 'N' : ''}`, 3, y - 3) }
    for (let lon = -120; lon <= 120; lon += 60) { const x = X((lon + 180) / g.dx); ctx.moveTo(x, 0); ctx.lineTo(x, h) }
    ctx.stroke()
  }
  if (forecastMap) drawForecastOverlays(ctx, g, X, Y, cw, ch)
  else drawClimateOverlays(ctx, g, X, Y)
  // The selected cell.
  const sel = inspectCell.value
  if (sel !== null && forecastMap && ['weather', 'severe'].includes(view.value)) {
    ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 2
    ctx.strokeRect(X(sel % g.nx), Y(Math.floor(sel / g.nx) + 1), cw, ch)
  }
}

function drawIsobars(ctx, g, X, Y, pressure) {
  ctx.strokeStyle = 'rgba(255,255,255,.55)'; ctx.lineWidth = 1
  ctx.fillStyle = 'rgba(255,255,255,.8)'; ctx.font = '9px DM Sans, sans-serif'
  const lo = Math.ceil(Math.min(...pressure) / 4) * 4, hi = Math.max(...pressure)
  for (let level = lo; level <= hi; level += 4) {
    const segs = contourLines(pressure, g.nx, g.ny, level)
    if (!segs.length) continue
    ctx.beginPath()
    for (const [a, b] of segs) { ctx.moveTo(X(a[0]), Y(a[1])); ctx.lineTo(X(b[0]), Y(b[1])) }
    ctx.stroke()
    const mid = segs[Math.floor(segs.length / 2)][0]
    ctx.fillText(String(level), X(mid[0]) + 2, Y(mid[1]) - 2)
  }
}

function drawArrow(ctx, x, y, u, v, scale) {
  const len = Math.hypot(u, v) * scale
  if (len < 1.5) return
  const a = Math.atan2(-v, u)   // screen y points down
  const x2 = x + Math.cos(a) * len, y2 = y + Math.sin(a) * len
  ctx.moveTo(x, y); ctx.lineTo(x2, y2)
  ctx.moveTo(x2, y2); ctx.lineTo(x2 - Math.cos(a - 0.5) * 4, y2 - Math.sin(a - 0.5) * 4)
  ctx.moveTo(x2, y2); ctx.lineTo(x2 - Math.cos(a + 0.5) * 4, y2 - Math.sin(a + 0.5) * 4)
}

function drawForecastOverlays(ctx, g, X, Y, cw, ch) {
  const f = forecast.value
  const P = (lat, lon) => { const p = projectToGrid(lat, lon, g); return [X(p.x), Y(p.y)] }
  if (view.value === 'weather') {
    drawIsobars(ctx, g, X, Y, f.frames[noonFrame(f, day.value)].pressure_hpa)
    if (weatherField.value === 'symbol') {
      ctx.font = `${Math.max(9, Math.min(cw, ch) * 0.9)}px DejaVu Sans, DM Sans, sans-serif`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'
      for (let j = 0; j < g.ny; j += 2) for (let i = 0; i < g.nx; i += 2) {
        const k = j * g.nx + i, s = symbolInfo(dayBlock.value.symbol[k])
        ctx.fillStyle = '#0b1a23'; ctx.fillText(s.glyph, X(i + 0.5), Y(j + 0.5))
      }
      ctx.textAlign = 'start'; ctx.textBaseline = 'alphabetic'
    }
  }
  if (view.value === 'wind') {
    const frame = f.frames[windFrame.value.index], w = windNow.value
    drawIsobars(ctx, g, X, Y, frame.pressure_hpa)
    ctx.strokeStyle = 'rgba(10,20,28,.85)'; ctx.lineWidth = 1.1; ctx.beginPath()
    const stride = cw < 14 ? 2 : 1
    for (let j = 0; j < g.ny; j += stride) for (let i = 0; i < g.nx; i += stride) { const k = j * g.nx + i; drawArrow(ctx, X(i + 0.5), Y(j + 0.5), w.u[k], w.v[k], cw * stride * 0.06) }
    ctx.stroke()
    // Highs and lows of member 1 (the pressure systems that steer the weather).
    ctx.font = 'bold 15px DM Sans, sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'
    for (const s of frame.systems_member_1) {
      const [x, y] = P(s.latitude_deg, s.longitude_deg)
      if (x < 0 || y < 0 || x > mapSize.value.w || y > mapSize.value.h) continue
      ctx.fillStyle = s.kind === 'low' ? '#ff6b6b' : '#64b5f6'; ctx.fillText(s.kind === 'low' ? 'L' : 'H', x, y)
    }
    ctx.textAlign = 'start'; ctx.textBaseline = 'alphabetic'
    // The hurricane of every member at this hour.
    for (const t of tracks.value) {
      const p = t.points.find(q => q.hour === windFrame.value.hour)
      if (!p) continue
      const [x, y] = P(p.lat, p.lon)
      ctx.strokeStyle = t.truth ? '#ffffff' : categoryColor(p.category); ctx.lineWidth = t.truth ? 2 : 1.2
      ctx.beginPath(); ctx.arc(x, y, t.truth ? 5 : 3.5, 0, 2 * Math.PI); ctx.stroke()
    }
  }
  if (view.value === 'severe' && showTracks.value) {
    for (const t of tracks.value) {
      ctx.lineWidth = t.truth ? 2.2 : 1.2; ctx.setLineDash(t.truth ? [5, 3] : [])
      for (let k = 1; k < t.points.length; k++) {
        const a = t.points[k - 1], b = t.points[k]
        ctx.strokeStyle = t.truth ? 'rgba(255,255,255,.95)' : categoryColor(b.category)
        ctx.globalAlpha = t.truth ? 1 : 0.75
        ctx.beginPath(); ctx.moveTo(...P(a.lat, a.lon)); ctx.lineTo(...P(b.lat, b.lon)); ctx.stroke()
      }
      ctx.globalAlpha = 1; ctx.setLineDash([])
      const p = t.points.find(q => q.hour === trackHour.value)
      if (p) { const [x, y] = P(p.lat, p.lon); ctx.fillStyle = t.truth ? '#fff' : categoryColor(p.category); ctx.beginPath(); ctx.arc(x, y, t.truth ? 4 : 2.6, 0, 2 * Math.PI); ctx.fill() }
    }
  }
  // Cities.
  ctx.font = '10px DM Sans, sans-serif'
  for (const c of f.cities.filter(x => x.inside)) {
    const [x, y] = P(c.latitude_deg, c.longitude_deg)
    ctx.fillStyle = '#0b1a23'; ctx.beginPath(); ctx.arc(x, y, 3.2, 0, 2 * Math.PI); ctx.fill()
    ctx.fillStyle = c.name === windCityName.value && view.value === 'wind' ? '#ffd166' : '#f4f8fa'; ctx.beginPath(); ctx.arc(x, y, 2.2, 0, 2 * Math.PI); ctx.fill()
    ctx.fillStyle = 'rgba(10,20,28,.8)'; ctx.fillText(c.name, x + 5.5, y + 3.5)
    ctx.fillStyle = '#eef5f8'; ctx.fillText(c.name, x + 5, y + 3)
  }
}

function drawClimateOverlays(ctx, g, X, Y) {
  const c = climate.value
  ctx.font = '10px DM Sans, sans-serif'
  const P = (lat, lon) => { const p = geoToClimateCell(lat, lon, g); return [X(p.x), Y(p.y)] }
  if (view.value === 'co2') {
    for (const r of c.co2.regions.slice(0, 8)) {
      if (r.latitude_deg === null) continue
      const [x, y] = P(r.latitude_deg, r.longitude_deg), rad = 3 + Math.sqrt(r.fossil_gt_co2_yr + r.land_use_gt_co2_yr) * 4
      ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 1.2; ctx.beginPath(); ctx.arc(x, y, rad, 0, 2 * Math.PI); ctx.stroke()
      ctx.fillStyle = '#ffffff'; ctx.fillText(r.name.split(' (')[0], x + rad + 2, y + 3)
    }
    ctx.fillStyle = '#ffd166'
    for (const hsp of c.co2.hotspots.slice(0, 6)) {
      const [x, y] = P(hsp.latitude_deg, hsp.longitude_deg)
      ctx.beginPath(); ctx.moveTo(x, y - 6); ctx.lineTo(x + 5, y + 4); ctx.lineTo(x - 5, y + 4); ctx.closePath(); ctx.fill()
    }
  }
}

watch([cellValue, mapSize, inspectCell, showTracks, trackHour, windCityName], () => nextTick(draw))

function cellAt(event) {
  const g = mapGrid.value, el = canvas.value
  if (!g || !el) return null
  const r = el.getBoundingClientRect(), x = (event.clientX - r.left) / r.width * g.nx, y = (1 - (event.clientY - r.top) / r.height) * g.ny
  const i = Math.floor(x), j = Math.floor(y)
  return i >= 0 && j >= 0 && i < g.nx && j < g.ny ? j * g.nx + i : null
}
const hoverText = ref('')
function onMove(event) {
  const k = cellAt(event)
  hoverCell.value = k
  hoverText.value = k === null ? '' : describeCell(k)
}
function onLeave() { hoverCell.value = null; hoverText.value = '' }
function onClick(event) {
  const k = cellAt(event)
  if (view.value === 'wind' && forecast.value && k !== null) {
    // Pick the nearest city for the meteogram.
    const lat = forecast.value.static.latitude_deg[k], lon = forecast.value.static.longitude_deg[k]
    const near = forecast.value.cities.filter(c => c.inside && c.hourly).reduce((a, c) => { const d = Math.hypot(c.latitude_deg - lat, (c.longitude_deg - lon) * Math.cos(lat * Math.PI / 180)); return d < a.d ? { d, c } : a }, { d: Infinity, c: null })
    if (near.c) windCityName.value = near.c.name
    return
  }
  selectedCell.value = selectedCell.value === k ? null : k
}
function describeCell(k) {
  const v = cellValue.value
  if (!v) return ''
  if (mapKind.value === 'forecast') {
    const st = forecast.value.static
    const where = `${fmt(st.latitude_deg[k], 1)}°N ${fmt(Math.abs(st.longitude_deg[k]), 1)}°${st.longitude_deg[k] < 0 ? 'W' : 'E'}${st.land_mask[k] ? ` · land ${Math.round(st.elevation_m[k])} m` : ' · sea'}`
    if (v.level) return `${where} · ${LEVELS[v.values[k]].label}`
    if (view.value === 'weather' && weatherField.value === 'symbol') return `${where} · ${symbolInfo(v.values[k]).label}`
    if (view.value === 'wind') return `${where} · ${fmt(v.values[k], 1)} ${v.unit}${windLayer.value === 'speed' ? ` from ${compass(directionFrom(windNow.value.u[k], windNow.value.v[k]))} (Bft ${beaufort(v.values[k])})` : ''}`
    return `${where} · ${fmt(v.values[k], v.unit === '%' ? 0 : 1)} ${v.unit}`
  }
  const g = cgrid.value, lat = -90 + (Math.floor(k / g.nx) + 0.5) * g.dy, lon = -180 + ((k % g.nx) + 0.5) * g.dx
  const where = `${Math.abs(lat).toFixed(0)}°${lat < 0 ? 'S' : 'N'} ${Math.abs(lon).toFixed(0)}°${lon < 0 ? 'W' : 'E'}`
  const val = v.values[k]
  return `${where} · ${val === null ? 'land' : fmt(val, Math.abs(val) < 1 ? 3 : 1)} ${v.unit === 'K_small' ? 'K' : v.unit}`
}
// Legend of the current map: a gradient or a list of classes.
const legend = computed(() => {
  const v = cellValue.value
  if (!v) return null
  if (v.level) return { classes: LEVELS.slice(1).map(l => ({ label: l.label, color: l.color })) }
  if (view.value === 'weather' && weatherField.value === 'symbol') return { classes: SYMBOLS.map(s => ({ label: `${s.glyph} ${s.label}`, color: s.color })) }
  const stops = {
    '°C': [[-10, '−10'], [5, '5'], [20, '20'], [35, '35 °C']], 'mm': [[0.3, '0.3'], [3, '3'], [30, '30'], [150, '150 mm']], '%': [[10, '10'], [40, '40'], [70, '70'], [100, '100 %']],
    'm/s': [[0, '0'], [10, '10'], [20, '20'], [30, '30'], [40, '40 m/s']], 'K': view.value === 'drivers' ? [[-2, '−2'], [0, '0'], [2, '2'], [4, '4'], [6, '6'], [8, '8 K']] : [[-0.5, '−0.5'], [0, '0'], [1, '1'], [2, '2 K']],
    't CO₂/km²/yr': [[-1000, '−1000 (sink)'], [-10, '−10'], [0, '0'], [10, '10'], [1000, '1000 (source)']], 'ppm': [[-4, '−4'], [0, '0'], [4, '+4 ppm']], 'pH': [[0.07, '0.07'], [0.085, '0.085'], [0.1, '0.10 pH']],
    'Ω': [[0.6, '0.6'], [1, '1 (corrosive below)'], [3, '3']], '0–1': [[0, 'low'], [0.5, ''], [1, 'high']], '×': [[0, '0'], [1, '1'], [3, '3 ×']], 'K_small': [[-0.5, '−0.5'], [-0.25, '−0.25'], [0, '0 K']], 'K_spread': [[0, '0'], [1.5, '1.5'], [3, '3 K']],
  }
  const unit = view.value === 'weather' && weatherField.value === 'tmax_spread_k' ? 'K_spread' : v.unit
  const s = stops[unit]
  if (!s) return null
  const colorOf = x => {
    if (view.value === 'weather') return fieldInfo(weatherField.value).color(x)
    if (view.value === 'wind') return windLayer.value === 'p_gust_orange' ? probabilityColor(x) : windColor(x)
    if (view.value === 'severe') return severeInfo.value.color(x)
    if (view.value === 'drivers') return climateLayer.value === 'pattern' ? warmingColor(x - 1, 2) : unit === 'K_small' ? localWarmingColor(x * 4) : localWarmingColor(x)
    return co2LayerInfo(co2Layer.value).color(x)
  }
  // Sample the colours and place the ticks on the scale of the unit: linear, logarithmic (rain) or signed logarithmic
  // (CO2 fluxes from sinks to sources).
  const kind = unit === 't CO₂/km²/yr' ? 'slog' : unit === 'mm' ? 'log' : 'lin'
  const lo = s[0][0], hi = s.at(-1)[0]
  const toT = v => (kind === 'log' ? Math.log(v / lo) / Math.log(hi / lo) : kind === 'slog' ? (Math.sign(v) * Math.log10(Math.max(1, Math.abs(v))) + 3) / 6 : (v - lo) / ((hi - lo) || 1))
  const fromT = t => (kind === 'log' ? lo * (hi / lo) ** t : kind === 'slog' ? Math.sign(t * 6 - 3) * 10 ** Math.abs(t * 6 - 3) : lo + (hi - lo) * t)
  const n = 16
  return { css: `linear-gradient(90deg,${Array.from({ length: n }, (_, k) => colorOf(fromT(k / (n - 1))) ?? '#0d2433').join(',')})`, ticks: s.map(x => ({ label: x[1], left: Math.max(0, Math.min(100, toT(x[0]) * 100)) })) }
})
const pct = v => `${Math.round(v)} %`
const levelName = l => LEVELS[l].label
const dayOptions = computed(() => (forecast.value ? Array.from({ length: view.value === 'severe' ? forecast.value.forecast_days : horizon.value }, (_, k) => k + 1) : []))
const cityRowsWind = computed(() => windCities.value)
const hazardCells = computed(() => matrix.value)
const barX = (v, max) => 50 + v / max * 48
const timeLabel = h => (forecast.value ? validTime(forecast.value, h).toISOString().slice(5, 16).replace('T', ' ') : '')
</script>

<template>
  <section class="earth">
    <div class="earth-head">
      <div>
        <div class="eyebrow">COUPLED EARTH · CLIMATE ↔ WEATHER</div>
        <h1>Everything coupled, <span>in one place.</span></h1>
        <p class="intro">The drivers that change the climate, where CO₂ hits hardest, and what the ocean, the land, the mountains and the Sun do to the weather of the next days — from two coupled engine runs.</p>
      </div>
      <label class="upload" title="Open a terraflux-run-earth-forecast or terraflux-run-earth-climate timeline">↥ Open coupled JSON<input type="file" accept=".json,application/json" @change="upload"></label>
    </div>
    <nav class="earth-tabs" aria-label="Coupled Earth views">
      <button v-for="v in EARTH_VIEWS" :key="v.key" :class="{ selected: view === v.key }" :title="v.tooltip" @click="view = v.key">{{ v.label }}</button>
    </nav>
    <p v-if="loading" class="earth-status">{{ loading }}</p>
    <p v-if="error" class="earth-error" role="alert">{{ error }}</p>

    <!-- ============================ OVERVIEW ============================ -->
    <template v-if="view === 'overview'">
      <div class="earth-cards">
        <div v-for="a in answers" :key="a.key" class="earth-card"><small>{{ a.label }}</small><strong>{{ a.value }}</strong><p>{{ a.detail }}</p></div>
      </div>
      <div v-if="graph" class="earth-grid">
        <article class="earth-panel">
          <div class="section-label">WHAT IS COUPLED TO WHAT</div>
          <svg class="coupling-graph" viewBox="0 0 100 66" aria-label="Coupling diagram">
            <defs><marker id="earth-arrow" viewBox="0 0 6 6" refX="5" refY="3" markerWidth="4" markerHeight="4" orient="auto"><path d="M0,0 L6,3 L0,6 z" fill="#9fb4bf" /></marker></defs>
            <g v-for="e in graph.edges" :key="e.from.key + e.to.key" class="edge" :class="{ hot: hoverEdge === e }" @mouseenter="hoverEdge = e" @mouseleave="hoverEdge = null">
              <path :d="edgePath(e).d" fill="none" :stroke="e.from.color" :stroke-width="0.12 + e.weight * 0.55" :stroke-opacity="0.35 + e.weight * 0.5" marker-end="url(#earth-arrow)" />
              <path :d="edgePath(e).d" fill="none" stroke="transparent" stroke-width="3" />
            </g>
            <g v-for="n in graph.nodes" :key="n.key">
              <circle :cx="n.x" :cy="n.y" r="2.6" fill="#10212b" :stroke="n.color" stroke-width="0.5" />
              <text :x="n.x" :y="n.y + 5.4" text-anchor="middle" class="node-label">{{ n.label }}</text>
            </g>
          </svg>
          <p class="hint" v-if="hoverEdge"><strong>{{ hoverEdge.from.label }} → {{ hoverEdge.to.label }}</strong>: <template v-for="(l, k) in hoverEdge.links" :key="k">{{ k ? ' · ' : '' }}{{ l.mechanism }} ({{ fmt(l.magnitude, l.magnitude < 1 ? 3 : 1) }} {{ l.unit }}, {{ l.strength }})</template></p>
          <p class="hint" v-else>Arrows: thicker = stronger coupling. Hover an arrow for the mechanism and its size in the runs.</p>
        </article>
        <article class="earth-panel">
          <div class="section-label">WHAT THE FORECAST OWES TO EACH COUPLING · FIRST {{ sensitivity.runs[0]?.days ?? 3 }} DAYS</div>
          <p class="hint">The unperturbed control run repeated with one coupling switched off. Bars: change of thunderstorms and rain; numbers: land temperatures.</p>
          <div v-for="r in sensitivity.runs" :key="r.run" class="sens-row">
            <div><strong>{{ r.run.replaceAll('_', ' ') }}</strong><small>{{ r.description }}</small></div>
            <svg viewBox="0 0 100 12" preserveAspectRatio="none"><line x1="50" x2="50" y1="0" y2="12" stroke="#4a5d68" stroke-width=".4" />
              <rect :x="Math.min(50, barX(r.thunder_change_pct, sensitivity.max))" y="1" :width="Math.abs(barX(r.thunder_change_pct, sensitivity.max) - 50)" height="4.5" fill="#c79bff" />
              <rect :x="Math.min(50, barX(r.precip_change_pct, sensitivity.max))" y="6.5" :width="Math.abs(barX(r.precip_change_pct, sensitivity.max) - 50)" height="4.5" fill="#64a9ee" /></svg>
            <small>thunder {{ signed(r.thunder_change_pct, 0) }} % · rain {{ signed(r.precip_change_pct, 1) }} %<br>Tmax {{ signed(r.tmax_land_change_k, 2) }} K · Tmin {{ signed(r.tmin_land_change_k, 2) }} K</small>
          </div>
          <p class="hint" v-if="!sensitivity.runs.length">Load the forecast to see its sensitivity runs.</p>
        </article>
      </div>
      <article v-if="climate" class="earth-panel">
        <div class="section-label">ALL COUPLINGS IN THE RUNS</div>
        <table class="earth-table"><thead><tr><th>From → to</th><th>Mechanism</th><th>Size</th><th>Strength</th><th>Model</th></tr></thead>
          <tbody><tr v-for="(c, k) in climate.couplings" :key="k"><td>{{ c.from.replaceAll('_', ' ') }} → {{ c.to.replaceAll('_', ' ') }}</td><td>{{ c.mechanism }}</td><td>{{ fmt(c.magnitude, Math.abs(c.magnitude) < 1 ? 3 : 1) }} {{ c.unit }}</td><td><i class="strength" :style="{ width: `${strengthWeight(c.strength) * 40}px` }"></i> {{ c.strength }}</td><td>{{ c.source }}</td></tr></tbody></table>
        <p class="hint">Limitations: {{ climate.limitations.join(' · ') }}<template v-if="forecast"> · Forecast: {{ forecast.limitations.join(' · ') }}</template></p>
      </article>
    </template>

    <!-- ============================ MAP VIEWS ============================ -->
    <div v-if="mapKind && (mapKind === 'forecast' ? forecast : climate)" class="earth-grid map-grid">
      <article class="earth-panel map-panel">
        <div class="map-title"><strong>{{ mapTitle }}</strong><small v-if="hoverText">{{ hoverText }}</small><small v-else>{{ mapKind === 'forecast' ? 'Hover for values · click a cell to pin it' + (view === 'wind' ? ' (wind: the nearest city)' : '') : 'Hover for values' }}</small></div>
        <div ref="mapHost" class="earth-map"><canvas ref="canvas" @mousemove="onMove" @mouseleave="onLeave" @click="onClick" /></div>
        <div v-if="legend" class="earth-legend">
          <template v-if="legend.css"><i class="bar" :style="{ background: legend.css }"></i><span class="ticks"><span v-for="(t, k) in legend.ticks" :key="k" :style="{ left: `${t.left}%` }" :class="{ end: t.left > 90, start: t.left < 10 }">{{ t.label }}</span></span></template>
          <template v-else><span v-for="c in legend.classes" :key="c.label" class="cls"><i :style="{ background: c.color }"></i>{{ c.label }}</span></template>
        </div>
        <p v-if="view === 'wind'" class="hint">Arrows: the ensemble-mean wind at 10 m (the coupled 1.5 km layer slowed by surface friction, plus the hurricane vortex) · white lines: isobars (hPa) · H/L: pressure systems of member 1 · circles: the hurricane in every member (white: the truth run).</p>
        <p v-if="view === 'weather'" class="hint">White lines: ensemble-mean isobars at noon of the day (hPa). Synthetic weather situation on a 210 km grid; real land mask.</p>
        <p v-if="view === 'severe'" class="hint">Warnings are issued for land and the sea next to it. Tracks: the hurricane in every member (colour: category) and in the truth run (dashed white); dots: +{{ trackHour }} h.</p>
      </article>

      <!-- Side panel per view -->
      <article class="earth-panel side-panel">
        <template v-if="view === 'weather' && forecast">
          <div class="section-label">FORECAST RANGE</div>
          <div class="segmented"><button v-for="h in horizons" :key="h" :class="{ selected: horizon === h }" @click="horizon = h">{{ h }} days</button></div>
          <div class="section-label">DAY</div>
          <div class="segmented days"><button v-for="d in dayOptions" :key="d" :class="{ selected: day === d }" @click="day = d">{{ d }}<small>{{ dayLabel(forecast, d) }}</small></button></div>
          <div class="section-label">MAP</div>
          <select v-model="weatherField" aria-label="Forecast field"><option v-for="f in FORECAST_FIELDS" :key="f.key" :value="f.key">{{ f.label }}</option></select>
          <template v-if="inspector">
            <div class="section-label">{{ selectedCell !== null ? 'PINNED CELL' : 'CELL UNDER THE POINTER' }}</div>
            <p class="hint">{{ fmt(inspector.lat, 1) }}°N {{ fmt(Math.abs(inspector.lon), 1) }}°{{ inspector.lon < 0 ? 'W' : 'E' }} · {{ inspector.land ? `land, ${Math.round(inspector.elevation)} m` : `sea, ${fmt(inspector.sst, 1)} °C` }}</p>
            <table class="earth-table compact"><tbody><tr v-for="d in inspector.days" :key="d.day" :class="{ selected: d.day === day }"><td>{{ d.day }}</td><td :style="{ color: symbolInfo(d.symbol).color }" :title="symbolInfo(d.symbol).label">{{ symbolInfo(d.symbol).glyph }}</td><td>{{ fmt(d.tmax, 0) }}° / {{ fmt(d.tmin, 0) }}°</td><td>{{ fmt(d.precip, 1) }} mm</td><td>{{ d.rainChance }} %</td><td>{{ fmt(d.gust, 0) }} m/s</td><td><i v-for="w in d.warnings" :key="w.key" class="warn-dot" :style="{ background: levelColor(w.level) }" :title="`${hazardInfo(w.key).label}: ${levelName(w.level)}`"></i></td></tr></tbody></table>
          </template>
          <div class="section-label">SKILL (AGAINST THE HIDDEN TRUTH RUN)</div>
          <p class="hint">Useful for about <strong>{{ Math.round(lead.hours) }} h</strong>: beyond that the pressure pattern of the ensemble mean correlates less than 0.6 with the truth. Ensemble of {{ forecast.members }} members.</p>
        </template>

        <template v-if="view === 'wind' && forecast && windFrame">
          <div class="section-label">HOUR</div>
          <div class="playback"><button @click="togglePlay">{{ playing ? '❚❚ Pause' : '▶ Play' }}</button><span>+{{ windFrame.hour }} h · {{ timeLabel(windFrame.hour) }} UTC</span></div>
          <input type="range" min="0" :max="windList.length - 1" v-model.number="windIndex" aria-label="Forecast hour">
          <div class="section-label">MAP</div>
          <div class="segmented"><button :class="{ selected: windLayer === 'speed' }" @click="windLayer = 'speed'">Mean wind</button><button :class="{ selected: windLayer === 'gust' }" @click="windLayer = 'gust'">Gusts</button><button :class="{ selected: windLayer === 'p_gust_orange' }" @click="windLayer = 'p_gust_orange'">Storm chance</button></div>
          <div v-if="windStats" class="earth-stats">
            <div><small>STRONGEST 10 M WIND</small><strong>{{ fmt(windStats.max, 0) }} m/s</strong><em>{{ fmt(windStats.lat[windStats.at], 0) }}°N {{ fmt(Math.abs(windStats.lon[windStats.at]), 0) }}°W · Bft {{ beaufort(windStats.max) }}</em></div>
            <div><small>STRONGEST GUST (MEAN)</small><strong>{{ fmt(windStats.gust, 0) }} m/s</strong><em>{{ fmt(windStats.lat[windStats.gustAt], 0) }}°N {{ fmt(Math.abs(windStats.lon[windStats.gustAt]), 0) }}°W</em></div>
            <div><small>CELLS ≥ 30 % STORM CHANCE</small><strong>{{ windStats.orange }}</strong><em>gusts ≥ {{ forecast.warnings.thresholds.gust_m_s[1] }} m/s</em></div>
          </div>
          <div class="section-label">CITY</div>
          <select v-model="windCityName" aria-label="City"><option v-for="c in windCities" :key="c.name" :value="c.name">{{ c.name }}</option></select>
        </template>

        <template v-if="view === 'severe' && forecast">
          <div class="section-label">DAY</div>
          <div class="segmented days"><button v-for="d in dayOptions" :key="d" :class="{ selected: severeDay === d }" @click="severeDay = d">{{ d }}<small>{{ dayLabel(forecast, d) }}</small></button></div>
          <div class="section-label">HAZARD</div>
          <div class="segmented"><button :class="{ selected: severeHazard === 'all' }" @click="severeHazard = 'all'">All</button><button v-for="h in HAZARDS" :key="h.key" :class="{ selected: severeHazard === h.key }" @click="severeHazard = h.key">{{ h.icon }} {{ h.label }}</button></div>
          <div class="section-label">MAP</div>
          <select v-model="severeLayer" aria-label="Severe-weather layer"><option v-for="l in SEVERE_LAYERS" :key="l.key" :value="l.key">{{ l.label }}</option></select>
          <label class="toggle"><input type="checkbox" v-model="showTracks"> Hurricane track plume</label>
          <label v-if="showTracks" class="slider-label">Positions at +{{ trackHour }} h <input type="range" :min="0" :max="trackHours.length - 1" :value="trackHours.indexOf(trackHour)" @input="trackHour = trackHours[+$event.target.value]" aria-label="Track hour"></label>
          <div class="earth-stats">
            <div v-for="l in [3, 2, 1]" :key="l"><small>{{ levelName(l).toUpperCase() }} AREAS</small><strong :style="{ color: levelColor(l) }">{{ severeCounts[l] }}</strong></div>
          </div>
          <template v-if="inspector">
            <div class="section-label">{{ selectedCell !== null ? 'PINNED CELL' : 'CELL UNDER THE POINTER' }}</div>
            <table class="earth-table compact"><tbody><tr v-for="d in inspector.days" :key="d.day" :class="{ selected: d.day === severeDay }"><td>day {{ d.day }}</td><td><template v-if="d.warnings.length"><span v-for="w in d.warnings" :key="w.key" class="warn-chip" :style="{ borderColor: levelColor(w.level) }">{{ hazardInfo(w.key).icon }} {{ hazardInfo(w.key).label }}</span></template><template v-else>—</template></td></tr></tbody></table>
          </template>
        </template>

        <template v-if="view === 'drivers' && climate && bars">
          <div class="section-label">SCENARIO</div>
          <div class="segmented"><button v-for="s in climate.scenarios" :key="s.key" :class="{ selected: scenario === s.key }" @click="scenario = s.key">{{ s.name.replace(/ \(about (.*)\)/, ' · $1') }}</button></div>
          <div class="section-label">WHICH DRIVERS WARM OR COOL · {{ PERIODS.find(p => p.key === period).label.toUpperCase() }}</div>
          <div class="segmented"><button v-for="p in PERIODS" :key="p.key" :class="{ selected: period === p.key }" @click="period = p.key">{{ p.label }}</button></div>
          <div class="driver-bars">
            <div v-for="r in bars.rows" :key="r.key" class="driver-row" :title="`${r.label}: ${signed(r.warming_k)} K (range ${signed(r.lo)} … ${signed(r.hi)} K), forcing ${signed(r.forcing_w_m2)} W/m²${r.hypothesis_k ? ` · contested hypothesis up to ${fmt(r.hypothesis_k, 2)} K` : ''}`">
              <div class="driver-head"><span><i :style="{ background: r.color }"></i>{{ r.label }}</span><em>{{ signed(r.warming_k) }} K</em></div>
              <svg viewBox="0 0 100 8" preserveAspectRatio="none"><line x1="50" x2="50" y1="0" y2="8" stroke="#4a5d68" stroke-width=".5" />
                <rect :x="Math.min(50, barX(r.warming_k, bars.max))" y="1.5" :width="Math.max(0.4, Math.abs(barX(r.warming_k, bars.max) - 50))" height="5" :fill="r.color" />
                <line :x1="barX(r.lo, bars.max)" :x2="barX(r.hi, bars.max)" y1="4" y2="4" stroke="#e8f4ff" stroke-width=".6" />
                <line v-if="r.hypothesis_k" :x1="barX(r.hypothesis_k, bars.max)" :x2="barX(r.hypothesis_k, bars.max)" y1="0" y2="8" stroke="#7fe0e0" stroke-width=".6" stroke-dasharray="1 1" /></svg>
            </div>
          </div>
          <p class="hint">Sum {{ signed(bars.total) }} K. Bars: central estimate; white line: the assessed range of the forcing (aerosols are the most uncertain). Space drivers: bounds from the solar-wind and cosmic-dust runs (dashed: a contested cosmic-ray–cloud hypothesis).</p>
          <div class="section-label">MAP</div>
          <select v-model="climateLayer" aria-label="Climate map layer"><option v-for="l in CLIMATE_LAYERS" :key="l.key" :value="l.key">{{ l.label }}</option></select>
          <label class="slider-label">Year {{ climateYear }} <input type="range" min="0" :max="climate.map.frames.length - 1" v-model.number="climateFrame" aria-label="Map year"></label>
        </template>

        <template v-if="view === 'co2' && climate && budget">
          <div class="section-label">MAP</div>
          <select v-model="co2Layer" aria-label="CO2 map layer"><option v-for="l in CO2_LAYERS" :key="l.key" :value="l.key">{{ l.label }}</option></select>
          <div class="section-label">WHERE THE CO₂ GOES · Gt CO₂ PER YEAR</div>
          <div v-for="r in budget.rows" :key="r.key" class="budget-row"><span>{{ r.label }}</span><svg viewBox="0 0 100 8" preserveAspectRatio="none"><line x1="50" x2="50" y1="0" y2="8" stroke="#4a5d68" stroke-width=".5" /><rect :x="Math.min(50, barX(r.value, budget.max))" y="1" :width="Math.abs(barX(r.value, budget.max) - 50)" height="6" :fill="r.color" /></svg><em>{{ signed(r.value, 1) }}</em></div>
          <p class="hint"><strong>{{ Math.round(budget.airborne * 100) }} %</strong> of the emissions stay in the air: +{{ fmt(budget.growth, 2) }} ppm per year; the north holds {{ fmt(budget.northSouth, 1) }} ppm more than the south (where the emissions are). {{ fmt(budget.cumulative, 0) }} Gt CO₂ emitted since 1850 → {{ fmt(budget.warmingFromCumulative, 2) }} K (TCRE {{ budget.tcre }} K per 1000 Gt). +100 ppm → {{ fmt(budget.per100, 2) }} K transient.</p>
          <div class="section-label">HIT HARDEST</div>
          <table class="earth-table compact"><tbody><tr v-for="h in climate.co2.hotspots.slice(0, 8)" :key="h.name"><td>{{ h.name }}</td><td><i class="strength" :style="{ width: `${h.score * 40}px`, background: '#ff8a65' }"></i></td><td>{{ fmt(h.warming_per_100ppm_k, 2) }} K</td><td>{{ h.ph_drop_per_100ppm === null ? '' : `−${fmt(h.ph_drop_per_100ppm, 3)} pH` }}</td></tr></tbody></table>
          <p class="hint">Per +100 ppm: local transient warming, ocean pH drop. Score: warming beyond the global mean on land; acidification and corrosive water at sea.</p>
        </template>
      </article>
    </div>

    <!-- ============================ BELOW THE MAP ============================ -->
    <template v-if="view === 'weather' && forecast">
      <article class="earth-panel">
        <div class="section-label">CITY FORECAST · {{ horizon }} DAYS · ENSEMBLE MEAN (10–90 % RANGE OF THE MAXIMUM)</div>
        <div class="table-scroll"><table class="earth-table city-table"><thead><tr><th>City</th><th v-for="d in cities[0]?.days ?? []" :key="d.day" :class="{ selected: d.day === day }">{{ dayLabel(forecast, d.day) }}</th></tr></thead>
          <tbody><tr v-for="c in cities" :key="c.name"><td>{{ c.name }}<small>{{ c.land ? `${Math.round(c.elevation)} m` : 'coast / sea cell' }}</small></td>
            <td v-for="d in c.days" :key="d.day" :class="{ selected: d.day === day }" :style="{ boxShadow: d.level ? `inset 0 -3px 0 ${levelColor(d.level)}` : 'none' }" :title="`${symbolInfo(d.symbol).label}; max ${fmt(d.tmaxRange[0], 1)}–${fmt(d.tmaxRange[1], 1)} °C; rain ${fmt(d.precip, 1)} mm (90 %: ${fmt(d.precipP90, 1)}); chance ${d.rainChance} %; gusts ${fmt(d.gust, 0)} m/s${d.hazards.length ? '; warnings: ' + d.hazards.map(h => `${hazardInfo(h.key).label} ${levelName(h.level)}`).join(', ') : ''}`">
              <span class="sym" :style="{ color: symbolInfo(d.symbol).color }">{{ symbolInfo(d.symbol).glyph }}</span><b :style="{ color: temperatureColor(d.tmax) }">{{ fmt(d.tmax, 0) }}°</b> <span class="muted">{{ fmt(d.tmin, 0) }}°</span><small>{{ d.precip >= 0.1 ? `${fmt(d.precip, 1)} mm · ${d.rainChance} %` : 'dry' }}</small></td></tr></tbody></table></div>
        <p class="hint">Underline: the highest warning of the day in the city's cell. Hover a cell for the range and the warnings.</p>
      </article>
      <div class="earth-grid">
        <article class="earth-panel" v-if="skillChart">
          <div class="section-label">HOW FAR AHEAD IT HOLDS · SKILL AGAINST THE TRUTH RUN</div>
          <div class="chart-pair">
            <div><small>PRESSURE PATTERN: ANOMALY CORRELATION (0–1) · RAIN ≥ 1 mm: BRIER SKILL</small>
              <svg viewBox="0 0 100 50" preserveAspectRatio="none"><line x1="0" x2="100" :y1="skillChart.accLine" :y2="skillChart.accLine" stroke="#6a7d88" stroke-dasharray="1 1" stroke-width=".3" /><line v-if="skillChart.useful !== null" :x1="skillChart.useful" :x2="skillChart.useful" y1="0" y2="50" stroke="#ffd166" stroke-width=".4" /><polyline :points="skillChart.acc" fill="none" stroke="#e8f4ff" stroke-width=".7" /><polyline :points="skillChart.rain" fill="none" stroke="#64a9ee" stroke-width=".6" /></svg><div class="axis"><span v-for="t in skillChart.ticks" :key="t.h" :style="{ left: `${t.x}%` }">{{ t.h }} h</span></div></div>
            <div><small>TEMPERATURE: ERROR (WHITE) VS SPREAD (GREEN) VS CLIMATOLOGY (GREY) · 0–{{ fmt(skillChart.tMax, 1) }} K</small>
              <svg viewBox="0 0 100 50" preserveAspectRatio="none"><polyline :points="skillChart.tClim" fill="none" stroke="#6a7d88" stroke-width=".5" /><polyline :points="skillChart.tSpread" fill="none" stroke="#7fe0c4" stroke-width=".6" /><polyline :points="skillChart.tRmse" fill="none" stroke="#e8f4ff" stroke-width=".7" /></svg><div class="axis"><span v-for="t in skillChart.ticks" :key="t.h" :style="{ left: `${t.x}%` }">{{ t.h }} h</span></div></div>
          </div>
          <table class="earth-table compact"><thead><tr><th>Lead</th><th>Pressure error</th><th>Spread</th><th>Pattern correlation</th><th>Temperature error</th><th>Tmax error</th><th>Rain skill</th></tr></thead>
            <tbody><tr v-for="l in forecast.verification.lead_days" :key="l.day"><td>day {{ l.day }}</td><td>{{ fmt(l.pressure_rmse_hpa, 1) }} hPa</td><td>{{ fmt(l.pressure_spread_hpa, 1) }} hPa</td><td>{{ fmt(l.pressure_anomaly_correlation, 2) }}</td><td>{{ fmt(l.temperature_rmse_k, 2) }} K</td><td>{{ fmt(l.tmax_rmse_k, 2) }} K</td><td>{{ fmt(l.rain_brier_skill, 2) }}</td></tr></tbody></table>
          <p class="hint">{{ forecast.verification.note }}. Spread ≈ error means the ensemble knows its uncertainty. Skill 1 is perfect, 0 is no better than the climatology.</p>
        </article>
        <article class="earth-panel">
          <div class="section-label">WHAT THE SURFACE DOES TO THE FORECAST (CONTROL RUN)</div>
          <table class="earth-table compact"><tbody>
            <tr><td>Land maximum / minimum (mean)</td><td>{{ fmt(forecast.couplings.control.tmax_land_c, 1) }} / {{ fmt(forecast.couplings.control.tmin_land_c, 1) }} °C</td></tr>
            <tr><td>Precipitation (domain mean)</td><td>{{ fmt(forecast.couplings.control.precip_mm_per_day, 2) }} mm/day</td></tr>
            <tr v-for="(v, k) in forecast.couplings.control.precip_sources" :key="k"><td>… from {{ k.replaceAll('_', ' ') }}</td><td>{{ fmt(100 * v / Object.values(forecast.couplings.control.precip_sources).reduce((a, b) => a + b, 0), 1) }} %</td></tr>
            <tr><td>Strongest hurricane wind</td><td>{{ fmt(forecast.couplings.control.max_hurricane_wind_m_s, 0) }} m/s</td></tr>
          </tbody></table>
          <div v-for="r in forecast.couplings.runs" :key="r.run" class="hint"><strong>{{ r.run.replaceAll('_', ' ') }}</strong> ({{ r.description }}): Tmax {{ signed(r.tmax_land_change_k) }} K, Tmin {{ signed(r.tmin_land_change_k) }} K, rain {{ signed(r.precip_change_pct, 1) }} %, thunderstorms {{ signed(r.thunder_change_pct, 0) }} %.</div>
        </article>
      </div>
    </template>

    <template v-if="view === 'wind' && forecast">
      <article v-if="windGram && windCity" class="earth-panel">
        <div class="section-label">METEOGRAM · {{ windCity.name.toUpperCase() }} · NEXT 72 HOURS</div>
        <div class="chart-box"><span v-for="l in windGram.levels" :key="'l' + l.v" class="ylab" :style="{ top: `${l.y / 52 * 100}%` }">{{ l.v }} m/s</span><svg class="meteogram" viewBox="0 0 100 52" preserveAspectRatio="none" aria-label="Wind meteogram">
          <line v-for="l in windGram.levels" :key="l.v" x1="0" x2="100" :y1="l.y" :y2="l.y" stroke="#223642" stroke-width=".25" />
          <polygon :points="windGram.band" fill="#2f6f8f" fill-opacity=".45" />
          <polyline :points="windGram.gustP90" fill="none" stroke="#f08a24" stroke-width=".35" stroke-dasharray="1 .8" />
          <polyline :points="windGram.gust" fill="none" stroke="#f08a24" stroke-width=".6" />
          <polyline :points="windGram.wind" fill="none" stroke="#e8f4ff" stroke-width=".7" />
          <polyline :points="windGram.truth" fill="none" stroke="#ffd166" stroke-width=".45" stroke-dasharray=".8 .8" />
          <line v-if="windFrame" :x1="windGram.x(Math.max(1, windFrame.hour))" :x2="windGram.x(Math.max(1, windFrame.hour))" y1="0" y2="52" stroke="#93a5af" stroke-width=".3" stroke-dasharray="1 1" />
        </svg></div>
        <div class="axis"><span v-for="t in windGram.ticks" :key="t.hour" :style="{ left: `${t.x}%` }">+{{ t.hour }} h</span></div>
        <div class="dir-strip"><span v-for="(a, k) in windGram.arrows" :key="k" :style="{ left: `${a.x}%` }" :title="`${compass(a.direction)} ${fmt(a.speed, 0)} m/s`"><i :style="{ transform: `rotate(${a.direction + 180}deg)` }">↑</i></span></div>
        <p class="hint">White: mean wind (ensemble mean) with the 10–90 % range of the members (band) · orange: gusts (dashed: 90 % of the members stay below) · yellow dotted: the truth run · arrows: where the wind blows to.</p>
      </article>
      <article class="earth-panel">
        <div class="section-label">STRONGEST GUSTS PER DAY · CITIES</div>
        <div class="table-scroll"><table class="earth-table"><thead><tr><th>City</th><th>Day 1</th><th>Day 2</th><th>Day 3</th><th>Mean wind (72 h)</th></tr></thead>
          <tbody><tr v-for="c in cityRowsWind" :key="c.name" :class="{ selected: c.name === windCityName }" @click="windCityName = c.name"><td>{{ c.name }}</td>
            <td v-for="(d, k) in c.days" :key="k"><template v-if="d"><b :style="{ color: d.gust < 8 ? '#9db0b8' : windColor(d.gust) }">{{ fmt(d.gust, 0) }}</b> m/s <small>{{ compass(d.direction) }} · +{{ d.hour }} h · 90 %: {{ fmt(d.gustP90, 0) }}</small></template></td>
            <td>{{ fmt(c.meanWind, 1) }} m/s</td></tr></tbody></table></div>
        <p class="hint">Gusts: mean wind × {{ forecast.request.gusts?.gust_factor ?? 1.45 }} over open terrain, plus downdrafts under thunderstorms; inside a hurricane its own wind field. Click a row for its meteogram.</p>
      </article>
    </template>

    <template v-if="view === 'severe' && forecast">
      <article class="earth-panel">
        <div class="section-label">WARNINGS DAY BY DAY · HIGHEST LEVEL</div>
        <div class="table-scroll"><table class="earth-table matrix"><thead><tr><th>Hazard</th><th v-for="d in hazardCells" :key="d.day" :class="{ selected: d.day === severeDay }" @click="severeDay = d.day">{{ d.day }}<small>{{ dayLabel(forecast, d.day) }}</small></th></tr></thead>
          <tbody><tr v-for="(h, k) in HAZARDS" :key="h.key" :class="{ selected: severeHazard === h.key }"><td @click="severeHazard = h.key">{{ h.icon }} {{ h.label }}</td>
            <td v-for="d in hazardCells" :key="d.day" class="level-cell" :style="{ background: levelColor(d.hazards[k].level) ?? 'transparent' }" :title="`${h.label}, day ${d.day}: ${levelName(d.hazards[k].level)} (${d.hazards[k].cells} cells)`" @click="severeDay = d.day; severeHazard = h.key">{{ d.hazards[k].cells || '' }}</td></tr></tbody></table></div>
        <p class="hint">Numbers: warned grid cells ({{ Math.round(forecast.request.grid.spacing_m / 1000) }} km). A level is issued where at least {{ Math.round(forecast.warnings.thresholds.min_probability * 100) }} % of the members exceed its threshold (tornado {{ Math.round(forecast.warnings.thresholds.tornado_min_probability * 100) }} %, red at least {{ Math.round(forecast.warnings.thresholds.red_min_probability * 100) }} %).</p>
      </article>
      <div class="earth-grid">
        <article class="earth-panel">
          <div class="section-label">WARNING AREAS · DAY {{ severeDay }} · {{ severeHazard === 'all' ? 'ALL HAZARDS' : hazardInfo(severeHazard).label.toUpperCase() }}</div>
          <div class="table-scroll short"><table class="earth-table"><thead><tr><th>Level</th><th>Hazard</th><th>Where</th><th>Area</th><th>Peak</th><th>Chance</th></tr></thead>
            <tbody><tr v-for="(r, k) in severeRegions.slice(0, 40)" :key="k" @click="selectedCell = r.peak_cell"><td><i class="warn-dot" :style="{ background: levelColor(r.level) }"></i>{{ levelName(r.level) }}</td><td>{{ hazardInfo(r.hazard).icon }} {{ hazardInfo(r.hazard).label }}</td>
              <td>{{ r.cities.length ? r.cities.join(', ') : `${fmt(r.nearest_city_km, 0)} km from ${r.nearest_city}` }}<small>{{ fmt(r.latitude_deg, 1) }}°N {{ fmt(Math.abs(r.longitude_deg), 1) }}°W</small></td><td>{{ Math.round(r.area_km2 / 1000).toLocaleString() }}k km²</td><td>{{ peakValueLabel(r) }}</td><td>{{ Math.round(r.peak_probability * 100) }} %</td></tr></tbody></table></div>
          <p class="hint" v-if="!severeRegions.length">No warnings for this day and hazard.</p>
          <p class="hint" v-else>{{ severeRegions.length }} areas{{ severeRegions.length > 40 ? ' (the 40 most severe shown)' : '' }}. Click a row to pin its peak cell on the map.</p>
        </article>
        <article class="earth-panel">
          <div class="section-label">HURRICANE TRACK PLUME</div>
          <table class="earth-table compact"><thead><tr><th>Lead</th><th>Members</th><th>Mean position</th><th>Spread</th><th>Strongest member</th></tr></thead>
            <tbody><tr v-for="s in spreadRows" :key="s.hour"><td>+{{ s.hour }} h</td><td>{{ s.members }}</td><td>{{ fmt(s.lat, 1) }}°N {{ fmt(Math.abs(s.lon), 1) }}°W</td><td>{{ fmt(s.spreadKm, 0) }} km</td><td>{{ fmt(s.maxWind, 0) }} m/s</td></tr></tbody></table>
          <p class="hint">The storm is seeded in every member with an error of position and strength; the members then steer it with their own pressure systems. Categories: <span v-for="(l, k) in CATEGORY_LABELS" :key="k" class="cat"><i :style="{ background: categoryColor(k) }"></i>{{ l }}</span></p>
        </article>
      </div>
    </template>

    <template v-if="view === 'drivers' && climate">
      <div class="earth-grid">
        <article class="earth-panel" v-if="warmingChart">
          <div class="section-label">SERIES 1850–{{ climate.years.at(-1) }} · ALL SCENARIOS</div>
          <select v-model="seriesKey" aria-label="Series"><option v-for="s in SERIES_CHOICES" :key="s.key" :value="s.key">{{ s.label }}</option></select>
          <svg class="chart" viewBox="0 0 100 50" preserveAspectRatio="none"><polygon v-if="warmingChart.band" :points="warmingChart.band" :fill="scenarioColor(climate, scenario)" fill-opacity=".18" /><line x1="0" x2="100" :y1="warmingChart.zero" :y2="warmingChart.zero" stroke="#35505e" stroke-width=".3" /><line :x1="warmingChart.today" :x2="warmingChart.today" y1="0" y2="50" stroke="#93a5af" stroke-width=".3" stroke-dasharray="1 1" /><polyline v-for="l in warmingChart.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" /></svg><div class="axis"><span v-for="t in warmingChart.ticks" :key="t.year" :style="{ left: `${t.x}%` }">{{ t.year }}</span></div>
          <p class="hint">{{ fmt(warmingChart.lo, 2) }} … {{ fmt(warmingChart.hi, 2) }} · <span v-for="s in climate.scenarios" :key="s.key" class="cat"><i :style="{ background: scenarioColor(climate, s.key) }"></i>{{ s.name }}</span><template v-if="seriesChoice.band"> · band: climate sensitivity {{ climate.climate.ecs_low_k }}–{{ climate.climate.ecs_high_k }} K</template></p>
        </article>
        <article class="earth-panel" v-if="groupChart">
          <div class="section-label">FORCING BY GROUP · {{ scenarioName(scenario).toUpperCase() }} · W/m²</div>
          <svg class="chart" viewBox="0 0 100 50" preserveAspectRatio="none"><line x1="0" x2="100" :y1="groupChart.zero" :y2="groupChart.zero" stroke="#35505e" stroke-width=".3" /><line :x1="groupChart.today" :x2="groupChart.today" y1="0" y2="50" stroke="#93a5af" stroke-width=".3" stroke-dasharray="1 1" /><polyline v-for="l in groupChart.lines" :key="l.group" :points="l.points" fill="none" :stroke="l.color" stroke-width=".6" /></svg>
          <p class="hint">{{ fmt(groupChart.lo, 1) }} … {{ fmt(groupChart.hi, 1) }} W/m² · <span v-for="l in groupChart.lines" :key="l.group" class="cat"><i :style="{ background: l.color }"></i>{{ l.group }}</span> · volcanic dips: eruptions (Krakatoa 1883, Pinatubo 1991)</p>
          <div class="section-label">FEEDBACKS: HOW THE WARMING IS AMPLIFIED</div>
          <table class="earth-table compact"><thead><tr><th></th><th>Today</th><th>2100 ({{ scenarioName(climate.central_scenario).replace(/.*\(about (.*)\)/, '$1') }})</th></tr></thead><tbody><tr v-for="f in feedbacks" :key="f.key"><td>{{ f.label }}</td><td>{{ fmt(f.today, 2) }} K</td><td>{{ fmt(f.y2100, 2) }} K</td></tr></tbody></table>
          <p class="hint">ECS {{ climate.climate.ecs_k }} K, TCR {{ climate.climate.tcr_k }} K; net feedback {{ climate.climate.feedback_w_m2_k }} W/m²/K.</p>
        </article>
      </div>
      <article class="earth-panel">
        <div class="section-label">WHAT THE WARMING DOES · IMPACTS</div>
        <div class="table-scroll"><table class="earth-table"><thead><tr><th>Indicator</th><th>Pre-industrial</th><th>Today</th><th v-for="s in climate.scenarios" :key="s.key" colspan="2">{{ s.name.replace(/ \(about (.*)\)/, ' · $1') }}<small>2050 · 2100</small></th></tr></thead>
          <tbody><tr v-for="i in impacts" :key="i.key"><td>{{ i.label }}<small v-if="i.irreversible_on_human_time_scales">irreversible for centuries</small></td><td>{{ i.fmt(i.preindustrial) }}</td><td><b>{{ i.fmt(i.today) }}</b></td><template v-for="s in climate.scenarios" :key="s.key"><td>{{ i.fmt(i.scenarios[s.key].y2050) }}</td><td>{{ i.fmt(i.scenarios[s.key].y2100) }}</td></template></tr></tbody></table></div>
      </article>
    </template>

    <template v-if="view === 'co2' && climate">
      <div class="earth-grid">
        <article class="earth-panel">
          <div class="section-label">LARGEST EMITTERS · ANNUAL AND SINCE 1850</div>
          <div class="table-scroll short"><table class="earth-table"><thead><tr><th>Region</th><th>Per year</th><th>Share</th><th>Since 1850</th><th>Share</th><th>Warming caused</th></tr></thead>
            <tbody><tr v-for="r in climate.co2.regions" :key="r.name"><td>{{ r.name }}</td><td>{{ fmt(r.fossil_gt_co2_yr + r.land_use_gt_co2_yr, 2) }} Gt</td><td>{{ pct(r.share_of_annual * 100) }}</td><td>{{ fmt(r.cumulative_gt_co2, 0) }} Gt</td><td>{{ pct(r.share_of_cumulative * 100) }}</td><td>{{ fmt(r.warming_k, 3) }} K</td></tr></tbody></table></div>
          <p class="hint">Rounded national totals (hand-entered, not verified against the Global Carbon Budget files). Warming caused: cumulative emissions × TCRE.</p>
        </article>
        <article class="earth-panel">
          <div class="section-label">CO₂ AND OCEAN pH · ALL SCENARIOS</div>
          <div class="chart-pair">
            <div v-if="co2Chart"><small>CO₂ · {{ fmt(co2Chart.lo, 0) }}–{{ fmt(co2Chart.hi, 0) }} ppm</small><svg viewBox="0 0 100 50" preserveAspectRatio="none"><line :x1="co2Chart.today" :x2="co2Chart.today" y1="0" y2="50" stroke="#93a5af" stroke-width=".3" stroke-dasharray="1 1" /><polyline v-for="l in co2Chart.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" /></svg><div class="axis"><span v-for="t in co2Chart.ticks" :key="t.year" :style="{ left: `${t.x}%` }">{{ t.year }}</span></div></div>
            <div v-if="phChart"><small>SURFACE pH · {{ fmt(phChart.lo, 2) }}–{{ fmt(phChart.hi, 2) }}</small><svg viewBox="0 0 100 50" preserveAspectRatio="none"><line :x1="phChart.today" :x2="phChart.today" y1="0" y2="50" stroke="#93a5af" stroke-width=".3" stroke-dasharray="1 1" /><polyline v-for="l in phChart.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" /></svg><div class="axis"><span v-for="t in phChart.ticks" :key="t.year" :style="{ left: `${t.x}%` }">{{ t.year }}</span></div></div>
          </div>
          <div class="section-label">SINKS</div>
          <table class="earth-table compact"><thead><tr><th>Sink</th><th>Land</th><th>Ocean</th><th>Net</th></tr></thead><tbody><tr v-for="s in climate.co2.sinks" :key="s.name"><td>{{ s.name }}</td><td>{{ signed(s.land_gt_co2_yr, 2) }}</td><td>{{ signed(s.ocean_gt_co2_yr, 2) }}</td><td>{{ signed(s.net_gt_co2_yr, 2) }} Gt/yr</td></tr></tbody></table>
        </article>
      </div>
    </template>

    <p class="earth-source"><template v-if="sources.climate">Climate: {{ sources.climate }}. </template><template v-if="sources.forecast">Forecast: {{ sources.forecast }}{{ forecast ? `, start ${forecast.start_utc.replace('T', ' ').replace(':00Z', ' UTC')}, ${forecast.members} members` : '' }}. </template>Synthetic weather situation, hand-entered emission and concentration values; verification is against a run of the same model, not against observations.</p>
  </section>
</template>
