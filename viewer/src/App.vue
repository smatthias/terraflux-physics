<script setup>
import { ref, shallowRef, computed, watch, onMounted, onBeforeUnmount, nextTick } from 'vue'
import CoupledEarth from './CoupledEarth.vue'
import DataStream from './DataStream.vue'
import InspectOverlay from './InspectOverlay.vue'
import { vWobbly } from './wobbly.js'
import { cloudPuffs, cloudMesh, precipitationMesh, precipitationIntensity, CLOUD_FORMS } from './clouds3d.js'
import { arrowField } from './flowArrows.js'
import { waveField, drawnWave, seaState } from './waves.js'
import { waterMesh } from './waterSurface.js'
import { planeSurface, globeSurface, surfaceSkirt, surfaceMaterial, skirtMaterial, surfaceSubdivisions, centreHeights, cellAtUv } from './surface.js'
import { RELIEF_TARGET, reliefRange, verticalScale, exaggerationFactor, snowlineFor, treelineFor, strataSpacing, roughness } from './terrain.js'
import { explainCell, whereWords, windAt, senseOf } from './explain.js'
import { SHORTCUTS, isEditable, shortcutAction, hudLayout, hudStyle, defaultPanels, sceneOffset, fitZoom, approach } from './immersive.js'
import { VIEW_LABELS, frameChannels, overlayChips, describeChanges, appendEntries } from './dataStream.js'
import { earthDocumentKind } from './coupledEarth.js'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { demo, validateSequence } from './data.js'
import { regionalWeatherDemo, summarizeFrame } from './weatherSystems.js'
import { validateComparison, waterComparison, comparisonCsv, dischargeSeries } from './comparison.js'
import { defaultCoupledForm, formToRequest, formFromRequest, checkCoupledRequest, requestFromCoupledAtmosphere, CORIOLIS_MODES, DENSITY_MODES, SCALAR_BOUNDARIES, GRADIENT_BOUNDARIES, CLOUD_CONFIG_KEYS } from './coupledWind.js'
import { cloudFormationDemo, summarizeClouds, CLOUD_TYPES, cloudTypeIndex, cloudTypeInfo, FRONT_COLORS } from './cloudFormation.js'
import { isobars, rateColor, summarizePrecipitation, precipitationTypeInfo, PRECIPITATION_TYPES, HAZARD_LABELS } from './weatherMap.js'
import { PRECIP_CONFIG_KEYS } from './coupledWind.js'
import { summarizeStorms, summarizeSevere, categoryInfo, CATEGORIES, RISK_COLORS } from './stormSystems.js'
import { summarizeOrography, hillshade } from './orography.js'
import { summarizeHydrology, riverSegments, waterPath, hydrographSeries, floodClassInfo, waterColor, riverWidth, sedimentColor, basinColor, FLOOD_CLASSES } from './hydrology.js'
import { LAYER_GROUPS, LAYERS, EXAMPLE_GROUPS, groupOfLayer, layerSubtitle } from './layers.js'
import { summarizeCoast, coastRanking, coastEventTable, firstYearMaps, coastalCells, cellFrequency, scenarioOf, floodDepthColor, yearLostColor, frequencyColor, coastLandColor, coastSeaColor, scenarioTint, COAST_EVENTS, COAST_EVENT_LABELS, COMPONENT_LABELS, RANK_METRICS, COAST_TYPES } from './coastal.js'
import { summarizePolar, eventTable, scenarioSeries, scenarioColor, iceThicknessColor, smbColor, EVENT_KEYS, EVENT_LABELS } from './polar.js'
import { summarizeCarbon, carbonSeries, carbonRunSummary, carbonColumn, oxygenSection, worstCell, carbonBudget, habitatInfo, oxygenLevelColor, ppmColor, fluxColor, phColor, phRange, omegaColor, productionColor, nitrateColor, HABITAT_CLASSES, CARBON_BASES } from './oceanCarbon.js'
import { summarizeWind, windLayerValues, windLayerRange, windColor, chartLines, annualMeans, polarDial, ovalBoundary, ovalRing, magneticToVector, escapeRows, sunFixedRotation, geographicVector, vectorToLatLon, magnetosphereSketch, displayRadius, magneticPosition, magneticLocalTime, cellDose, cellSep, formatWind, sig, REGIONS, G_SCALE, WIND_LAYER_KEYS } from './solarWind.js'
import { summarizeDust, fateShares, sizeRows, profileRows, budgetRows, dustRampColor, ratioLabel, localSolarTime, formatNumber, formatSig, DUST_STATES, DUST_SCENARIOS, SOURCE_SYMBOLS } from './cosmicDust.js'
import { longTermWindow, yearlyPoints, longTermAt, epochBands, epochTable, dynamoExample, holoceneStrip, LONG_TERM_WINDOWS, EPOCH_COLORS, EPOCH_LABELS } from './solar.js'
import { summarizeSolar, sunspotChart, cycleTable, uvSites, flareYears, butterflyImage, diskGeometry, regionPosition, contrastPct, brightnessColor, contrastColor, cycleColor, flareLabel, fmtYear, monthlyPoints, polyline, band, chartWindow, chartY, SOLAR_BASES, R_SCALE, R_COLORS } from './solar.js'
import { summarizeOcean, streamLabel, speedColor, salinityColor, temperatureColor, circulationSeries, summarizeOceanHeat, columnProfile, columnSection, heatSeries, strongestCooling, heatFluxColor, evaporationColor, depthColor, rampColor, summarizePolarOcean, polarOceanSeries, polarOceanYears, overturningEvents, sameSeason, seaIceColor, oxygenColor } from './ocean.js'
// Coupled Earth: a separate workspace (header switch) for the coupled climate and forecast runs.
const mode=ref('observatory'),earthOpened=ref(false),earthDocument=ref(null),earthGoto=ref(null)
function openEarth(view){mode.value='earth';earthOpened.value=true;if(view)earthGoto.value={view,n:Date.now()}}
function openEarthForecast(){openEarth('weather')}
function openEarthClimate(){openEarth('drivers')}
// Ocean streams: the currents, their salt, the fresh water rivers pour in, where it mixes and where the water sinks.
const oceanMetrics=computed(()=>data.value.ocean?summarizeOcean(data.value):null)
const oceanLayers=ref({base:'surface',streams:true,arrows:true,rivers:true,plumes:true,fronts:true,formation:true,sponge:false,labels:true,rain:true,runoff:true,lenses:true,coast:true,seaIce:true,iceSheet:true,hosing:true,iceDrift:true})
const oceanSeries=computed(()=>data.value.ocean?circulationSeries(visibleFrames.value):[])
const oceanSeriesMax=computed(()=>({overturning:Math.max(0.1,...oceanSeries.value.map(p=>p.overturning_sv)),stream:Math.max(0.1,...oceanSeries.value.map(p=>p.stream_sv)),river:Math.max(0.01,...oceanSeries.value.map(p=>p.river_sv))}))
function oceanPath(key,max){const s=oceanSeries.value,start=s[0]?.elapsed_seconds??0,duration=(s.at(-1)?.elapsed_seconds??0)-start||1;return s.map(p=>`${(p.elapsed_seconds-start)/duration*100},${45-(p[key]??0)/max*40}`).join(' ')}
const oceanTimePosition=computed(()=>{const s=oceanSeries.value;if(!s.length)return 0;return (s[Math.min(frameIndex.value,s.length-1)].elapsed_seconds-s[0].elapsed_seconds)/(s.at(-1).elapsed_seconds-s[0].elapsed_seconds||1)*100})
const selectedStream=computed(()=>{if(!chosen.value||!data.value.ocean)return null;const id=data.value.ocean.streamId[chosen.value.i];return id>=0?data.value.ocean.streams.find(s=>s.id===id)??null:null})
// Rain over the sea and the coast, evaporation, the air-sea heat exchange and the layered water column.
const oceanHeatMetrics=computed(()=>data.value.ocean?summarizeOceanHeat(data.value):null)
const oceanHeatSeries=computed(()=>data.value.ocean?.processes?.length?heatSeries(visibleFrames.value):[])
const oceanHeatMax=computed(()=>{const s=oceanHeatSeries.value,v=k=>s.map(p=>p[k]);return {sst:[Math.min(...v('sst_c')),Math.max(...v('sst_c'))],mld:Math.max(1,...v('mld_m')),net:Math.max(50,...v('net_heat_w_m2').map(Math.abs)),water:Math.max(1,...v('rain_sea_m3_s'),...v('evaporation_m3_s'),...v('runoff_m3_s'))}})
function heatPath(key,transform){const s=oceanHeatSeries.value,start=s[0]?.elapsed_seconds??0,duration=(s.at(-1)?.elapsed_seconds??0)-start||1;return s.map(p=>`${((p.elapsed_seconds-start)/duration*100).toFixed(2)},${transform(p[key]).toFixed(2)}`).join(' ')}
// SST colours over the whole run so that playback compares like with like.
const oceanSstRange=computed(()=>{if(!data.value.ocean)return [0,1];let lo=Infinity,hi=-Infinity;for(const f of snapshots.value){const o=f.ocean;if(!o)continue;const t=o.thermal?o.thermal.sst:o.temperature;t.forEach((v,i)=>{if(o.mask[i]){lo=Math.min(lo,v);hi=Math.max(hi,v)}})};return [lo,hi]})
// The column panel follows the selected sea cell, otherwise the cell that cooled most over the run.
const columnCell=computed(()=>{const o=data.value.ocean;if(!o?.thermal)return null;if(chosen.value&&o.mask[chosen.value.i])return chosen.value.i;return strongestCooling(visibleFrames.value[0],visibleFrames.value.at(-1))})
const columnNow=computed(()=>columnCell.value===null?null:columnProfile(data.value,columnCell.value))
const columnHistory=computed(()=>columnCell.value===null?null:columnSection(visibleFrames.value,columnCell.value))
const columnTemperatureRange=computed(()=>{const p=columnNow.value;if(!p)return [0,1];const t=p.layers.map(l=>l.temperature_k);return [Math.min(...t)-.3,Math.max(...t)+.3]})
const columnSalinityRange=computed(()=>{const p=columnNow.value;if(!p)return [0,1];const v=p.layers.map(l=>l.salinity_psu);return [Math.min(...v)-.05,Math.max(...v)+.05]})
const columnDepthY=(z,max)=>Math.sqrt(Math.max(0,z)/(max||1))*56+2
function columnStep(key,[lo,hi]){const p=columnNow.value;if(!p)return '';const x=v=>((v-lo)/((hi-lo)||1)*92+4).toFixed(2);return p.layers.flatMap(l=>[`${x(l[key])},${columnDepthY(l.top,p.thickness_m).toFixed(2)}`,`${x(l[key])},${columnDepthY(l.bottom,p.thickness_m).toFixed(2)}`]).join(' ')}
const sectionRects=computed(()=>{const h=columnHistory.value;if(!h)return [];const span=(h.end-h.start)||1,[lo,hi]=h.temperature_range_k,y=z=>columnDepthY(z,h.max_depth_m)*50/60;return h.rects.map(r=>({x:(r.t0-h.start)/span*100,w:Math.max(.05,(r.t1-r.t0)/span*100),y:y(r.z0),h:Math.max(.05,y(r.z1)-y(r.z0)),fill:rampColor(r.temperature_k,lo,hi)}))})
const sectionTimePosition=computed(()=>{const h=columnHistory.value;return h?(forecast.value.elapsed_seconds-h.start)/((h.end-h.start)||1)*100:0})
const sectionMld=computed(()=>{const h=columnHistory.value;if(!h)return '';return h.depths.map(d=>`${((d.elapsed_seconds-h.start)/((h.end-h.start)||1)*100).toFixed(2)},${(columnDepthY(d.mld,h.max_depth_m)*50/60).toFixed(2)}`).join(' ')})
const lensLabel=l=>`${l.kind==='coastal_runoff'?'coastal runoff':l.kind==='glacial'?'glacial meltwater':l.kind} lens · ${Math.round(l.area_km2).toLocaleString()} km² · ${l.min_surface_salinity_psu.toFixed(1)} psu`
// Sea level & coasts: the coastal-flooding block (land lost, storm floods, defences, rivers, islands).
const COAST_RELIEF=30
const coastLayers=ref({base:'terrain',lost:true,storm:true,protected:true,defences:true,rivers:true,labels:true,wind:true,scenario:'',rank:'lost_pct'})
const coastBlock=computed(()=>data.value.coast?.block??null)
const coastScenario=computed(()=>{const b=coastBlock.value;if(!b)return '';return b.scenarios.some(x=>x.name===coastLayers.value.scenario)?coastLayers.value.scenario:b.map_scenario})
const coastIsMap=computed(()=>!!coastBlock.value&&coastScenario.value===coastBlock.value.map_scenario)
const coastSummary=computed(()=>data.value.coast?summarizeCoast(data.value,coastScenario.value):null)
const coastRank=computed(()=>coastSummary.value?coastRanking(coastSummary.value,coastLayers.value.rank):[])
const coastEvents=computed(()=>coastBlock.value?coastEventTable(coastBlock.value):[])
const coastYears=computed(()=>coastBlock.value?firstYearMaps(coastBlock.value,coastScenario.value):null)
const coastCoastal=computed(()=>data.value.coast?coastalCells(data.value.coast,grid.value):null)
const coastStatic=computed(()=>{const b=coastBlock.value;if(!b)return null;const defence=new Map(b.defences),river=new Map();b.rivers.forEach((r,k)=>r.cells.forEach(i=>river.set(i,k)));return {defence,river}})
// Cells lost / storm-flooded at the frame's year: the maps for the map scenario, the first-year maps otherwise.
function coastLostAt(i){const c=data.value.coast;if(coastIsMap.value)return c.lost[i];const y=coastYears.value.lost[i];return y!==null&&y<=c.year?1:0}
function coastStormAt(i){const c=data.value.coast;if(coastIsMap.value)return c.storm[i];const y=coastYears.value.storm[i];return y!==null&&y<=c.year?1:0}
// A line chart over the output years: one line per entry, own range, the current year marked.
function coastLines(entries,{zero=true}={}){
 const b=coastBlock.value;if(!b)return null
 const years=b.output_years,y0=years[0],y1=years.at(-1),vals=entries.flatMap(e=>e.values.filter(Number.isFinite))
 if(!vals.length)return null
 let lo=Math.min(...vals),hi=Math.max(...vals);if(zero){lo=Math.min(0,lo);hi=Math.max(0,hi)}if(hi-lo<1e-9)hi=lo+1
 const X=y=>(y-y0)/((y1-y0)||1)*100,Y=v=>45-(v-lo)/(hi-lo)*40
 return {lo,hi,x:X(data.value.coast?.year??y0),zero:Y(0),lines:entries.map(e=>({name:e.name,color:e.color,width:e.width??.6,dash:e.dash??'',points:years.map((y,k)=>Number.isFinite(e.values[k])?`${X(y).toFixed(2)},${Y(e.values[k]).toFixed(2)}`:null).filter(Boolean).join(' ')}))}
}
const coastScenarioLines=pick=>coastBlock.value?coastBlock.value.scenarios.map((x,k)=>({name:x.name,color:scenarioTint(k),width:x.name===coastScenario.value?.9:.5,values:pick(x)})):[]
const coastChartSea=computed(()=>coastLines([...coastScenarioLines(x=>x.sea_level.regional_m),...(coastBlock.value?[{name:'global',color:'#8a9aa5',dash:'1.5 1',values:scenarioOf(coastBlock.value,coastScenario.value).sea_level.global_m}]:[])]))
const coastChartLost=computed(()=>coastLines(coastScenarioLines(x=>x.totals.lost_km2)))
const coastChartPeople=computed(()=>coastLines(coastScenarioLines(x=>x.totals.people_storm_flooded.map(v=>v/1e6))))
const coastChartRegions=computed(()=>{const b=coastBlock.value;if(!b)return null;const s=scenarioOf(b,coastScenario.value);return coastLines(s.regions.map((r,k)=>({name:r.name,color:['#ff6b6b','#ffd166','#7fd1ff','#9fe870','#b388ff'][k%5],values:r.series.lost_pct})))})
const coastChartRivers=computed(()=>{const b=coastBlock.value;if(!b)return null;const s=scenarioOf(b,coastScenario.value);return coastLines(s.rivers.flatMap((r,k)=>[{name:r.name+' surge',color:['#4fc3f7','#ffd166','#9fe870'][k%3],values:r.series.surge_reach_km},{name:r.name+' salt',color:['#4fc3f7','#ffd166','#9fe870'][k%3],dash:'1.5 1',values:r.series.salt_intrusion_km}]))})
const coastComponentBar=computed(()=>{const m=coastSummary.value;if(!m)return [];const total=Object.values(m.components).reduce((a,v)=>a+Math.max(0,v),0)||1;let x=0;return Object.entries(m.components).map(([k,v],j)=>{const w=Math.max(0,v)/total*100,seg={key:k,label:COMPONENT_LABELS[k],value:v,x,w,color:['#ff8a65','#7fd1ff','#b388ff','#e8f4ff','#9fe870'][j]};x+=w;return seg})})
const fmtPeople=v=>v>=1e6?(v/1e6).toFixed(2)+' M':v>=1e3?(v/1e3).toFixed(0)+' k':Math.round(v).toString()
const fmtYears=v=>!Number.isFinite(v)?'never':v<=1.0001?'every year':v<10?v.toFixed(1)+' yr':Math.round(v)+' yr'
const coastFocus=ref(null)
function focusRegion(r){const c=data.value.coast;if(!c)return;coastFocus.value=r;const g=grid.value;let sx=0,sy=0,n=0;for(let i=0;i<c.count;i++)if(c.block.region[i]===r&&!c.sea[i]){sx+=i%g.nx;sy+=Math.floor(i/g.nx);n++}if(n)selected.value=Math.round(sy/n)*g.nx+Math.round(sx/n)}
// Carbon & ocean life: the CO2 of the air over land and sea, the ocean's uptake and acidification, its oxygen, the
// habitat of each water level and the dead zones.
const carbonLayers=ref({base:'bottomo2',level:6,air:true,dead:true,anoxic:true,corrosive:true,sources:true,rivers:true,labels:true,wind:false})
const carbonMetrics=computed(()=>data.value.carbon?summarizeCarbon(data.value):null)
const carbonSeriesData=computed(()=>data.value.carbon?carbonSeries(visibleFrames.value):[])
const carbonBudgetData=computed(()=>data.value.carbon?carbonBudget(data.value):null)
const carbonRun=computed(()=>carbonSeriesData.value.length?carbonRunSummary(carbonSeriesData.value):null)
// The water column panel follows the selected sea cell, otherwise the cell with the lowest bottom oxygen of the run.
const carbonWorst=computed(()=>data.value.carbon?worstCell(visibleFrames.value):null)
const carbonCell=computed(()=>{const c=data.value.carbon;if(!c)return null;if(chosen.value&&c.mask[chosen.value.i])return chosen.value.i;return carbonWorst.value})
const carbonProfile=computed(()=>carbonCell.value===null?null:carbonColumn(data.value,carbonCell.value))
const carbonHistory=computed(()=>carbonCell.value===null?null:oxygenSection(visibleFrames.value,carbonCell.value))
// CO2 anomalies of the air over the whole run (colour range), at least 3 ppm.
const carbonPhRange=computed(()=>phRange(snapshots.value))
const carbonAirRange=computed(()=>{let m=3;for(const f of snapshots.value){const c=f.carbon;if(c)for(const v of c.ppm)m=Math.max(m,Math.abs(v-c.background))}return Math.min(m,60)})
// A chart over the frames: polylines of the chosen keys in a 100 x 50 box, with the time cursor.
function carbonChart(entries,{zero=false,pad=0}={}){
 const s=carbonSeriesData.value;if(!s.length)return null
 const t0=s[0].elapsed_seconds,t1=s.at(-1).elapsed_seconds,X=t=>(t-t0)/((t1-t0)||1)*100
 const all=entries.flatMap(e=>s.map(p=>e.value(p)));let lo=Math.min(...all),hi=Math.max(...all);if(zero){lo=Math.min(0,lo);hi=Math.max(0,hi)};if(!(hi>lo)){hi=lo+1};const span=hi-lo;lo-=span*pad;hi+=span*pad
 const Y=v=>46-(v-lo)/(hi-lo)*42
 return {lo,hi,zero:Y(0),x:X(forecast.value.elapsed_seconds),lines:entries.map(e=>({name:e.name,color:e.color,dash:e.dash??'',points:s.map(p=>`${X(p.elapsed_seconds).toFixed(2)},${Y(e.value(p)).toFixed(2)}`).join(' ')}))}
}
const carbonChartDead=computed(()=>carbonChart([{name:'dead zones',color:'#e4572e',value:p=>p.dead_zone_km2/1e3},{name:'anoxic',color:'#b5a2a8',dash:'1 1',value:p=>p.anoxic_km2/1e3},{name:'corrosive',color:'#e066c8',dash:'2 1',value:p=>p.corrosive_km2/1e3}],{zero:true}))
const carbonChartUptake=computed(()=>carbonChart([{name:'uptake',color:'#6fa8ff',value:p=>p.uptake_mt_co2_yr}],{zero:true}))
const carbonChartPh=computed(()=>carbonChart([{name:'pH',color:'#7fd1ff',value:p=>p.mean_ph}],{pad:.1}))
const carbonChartOmega=computed(()=>carbonChart([{name:'Ω',color:'#f4a261',value:p=>p.mean_omega}],{pad:.1}))
const carbonChartAir=computed(()=>carbonChart([{name:'background',color:'#93a5af',dash:'1.5 1',value:p=>p.background_ppm},{name:'over the sea',color:'#6fa8ff',value:p=>p.ocean_ppm},{name:'over land',color:'#9fe870',value:p=>p.land_ppm},{name:'highest',color:'#ff6b6b',value:p=>p.max_ppm}],{pad:.05}))
const carbonChartProduction=computed(()=>carbonChart([{name:'production',color:'#9fe870',value:p=>p.production_mt_c_yr}],{zero:true}))
// Column profile: depth on a square-root scale (0-56 of a 60-high box), one panel per quantity.
const carbonDepthY=(z,max)=>Math.sqrt(Math.max(0,z)/(max||1))*56+2
function carbonStep(key,lo,hi,x0,width){const p=carbonProfile.value;if(!p)return '';const x=v=>(x0+Math.max(0,Math.min(1,(v-lo)/((hi-lo)||1)))*width).toFixed(2);return p.levels.flatMap(l=>[`${x(l[key])},${carbonDepthY(l.top,p.bottom_m).toFixed(2)}`,`${x(l[key])},${carbonDepthY(l.bottom,p.bottom_m).toFixed(2)}`]).join(' ')}
const carbonProfileBands=computed(()=>{const p=carbonProfile.value;if(!p)return [];return p.levels.map(l=>({y:carbonDepthY(l.top,p.bottom_m),h:Math.max(.3,carbonDepthY(l.bottom,p.bottom_m)-carbonDepthY(l.top,p.bottom_m)),fill:habitatInfo(l.cls).color,title:`${l.top.toFixed(0)}–${l.bottom.toFixed(0)} m: ${habitatInfo(l.cls).label} · O₂ ${l.oxygen_umol_kg.toFixed(0)} µmol/kg · pH ${l.ph.toFixed(2)} · Ω ${l.omega.toFixed(2)}`}))})
const carbonSectionRects=computed(()=>{const h=carbonHistory.value;if(!h)return [];const span=(h.end-h.start)||1,y=z=>carbonDepthY(z,h.max_depth_m)*50/60;return h.rects.map(r=>({x:(r.t0-h.start)/span*100,w:Math.max(.05,(r.t1-r.t0)/span*100),y:y(r.z0),h:Math.max(.05,y(r.z1)-y(r.z0)),fill:oxygenLevelColor(r.oxygen_umol_kg)}))})
const carbonSectionTime=computed(()=>{const h=carbonHistory.value;return h?(forecast.value.elapsed_seconds-h.start)/((h.end-h.start)||1)*100:0})
// Scenario comparison: the other fixture (today <-> 2100), loaded on demand.
const CARBON_EXAMPLES={today:{path:'ocean-carbon',label:'Today (2025)',source:'Engine output · ocean carbon today'},future:{path:'ocean-carbon-2100',label:'2100 (high emissions)',source:'Engine output · ocean carbon 2100'}}
const carbonCompare=ref(null)
const carbonScenarioKey=computed(()=>source.value===CARBON_EXAMPLES.future.source?'future':source.value===CARBON_EXAMPLES.today.source?'today':null)
async function compareCarbonScenarios(){
 const mine=carbonScenarioKey.value,other=mine==='future'?'today':'future'
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}${CARBON_EXAMPLES[other].path}/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load the ${CARBON_EXAMPLES[other].label} run (${response.status}).`)
  const frames=validateSequence(await response.json())
  carbonCompare.value={label:CARBON_EXAMPLES[other].label,mine:CARBON_EXAMPLES[mine??'today'].label,summary:carbonRunSummary(carbonSeries(frames))}
 }catch(err){error.value=err.message}
}
// Rows of the comparison, always today -> 2100 whichever run is loaded.
const carbonCompareRows=computed(()=>{const loaded=carbonRun.value,other=carbonCompare.value?.summary;if(!loaded||!other)return [];const future=carbonScenarioKey.value==='future',a=future?other:loaded,b=future?loaded:other
 const r=(label,k,d,unit='')=>({label,today:a[k].toFixed(d)+unit,future:b[k].toFixed(d)+unit,change:(b[k]-a[k]>=0?'+':'')+(b[k]-a[k]).toFixed(d)+unit})
 return [r('Background CO₂ (mean)','background_ppm',0,' ppm'),r('Surface pH (annual mean)','mean_ph',3),r('Lowest monthly pH','min_ph',3),r('Aragonite saturation Ω (mean)','mean_omega',2),r('Ocean uptake (mean)','mean_uptake_mt_co2_yr',1,' Mt CO₂/yr'),r('Largest dead zone','max_dead_zone_km2',0,' km²'),r('Largest anoxic area','max_anoxic_km2',0,' km²'),r('Largest corrosive area','max_corrosive_km2',0,' km²'),r('Habitable volume (lowest)','min_habitable_volume_pct',2,' %'),r('Plankton production (mean)','mean_production_mt_c_yr',0,' Mt C/yr')]})
const fmtKm2=v=>v>=1e4?(v/1e3).toFixed(0)+'k':Math.round(v).toLocaleString()
const signed=(v,d=1)=>(v>=0?'+':'')+v.toFixed(d)
// The carbon view's map: the sea by the chosen quantity, land tinted by the CO2 of the air above it.
function carbonCellColor(d,i){
 const c=d.carbon,L=carbonLayers.value,color=new THREE.Color(),excess=c.ppm[i]-c.background
 if(!c.mask[i]){const [lo,hi]=terrainRange.value,norm=(d.terrain.values[i]-lo)/((hi-lo)||1),shade=terrainShade.value?terrainShade.value[i]:.5;color.setHSL((110-norm*80)/360,.22,.2+norm*.25).multiplyScalar(.6+.8*shade)
  if(L.air)color.lerp(new THREE.Color(ppmColor(excess,carbonAirRange.value)),.7)
  return color}
 const b=L.base
 if(b==='bottomo2')color.set(oxygenLevelColor(c.o2Bottom[i]))
 else if(b==='habitat')color.set(habitatInfo(c.columnClass[i]).color)
 else if(b==='depth'){const k=Math.min(L.level,c.levels.length-1);color.set(c.levelClass[k][i]>=0?oxygenLevelColor(c.levelO2[k][i]):'#3b3f44')}
 else if(b==='ph')color.set(phColor(c.ph[i],...carbonPhRange.value))
 else if(b==='omega')color.set(omegaColor(c.omega[i]))
 else if(b==='omegabottom')color.set(omegaColor(c.omegaBottom[i]))
 else if(b==='flux')color.set(fluxColor(c.flux[i]))
 else if(b==='air')color.set(ppmColor(excess,carbonAirRange.value))
 else if(b==='production')color.set(productionColor(c.production[i]))
 else if(b==='nitrate')color.set(nitrateColor(c.nitrate[i]))
 else color.set('#0b2f52')
 if(L.anoxic&&c.columnClass[i]===3&&b!=='habitat')color.lerp(new THREE.Color('#14030a'),.55)
 return color
}
async function loadCarbonExample(key='today'){
 loading.value=true;error.value=''
 try{
  const e=CARBON_EXAMPLES[key],response=await fetch(`${import.meta.env.BASE_URL}${e.path}/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load the ocean carbon example (${response.status}).`)
  loadFrames(await response.json());source.value=e.source;view.value='carbon';mapLayers.value.relief=false;layer.value='bottomo2';selected.value=null;carbonCompare.value=null
  // Open on the month with the largest dead zone.
  const s=carbonSeries(snapshots.value);seek(s.reduce((best,p,k)=>p.dead_zone_km2>s[best].dead_zone_km2?k:best,0))
 }catch(err){error.value=err.message}finally{loading.value=false}
}
// Map key of the carbon view: the gradient of the sea colour and of the air over land.
const gradient=(fn,lo,hi,n=8)=>`linear-gradient(90deg,${Array.from({length:n},(_,k)=>fn(lo+(hi-lo)*k/(n-1))).join(',')})`
const carbonKey=computed(()=>{const c=data.value.carbon;if(!c)return null;const b=carbonLayers.value.base,r=carbonAirRange.value
 const sea=b==='bottomo2'||b==='depth'?{label:`Oxygen${b==='depth'?` at ${c.tops[Math.min(carbonLayers.value.level,c.levels.length-1)]}–${c.levels[Math.min(carbonLayers.value.level,c.levels.length-1)]} m`:' at the bottom'}: 0 · 60 hypoxic · 260 µmol/kg`,css:gradient(oxygenLevelColor,0,260)}
  :b==='ph'?{label:`Surface pH ${carbonPhRange.value[0].toFixed(2)} (more acid) → ${carbonPhRange.value[1].toFixed(2)} (run range)`,css:gradient(v=>phColor(v,...carbonPhRange.value),...carbonPhRange.value)}
  :b==='omega'||b==='omegabottom'?{label:`Aragonite Ω ${b==='omega'?'at the surface':'at the bottom'}: < 1 corrosive (magenta) → 4`,css:gradient(omegaColor,0,4)}
  :b==='flux'?{label:'CO₂ flux −3 (outgassing) … +3 (uptake) mol/m²/yr',css:gradient(fluxColor,-3,3)}
  :b==='air'?{label:`CO₂ over the sea: −${r.toFixed(0)} … +${r.toFixed(0)} ppm from the background`,css:gradient(v=>ppmColor(v,r),-r,r)}
  :b==='production'?{label:'Production 0 → 3 g C/m²/day',css:gradient(productionColor,0,3)}
  :b==='nitrate'?{label:'Surface nitrate 0 → 30 µmol/kg',css:gradient(nitrateColor,0,30)}:null
 return {sea,air:carbonLayers.value.air?{label:`CO₂ over land: −${r.toFixed(0)} … +${r.toFixed(0)} ppm from ${c.background.toFixed(0)} ppm`,css:gradient(v=>ppmColor(v,r),-r,r)}:null}})
function loadCarbon2100Example(){return loadCarbonExample('future')}
function seekYear(year){const k=snapshots.value.findIndex(f=>f.forecast.elapsed_seconds>=year*31557600-1);if(k>=0)seek(k)}
// Sun & space weather: the sunspot cycle, its minima and maxima, the forecast, and what the cycle does to the UV,
// the ozone, the flares and the climate. Every number comes from the engine's solar block.
const solarLayers=ref({base:'brightness',window:'focus',relief:true,flares:true,labels:true,grid:true})
const solarBlock=computed(()=>data.value.solar?.block??null)
const solarMetrics=computed(()=>data.value.solar?summarizeSolar(data.value):null)
const solarGeometry=computed(()=>solarBlock.value?diskGeometry(solarBlock.value,grid.value):null)
const solarCycleRows=computed(()=>solarBlock.value?cycleTable(solarBlock.value):[])
const solarSites=computed(()=>data.value.solar?uvSites(data.value):[])
const solarNext=computed(()=>{const t=solarBlock.value?.forecast.turning_points??[];return {minimum:t.find(x=>x.kind==='minimum')??null,maximum:t.find(x=>x.kind==='maximum'&&x.cycle>(solarBlock.value?.forecast.current_cycle.cycle??0))??null,current:t.find(x=>x.kind==='maximum'&&x.cycle===solarBlock.value?.forecast.current_cycle.cycle)??null}})
const solarUvPick=computed(()=>{const m=solarMetrics.value;if(!m)return null;const by=k=>m.uv.find(u=>u.key===k);return {herzberg:by('o2_herzberg')??m.uv[0],uvb:by('uv_b')??m.uv.at(-1),lyman:by('lyman_alpha')??m.uv[0]}})
const solarSigned=(v,d=1)=>v===null||v===undefined?'—':(v>=0?'+':'')+v.toFixed(d)
// Charts share the window of the sunspot chart: x in years, y up, in 100 x 50 boxes.
const solarX=(year,[x0,x1])=>(year-x0)/((x1-x0)||1)*100
function yearTicks([x0,x1]){const step=x1-x0>120?25:x1-x0>50?10:5,out=[];for(let y=Math.ceil(x0/step)*step;y<=x1;y+=step)out.push({year:y,x:solarX(y,[x0,x1])});return out}
const solarSunspot=computed(()=>{const c=solarBlock.value?sunspotChart(solarBlock.value,solarLayers.value.window):null;if(!c)return null;const xr=c.x,yr=c.y
 return {...c,paths:{monthly:polyline(c.monthly,xr,yr),smoothed:polyline(c.smoothed,xr,yr),model:polyline(c.model,xr,yr),median:polyline(c.forecast.p50,xr,yr),outer:band(c.forecast.p5,c.forecast.p95,xr,yr),inner:band(c.forecast.p25,c.forecast.p75,xr,yr)},
  issue:solarX(c.issue,xr),cursor:solarX(solarMetrics.value?.year??0,xr),ticks:yearTicks(xr),levels:[100,200,300].filter(v=>v<yr[1]).map(v=>({v,y:chartY(v,yr)})),
  marks:c.extrema.map(e=>({...e,x:solarX(e.year,xr),y:chartY(e.value,yr)}))}})
// A record series and its forecast percentiles over the chart window: the record, the median and the 90 % band.
function solarSeriesChart(recordValues,forecastTriple,{zero=false,pad=.08}={}){
 const b=solarBlock.value;if(!b)return null;const xr=chartWindow(b,solarLayers.value.window),stride=xr[1]-xr[0]>80?3:1
 const rec=monthlyPoints(b.record.first_month,recordValues,xr[0],xr[1],stride),p5=monthlyPoints(b.forecast.first_month,forecastTriple.p5,xr[0],xr[1]),p50=monthlyPoints(b.forecast.first_month,forecastTriple.p50,xr[0],xr[1]),p95=monthlyPoints(b.forecast.first_month,forecastTriple.p95,xr[0],xr[1])
 const vals=[...rec,...p5,...p95].map(p=>p[1]);let lo=Math.min(...vals),hi=Math.max(...vals);if(zero){lo=Math.min(0,lo);hi=Math.max(0,hi)};const span=(hi-lo)||1;lo-=span*pad;hi+=span*pad
 return {lo,hi,record:polyline(rec,xr,[lo,hi]),median:polyline(p50,xr,[lo,hi]),band:band(p5,p95,xr,[lo,hi]),zero:chartY(0,[lo,hi]),issue:solarX(b.forecast.issue_year,xr),cursor:solarX(solarMetrics.value?.year??0,xr),ticks:yearTicks(xr)}}
const solarTsiChart=computed(()=>solarBlock.value?solarSeriesChart(solarBlock.value.record.tsi_w_m2,solarBlock.value.forecast.tsi_w_m2):null)
const solarClimateChart=computed(()=>solarBlock.value?solarSeriesChart(solarBlock.value.record.surface_k,solarBlock.value.forecast.surface_k,{zero:true}):null)
// UV at the top of the atmosphere (the band that makes ozone) against the UV index at the ground, on one scale.
const solarUvChart=computed(()=>{const b=solarBlock.value;if(!b)return null;const key=b.irradiance.bands.some(x=>x.key==='o2_herzberg')?'o2_herzberg':b.irradiance.bands[0].key
 const xr=chartWindow(b,solarLayers.value.window),stride=xr[1]-xr[0]>80?3:1
 const pts=(rv,ft)=>({rec:monthlyPoints(b.record.first_month,rv,xr[0],xr[1],stride),p50:monthlyPoints(b.forecast.first_month,ft.p50,xr[0],xr[1])})
 const top=pts(b.record.uv_change_pct[key],b.forecast.uv_change_pct[key]),ground=pts(b.record.uv_index_change_pct,b.forecast.uv_index_change_pct)
 const vals=[...top.rec,...top.p50,...ground.rec,...ground.p50].map(p=>p[1]),lo=Math.min(0,...vals)*1.1,hi=Math.max(0,...vals)*1.08,yr=[lo,hi]
 return {key,label:b.irradiance.bands.find(x=>x.key===key).label,lo,hi,top:{record:polyline(top.rec,xr,yr),median:polyline(top.p50,xr,yr)},ground:{record:polyline(ground.rec,xr,yr),median:polyline(ground.p50,xr,yr)},
  zero:chartY(0,yr),issue:solarX(b.forecast.issue_year,xr),cursor:solarX(solarMetrics.value?.year??0,xr),ticks:yearTicks(xr)}})
// Annual X flares: synthetic counts of the record (bars), their expectation and the forecast's median with its range.
const solarFlareChart=computed(()=>{const b=solarBlock.value;if(!b)return null;const xr=chartWindow(b,solarLayers.value.window),{past,future}=flareYears(b,xr[0],xr[1])
 const hi=Math.max(5,...past.map(y=>Math.max(y.x1_count,y.x1_expected)),...future.map(y=>y.x1_expected_p95))*1.08,yr=[0,hi],w=100/((xr[1]-xr[0])||1)
 return {hi,bars:past.map(y=>({x:solarX(y.year,xr),w:Math.max(.15,w*.8),y:chartY(y.x1_count,yr),h:Math.max(0,chartY(0,yr)-chartY(y.x1_count,yr)),year:y.year,count:y.x1_count,expected:y.x1_expected})),
  expected:polyline(past.map(y=>[y.year+.5,y.x1_expected]),xr,yr),median:polyline(future.map(y=>[y.year+.5,y.x1_expected_p50]),xr,yr),band:band(future.map(y=>[y.year+.5,y.x1_expected_p5]),future.map(y=>[y.year+.5,y.x1_expected_p95]),xr,yr),
  issue:solarX(b.forecast.issue_year,xr),cursor:solarX(solarMetrics.value?.year??0,xr),ticks:yearTicks(xr)}})
const solarCycleRowsShown=computed(()=>{const b=solarBlock.value;if(!b)return [];const x0=chartWindow(b,solarLayers.value.window)[0]-11;return solarCycleRows.value.filter(r=>r.predicted||r.minimum_year>=x0)})
const solarOutlook=computed(()=>solarBlock.value?solarBlock.value.forecast.annual.slice(0,12):[])
// UV change by band on a logarithmic scale from 0.05 % to 100 % (Lyman-alpha varies a hundred times more than UV-A).
const solarLogPct=v=>Math.max(0,Math.min(100,(Math.log10(Math.max(v,.05))-Math.log10(.05))/(2-Math.log10(.05))*100))
const solarSpectrum=computed(()=>{const m=solarMetrics.value;if(!m)return [];return m.uv.map((u,k)=>({...u,w:u.change_pct>0?solarLogPct(u.change_pct):0,ref:solarLogPct(u.reference_pct),color:`hsl(${270-k*42} 70% 62%)`}))})
// The butterfly diagram is drawn into a canvas; the cursor and the issue date are SVG lines over it.
const butterflyCanvas=ref(null)
const solarButterfly=computed(()=>{const b=solarBlock.value;if(!b)return null;const xr=chartWindow(b,solarLayers.value.window);return {x:xr,issue:solarX(b.forecast.issue_year,xr),cursor:solarX(solarMetrics.value?.year??0,xr),ticks:yearTicks(xr),latitude:[b.butterfly.latitudes_deg[0],b.butterfly.latitudes_deg.at(-1)]}})
// Long term: grand minima and maxima, what they did to the irradiance, the cosmic rays and the climate, and the dynamo
// map that makes them. All series come from the engine's long_term block.
const longTerm=computed(()=>solarBlock.value?.long_term??null)
const longLayers=ref({window:'all',scenario:true,regional:false,zoom:false})
const longWindow=computed(()=>longTerm.value?longTermWindow(longTerm.value,longLayers.value.window):[0,1])
const longNow=computed(()=>longTerm.value&&solarMetrics.value?longTermAt(longTerm.value,solarMetrics.value.year):null)
function longTicks([x0,x1]){const span=x1-x0,step=span>600?100:span>250?50:span>120?20:10,out=[];for(let y=Math.ceil(x0/step)*step;y<=x1;y+=step)out.push({year:y,x:solarX(y,[x0,x1])});return out}
// A chart of annual long-term series over a window, with the epoch bands, the episodes and the issue date.
function longChart(L,xr,lines,{zero=false,pad=.06,fixed=null}={}){
 const stride=xr[1]-xr[0]>500?2:1,pts=lines.map(l=>({...l,points:yearlyPoints(L,l.values,xr,stride).filter(([y])=>!l.after||y>=l.after)}))
 const vals=pts.flatMap(l=>l.points.map(p=>p[1]));let lo=fixed?fixed[0]:Math.min(...vals),hi=fixed?fixed[1]:Math.max(...vals);if(zero){lo=Math.min(0,lo);hi=Math.max(0,hi)};if(!fixed){const span=(hi-lo)||1;lo-=span*pad;hi+=span*pad}
 const yr=[lo,hi]
 return {x:xr,lo,hi,zero:chartY(0,yr),lines:pts.map(l=>({name:l.name,color:l.color,dash:l.dash??'',width:l.width??.4,points:polyline(l.points,xr,yr),band:l.band?band(yearlyPoints(L,l.band[0],xr,stride),yearlyPoints(L,l.band[1],xr,stride),xr,yr):null})),
  bands:epochBands(L,xr).map(b=>({...b,x:solarX(b.from,xr),w:Math.max(.2,solarX(b.to,xr)-solarX(b.from,xr))})),
  episodes:L.episodes.filter(e=>e.end_year>=xr[0]&&e.start_year<=xr[1]).map(e=>({...e,x:solarX(Math.max(xr[0],e.start_year),xr),w:Math.max(.2,solarX(Math.min(xr[1],e.end_year+1),xr)-solarX(Math.max(xr[0],e.start_year),xr))})),
  issue:solarX(L.issue_year,xr),cursor:solarX(solarMetrics.value?.year??0,xr),ticks:longTicks(xr),y:v=>chartY(v,yr)}
}
const longActivity=computed(()=>{const L=longTerm.value;if(!L)return null;const g=L.episode_definition
 const c=longChart(L,longWindow.value,[{name:'annual mean',color:'#7d8f99',values:L.series.sunspot,width:.25},{name:'decadal mean',color:'#ffd166',values:L.series.decadal_sunspot,width:.6},
  ...(longLayers.value.scenario?[{name:'grand-minimum scenario',color:'#7fd1ff',dash:'1.2 .6',values:L.series.sunspot_grand_minimum_scenario,after:L.issue_year,width:.5}]:[])],{zero:true})
 return {...c,thresholds:[{v:g.grand_minimum_mean,label:'grand minimum below'},{v:g.grand_maximum_mean,label:'grand maximum above'}].map(t=>({...t,y:c.y(t.v)}))}})
const longClimate=computed(()=>{const L=longTerm.value;if(!L)return null;const T=L.series.temperature_k
 return longChart(L,longWindow.value,[{name:'solar (central; band: low–high background)',color:'#ffb347',values:T.solar_central,band:[T.solar_low.map((v,k)=>Math.min(v,T.solar_high[k])),T.solar_low.map((v,k)=>Math.max(v,T.solar_high[k]))],width:.5},
  {name:'volcanic',color:'#9aa5ab',values:T.volcanic,width:.35},{name:'CO₂',color:'#ff6b6b',values:T.co2,width:.45},{name:'total (central)',color:'#e8f4ff',values:T.total,width:.55},
  ...(longLayers.value.scenario?[{name:'total with a new grand minimum',color:'#7fd1ff',dash:'1.2 .6',values:T.total_grand_minimum,after:L.issue_year,width:.5}]:[]),
  ...(longLayers.value.regional?[{name:L.regional_winter.name,color:'#b388ff',values:T.regional_winter,width:.35}]:[])],{zero:true,fixed:longLayers.value.zoom?[-.45,.45]:null})})
const longTsi=computed(()=>{const L=longTerm.value;if(!L)return null;const S=L.series.tsi_w_m2
 return longChart(L,longWindow.value,[{name:'central',color:'#ffd166',values:S.central,band:[S.high,S.low],width:.45},...(longLayers.value.scenario?[{name:'grand-minimum scenario',color:'#7fd1ff',dash:'1.2 .6',values:S.grand_minimum_scenario,after:L.issue_year}]:[])])})
const longProduction=computed(()=>{const L=longTerm.value;if(!L)return null
 return longChart(L,longWindow.value,[{name:'¹⁴C / ¹⁰Be production (relative to the satellite era)',color:'#9fe870',values:L.series.cosmogenic_production,width:.4}])})
const longEpochs=computed(()=>longTerm.value?epochTable(longTerm.value):[])
// Short band labels: weak epochs on a second row, narrow bands abbreviated.
function longLabel(b){const name=b.name.replace(' around 1900','').replace(' Minimum','').replace(' Maximum',' max.').replace(' minimum','');return b.w>6?name:name.slice(0,4)+'.'}
const longHorizonRow=computed(()=>{const L=longTerm.value;if(!L)return null;return [...L.dynamo.outlook.cycles].reverse().find(c=>c.peak_year<=L.horizon_year)??null})
// The case study (the Maunder Minimum by default): its epoch row and a chart of its window.
const longCase=computed(()=>{const L=longTerm.value;if(!L?.case_study)return null;const row=longEpochs.value.find(e=>e.name===L.case_study.epoch);if(!row)return null
 const xr=[Math.max(L.start_year,L.case_study.from_year),Math.min(L.horizon_year+1,L.case_study.to_year)]
 const chart=longChart(L,xr,[{name:'annual mean',color:'#ffd166',values:L.series.sunspot,width:.5},{name:'¹⁴C / ¹⁰Be production × 100',color:'#9fe870',values:L.series.cosmogenic_production.map(v=>v*100),width:.4,dash:'1 .6'}],{zero:true})
 const cycles=L.cycles.filter(c=>c.peak_year>=xr[0]&&c.peak_year<=xr[1])
 return {row,chart,cycles,xr}})
const longDynamo=computed(()=>longTerm.value?dynamoExample(longTerm.value):null)
const longDynamoBars=computed(()=>{const x=longDynamo.value;if(!x)return null;const n=x.rows.length,w=100/n,max=Math.max(x.maxAmplitude,x.threshold)*1.1
 return {bars:x.rows.map(r=>({...r,x:r.k*w+w*.1,w:w*.8,y:46-r.amplitude/max*42,h:r.amplitude/max*42,e:46-r.emergence*42})),threshold:46-x.threshold/max*42,emergence:x.rows.map(r=>`${(r.k*w+w/2).toFixed(2)},${(46-r.emergence*42).toFixed(2)}`).join(' '),max,episode:{x:x.start*w,w:x.cycles*w}}})
const longHolocene=computed(()=>{const L=longTerm.value;if(!L)return null;const h=holoceneStrip(L),hi=Math.max(...h.points.map(p=>p[1]))*1.05
 return {...h,line:polyline(h.points,[0,h.years],[0,hi]),bands:h.bands.map(b=>({...b,x:b.from/h.years*100,w:Math.max(.15,(b.to-b.from)/h.years*100)}))}})
const longTargets=computed(()=>{const L=longTerm.value;if(!L)return [];const s=L.dynamo.statistics,t=L.dynamo.targets??{}
 const r=(label,key,d,unit='')=>({label,model:s[key]===null||s[key]===undefined?'—':(key.endsWith('fraction')?(s[key]*100).toFixed(0)+' %':s[key].toFixed(d)+unit),target:t[key]===undefined?'—':(key.endsWith('fraction')?(t[key]*100).toFixed(0)+' %':t[key].toFixed(d)+unit)})
 return [r('Grand minima per 11,400 years','grand_minima_per_11400_years',0),r('Share of time in grand minima','grand_minimum_fraction',0),r('Mean length of a grand minimum','grand_minimum_mean_years',0,' yr'),
  r('Grand maxima per 11,400 years','grand_maxima_per_11400_years',0),r('Share of time in grand maxima','grand_maximum_fraction',0),r('Mean length of a grand maximum','grand_maximum_mean_years',0,' yr')]})
// Cosmic dust: the dust and small meteoroids entering the Earth, what burns up, what reaches the ground, where the
// extraterrestrial water goes, and the budgets it is compared with. Maps are drawn for the central influx and scaled.
const dustLayers=ref({base:'hour',scenario:'central',night:true,sources:true,labels:true,relief:true})
const dustBlock=computed(()=>data.value.dust?.block??null)
const dustMetrics=computed(()=>data.value.dust?summarizeDust(data.value,dustLayers.value.scenario):null)
const dustScenario=computed(()=>dustBlock.value?dustBlock.value.scenarios[dustLayers.value.scenario]??dustBlock.value.scenarios.central:null)
const dustValues=computed(()=>{const d=data.value.dust;if(!d)return [];const s=dustMetrics.value.scale;return (dustLayers.value.base==='daily'?d.daily:d.influx).map(v=>v*s)})
// One colour range over the whole day (and the daily mean), so that the hours compare.
const dustRange=computed(()=>{const b=dustBlock.value;if(!b)return [0,1];const s=dustMetrics.value.scale;let lo=Infinity,hi=-Infinity;for(const f of b.map.frames)for(const v of f.influx_ug_m2_day){lo=Math.min(lo,v);hi=Math.max(hi,v)};return [lo*s,hi*s]})
function dustCellColor(d,i){
 const v=dustValues.value[i],[lo,hi]=dustRange.value,color=new THREE.Color(dustRampColor(v,lo,hi))
 if(d.dust.land.length&&d.dust.land[i])color.lerp(new THREE.Color('#7a9a52'),.38)
 if(dustLayers.value.night&&dustLayers.value.base==='hour'&&!d.dust.daylight[i])color.multiplyScalar(.55)
 return color
}
const dustCell=(lat,lon)=>{const g=grid.value;const col=Math.min(g.nx-1,Math.max(0,Math.floor((lon+180)/360*g.nx))),row=Math.min(g.ny-1,Math.max(0,Math.floor((lat+90)/180*g.ny)));return row*g.nx+col}
const dustLatLon=i=>{const g=grid.value;return {lat:-90+(Math.floor(i/g.nx)+.5)*180/g.ny,lon:-180+((i%g.nx)+.5)*360/g.nx}}
const dustShares=computed(()=>dustBlock.value?fateShares(dustBlock.value):[])
const dustBudget=computed(()=>dustBlock.value?budgetRows(dustBlock.value,dustLayers.value.scenario):[])
// Sizes: mass share of each bin (bars) and the share surviving to the ground (line), against the log diameter.
const dustSizes=computed(()=>{const b=dustBlock.value;if(!b)return null;const rows=sizeRows(b),x0=Math.log10(rows[0].diameter_um),x1=Math.log10(rows.at(-1).diameter_um),max=Math.max(...rows.map(r=>r.mass_share))
 const X=d=>(Math.log10(d)-x0)/((x1-x0)||1)*96+2,w=96/rows.length*.8
 const ticks=[1,10,100,1000,10000].filter(d=>Math.log10(d)>=x0&&Math.log10(d)<=x1).map(d=>({d,x:X(d)}))
 return {bars:rows.map(r=>({...r,x:X(r.diameter_um)-w/2,w,h:r.mass_share/max*40,y:46-r.mass_share/max*40})),surviving:rows.map(r=>`${X(r.diameter_um).toFixed(2)},${(46-r.surviving*40).toFixed(2)}`).join(' '),ticks,max}})
// Altitude profiles: altitude upward, each quantity normalised to its maximum.
const dustProfile=computed(()=>{const b=dustBlock.value;if(!b)return null;const rows=profileRows(b),h0=rows[0].altitude_km,h1=rows.at(-1).altitude_km,Y=h=>48-(h-h0)/((h1-h0)||1)*46
 const line=key=>rows.map(r=>`${(2+r[key]*96).toFixed(2)},${Y(r.altitude_km).toFixed(2)}`).join(' ')
 const peak=key=>rows.reduce((best,r)=>r[key]>best[key]?r:best)
 return {ablation:line('ablation'),water:line('water'),energy:line('energy'),levels:[60,80,100,120,140,160].filter(h=>h>=h0&&h<=h1).map(h=>({h,y:Y(h)})),metal:{y0:Y(105),y1:Y(80)},nlc:Y(83),h0,h1,
  peaks:{ablation:peak('ablation').altitude_km,water:peak('water').altitude_km,energy:peak('energy').altitude_km}}})
// The day at one place (the selected cell, else central Europe): influx per hour against local solar time.
const dustPlace=computed(()=>{const d=data.value.dust;if(!d)return null;const i=chosen.value?chosen.value.i:dustCell(48,10),{lat,lon}=dustLatLon(i),b=d.block,s=dustMetrics.value.scale
 const rows=b.map.frames.map(f=>({hour:f.hour_utc,local:localSolarTime(lon,f.subsolar.longitude_deg),value:f.influx_ug_m2_day[i]*s,day:f.daylight[i]}))
 const max=Math.max(...rows.map(r=>r.value))*1.1,X=h=>h/24*100
 const sorted=[...rows].sort((a,c)=>a.local-c.local)
 return {i,lat,lon,rows,max,line:sorted.map(r=>`${X(r.local).toFixed(2)},${(46-r.value/max*42).toFixed(2)}`).join(' '),dots:rows.map(r=>({...r,x:X(r.local),y:46-r.value/max*42})),
  peak:rows.reduce((best,r)=>r.value>best.value?r:best),low:rows.reduce((best,r)=>r.value<best.value?r:best),cursor:X(localSolarTime(lon,d.subsolar.longitude_deg))}})
const dustLatitude=computed(()=>{const b=dustBlock.value;if(!b)return null;const s=dustMetrics.value.scale,rows=b.map.by_latitude,max=Math.max(...rows.map(r=>r.influx_ug_m2_day))*s*1.1
 return {max,line:rows.map(r=>`${((r.latitude_deg+90)/180*100).toFixed(2)},${(46-r.influx_ug_m2_day*s/max*42).toFixed(2)}`).join(' ')}})
async function loadDustExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}dust/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load the cosmic dust example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · cosmic dust';view.value='dust';layer.value='dustinflux';selected.value=null;seek(Math.min(6,snapshots.value.length-1))
 }catch(err){error.value=err.message}finally{loading.value=false}
}
// Solar wind: the polar caps of the magnetosphere, the aurora, the ions escaping to space, cosmic radiation and
// solar protons over the poles, the storm hour by hour, the solar cycle, scenarios and the global budgets.
const windLayers=ref({globe:true,field:true,magnetopause:true,sunwind:true,outflow:true,ovals:true,night:true,relief:true,labels:true})
const windBlock=computed(()=>data.value.solarWind?.block??null)
const windMetrics=computed(()=>data.value.solarWind?summarizeWind(data.value):null)
const windKey=computed(()=>WIND_LAYER_KEYS.includes(layer.value)?layer.value:'windaurora')
const windValues=computed(()=>data.value.solarWind?windLayerValues(data.value,windKey.value):[])
const windRange=computed(()=>snapshots.value[0]?.solarWind?windLayerRange(snapshots.value,windKey.value):[0,1])
const WIND_UNITS={'windaurora':'mW/m²','windoutflow':'10⁷ O⁺/cm²/s','winddose':'µSv/h','windsep':'10⁹ ion pairs/cm²/s','windcutoff':'GV','windthermo':'K','windregion':''}
const WIND_TITLES={windaurora:'Aurora: particle energy raining into the upper atmosphere',windoutflow:'Oxygen ions flowing out of the polar ionosphere',winddose:'Cosmic radiation at flight altitude (12 km)',windsep:'Ionisation by solar protons (polar cap absorption)',windcutoff:'Cutoff rigidity: how strongly the field shields',windthermo:'Thermosphere temperature',windregion:'Open and closed magnetic field'}
// Scene position of a cell on the globe: the Sun along +x (a Sun-fixed frame), north up, tilted by the season.
const windRotation=computed(()=>data.value.solarWind?sunFixedRotation(data.value.solarWind.map.subsolar):null)
const windLatLon=i=>{const g=grid.value;return {lat:-90+(Math.floor(i/g.nx)+.5)*180/g.ny,lon:-180+(i%g.nx+.5)*360/g.nx}}
const windDaylight=computed(()=>{const w=data.value.solarWind,rot=windRotation.value;if(!w)return [];return w.static.cutoff_gv.map((_,i)=>{const {lat,lon}=windLatLon(i);return rot(geographicVector(lat,lon))[0]>0})})
function windCellColor(d,i){
 const key=windKey.value,[lo,hi]=windRange.value,v=windValues.value[i],color=new THREE.Color(windColor(key,v,lo,hi))
 const land=d.solarWind.static.land_mask
 if(land&&land[i]&&key!=='windregion')color.lerp(new THREE.Color('#6f8f5a'),(key==='windaurora'||key==='windoutflow'||key==='windsep')&&v<=lo+.02*(hi-lo)?.45:.18)
 if(windLayers.value.night&&!windDaylight.value[i])color.multiplyScalar(.55)
 return color
}
const windSeries=computed(()=>windBlock.value?.storm.series??null)
const windHour=computed(()=>data.value.solarWind?.hour??0)
// Storm charts: several lines on one time axis, with the current hour marked.
const windCharts=computed(()=>{const s=windSeries.value;if(!s)return [];const x=s.hour,c=(title,lines,opts={},note='')=>{const ch=chartLines(x,lines,opts);return {title,note,...ch}}
 return [
  c('SOLAR WIND SPEED · km/s',[{values:s.speed_km_s,color:'#ffd166'}],{lo:300}),
  c('INTERPLANETARY FIELD Bz · nT (south drives storms)',[{values:s.bz_nt,color:'#7fb8ff'},{values:s.bz_nt.map(()=>0),color:'#425561'}]),
  c('RING CURRENT · Dst nT',[{values:s.dst_nt,color:'#ff7b6b'},{values:s.dst_nt.map(()=>0),color:'#425561'}]),
  c('Kp INDEX · 0–9 (G1 from 5, G5 at 9)',[{values:s.kp,color:'#c9a6ff'},{values:s.kp.map(()=>5),color:'#425561'}],{lo:0,hi:9}),
  c('POLAR CAP AND OVAL · magnetic latitude °',[{values:s.cap_boundary_mlat_deg,color:'#ff9f5a'},{values:s.oval_equatorward_midnight_mlat_deg,color:'#58e07a'}],{lo:40,hi:85},'orange: edge of the open polar cap · green: equatorward edge of the oval at midnight'),
  c('MAGNETOPAUSE · Earth radii',[{values:s.standoff_re,color:'#c9a6ff'},{values:s.standoff_re.map(()=>6.6),color:'#ff5c5c'}],{lo:4},'red: geostationary orbit (6.6 Earth radii)'),
  c('ENERGY INTO THE UPPER ATMOSPHERE · GW per hemisphere',[{values:s.hemispheric_power_gw,color:'#58e07a'},{values:s.joule_heating_gw,color:'#ff7b6b'}],{lo:0},'green: aurora (particles) · red: Joule heating (currents)'),
  c('THERMOSPHERE · global mean exospheric temperature K',[{values:s.exospheric_k,color:'#ffb86b'}],{},'density at 400 km up to ×'+Math.max(...s.density_400km_ratio).toFixed(1)+' (satellite drag)'),
  c('ESCAPE TO SPACE · particles per second (log)',[{values:s.hydrogen_escape_per_s,color:'#7fe0ff'},{values:s.hydrogen_jeans_per_s,color:'#3f8fb0'},{values:s.oxygen_escape_per_s,color:'#ff8a3d'},{values:s.helium_escape_per_s,color:'#e8e8a0'}],{log:true},'cyan: hydrogen (dark: Jeans part) · orange: oxygen ions · yellow: helium'),
  c('GALACTIC COSMIC RAYS · change at the ground %',[{values:s.neutron_pole_change_pct,color:'#c9a6ff'},{values:s.neutron_equator_change_pct,color:'#7fb8ff'}],{hi:0},'violet: polar caps · blue: magnetic equator (Forbush decrease)'),
  c('SOLAR PROTONS > 10 MeV · pfu (log)',[{values:s.sep_flux_pfu.map(v=>Math.max(v,.1)),color:'#ff5fb0'}],{log:true,lo:.1},'S1 from 10, S3 from 1,000, S4 from 10,000 pfu'),
  c('OZONE · polar upper stratosphere (35–50 km) %',[{values:s.ozone_upper_north_pct,color:'#9fe870'},{values:s.ozone_upper_south_pct,color:'#4fc3f7'}],{hi:0},'NOx from solar protons · green: north, blue: south (the same in this model)'),
 ]})
const windCursor=computed(()=>{const s=windSeries.value;if(!s)return 0;return windHour.value/(s.hour.at(-1)||1)*100})
// The solar cycle in annual means.
const windCycle=computed(()=>windBlock.value?.cycle??null)
const windCycleCharts=computed(()=>{const cy=windCycle.value;if(!cy)return [];const years=annualMeans(cy,'sunspot_number').map(r=>r.year),a=k=>annualMeans(cy,k).map(r=>r.value),amu=1.66053907e-27
 const hy=a('hydrogen_escape_per_s'),ox=a('oxygen_escape_per_s')
 const c=(title,lines,opts={},note='')=>({title,note,...chartLines(years,lines,opts)})
 return [
  c('SUNSPOT NUMBER',[{values:a('sunspot_number'),color:'#ffd166'}],{lo:0},cy.data_origin==='synthetic_from_cycle_table'?'synthetic record from a cycle table, then the forecast':''),
  c('ESCAPE · kg/s (log)',[{values:hy.map(v=>v*1.00784*amu),color:'#7fe0ff'},{values:ox.map(v=>v*15.999*amu),color:'#ff8a3d'},{values:a('helium_escape_per_s').map(v=>v*4.0026*amu),color:'#e8e8a0'}],{log:true},'cyan: hydrogen (steady: limited by supply) · orange: oxygen · yellow: helium'),
  c('HYDROGEN ROUTES · share of the escape %',[{values:a('hydrogen_jeans_per_s').map((v,k)=>v/hy[k]*100),color:'#3f8fb0'},{values:a('hydrogen_charge_exchange_per_s').map((v,k)=>v/hy[k]*100),color:'#7fe0ff'},{values:a('hydrogen_polar_wind_per_s').map((v,k)=>v/hy[k]*100),color:'#ff9f5a'}],{lo:0,hi:100},'dark: Jeans (thermal) · cyan: charge exchange · orange: polar wind'),
  c('COSMIC RADIATION AT 12 km · µSv/h',[{values:a('dose_pole_usv_h'),color:'#c9a6ff'},{values:a('dose_50deg_usv_h'),color:'#9a8bd8'},{values:a('dose_equator_usv_h'),color:'#7fb8ff'}],{lo:0},'violet: polar caps · 50° magnetic · blue: equator'),
  c('POLAR CAP · mean boundary °',[{values:a('cap_boundary_mlat_deg'),color:'#ff9f5a'},{values:a('oval_equatorward_midnight_mlat_deg'),color:'#58e07a'}],{},'orange: open/closed boundary · green: oval at midnight (monthly means)'),
  c('NOx INTO THE POLAR STRATOSPHERE · Gmol/yr and OZONE %',[{values:a('sep_nox_mol_yr').map(v=>v/1e9),color:'#ff5fb0'},{values:a('epp_nox_mol_yr').map(v=>v/1e9),color:'#58e07a'},{values:a('ozone_upper_pct').map(v=>-v),color:'#9fe870'}],{lo:0},'pink: NOx from solar protons · green: descending auroral NOx (part of the background) · light green: ozone loss from the solar protons, % (35–50 km, polar)'),
 ]})
// The latitude profile of the shielding.
const windProfile=computed(()=>{const b=windBlock.value;if(!b)return null;const p=b.latitude_profile,x=p.magnetic_latitude_deg
 return {dose:chartLines(x,[{values:p.dose_min_usv_h,color:'#c9a6ff'},{values:p.dose_max_usv_h,color:'#7fb8ff'},{values:p.dose_sep_peak_usv_h,color:'#ff5fb0'}],{lo:0}),
  cutoff:chartLines(x,[{values:p.cutoff_gv,color:'#ffd166'}],{lo:0}),
  sep:chartLines(x,[{values:p.sep_ion_pairs_peak_cm2_s.map(v=>Math.max(v,1e3)),color:'#ff5fb0'}],{log:true,lo:1e3})}})
// Polar dials (magnetic latitude above 50 deg, noon at the top) of the current layer, with the oval's edges.
const windDials=computed(()=>{const d=data.value;if(!d.solarWind)return [];const key=windKey.value,[lo,hi]=windRange.value,vals=windValues.value,o=d.solarWind.map.oval
 const edge=kind=>Array.from({length:49},(_,k)=>{const mlt=k/2,colat=Math.min(50,ovalBoundary(o,mlt,kind)),a=(mlt-12)/24*2*Math.PI,r=colat/50*45;return `${(50-r*Math.sin(a)).toFixed(2)},${(50-r*Math.cos(a)).toFixed(2)}`}).join(' ')
 return [1,-1].map(h=>({hemisphere:h,cells:polarDial(d,h,vals).map(c=>({...c,color:windColor(key,c.value,lo,hi)})),pole:edge('poleward'),equator:edge('equatorward')}))})
const windEscape=computed(()=>windBlock.value?escapeRows(windBlock.value):[])
const windCity=computed(()=>windBlock.value?windBlock.value.cities:[])
const windSigned=(v,d=0)=>(v>0?'+':v<0?'−':'')+Math.abs(v).toFixed(d)
const windSpan=c=>`${sig(c.lo,3)}–${sig(c.hi,3)}`
const windSepReach=computed(()=>{const p=windBlock.value?.latitude_profile;if(!p)return 60;const v=p.sep_ion_pairs_peak_cm2_s,top=Math.max(...v);const k=v.findIndex(x=>x>=.1*top);return k<0?90:p.magnetic_latitude_deg[k]})
const windInGeo=computed(()=>windSeries.value?windSeries.value.standoff_re.filter(v=>v<6.6).length:0)
async function loadWindExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}solar-wind/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load the solar wind example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · solar wind & polar caps';view.value='solarwind';layer.value='windaurora';selected.value=null;seek(Math.min(19,snapshots.value.length-1))
 }catch(err){error.value=err.message}finally{loading.value=false}
}
function drawButterfly(){const c=butterflyCanvas.value,b=solarBlock.value;if(!c||!b)return;const img=butterflyImage(b,chartWindow(b,solarLayers.value.window),480,110);c.width=img.width;c.height=img.height;c.getContext('2d').putImageData(new ImageData(img.pixels,img.width,img.height),0,0)}
// For the cycle colouring: the region whose centre lies closest to each cell, within a few spot radii.
const solarRegionOf=computed(()=>{const d=data.value.solar,geo=solarGeometry.value;if(!d||!geo)return []
 const regions=d.regions.map(r=>({...regionPosition(r),reach:Math.max(Math.acos(1-Math.min(.5,r.area_uhem*1e-6*24)),.035)}))
 return geo.map(g=>{if(g.mu===null)return -1;let best=-1,dist=Infinity;regions.forEach((r,k)=>{const a=Math.acos(Math.max(-1,Math.min(1,g.x*r.x+g.y*r.y+g.mu*r.mu)));if(a<r.reach&&a<dist){dist=a;best=k}});return best})})
function solarCellColor(d,i){
 const s=d.solar,t=s.brightness[i],q=s.quiet[i],L=solarLayers.value,color=new THREE.Color()
 if(t===null)return color.set('#04070a')
 if(L.base==='contrast')return color.set(contrastColor(contrastPct(t,q)))
 color.set(brightnessColor(t))
 if(L.base==='cycle'){color.lerp(new THREE.Color('#2b2f33'),.55);const k=solarRegionOf.value[i];if(k>=0)color.set(cycleColor(s.regions[k].cycle,solarMetrics.value?.cycle??s.regions[k].cycle)).multiplyScalar(Math.min(1,.55+.45*(t/q)))}
 return color
}
async function loadSolarExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}solar/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load the solar cycle example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · solar cycle';view.value='solar';layer.value='sunbrightness';selected.value=null
  // Open on the last observed frame, just before the forecast starts.
  const k=snapshots.value.findIndex(f=>f.solar.source==='forecast');seek(Math.max(0,k-1))
 }catch(err){error.value=err.message}finally{loading.value=false}
}
// Polar ocean run (Mode A): sea ice, the coupled Greenland ice sheet, glacial fresh water, hosing and warming.
const oceanPolarMetrics=computed(()=>data.value.ocean?.polar?summarizePolarOcean(data.value):null)
const oceanPolarSeries=computed(()=>data.value.ocean?.polar?polarOceanSeries(visibleFrames.value):[])
const oceanPolarYears=computed(()=>data.value.ocean?.polar?polarOceanYears(visibleFrames.value):[])
const oceanPolarEvents=computed(()=>overturningEvents(oceanPolarYears.value))
const oceanSameSeason=computed(()=>{if(!data.value.ocean?.polar)return [];const frames=visibleFrames.value,k=Math.min(frameIndex.value,frames.length-1);return sameSeason(frames,k).map(j=>({k:j,year:frames[j].forecast.elapsed_seconds/31557600,...summarizePolarOcean(frames[j])}))})
const oceanWarming=computed(()=>{const w=data.value.ocean?.forcing?.warming;if(!w)return null;const dT=Math.max(0,Math.min(w.max_k,w.rate_k_per_year*(forecast.value.elapsed_seconds-w.start_seconds)/31557600));return {base:dT,south:dT*w.amplification_south,north:dT*w.amplification_north,max:w.max_k}})
// A small time chart of polar series: keys [[key,colour]], own range (including zero when asked).
function polarOceanChart(keys,{zero=false,scale=v=>v}={}){
 const s=oceanPolarSeries.value;if(s.length<2)return null
 const t0=s[0].elapsed_seconds,t1=s.at(-1).elapsed_seconds||1,vals=s.flatMap(p=>keys.map(([k])=>p[k]).filter(v=>v!==null).map(scale))
 let lo=Math.min(...vals),hi=Math.max(...vals);if(zero){lo=Math.min(0,lo);hi=Math.max(0,hi)}if(hi-lo<1e-9){hi=lo+1}
 const X=t=>(t-t0)/((t1-t0)||1)*100,Y=v=>45-(v-lo)/(hi-lo)*40
 return {lo,hi,zero:Y(0),x:X(forecast.value.elapsed_seconds),lines:keys.map(([k,color])=>({key:k,color,points:s.filter(p=>p[k]!==null).map(p=>`${X(p.elapsed_seconds).toFixed(2)},${Y(scale(p[k])).toFixed(2)}`).join(' ')})),
  years:oceanPolarYears.value.map(y=>({x:X((y.year+.5)*31557600),y:keys[0][0]==='overturning_sv'?Y(y.overturning_mean_sv):null}))}
}
const poChartOverturning=computed(()=>polarOceanChart([['overturning_sv','#b388ff']],{zero:true}))
const poChartFresh=computed(()=>polarOceanChart([['glacial_sv','#7fffd4'],['hosing_sv','#ffd166']],{zero:true}))
const poChartSeaIce=computed(()=>polarOceanChart([['sea_ice_area_mkm2','#e8f4ff']],{zero:true}))
const poChartGreenland=computed(()=>polarOceanChart([['greenland_smb_gt_yr','#75bbfa']],{zero:true}))
const poChartNorth=computed(()=>polarOceanChart([['north_sst_c','#ffb366']]))
const poChartOxygen=computed(()=>polarOceanChart([['north_oxygen_umol_kg','#9fe870']]))
const fmtFlow=v=>Math.abs(v)>=1e5?(v/1e6).toFixed(2)+' Sv':Math.abs(v)>=1e3?(v/1e3).toFixed(1)+' ×10³ m³/s':Math.round(v).toLocaleString()+' m³/s'
// Polar melt: Greenland, the Arctic sea ice and the Atlantic overturning under warming scenarios (centuries).
// Greenland is 2,800 km long and only 3 km high: the ice dome needs a stronger vertical scale to be visible.
const POLAR_RELIEF=25
const polarLayers=ref({base:'thickness',scenario:''})
const polarBlock=computed(()=>data.value.polar?.block??null)
const polarScenario=computed(()=>{const b=polarBlock.value;if(!b)return '';return b.scenarios.some(x=>x.name===polarLayers.value.scenario)?polarLayers.value.scenario:b.map_scenario})
const polarMetrics=computed(()=>data.value.polar?summarizePolar(data.value,polarScenario.value):null)
const polarEvents=computed(()=>polarBlock.value?eventTable(polarBlock.value):[])
const polarYear=computed(()=>data.value.polar?.year??0)
// A chart: every scenario's series of one key over the years; returns polylines in a 100 x 50 box.
function polarChart(key,transform=v=>v){
 const b=polarBlock.value;if(!b)return {lines:[],lo:0,hi:1,x:0}
 const series=scenarioSeries(b,key).map(x=>({...x,points:x.points.map(([y,v])=>[y,transform(v)])}))
 const values=series.flatMap(x=>x.points.map(p=>p[1]));let lo=Math.min(0,...values),hi=Math.max(...values);if(!(hi>lo))hi=lo+1
 const y0=b.output_years[0],y1=b.output_years.at(-1)
 const X=y=>(y-y0)/((y1-y0)||1)*100,Y=v=>46-(v-lo)/(hi-lo)*42
 return {lo,hi,x:X(polarYear.value),zero:Y(0),lines:series.map((x,k)=>({name:x.name,control:x.control,color:scenarioColor(k-(b.scenarios[0].control?1:0),x.control),width:x.name===polarScenario.value?.9:.5,points:x.points.map(([y,v])=>`${X(y).toFixed(2)},${Y(v).toFixed(2)}`).join(' ')}))}
}
const chartOverturning=computed(()=>polarChart('overturning_sv'))
const chartSeaLevel=computed(()=>polarChart('sea_level_rise_vs_control_m',v=>v*100))
const chartSeptemberIce=computed(()=>polarChart('arctic_ice_september_m'))
const chartNorthAtlantic=computed(()=>polarChart('north_atlantic_temperature_change_k'))
const chartOxygen=computed(()=>polarChart('north_oxygen_change_pct'))
// Freshwater forcing against the threshold for the focused scenario.
const chartThreshold=computed(()=>{
 const b=polarBlock.value;if(!b)return null
 const s=b.scenarios.find(x=>x.name===polarScenario.value),y0=b.output_years[0],y1=b.output_years.at(-1)
 const all=[...s.series.freshwater_sv,...s.series.threshold_sv].filter(v=>v!==null),lo=Math.min(...all)*.95,hi=Math.max(...all)*1.05
 const X=y=>(y-y0)/((y1-y0)||1)*100,Y=v=>46-(v-lo)/((hi-lo)||1)*42
 const line=key=>b.output_years.map((y,k)=>s.series[key][k]===null?null:`${X(y).toFixed(2)},${Y(s.series[key][k]).toFixed(2)}`).filter(Boolean).join(' ')
 return {lo,hi,x:X(polarYear.value),forcing:line('freshwater_sv'),threshold:line('threshold_sv')}
})
// Equilibrium diagram: stable/unstable/reversed states against the freshwater forcing, with the trajectory.
const equilibrium=computed(()=>{
 const b=polarBlock.value;if(!b)return null
 const d=b.equilibrium_diagram,s=b.scenarios.find(x=>x.name===polarScenario.value)
 const pts=[...d.start.stable_strong,...d.start.unstable,...d.start.reversed,...d.warmest.stable_strong,...d.warmest.reversed,...s.trajectory]
 const fmax=Math.max(...pts.map(p=>p[0]))*1.05,qlo=Math.min(...pts.map(p=>p[1]))-1,qhi=Math.max(...pts.map(p=>p[1]))+1
 const X=f=>f/fmax*100,Y=q=>46-(q-qlo)/((qhi-qlo)||1)*42
 const line=list=>list.map(([f,q])=>`${X(f).toFixed(2)},${Y(q).toFixed(2)}`).join(' ')
 const k=Math.min(s.trajectory.length-1,Math.floor(data.value.polar.index*s.trajectory.length/b.output_years.length))
 return {fmax,qlo,qhi,zero:Y(0),start:{strong:line(d.start.stable_strong),unstable:line(d.start.unstable),reversed:line(d.start.reversed),threshold:X(d.start.threshold_sv)},
  warmest:{strong:line(d.warmest.stable_strong),unstable:line(d.warmest.unstable),reversed:line(d.warmest.reversed),threshold:X(d.warmest.threshold_sv),warming:d.warmest_warming_k},
  trajectory:line(s.trajectory),now:{x:X(s.trajectory[k][0]),y:Y(s.trajectory[k][1])},present:X(d.present_freshwater_sv)}
})
const eventCell=e=>e?`${e.year} (+${e.global_warming_k.toFixed(1)} K)`:'—'
async function loadPolarExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}polar/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load polar melt example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · polar melt & overturning';view.value='polar';mapLayers.value.relief=true;layer.value='icethickness';selected.value=null;polarLayers.value.scenario=''
 }catch(err){error.value=err.message}finally{loading.value=false}
}
function polarCellColor(d,i){
 const P=d.polar,L=polarLayers.value,color=new THREE.Color('#0b2f52')
 if(!P.land[i]){return color}
 const shade=terrainShade.value?terrainShade.value[i]:.5
 color.setHSL(.25,.18,.22+.2*shade)
 if(L.base==='thickness'){const c=iceThicknessColor(P.thickness[i]);if(c)color.set(c).multiplyScalar(.75+.5*shade)}
 else if(L.base==='smb'){if(P.thickness[i]>1||Math.abs(P.smb[i])>.01)color.set(smbColor(P.smb[i])).multiplyScalar(.75+.5*shade)}
 else if(L.base==='melt'){const m=P.melt[i];if(m>0.01)color.setHSL(.02,.8,Math.min(.6,.2+m/6));else if(P.thickness[i]>1)color.set('#dfe9f2').multiplyScalar(.75+.5*shade)}
 else if(L.base==='temperature'){const t=d.forecast.fields.air_temperature.values[i];color.setHSL((230-Math.max(0,Math.min(1,(t-243)/40))*230)/360,.6,.45)}
 return color
}
// Coast view: sea by depth (or today's wind setup), land by elevation / year lost / flood frequency / people /
// subsidence, then the design-storm flood, the land behind defences, the land lost, defences and rivers.
function coastCellColor(d,i){
 const c=d.coast,b=c.block,L=coastLayers.value,z=d.terrain.values[i],shade=terrainShade.value?terrainShade.value[i]:.5,color=new THREE.Color()
 if(c.sea[i]){color.set(coastSeaColor(z));if(L.base==='setup')color.lerp(new THREE.Color('#ffd166'),Math.min(.9,b.today.setup_m[i]/2.5));return color}
 if(L.base==='population'){const p=b.population[i]/c.km2;color.setHSL(.08,.55,Math.min(.78,.1+Math.log10(1+p)/5))}
 else if(L.base==='subsidence'){color.setHSL(.78,.5,Math.min(.7,.2+b.subsidence_mm_yr[i]/12))}
 else if(L.base==='year'){const css=yearLostColor(coastYears.value.lost[i],b.reference_year,b.output_years.at(-1));if(css)color.set(css);else color.set(coastLandColor(z)).multiplyScalar(.45+.5*shade)}
 else if(L.base==='frequency'&&coastCoastal.value[i])color.set(frequencyColor(cellFrequency(c,coastScenario.value,i).return_years))
 else color.set(coastLandColor(z)).multiplyScalar(.6+.8*shade)
 if(L.base!=='year'){
  const storm=coastStormAt(i),lost=coastLostAt(i)
  if(L.storm&&storm>0)color.lerp(new THREE.Color(floodDepthColor(coastIsMap.value?storm:1.5)),.8)
  if(L.protected&&coastIsMap.value&&c.protectedLow.has(i))color.lerp(new THREE.Color('#ff9f43'),.6)
  if(L.lost&&lost>0)color.lerp(new THREE.Color('#d7263d'),Math.min(1,.3+.7*lost))
 }
 const st=coastStatic.value
 if(L.defences&&st.defence.has(i))color.set('#1e1e1e')
 if(L.rivers&&st.river.has(i))color.set(coastIsMap.value&&c.salt.has(i)?'#f1faee':'#3fa9f5')
 return color
}
async function loadOceanHeatExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}ocean-heat/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load ocean heat and rain example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · ocean heat & rain';view.value='ocean';oceanLayers.value.base='sst';mapLayers.value.relief=false;layer.value='oceansst';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
async function loadPolarOceanExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}polar-ocean/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load polar ocean example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · polar ocean';view.value='ocean';oceanLayers.value.base='sst';oceanLayers.value.seaIce=true;oceanLayers.value.iceSheet=true;mapLayers.value.relief=false;layer.value='seaice';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
async function loadCoastExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}coast/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load the sea-level rise example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · sea level & coasts';view.value='coast';mapLayers.value.relief=true;layer.value='coastlost';selected.value=null;coastLayers.value.scenario='';seek(snapshots.value.findIndex(f=>f.coast?.year===2100))
 }catch(err){error.value=err.message}finally{loading.value=false}
}
async function loadOceanExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}ocean/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load Atlantic streams example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · Atlantic streams';view.value='ocean';mapLayers.value.relief=false;layer.value='current';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
// Ocean view: salinity / speed / temperature / thickness base on the water, land from the terrain, with plumes, fronts, formation sites and sponge overlays.
function oceanCellColor(d,i){
 const L=oceanLayers.value,o=d.ocean,color=new THREE.Color('#16242d')
 if(!o.mask[i]){const [lo,hi]=terrainRange.value,norm=(d.terrain.values[i]-lo)/((hi-lo)||1),shade=terrainShade.value?terrainShade.value[i]:.5;color.setHSL((110-norm*80)/360,.3,.18+norm*.3).multiplyScalar(.55+.9*shade)
  // Land: the coastal band that drains straight to the sea, its stored runoff and the rain falling on it.
  if(o.rain&&L.coast&&o.rain.receiver[i]>=0)color.lerp(new THREE.Color('#c9a86a'),.28)
  if(o.rain&&L.runoff&&o.rain.store[i]>1)color.lerp(new THREE.Color('#4fc3f7'),Math.min(.7,.2+o.rain.store[i]/80))
  if(o.rain&&L.rain&&o.rain.rate[i]>.2)color.lerp(new THREE.Color(rateColor(o.rain.rate[i])??'#7fb3ff'),.55)
  // The coupled ice sheet: white by thickness, melting margins (negative balance) reddish.
  if(o.iceSheet&&L.iceSheet&&o.iceSheet.thickness[i]>1){const c=iceThicknessColor(o.iceSheet.thickness[i]);color.set(c).multiplyScalar(.8+.4*shade);if(o.iceSheet.smb[i]<-.2)color.lerp(new THREE.Color('#ff8a65'),Math.min(.6,-o.iceSheet.smb[i]/4))}
  return color}
 const reference=o.config.physics.reference_salinity_psu
 if(L.base==='surface')color.set(salinityColor(o.surfaceSalinity[i],reference))
 else if(L.base==='salinity')color.set(salinityColor(o.salinity[i],reference))
 else if(L.base==='speed')color.set(speedColor(o.speed[i]))
 else if(L.base==='temperature')color.set(temperatureColor(o.temperature[i]))
 else if(L.base==='thickness'){const h=o.thickness[i],h0=o.config.ocean.initial_thickness_m,n=Math.max(0,Math.min(1,.5+(h-h0)/(h0||1)));color.setHSL(.6,.6,.15+.5*n)}
 else if(L.base==='freshwater'){color.setHSL(.55,.7,.12+Math.min(.7,o.freshwater[i]*6))}
 else if(L.base==='sst'){const [lo,hi]=oceanSstRange.value;color.set(rampColor(o.thermal?o.thermal.sst[i]:o.temperature[i],lo,hi))}
 else if(L.base==='mld'&&o.thermal)color.set(depthColor(o.thermal.mld[i]))
 else if(L.base==='netheat'&&o.airSea)color.set(heatFluxColor(o.airSea.net[i]))
 else if(L.base==='evaporation'&&o.airSea)color.set(evaporationColor(o.airSea.evaporation[i]))
 else if(L.base==='oxygen'&&o.oxygen)color.set(oxygenColor(o.oxygen[i]))
 else color.set('#0b2f52')
 if(L.plumes&&o.freshwater[i]>0.01&&L.base!=='freshwater')color.lerp(new THREE.Color('#dffcf0'),Math.min(.85,o.freshwater[i]*4))
 if(L.formation&&o.sinking[i]>0)color.lerp(new THREE.Color('#b388ff'),.6)
 if(L.fronts&&o.front[i])color.lerp(new THREE.Color('#ffffff'),.35)
 if(L.sponge&&o.spongeStrength[i]>0)color.lerp(new THREE.Color('#111111'),.6*o.spongeStrength[i])
 if(o.rain&&L.rain&&o.rain.rate[i]>.2)color.lerp(new THREE.Color(rateColor(o.rain.rate[i])??'#7fb3ff'),.6)
 // Sea ice on top: white by thickness, opacity by concentration.
 if(o.seaIce&&L.seaIce){const c=seaIceColor(o.seaIce.concentration[i],o.seaIce.thickness[i]);if(c)color.lerp(new THREE.Color(c.color),.25+.7*c.weight)}
 return color
}
// Catchment hydrology: where the rain goes (rivers, lakes, groundwater, ocean) and what massive rain does (floods, erosion, landslides).
const hydrologyMetrics=computed(()=>data.value.hydrology?summarizeHydrology(data.value):null)
const waterLayers=ref({base:'terrain',rivers:true,lakes:true,surface:true,snow:true,rain:true,arrows:false,labels:true})
const hazardLayers=ref({flood:true,landslides:true,runout:true,sediment:true,threshold:true,unstable:true,stability:false})
const terrainRange=computed(()=>{const v=data.value.terrain?.values;return v?[Math.min(...v),Math.max(...v)]:[0,1]})
const riverLines=computed(()=>data.value.hydrology&&(view.value==='water'||view.value==='hazards')?riverSegments(data.value.hydrology.network,grid.value):[])
const selectedPath=computed(()=>{try{return chosen.value&&data.value.hydrology?waterPath(data.value.hydrology.network,chosen.value.i):null}catch{return null}})
const peakCellOverall=computed(()=>{if(!data.value.hydrology)return null;let best=-1,cell=0;for(const f of snapshots.value){if(f.hydrology&&f.hydrology.maxDischarge>best){best=f.hydrology.maxDischarge;cell=f.hydrology.peakCell}}return cell})
const hydrographCell=computed(()=>data.value.hydrology?(chosen.value&&data.value.hydrology.network.channel[chosen.value.i]?chosen.value.i:peakCellOverall.value):null)
const hydrograph=computed(()=>hydrographCell.value===null?[]:hydrographSeries(visibleFrames.value,hydrographCell.value))
const hydrographMax=computed(()=>Math.max(1,...hydrograph.value.map(p=>p.discharge_m3_s)))
const hydrographRainMax=computed(()=>Math.max(1,...hydrograph.value.map(p=>p.rain_mm_h)))
function hydrographPath(key,max){const s=hydrograph.value,start=s[0]?.elapsed_seconds??0,duration=(s.at(-1)?.elapsed_seconds??0)-start||1;return s.map(p=>`${(p.elapsed_seconds-start)/duration*100},${45-(p[key]??0)/max*40}`).join(' ')}
const hydrographTimePosition=computed(()=>{const s=hydrograph.value;if(!s.length)return 0;return (s[Math.min(frameIndex.value,s.length-1)].elapsed_seconds-s[0].elapsed_seconds)/(s.at(-1).elapsed_seconds-s[0].elapsed_seconds||1)*100})
const visibleLandslides=computed(()=>data.value.hydrology?data.value.hydrology.events.filter(e=>e.elapsed_seconds<=forecast.value.elapsed_seconds):[])
const pathSummary=computed(()=>{const p=selectedPath.value;if(!p)return '';const n=data.value.hydrology.network;return `${p.cells.length-1} cells · ${p.distance_km.toFixed(1)} km to the ${p.ends}${p.lakes.length?` via lake${p.lakes.length>1?'s':''} ${p.lakes.map(l=>l+1).join(', ')}`:''}${p.basin!==null&&p.basin>=0?` · basin ${p.basin+1} (${n.basins[p.basin].area_km2.toFixed(0)} km², ${n.basins[p.basin].terminal})`:''}`})
async function loadFloodExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}flood/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load alpine flood example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · alpine flood';view.value='water';mapLayers.value.relief=true;layer.value='discharge';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
// Catchment view: terrain (or basins / water table) base with ocean, lakes, snow, overland water and rain.
function waterCellColor(d,i){
 const L=waterLayers.value,h=d.hydrology,n=h.network,color=new THREE.Color('#16242d')
 const [lo,hi]=terrainRange.value,norm=(d.terrain.values[i]-lo)/((hi-lo)||1),shade=terrainShade.value?terrainShade.value[i]:.5
 if(L.base==='basins'&&n.basin_id[i]>=0)color.set(basinColor(n.basin_id[i])).multiplyScalar(.45+.7*shade)
 else if(L.base==='groundwater'&&!n.ocean[i])color.setHSL(.58,.55,Math.max(.12,.58-h.waterTable[i]/30)).multiplyScalar(.6+.8*shade)
 else if(L.base==='saturation'&&!n.ocean[i])color.setHSL(.58,.6,.12+h.saturation[i]*.45).multiplyScalar(.6+.8*shade)
 else color.setHSL((110-norm*80)/360,.35,.18+norm*.42).multiplyScalar(.55+.9*shade)
 if(n.ocean[i])color.set('#0b2f52')
 if(L.snow&&h.snow[i]>1)color.lerp(new THREE.Color('#f4f9ff'),Math.min(.9,.3+h.snow[i]/100))
 if(L.lakes&&n.lake_id[i]>=0&&h.lakeDepth[i]>0.05)color.lerp(new THREE.Color('#1f6fd0'),Math.min(.95,.5+h.lakeDepth[i]/20))
 if(L.surface){const c=waterColor(h.surface[i]);if(c)color.lerp(new THREE.Color(c),Math.min(.85,.3+Math.log10(h.surface[i])/4))}
 if(L.rain&&h.rain[i]>0.2){const c=rateColor(h.rain[i]);if(c)color.lerp(new THREE.Color(c),.25)}
 return color
}
// Hazard view: grey relief with flood classes, erosion (red) / deposition (tan), unstable slopes and optional factor-of-safety shading.
function hazardCellColor(d,i){
 const L=hazardLayers.value,h=d.hydrology,n=h.network,color=new THREE.Color('#16242d')
 const [lo,hi]=terrainRange.value,norm=(d.terrain.values[i]-lo)/((hi-lo)||1),shade=terrainShade.value?terrainShade.value[i]:.5
 color.setHSL(0,0,.16+norm*.34).multiplyScalar(.55+.9*shade)
 if(n.ocean[i])color.set('#0b2f52')
 if(L.stability&&h.fs[i]!==null){const fs=h.fs[i];color.lerp(new THREE.Color(fs<1?'#d7263d':fs<1.3?'#ff9f43':fs<2?'#ffe066':'#2ecc71'),.55)}
 if(L.sediment){const s=sedimentColor(h.erosion[i],h.deposition[i]);if(s)color.lerp(new THREE.Color(s.color),s.weight)}
 if(L.flood&&h.floodClass[i]>0)color.lerp(new THREE.Color(floodClassInfo(h.floodClass[i]).color),.85)
 if(L.unstable&&h.unstable[i])color.lerp(new THREE.Color('#ff3b3b'),.5)
 return color
}
import { OROGRAPHY_KEYS } from './coupledWind.js'
const orographyMetrics=computed(()=>data.value.orography?summarizeOrography(data.value):null)
const terrainShade=computed(()=>data.value.terrain?hillshade(view.value==='polar'&&data.value.polar?data.value.polar.surface.map((v,i)=>data.value.polar.land[i]?v:data.value.terrain.values[i]):data.value.terrain.values,grid.value):null)
function addRidge(){coupledForm.value.ridges.push({orientation_deg:90,center_east_m:0,center_north_m:0,height_m:2000,half_width_m:70000,length_m:600000})}
function removeRidge(i){coupledForm.value.ridges.splice(i,1)}
function addBasin(){coupledForm.value.basins.push({center_east_m:0,center_north_m:150000,depth_m:250,radius_m:50000})}
function removeBasin(i){coupledForm.value.basins.splice(i,1)}
async function loadAlpsExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}alps/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load Alps foehn example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · Alps foehn';view.value='map';mapLayers.value.base='terrain';mapLayers.value.relief=true;layer.value='temperature';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
const stormMetrics=computed(()=>data.value.storms?summarizeStorms(data.value):null)
const severeMetrics=computed(()=>data.value.severe?summarizeSevere(data.value):null)
const visibleEvents=computed(()=>data.value.storms?data.value.storms.events.filter(e=>e.elapsed_seconds<=forecast.value.elapsed_seconds):[])
function addSeed(){coupledForm.value.storm_seeds.push({east_m:0,north_m:0,max_wind_m_s:18,radius_max_wind_m:40000})}
function removeSeed(i){coupledForm.value.storm_seeds.splice(i,1)}
async function loadHurricaneExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}hurricane/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load hurricane example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · hurricane landfall';view.value='map';layer.value='stormwind';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
// Weather map view: a flat composition of switchable layers over the loaded frames.
const view=ref('field')
// The folding layer list: groups open on demand (the active layer's group always), layers without data hidden.
const onlyAvailable=ref(true), openGroups=ref(['air']), openExamples=ref([]), requestOpen=ref(false)
const layerGroups=computed(()=>LAYER_GROUPS.map(g=>{const layers=g.layers.map(l=>({...l,available:!!l.needs(data.value),subtitle:layerSubtitle(l,data.value)}));return {...g,layers,shown:onlyAvailable.value?layers.filter(l=>l.available||l.key===layer.value):layers,available:layers.filter(l=>l.available).length,active:layers.some(l=>l.key===layer.value)}}).filter(g=>!onlyAvailable.value||g.available>0||g.active))
const availableLayerCount=computed(()=>LAYERS.filter(l=>l.needs(data.value)).length)
function onGroupToggle(id,event){const open=event.target.open,has=openGroups.value.includes(id);if(open&&!has)openGroups.value=[...openGroups.value,id];else if(!open&&has)openGroups.value=openGroups.value.filter(g=>g!==id)}
function onExampleToggle(id,event){const open=event.target.open,has=openExamples.value.includes(id);if(open&&!has)openExamples.value=[...openExamples.value,id];else if(!open&&has)openExamples.value=openExamples.value.filter(g=>g!==id)}
const mapLayers=ref({base:'temperature',isobars:true,isobarIntervalHpa:2,fronts:true,clouds:true,volumes:true,precipitation:true,symbols:true,hazards:true,wind:false,landSea:true,storms:true,stormWind:true,surge:true,tornado:true,relief:false,lifting:true,foehn:true,coldPools:true,oroPrecip:true,blocking:false})
const precipitationMetrics=computed(()=>data.value.precipitation?summarizePrecipitation(data.value):null)
const isobarLines=computed(()=>{try{return data.value.coupled&&view.value==='map'&&mapLayers.value.isobars?isobars(data.value.coupled.pressure,grid.value,mapLayers.value.isobarIntervalHpa*100):[]}catch{return []}})
const temperatureRange=computed(()=>{let lo=Infinity,hi=-Infinity;for(const d of snapshots.value)for(const v of d.forecast.fields.air_temperature.values){if(v<lo)lo=v;if(v>hi)hi=v}return [lo,hi]})   // a loop: large timelines exceed the argument limit of Math.min(...)
const pressureRange=computed(()=>data.value.coupled?[Math.min(...data.value.coupled.pressure),Math.max(...data.value.coupled.pressure)]:[0,1])
// Composite map colour of one cell as a THREE.Color.
function mapCellColor(d,i){
 if(view.value==='solar'&&d.solar)return solarCellColor(d,i)
 if(view.value==='dust'&&d.dust)return dustCellColor(d,i)
 if(view.value==='solarwind'&&d.solarWind)return windCellColor(d,i)
 if(view.value==='ocean'&&d.ocean)return oceanCellColor(d,i)
 if(view.value==='polar'&&d.polar)return polarCellColor(d,i)
 if(view.value==='coast'&&d.coast)return coastCellColor(d,i)
 if(view.value==='carbon'&&d.carbon)return carbonCellColor(d,i)
 if(view.value==='water'&&d.hydrology)return waterCellColor(d,i)
 if(view.value==='hazards'&&d.hydrology)return hazardCellColor(d,i)
 const L=mapLayers.value,color=new THREE.Color('#16242d')
 if(L.base==='terrain'&&d.terrain){const values=d.terrain.values,lo=Math.min(...values),hi=Math.max(...values),n=(values[i]-lo)/((hi-lo)||1),shade=terrainShade.value?terrainShade.value[i]:.5;color.setHSL((110-n*80)/360,.35,.18+n*.42).multiplyScalar(.55+.9*shade)}
 else if(L.base==='temperature'){const [lo,hi]=temperatureRange.value,n=(d.forecast.fields.air_temperature.values[i]-lo)/((hi-lo)||1);color.setHSL((223-n*212)/360,.55,.30+n*.12);if(terrainShade.value&&d.orography)color.multiplyScalar(.7+.6*terrainShade.value[i])}
 else if(L.base==='pressure'&&d.coupled){const [lo,hi]=pressureRange.value,n=(d.coupled.pressure[i]-lo)/((hi-lo)||1);color.setHSL((260-n*120)/360,.35,.26+n*.18)}
 if(L.landSea&&d.surface)color.lerp(new THREE.Color(d.surface.land[i]?'#4a5a3a':'#10324a'),L.base==='none'?1:.35)
 if(L.clouds&&d.clouds&&d.clouds.type[i]!=='none')color.lerp(new THREE.Color(cloudTypeInfo(d.clouds.type[i]).color),.35+.5*d.clouds.fraction[i])
 if(L.stormWind&&d.storms&&d.storms.env.storm_wind_speed_m_s[i]>8){const w=d.storms.env.storm_wind_speed_m_s[i];color.lerp(new THREE.Color(categoryInfo(categoryForWind(w)).color),Math.min(.9,.25+w/80))}
 if(L.surge&&d.storms&&d.storms.env.storm_surge_m[i]>0)color.lerp(new THREE.Color('#00e5ff'),Math.min(.9,.3+d.storms.env.storm_surge_m[i]/6))
 if(L.tornado&&d.severe&&RISK_COLORS[d.severe.risk[i]])color.lerp(new THREE.Color(RISK_COLORS[d.severe.risk[i]]),.6)
 if(L.lifting&&d.orography){const w=d.orography.lifting[i];if(w>0.03)color.lerp(new THREE.Color('#3da5ff'),Math.min(.7,w*2));else if(w<-0.03)color.lerp(new THREE.Color('#ff8c42'),Math.min(.7,-w*2))}
 if(L.blocking&&d.orography&&d.orography.blocked[i]>0.05)color.lerp(new THREE.Color('#f0e68c'),Math.min(.7,d.orography.blocked[i]))
 if(L.coldPools&&d.orography&&d.orography.coldPoolFlag[i])color.lerp(new THREE.Color('#9ad7ff'),.8)
 if(L.precipitation&&d.precipitation&&d.precipitation.rate[i]>0)color.lerp(new THREE.Color(rateColor(d.precipitation.rate[i])),.85)
 return color
}
const categoryForWind=w=>w<17?'depression':w<33?'tropical_storm':w<43?'category_1':w<50?'category_2':w<58?'category_3':w<70?'category_4':'category_5'
const mapCellStyles=computed(()=>view.value!=='field'?Array.from({length:grid.value.nx*grid.value.ny},(_,i)=>mapCellColor(data.value,i).getStyle()):[])
function glyphSprite(text,color,size=1.1){const canvas=document.createElement('canvas');canvas.width=96;canvas.height=96;const ctx=canvas.getContext('2d');ctx.font='bold 64px sans-serif';ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillStyle=color;ctx.fillText(text,48,52);const sprite=new THREE.Sprite(new THREE.SpriteMaterial({map:new THREE.CanvasTexture(canvas),depthTest:false,transparent:true}));sprite.scale.set(size,size,1);return sprite}
// A text label that grows with its text (no clipping), with a dark halo for contrast over any colour.
function textSprite(text,color,size=1){const font='bold 40px sans-serif',probe=document.createElement('canvas').getContext('2d');probe.font=font;const w=Math.max(256,Math.ceil(probe.measureText(text).width)+28);const canvas=document.createElement('canvas');canvas.width=w;canvas.height=64;const ctx=canvas.getContext('2d');ctx.font=font;ctx.textAlign='center';ctx.textBaseline='middle';ctx.shadowColor='rgba(4,12,18,.9)';ctx.shadowBlur=8;ctx.fillStyle=color;ctx.fillText(text,w/2,34);const sprite=new THREE.Sprite(new THREE.SpriteMaterial({map:new THREE.CanvasTexture(canvas),depthTest:false,transparent:true}));sprite.scale.set(size*4*w/256,size,1);return sprite}
const symbolStride=computed(()=>Math.max(1,Math.ceil(Math.sqrt(grid.value.nx*grid.value.ny/160))))
const cloudVolumes=ref(true), frontOverlay=ref(true)
const cloudMetrics=computed(()=>data.value.clouds?summarizeClouds(data.value):null)
function addAirmass(){coupledForm.value.airmasses.push({east_m:0,north_m:0,radius_m:300000,temperature_anomaly_k:5,humidity_anomaly_kg_kg:0.004})}
function removeAirmass(i){coupledForm.value.airmasses.splice(i,1)}
function useCloudMock(){loadFrames(cloudFormationDemo());source.value='Cloud formation · synthetic mock';layer.value='clouds';error.value=''}
async function loadCloudExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}clouds/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load cloud formation example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · cloud formation';layer.value='clouds';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
const cloudsEnabled=ref(true), systemsEnabled=ref(true)
// Coupled wind request editor: mirrors the C++ bounds; the request is downloaded and run with terraflux-run-coupled-wind.
const coupledForm=ref(defaultCoupledForm()), coupledNote=ref('')
const coupledReport=computed(()=>{try{return checkCoupledRequest(formToRequest(coupledForm.value))}catch(err){return {errors:[err.message],warnings:[],estimates:{}}}})
function downloadCoupledRequest(){
 if(coupledReport.value.errors.length)return
 const text=JSON.stringify(formToRequest(coupledForm.value),null,2)+'\n'
 const url=URL.createObjectURL(new Blob([text],{type:'application/json'}))
 const link=document.createElement('a');link.href=url;link.download='coupled-wind-request.json';link.click()
 setTimeout(()=>URL.revokeObjectURL(url),1000);coupledNote.value='Request downloaded. Run: terraflux-run-coupled-wind coupled-wind-request.json output.json, then Open JSON.'
}
function useLoadedCoupledSettings(){
 try{coupledForm.value=formFromRequest(requestFromCoupledAtmosphere(data.value.coupled.raw,snapshots.value));coupledNote.value='Editor filled from the loaded run.'}catch(err){coupledNote.value=err.message}
}
function resetCoupledForm(){coupledForm.value=defaultCoupledForm();coupledNote.value='Editor reset to the committed example.'}
function addCenter(){coupledForm.value.centers.push({kind:'low',east_m:0,north_m:0,radius_m:250000,pressure_anomaly_pa:-500,temperature_anomaly_k:0,humidity_anomaly_kg_kg:0})}
function removeCenter(i){coupledForm.value.centers.splice(i,1)}
const fmt=(v,digits=2)=>v===null||v===undefined?'n/a':Number(v).toFixed(digits)
const adjustedTemperature=ref(false)
const temperatureValues=d=>adjustedTemperature.value&&d.terrainTemperature?d.terrainTemperature.values:d.forecast.fields.air_temperature.values
const loading=ref(false), terrainEnabled=ref(true), exaggeration=ref(2)
const snapshots=ref(validateSequence(demo())), frameIndex=ref(0), playing=ref(false)
const comparisonFrames=ref(null),comparisonName=ref('Variant'),comparisonView=ref('baseline')
const data=computed(()=>comparisonView.value==='variant'&&comparisonFrames.value?comparisonFrames.value[frameIndex.value]:snapshots.value[frameIndex.value])
const source=ref('Synthetic demonstration'), layer=ref('temperature'), vectors=ref(true), selected=ref(null), error=ref(''), fallback=ref(false), host=ref(null)
let renderer, scene, camera, controls, observer, frame, group, cells=[], cellHeights=[], playback
function stop(){playing.value=false;clearInterval(playback)}
function seek(index){stop();frameIndex.value=Number(index)}
function togglePlayback(){
 if(playing.value){stop();return}
 if(frameIndex.value===snapshots.value.length-1)frameIndex.value=0
 playing.value=true;playback=setInterval(()=>{
  if(frameIndex.value<snapshots.value.length-1)frameIndex.value++
  if(frameIndex.value===snapshots.value.length-1)stop()
 },700)
}
function loadFrames(input){const parsed=validateSequence(input);stop();comparisonFrames.value=null;comparisonView.value='baseline';snapshots.value=parsed;frameIndex.value=0;selected.value=null}
watch(data,()=>{if(!data.value.terrainTemperature)adjustedTemperature.value=false;if(!LAYERS.find(l=>l.key===layer.value)?.needs(data.value))layer.value=data.value.solar?'sunbrightness':data.value.solarWind?'windaurora':'temperature';if(view.value==='polar'&&!data.value.polar)view.value='field';if((view.value==='water'||view.value==='hazards')&&!data.value.hydrology)view.value='field';if(view.value==='ocean'&&!data.value.ocean)view.value='field';if(view.value==='coast'&&!data.value.coast)view.value='field';if(view.value==='carbon'&&!data.value.carbon)view.value='field';if(view.value==='solar'&&!data.value.solar)view.value='field';if(view.value==='dust'&&!data.value.dust)view.value='field';if(view.value==='solarwind'&&!data.value.solarWind)view.value='field'})
watch(layer,k=>{const g=groupOfLayer(k);if(g&&!openGroups.value.includes(g))openGroups.value=[...openGroups.value,g]},{immediate:true})
function runExample(action){const actions={openEarthForecast,openEarthClimate,loadEngineExample,loadOutletExample,loadComparisonDemo,loadCoupledExample,loadCloudExample,loadHurricaneExample,loadAlpsExample,loadFloodExample,loadOceanExample,loadOceanHeatExample,loadPolarExample,loadPolarOceanExample,loadCoastExample,loadCarbonExample:()=>loadCarbonExample('today'),loadCarbon2100Example,loadSolarExample,loadDustExample,loadWindExample,useCloudMock,useRegionalDemo};actions[action]?.()}
const forecast=computed(()=>data.value.forecast), grid=computed(()=>forecast.value.grid)
// The values of any layer in a frame (the inspector also reads the other layers at a point).
function layerValuesFor(d,key){
 if(key==='cloud')return d.atmosphere?.cloud.map(v=>v*100)??[]
 if(key==='pressure')return d.coupled?d.coupled.pressure.map(v=>v/100):d.atmosphere?.pressure??[]
 if(key==='humidity')return d.coupled?.humidity??d.atmosphere?.humidity??[]
 if(key==='density')return d.coupled?.density??[]
 if(key==='moisture')return d.coupled?.specificHumidity.map(v=>v*1000)??[]
 if(key==='clouds')return d.clouds?.type.map(cloudTypeIndex)??[]
 if(key==='lifting')return d.orography?.lifting??[]
 if(key==='froude')return d.orography?.froude??[]
 if(key==='blocked')return d.orography?.blocked??[]
 if(key==='coldpool')return d.orography?.coldPool??[]
 if(key==='foehn')return d.orography?.foehnWarming??[]
 if(key==='oroprecip')return d.orography?.precipRate??[]
 if(key==='valley')return d.orography?.valley??[]
 if(key==='discharge')return d.hydrology?.discharge??[]
 if(key==='surfacewater')return d.hydrology?.surface??[]
 if(key==='saturation')return d.hydrology?.saturation??[]
 if(key==='watertable')return d.hydrology?.waterTable??[]
 if(key==='snowpack')return d.hydrology?.snow??[]
 if(key==='erosion')return d.hydrology?.erosion??[]
 if(key==='deposition')return d.hydrology?.deposition??[]
 if(key==='stability')return d.hydrology?.fs??[]
 if(key==='floodclass')return d.hydrology?.floodClass??[]
 if(key==='current')return d.ocean?.speed.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='salinity')return d.ocean?.salinity.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='surfacesalinity')return d.ocean?.surfaceSalinity.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='sst')return d.ocean?.temperature.map((v,i)=>d.ocean.mask[i]?v-273.15:null)??[]
 if(key==='thermocline')return d.ocean?.thickness.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='freshwater')return d.ocean?.freshwater.map((v,i)=>d.ocean.mask[i]?v*100:null)??[]
 if(key==='sinking')return d.ocean?.sinking.map((v,i)=>d.ocean.mask[i]?v*86400:null)??[]
 if(key==='icethickness')return d.polar?.thickness.map((v,i)=>d.polar.land[i]?v:null)??d.ocean?.iceSheet?.thickness.map((v,i)=>d.ocean.iceSheet.domain[i]?v:null)??[]
 if(key==='icesmb')return d.polar?.smb.map((v,i)=>d.polar.land[i]?v:null)??d.ocean?.iceSheet?.smb.map((v,i)=>d.ocean.iceSheet.domain[i]?v:null)??[]
 if(key==='icemelt')return d.polar?.melt.map((v,i)=>d.polar.land[i]?v:null)??d.ocean?.iceSheet?.melt.map((v,i)=>d.ocean.iceSheet.domain[i]?v:null)??[]
 if(key.startsWith('coast')&&d.coast){
  const c=d.coast,b=c.block
  if(key==='coastlost')return c.lost.map((v,i)=>c.sea[i]?null:v*100)
  if(key==='coastyear')return coastYears.value.lost.map((v,i)=>c.sea[i]?null:v)
  if(key==='coaststorm')return c.storm.map((v,i)=>c.sea[i]?null:v)
  if(key==='coastinundation')return c.inundation.map((v,i)=>c.sea[i]?null:v)
  if(key==='coastfrequency')return coastCoastal.value.map((v,i)=>v?Math.min(100,cellFrequency(c,coastScenario.value,i).return_years):null)
  if(key==='coastsetup')return b.today.setup_m.map((v,i)=>c.sea[i]?v:null)
  if(key==='coastsubsidence')return b.subsidence_mm_yr.map((v,i)=>c.sea[i]?null:v)
  if(key==='coastpopulation')return b.population.map((v,i)=>c.sea[i]?null:v/c.km2)
 }
 if(d.carbon&&['co2air','co2flux','ph','omega','omegabottom','bottomo2','habitat','habitable','production','nitrate'].includes(key)){
  const c=d.carbon,sea=v=>v.map((x,i)=>c.mask[i]?x:null)
  if(key==='co2air')return c.ppm
  if(key==='co2flux')return sea(c.flux)
  if(key==='ph')return sea(c.ph)
  if(key==='omega')return sea(c.omega)
  if(key==='omegabottom')return sea(c.omegaBottom)
  if(key==='bottomo2')return sea(c.o2Bottom)
  if(key==='habitat')return sea(c.columnClass)
  if(key==='habitable')return sea(c.habitable.map(v=>v*100))
  if(key==='production')return sea(c.production)
  if(key==='nitrate')return sea(c.nitrate)
 }
 if(key==='seaice')return d.ocean?.seaIce?.concentration.map((v,i)=>d.ocean.mask[i]?v*100:null)??[]
 if(key==='seaicethick')return d.ocean?.seaIce?.thickness.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='oxygen')return d.ocean?.oxygen?.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='oceansst')return d.ocean?.thermal?.sst.map((v,i)=>d.ocean.mask[i]?v-273.15:null)??[]
 if(key==='mld')return d.ocean?.thermal?.mld.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='netheat')return d.ocean?.airSea?.net.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='latent')return d.ocean?.airSea?.latent.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='evaporation')return d.ocean?.airSea?.evaporation.map((v,i)=>d.ocean.mask[i]?v:null)??[]
 if(key==='searain')return d.ocean?.rain?.rate??[]
 if(key==='coastalstore')return d.ocean?.rain?.store.map((v,i)=>d.ocean.mask[i]||d.ocean.rain.receiver[i]<0?null:v)??[]
 if(key==='stormwind')return d.storms?.env.storm_wind_speed_m_s??[]
 if(key==='mpi')return d.storms?.env.potential_intensity_m_s??[]
 if(key==='gpi')return d.storms?.env.genesis_potential_index??[]
 if(key==='surge')return d.storms?.env.storm_surge_m??[]
 if(key==='stp')return d.severe?.stp??[]
 if(key==='helicity')return d.severe?.helicity??[]
 if(key==='precip')return d.precipitation?.rate??[]
 if(key==='accum')return d.precipitation?.accumulation??[]
 if(key==='cape')return d.coupled?.columns?.cape??[]
 if(key==='cloudbase')return d.coupled?.columns?.lcl??[]
 if(key==='cloudtop')return d.coupled?.columns?.el??[]
 if(key==='sunbrightness')return d.solar?.brightness??[]
 if(key==='dustinflux')return d.dust?.influx??[]
 if(WIND_LAYER_KEYS.includes(key))return d.solarWind?windLayerValues(d,key):[]
 if(key==='dustdaily')return d.dust?.daily??[]
 if(key==='suncontrast')return d.solar?d.solar.brightness.map((t,i)=>contrastPct(t,d.solar.quiet[i])):[]
 if(key==='temperature')return temperatureValues(d).map(v=>v-273.15)
 if(key==='wind')return d.forecast.fields.wind_east.values.map((u,i)=>Math.hypot(u,d.forecast.fields.wind_north.values[i]))
 if(key==='soil')return d.surfaceWater?.soil_values?.map(v=>v===null?null:v*1000)??[]
 if(key==='water')return d.surfaceWater?.values.map(v=>v===null?null:v*1000)??[]
 if(key==='terrain')return d.terrain?.values??[]
 if(key==='slope')return d.terrainDerivatives?.slope_deg??[]
 if(key==='aspect')return d.terrainDerivatives?.aspect_deg??[]
 return d.rain?.values??[]
}
function layerValues(d){return layerValuesFor(d,layer.value)}
const values=computed(()=>layerValues(data.value))
const range=computed(()=>{
 if(layer.value==='cloud'||(layer.value==='humidity'&&!data.value.coupled))return [0,100]
 if(layer.value==='aspect')return [0,360]
 if(layer.value==='clouds')return [0,CLOUD_TYPES.length-1]
 if(layer.value==='saturation')return [0,1]
 if(layer.value==='stability')return [0.5,3]
 if(layer.value==='floodclass')return [0,4]
 if(layer.value==='habitat')return [0,3]
 if(['salinity','surfacesalinity'].includes(layer.value))return [30,37]
 const a=[...snapshots.value,...(comparisonFrames.value??[])].flatMap(layerValues).filter(v=>v!==null)
 return a.length?a.reduce(([lo,hi],v)=>[Math.min(lo,v),Math.max(hi,v)],[Infinity,-Infinity]):[0,0]
})
function hue(value){
 if(layer.value==='aspect')return value
 const norm=(value-range.value[0])/(range.value[1]-range.value[0]||1)
 if(layer.value==='cloud')return 210
 if(layer.value==='stability')return Math.max(0,Math.min(120,norm*120))
 if(layer.value==='floodclass')return 60-norm*60
 if(layer.value==='habitat')return 170-norm*170
 if(['bottomo2','habitable','ph','omega','omegabottom','co2flux'].includes(layer.value))return norm*220
 if(['salinity','surfacesalinity'].includes(layer.value))return 160+norm*110
 return ['rainfall','water','soil','discharge','surfacewater','saturation','snowpack','watertable','current','freshwater','thermocline','sinking','mld','searain','coastalstore','seaice','seaicethick'].includes(layer.value)?212-norm*47:223-norm*212
}
function unitFor(key){return WIND_LAYER_KEYS.includes(key)?WIND_UNITS[key]:['dustinflux','dustdaily'].includes(key)?'µg/m²/day':key==='sunbrightness'?'K':key==='suncontrast'?'%':['cloud','humidity'].includes(key)?'%':key==='pressure'?'hPa':key==='density'?'kg/m³':key==='moisture'?'g/kg':key==='cape'?'J/kg':['cloudbase','cloudtop'].includes(key)?'m':key==='clouds'?'':key==='precip'?'mm/h':key==='accum'?'mm':['stormwind','mpi'].includes(key)?'m/s':key==='gpi'?'':key==='surge'?'m':key==='stp'?'':key==='helicity'?'m²/s²':key==='lifting'?'m/s':['froude','blocked'].includes(key)?'':['coldpool','foehn'].includes(key)?'K':key==='oroprecip'?'mm/h':key==='valley'?'m':key==='discharge'?'m³/s':['surfacewater','snowpack','erosion','deposition'].includes(key)?'mm':key==='saturation'?'':key==='watertable'?'m':key==='stability'?'FS':key==='floodclass'?'class':key==='current'?'m/s':['salinity','surfacesalinity'].includes(key)?'psu':key==='sst'?'°C':key==='thermocline'?'m':key==='freshwater'?'%':key==='sinking'?'m/day':key==='oceansst'?'°C':key==='mld'?'m':['netheat','latent'].includes(key)?'W/m²':key==='evaporation'?'mm/day':key==='searain'?'mm/h':key==='coastalstore'?'mm':['icethickness','seaicethick'].includes(key)?'m':key==='seaice'?'%':key==='oxygen'?'µmol/kg':key==='coastlost'?'%':key==='coastyear'?'':['coaststorm','coastinundation','coastsetup'].includes(key)?'m':key==='coastfrequency'?'years':key==='coastsubsidence'?'mm/yr':key==='coastpopulation'?'per km²':key==='co2air'?'ppm':key==='co2flux'?'mol/m²/yr':['ph','omega','omegabottom','habitat'].includes(key)?'':['bottomo2','nitrate'].includes(key)?'µmol/kg':key==='habitable'?'%':key==='production'?'g C/m²/day':['icesmb','icemelt'].includes(key)?'m ice/yr':key==='temperature'?'°C':key==='wind'?'m/s':key==='terrain'?'m':['slope','aspect'].includes(key)?'°':'mm'}
const unit=computed(()=>unitFor(layer.value))
const time=computed(()=>new Date(Date.parse(forecast.value.start_time_utc)+forecast.value.elapsed_seconds*1000).toISOString().replace('T',' ').replace('.000Z',' UTC'))
// Timeline label: years and warming for the polar scenarios, the model date for multi-year ocean runs, else minutes.
const playbackLabel=computed(()=>{
 const wm=windMetrics.value
 if(wm)return `${wm.time.replace('T',' ').replace(':00Z',' UTC')} · ${wm.phase} · solar wind ${wm.speed.toFixed(0)} km/s, Bz ${wm.bz.toFixed(0)} nT · Kp ${wm.kp.toFixed(1)}${wm.g?` (${G_SCALE[wm.g]})`:''} · Dst ${wm.dst.toFixed(0)} nT`
 const du=dustMetrics.value
 if(du)return `${data.value.dust.block.map.date_utc} ${String(Math.floor(du.hour)).padStart(2,'0')}:${String(Math.round((du.hour%1)*60)).padStart(2,'0')} UTC · ${du.scenario.influx_t_per_day} t/day · ${formatNumber(du.min_ug_m2_day,3)}–${formatNumber(du.max_ug_m2_day,3)} µg/m²/day (the morning side gets ${du.contrast.toFixed(1)}× more)`
 const so=solarMetrics.value
 if(so)return `${so.month} · cycle ${so.cycle}, ${so.trend} · sunspot number ${so.sunspot.toFixed(0)}${so.source==='forecast'?` (forecast, 90 %: ${so.sunspot_p5.toFixed(0)}–${so.sunspot_p95.toFixed(0)})`:data.value.solar.block.record_kind==='cycle_table'?' (synthetic record)':' (record)'} · ${data.value.solar.regions.length} spot groups`
 const cst=data.value.coast
 if(cst){const m=coastSummary.value;return `Year ${cst.year} · ${m.scenario} · sea +${m.regional_rise_m.toFixed(2)} m here (global +${m.global_rise_m.toFixed(2)} m) · ${m.lost_km2.toFixed(0)} km² lost`}
 const p=data.value.polar
 if(p){const m=polarMetrics.value;return `Year ${p.year} · ${p.block.map_scenario}${m?.global_warming_k!=null?` · +${m.global_warming_k.toFixed(2)} K global warming`:''}`}
 const seconds=forecast.value.elapsed_seconds
 const cb=carbonMetrics.value
 if(cb)return `${time.value.slice(0,10)} · CO₂ ${cb.background_ppm.toFixed(0)} ppm · sea pH ${cb.mean_ph.toFixed(2)} · dead zones ${fmtKm2(cb.dead_zone_km2)} km² (${cb.dead_zone_pct.toFixed(1)} % of the sea)`
 if(data.value.ocean&&seconds>=86400*30)return `${time.value.slice(0,10)} · year ${(seconds/31557600).toFixed(2)} · ${rainfallPhase.value}`
 return `+${seconds/60} min · ${rainfallPhase.value}`
})
const rainfallPhase=computed(()=>{
 if(frameIndex.value===0)return 'Initial state'
 const oceanRain=data.value.ocean?.rain
 const seaIce=data.value.ocean?.seaIce
 if(!oceanRain&&data.value.ocean)return seaIce?`Sea ice ${(seaIce.area/1e6).toFixed(2)} M km² · overturning ${data.value.ocean.overturning.toFixed(1)} Sv`:`Overturning ${data.value.ocean.overturning.toFixed(1)} Sv`
 if(oceanRain)return oceanRain.onSea+oceanRain.onCoast+oceanRain.inland>1?`Raining: ${fmtFlow(oceanRain.onSea)} on the sea · ${fmtFlow(oceanRain.onCoast+oceanRain.inland)} on land`:oceanRain.runoff>1?`Dry · the coast still drains ${fmtFlow(oceanRain.runoff)}`:'Dry'
 const current=data.value.rain?.values,previous=snapshots.value[frameIndex.value-1].rain?.values
 if(!current)return 'Rainfall unavailable'
 const increment=current.reduce((total,v,i)=>total+(v===null?0:v-(previous?.[i]??0)),0)
 return increment>1e-9?'Rain-fed interval':'Rain-free interval'
})
const visibleFrames=computed(()=>comparisonView.value==='variant'&&comparisonFrames.value?comparisonFrames.value:snapshots.value)
const outletSeries=computed(()=>data.value.surfaceWater?.boundary==='configured_outlets'?dischargeSeries(visibleFrames.value):[])
const outletVolumeMax=computed(()=>Math.max(1,...outletSeries.value.map(p=>p.cumulative_m3)))
const outletRateMax=computed(()=>Math.max(1,...outletSeries.value.map(p=>p.mean_rate_m3_s??0)))
const outletTimePosition=computed(()=>{
 const series=outletSeries.value;if(!series.length)return 0
 return (series[frameIndex.value].elapsed_seconds-series[0].elapsed_seconds)/(series.at(-1).elapsed_seconds-series[0].elapsed_seconds||1)*100
})
function outletPath(key,max){
 const series=outletSeries.value,start=series[0]?.elapsed_seconds??0,duration=(series.at(-1)?.elapsed_seconds??0)-start||1
 return series.map(p=>`${(p.elapsed_seconds-start)/duration*100},${45-(p[key]??0)/max*40}`).join(' ')
}
async function loadOutletExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}outlet/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load outlet example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · synthetic inputs';layer.value='water'
 }catch(err){error.value=err.message}finally{loading.value=false}
}
const comparisonRows=computed(()=>comparisonFrames.value?waterComparison(snapshots.value[frameIndex.value],comparisonFrames.value[frameIndex.value]):[])
const chartMaximum=computed(()=>Math.max(1,...[...snapshots.value,...(comparisonFrames.value??[])].map(d=>d.surfaceWater?.stored_volume_m3??0)))
function chartPath(frames){return frames.map((d,i)=>`${i/(frames.length-1||1)*100},${45-d.surfaceWater.stored_volume_m3/chartMaximum.value*40}`).join(' ')}
function exportComparison(){
 try{
  if(!comparisonFrames.value)throw Error('Load a comparison first.')
  const csv=comparisonCsv(snapshots.value,comparisonFrames.value,'Baseline',comparisonName.value)
  const url=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'}))
  const link=document.createElement('a');link.href=url;link.download='terraflux-water-comparison.csv';link.click()
  setTimeout(()=>URL.revokeObjectURL(url),1000)
 }catch(err){error.value=err.message}
}
async function loadComparisonDemo(){
 loading.value=true;error.value=''
 try{
  const responses=await Promise.all(['examples/engine-timeline.json','comparison/engine-timeline.json'].map(path=>fetch(`${import.meta.env.BASE_URL}${path}`)))
  if(responses.some(r=>!r.ok))throw Error('Could not load scenario comparison.')
  const [a,b]=await Promise.all(responses.map(r=>r.json())),baseline=validateSequence(a),variant=validateComparison(baseline,b)
  loadFrames(a);comparisonFrames.value=variant;comparisonName.value='Lower infiltration';source.value='Engine output · synthetic inputs';layer.value='water'
 }catch(err){error.value=err.message}finally{loading.value=false}
}
async function uploadComparison(e){
 const file=e.target.files[0];if(!file)return
 try{
  if(file.size>20*1024*1024)throw Error('Choose a comparison file smaller than 20 MB.')
  const variant=validateComparison(snapshots.value,JSON.parse(await file.text()))
  stop();comparisonFrames.value=variant;comparisonName.value=file.name;error.value=''
 }catch(err){error.value=err.message}finally{e.target.value=''}
}
const metrics=computed(()=>summarizeFrame(data.value,temperatureValues(data.value)))
function useRegionalDemo(){loadFrames(regionalWeatherDemo());source.value='Regional weather · synthetic';layer.value='pressure';error.value=''}
const chosen=computed(()=>selected.value===null?null:{i:selected.value,x:selected.value%grid.value.nx,y:Math.floor(selected.value/grid.value.nx)})
function disposeGroup(){ if(!group)return; group.traverse(o=>{o.geometry?.dispose(); if(o.material){o.material.map?.dispose();o.material.dispose()} if(o.isInstancedMesh)o.dispose()});scene.remove(group) }
// The colour of a field-explorer cell (the map views compose theirs in mapCellColor); the data stream uses it too.
function fieldColor(value){
 const norm=value===null?0:(value-range.value[0])/(range.value[1]-range.value[0]||1)
 return value===null?new THREE.Color('#283b49'):layer.value==='clouds'?new THREE.Color(CLOUD_TYPES[value].color):new THREE.Color().setHSL(hue(value)/360,layer.value==='cloud'?.15:.70,layer.value==='cloud'?.18+norm*.72:layer.value==='aspect'?.50:.46+norm*.08)
}
// Every scene rebuild goes through draw(): it builds the scene and reports the rebuild to the data stream.
function draw(){const t0=performance.now();drawScene();recordRender(renderer&&!fallback.value?performance.now()-t0:null)}
function drawScene(){
 if(!renderer || fallback.value)return
 disposeGroup(); group=new THREE.Group();scene.add(group);cells=[];animated=[]
 // Arrows are collected and drawn at the end as one animated field (flowArrows.js): same arguments as THREE.ArrowHelper,
 // plus an optional flow strength 0..1 where the length does not carry it.
 const flowList=[]
 const flowArrow=(dir,origin,length=1,color=0xffffff,headLength=length*.2,headWidth=headLength*.2,rate=null)=>{flowList.push({dir:dir.clone(),origin:origin.clone(),length,color:new THREE.Color(color).getHex(),headLength,headWidth,rate});return new THREE.Object3D()}
 const g=grid.value, scale=32/Math.max(g.nx*g.spacing_east_m,g.ny*g.spacing_north_m), dx=g.spacing_east_m*scale, dz=g.spacing_north_m*scale
  const elevations=view.value==='polar'&&data.value.polar?data.value.polar.surface.map((v,i)=>data.value.polar.land[i]?v:data.value.terrain.values[i]):view.value==='coast'&&data.value.coast?data.value.terrain.values.map((v,i)=>data.value.coast.sea[i]||coastLostAt(i)>=1?0:Math.max(0,v)+(coastLayers.value.defences&&coastStatic.value.defence.has(i)?coastStatic.value.defence.get(i)-Math.max(0,v):0)):data.value.terrain?.values
 const terrainBase=elevations?elevations.reduce((lo,v)=>Math.min(lo,v),Infinity):0
 const mapLike=view.value!=='field'
 // Relief. True scale flattens regional terrain (2.5 km of Alps over 960 km is a sheet of paper), so heights are
 // exaggerated to a readable relief (terrain.js); the factor is shown in the scene note. The coast keeps square-root
 // heights, so that metres of lowland stay visible next to hills of hundreds of metres.
 const reliefOn=!!elevations&&(terrainEnabled.value||layer.value==='terrain'||(mapLike&&mapLayers.value.relief))
 const coastView=view.value==='coast'&&!!data.value.coast,polarView=view.value==='polar'&&!!data.value.polar,heightScale=Number(exaggeration.value)
 const relief=reliefRange(elevations),vscale=verticalScale(relief.range,scale),coastK=RELIEF_TARGET*.6/2/Math.sqrt(Math.max(1,relief.max))
 // Realistic terrain (rock, snow, ice, water, shadows) wherever relief is shown, unless switched off.
 const realistic=reliefOn&&terrainStyle.value.realistic&&!(view.value==='solarwind'&&windLayers.value.globe)
 const waterCell=i=>coastView?!!(data.value.coast.sea[i]||coastLostAt(i)>=1):polarView?!data.value.polar.land[i]:elevations[i]<0
 // The sea. With waves (waves.js) the water is its own animated surface at sea level and the seabed lies below it
 // (depth square-root compressed, so shelves show through the shallows); without them, in the realistic look, the sea
 // is a flat surface at sea level, not a basin painted blue.
 const wavesShown=waveStyle.value.on&&!!waveInfo.value&&(reliefOn||mapLike)
 const seaLevelY=coastView?0.15:0.15+(0-relief.min)*vscale*heightScale
 const bedDepth=i=>{const e=data.value.terrain?.values[i];return Number.isFinite(e)&&e<0?-e:1}
 let deepest=1;if(wavesShown&&elevations)for(let i=0;i<elevations.length;i++)if(waterCell(i))deepest=Math.max(deepest,bedDepth(i))
 const seabed=i=>seaLevelY-(.04+Math.sqrt(bedDepth(i)/deepest)*.7*heightScale/2)
 let surface=i=>{
  if(!reliefOn)return null
  if(realistic&&waterCell(i))return wavesShown?seabed(i):coastView?0.15:0.15+(Math.max(0,elevations[i])-relief.min)*vscale*heightScale
  return coastView?0.15+Math.sqrt(Math.max(0,elevations[i]))*coastK*heightScale:0.15+(elevations[i]-relief.min)*vscale*heightScale
 }
 const reliefUnits=coastView?RELIEF_TARGET*.6*heightScale/2:relief.range*vscale*heightScale
 const count=g.nx*g.ny
 // The solar disk as a dome: heights follow mu (a hemisphere at height scale 4), the sky around it flat.
 // Cosmic dust: heights by the influx.
 const dustHeights=view.value==='dust'&&data.value.dust?dustValues.value.map(v=>dustLayers.value.relief?.2+(v-dustRange.value[0])/((dustRange.value[1]-dustRange.value[0])||1)*3:.2):null
 // The solar wind: the Earth as a globe in a Sun-fixed frame (Sun along +x), or a flat world map; heights by the value.
 const windOn=view.value==='solarwind'&&!!data.value.solarWind
 const windHeights=windOn?windValues.value.map(v=>{const [lo,hi]=windRange.value,u=Math.max(0,Math.min(1,(v-lo)/((hi-lo)||1)));return windLayers.value.relief&&windKey.value!=='windregion'?(windLayers.value.globe?.05+u*.9:.2+u*2.6):(windLayers.value.globe?.05:.2)}):null
 const windGlobe=windOn&&windLayers.value.globe?(()=>{const rot=windRotation.value,R=6,centre=new THREE.Vector3(0,R+1.6,0),dlon=360/g.nx*Math.PI/180,dlat=180/g.ny*Math.PI/180
  const cellsOnGlobe=Array.from({length:count},(_,i)=>{const {lat,lon}=windLatLon(i),a=lat*Math.PI/180,b=lon*Math.PI/180,v=x=>new THREE.Vector3(...rot(x))
   return {u:v(geographicVector(lat,lon)),e:v([-Math.sin(b),Math.cos(b),0]),s:v([Math.sin(a)*Math.cos(b),Math.sin(a)*Math.sin(b),-Math.cos(a)]),w:Math.max(.02,R*dlon*Math.cos(a)*.96),n:R*dlat*.96}})
  return {rot,R,centre,cells:cellsOnGlobe}})():null
 const solarDome=view.value==='solar'&&data.value.solar&&solarGeometry.value?{geo:solarGeometry.value,R:g.nx*dx/(2*solarBlock.value.disk.extent_radii),f:solarLayers.value.relief?Number(exaggeration.value)/4:0}:null
 // One smooth surface through the cell tops (see surface.js) instead of a box per cell: heights and colours are set
 // per cell and blended between the cells; fronts are tinted into the colours. A coarser invisible copy is picked.
 cellHeights=new Array(count)
 const heightField=new Float32Array(count),colourField=new Float32Array(count*3),valueField=new Array(count)
 const fronts=data.value.clouds&&(view.value==='field'?frontOverlay.value:mapLayers.value.fronts)?data.value.clouds.frontType:null
 for(let i=0;i<count;i++){
  const value=values.value[i]??null, norm=value===null?0:(value-range.value[0])/(range.value[1]-range.value[0]||1)
  const color=mapLike?mapCellColor(data.value,i):fieldColor(value)
  if(fronts&&fronts[i]!=='none')color.lerp(new THREE.Color(FRONT_COLORS[fronts[i]]),.65)
  const h=windHeights?windHeights[i]:dustHeights?dustHeights[i]:solarDome?(solarDome.geo[i].mu===null?.06:.2+solarDome.geo[i].mu*solarDome.R*solarDome.f):mapLike?(view.value==='carbon'&&data.value.carbon?(data.value.carbon.mask[i]?.2:.55):mapLayers.value.relief&&data.value.terrain?(surface(i)??.2):.2):surface(i)??(value===null?.10:layer.value==='clouds'?.15+data.value.clouds.fraction[i]*.6:.20+norm*2)
  heightField[i]=h;cellHeights[i]=h;colourField[i*3]=color.r;colourField[i*3+1]=color.g;colourField[i*3+2]=color.b
  valueField[i]=value===null||!Number.isFinite(value)?null:Math.max(0,Math.min(1,norm))
 }
 // Iso-lines and contours only where the colour is the value (the field explorer, not the composed map views).
 const colouredByValue=!mapLike&&layer.value!=='clouds'
 const pickSteps=Math.min(2,surfaceSubdivisions(g.nx,g.ny))
 let sheetGeometry,pickGeometry,anchorOf
 if(windGlobe){
  const dir=(lat,lon)=>windGlobe.rot(geographicVector(lat,lon))
  const opts={nx:g.nx,ny:g.ny,heights:heightField,colors:colourField,values:valueField,R:windGlobe.R,centre:windGlobe.centre,dir}
  sheetGeometry=globeSurface(opts);pickGeometry=globeSurface({...opts,s:pickSteps})
  anchorOf=i=>{const {lat,lon}=windLatLon(i),d=dir(lat,lon);return new THREE.Vector3(d[0],d[1],d[2]).multiplyScalar(windGlobe.R+heightField[i]).add(windGlobe.centre)}
 }else{
  const land=realistic?terrainCells(elevations,waterCell):null
  const opts={nx:g.nx,ny:g.ny,dx,dz,heights:heightField,colors:colourField,values:valueField}
  if(land)Object.assign(opts,{s:surfaceSubdivisions(g.nx,g.ny,110000),extras:land.extras,occlusion:reliefUnits*.05,
   detail:{amplitude:land.roughness.map(w=>w*reliefUnits*.085),seed:(g.nx*31+g.ny*17)%997}})
  sheetGeometry=planeSurface(opts);pickGeometry=planeSurface({...opts,s:pickSteps,extras:null,occlusion:0})
  // With fine relief the overlays (rivers, labels, markers) sit on the final surface over each cell centre.
  if(land){const top=centreHeights(sheetGeometry,g.nx,g.ny);for(let i=0;i<count;i++)cellHeights[i]=top[i];surface=i=>reliefOn?top[i]:null}
  anchorOf=i=>new THREE.Vector3(((i%g.nx)+.5-g.nx/2)*dx,cellHeights[i],-(Math.floor(i/g.nx)+.5-g.ny/2)*dz)
 }
 const latitude=g.geographic_anchor_latitude_deg,snowline=snowlineFor(latitude)
 const reliefBase=(view.value==='map'&&mapLayers.value.base==='terrain')||(view.value==='water'&&waterLayers.value.base==='terrain')||(coastView&&coastLayers.value.base==='terrain')||(view.value==='field'&&['terrain','slope','aspect'].includes(layer.value))
 const terrainLook=realistic&&!windGlobe?{dataMix:terrainStyle.value.dataMix/100*(reliefBase?.35:1),snowline,treeline:treelineFor(snowline),strata:strataSpacing(relief.range)}:null
 const sheetMaterial=surfaceMaterial({nx:g.nx,ny:g.ny,wrap:!!windGlobe,contours:colouredByValue&&!terrainLook?.07:0,terrain:terrainLook})
 surfaceUniforms=sheetMaterial.userData.uniforms;syncSurfaceUniforms(true)
 const sheet=new THREE.Mesh(sheetGeometry,sheetMaterial);sheet.castShadow=sheet.receiveShadow=!!terrainLook;group.add(sheet)
 if(!windGlobe){
  // The sides: a geological cut in the realistic look (soil, rock strata, ice, water), darkened data colours otherwise.
  const strataUnits=strataSpacing(relief.range)*(coastView?coastK/Math.sqrt(Math.max(1,relief.max))*relief.max:vscale)*heightScale
  const iceDepth=data.value.polar?Math.max(...data.value.polar.thickness.filter(Number.isFinite))*vscale*heightScale:reliefUnits*.08
  const side=new THREE.Mesh(surfaceSkirt(sheetGeometry,0,{iceDepth}),skirtMaterial({terrain:!!terrainLook,strata:Math.min(12,Math.max(1.2,1/Math.max(1e-6,strataUnits)))}))
  side.castShadow=side.receiveShadow=!!terrainLook;group.add(side)
 }
 setTerrainLighting(!!terrainLook)
 waveNote.value=null
 if(wavesShown&&!windGlobe){
  // Realistic water over the realistic land; glass (highlights and foam only) wherever the sea carries the data shown:
  // the ocean and carbon views, the weather map, or a field layer with values over the sea.
  const w=waveInfo.value,n=count,ice=data.value.ocean?.seaIce?.concentration
  let seaValued=0,seaCount=0;for(let i=0;i<n;i++)if(w.sea[i]){seaCount++;const v=values.value[i];if(v!==null&&v!==undefined&&Number.isFinite(v))seaValued++}
  const seaCarriesData=['ocean','carbon','map'].includes(view.value)||(view.value==='field'&&!['terrain','slope','aspect'].includes(layer.value)&&seaValued>.3*seaCount)
  const glass=!terrainLook||seaCarriesData
  const f32=()=>new Float32Array(n),cellsW={sea:f32(),amp:f32(),len:f32(),per:f32(),dirX:f32(),dirZ:f32(),foam:f32(),depth:f32()}
  let highest=0
  for(let i=0;i<n;i++){
   const d=drawnWave({hs:w.hs[i],tp:w.tp[i],length:w.length[i],speed:w.speed[i]},heightScale)
   cellsW.sea[i]=w.sea[i]*(1-Math.min(1,ice?.[i]??0));cellsW.amp[i]=w.sea[i]?d.amplitude:0;cellsW.len[i]=d.wavelength;cellsW.per[i]=d.period
   cellsW.dirX[i]=w.dirEast[i];cellsW.dirZ[i]=-w.dirNorth[i];cellsW.foam[i]=smoothstep(6,14,w.wind[i]);cellsW.depth[i]=bedDepth(i)
   if(w.sea[i])highest=Math.max(highest,w.hs[i])
  }
  const water=waterMesh({nx:g.nx,ny:g.ny,dx,dz,y:(reliefOn?seaLevelY:.2)+(glass?.015:0),cells:cellsW,glass,sun:sunLight?.position})
  animated.push(water.userData.uniforms);group.add(water)
  waveNote.value={highest,state:seaState(highest)}
 }
 reliefInfo.value=reliefOn?{factor:exaggerationFactor(vscale,scale,heightScale),sqrt:coastView,realistic:!!terrainLook,snowline}:null
 const pickMesh=new THREE.Mesh(pickGeometry,new THREE.MeshBasicMaterial({visible:false}));pickMesh.userData.surface=true;group.add(pickMesh);cells.push(pickMesh)
 surfaceInfo={nx:g.nx,ny:g.ny,colouredByValue,anchorOf,wrap:!!windGlobe,centre:windGlobe?windGlobe.centre:null}
 if(dustHeights&&(dustLayers.value.sources||dustLayers.value.labels)){
  // The sources' zenith points (where each source stands overhead) and the subsolar point.
  const at=(lat,lon,lift)=>new THREE.Vector3(((lon+180)/360*g.nx-g.nx/2)*dx,3.4+lift,-((lat+90)/180*g.ny-g.ny/2)*dz)
  if(dustLayers.value.sources)for(const pnt of data.value.dust.points){const sprite=glyphSprite(SOURCE_SYMBOLS[pnt.name]??'•',pnt.name==='apex'?'#ffd166':'#e8f4ff',pnt.name==='apex'?1.6:1.1);sprite.position.copy(at(pnt.latitude_deg,pnt.longitude_deg,.8));group.add(sprite)
   if(dustLayers.value.labels){const label=textSprite(pnt.name==='apex'?'apex: the Earth moves toward it':pnt.name,pnt.name==='apex'?'#ffd166':'#c7d6de',.55);label.position.copy(at(pnt.latitude_deg,pnt.longitude_deg,1.9));group.add(label)}}
  const sun=data.value.dust.subsolar;if(sun&&dustLayers.value.sources){const sprite=glyphSprite('☀','#ffb347',1.4);sprite.position.copy(at(sun.latitude_deg,sun.longitude_deg,.8));group.add(sprite)}
 }
 if(windOn){
  const w=data.value.solarWind,L=windLayers.value,m=windMetrics.value,sunMlon=w.map.subsolar.magnetic_longitude_deg
  const mat=(color,opacity,onTop=false)=>new THREE.LineBasicMaterial({color,transparent:opacity<1,opacity,depthWrite:false,depthTest:!onTop})
  if(windGlobe){
   const R=windGlobe.R,C=windGlobe.centre,rot=windGlobe.rot
   const V=p=>new THREE.Vector3(p[0],p[1],p[2]).add(C)
   const line=(pts,color,opacity)=>group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts.map(V)),mat(color,opacity)))
   const onEarth=(mlat,mlt,lift)=>{const v=rot(magneticToVector(mlat,sunMlon+(mlt-12)*15,w.dipole));return {dir:new THREE.Vector3(...v),at:V(v.map(x=>x*(R+lift)))}}
   const sketch=magnetosphereSketch(w,rot,R,{tail:32})
   if(L.field){sketch.closed.forEach(l=>line(l.points,0x7fb8ff,.4));sketch.open.forEach(l=>line(l.points,0xffa65a,.55))}
   if(L.magnetopause)sketch.pause.forEach(p=>line(p,0xc9a6ff,.2))
   if(L.ovals)for(const hemi of [1,-1]){line(ovalRing(w,hemi,'poleward').map(v=>rot(v).map(x=>x*(R+.12))),0xff9f5a,.95);line(ovalRing(w,hemi,'equatorward').map(v=>rot(v).map(x=>x*(R+.12))),0x58e07a,.95)}
   if(L.sunwind){
    const fast=m.speed>=800?0xff6a3d:m.speed>=550?0xffb347:0xffe08a
    for(const y of [-7,-2.5,2.5,7])for(const z of [-9,-3,3,9])group.add(flowArrow(new THREE.Vector3(-1,0,0),new THREE.Vector3(27,C.y+y,z),2.2+Math.min(3,m.speed/400),fast,.8,.45,Math.min(1,m.speed/1000)))
    const sun=glyphSprite('☀','#ffb347',3.2);sun.position.set(31,C.y,0);group.add(sun)
    if(L.labels){const t=textSprite(`solar wind ${m.speed.toFixed(0)} km/s · ${m.density.toFixed(0)} /cm³ · Bz ${m.bz>0?'+':''}${m.bz.toFixed(0)} nT`,m.bz<-5?'#ff8a7a':'#ffe08a',1.1);t.position.set(24,C.y+10.5,0);group.add(t)
     const ts=textSprite('to the Sun','#ffb347',.9);ts.position.set(31,C.y+2.6,0);group.add(ts)}
   }
   if(L.outflow){
    // Escaping oxygen from the cusp (cleft ion fountain) and the night-side oval; the polar wind of H+ over the cap.
    const length=.7+.45*Math.log10(Math.max(1,m.o_escape/1e24))
    for(const hemi of [1,-1]){
     for(const mlt of [10,11,12,13,14]){const p=onEarth(hemi*(90-ovalBoundary(w.map.oval,mlt,'poleward')),mlt,.15);group.add(flowArrow(p.dir,p.at,length,0xff8a3d,.35,.22))}
     for(const mlt of [20,22,0]){const mid=.5*(ovalBoundary(w.map.oval,mlt,'poleward')+ovalBoundary(w.map.oval,mlt,'equatorward')),p=onEarth(hemi*(90-mid),mlt,.15);group.add(flowArrow(p.dir,p.at,length*.8,0xff8a3d,.3,.2))}
     for(const mlt of [0,8,16]){const p=onEarth(hemi*(90-.4*w.map.oval.cap_colatitude_deg),mlt,.15);group.add(flowArrow(p.dir,p.at,1.1,0x7fe0ff,.3,.18))}
    }
    if(L.labels){const p=onEarth(90-ovalBoundary(w.map.oval,12,'poleward'),12,length+.4),t=textSprite(`O⁺ escaping · ${sig(m.o_kg,2)} kg/s`,'#ff8a3d',.85);t.position.copy(p.at).add(new THREE.Vector3(4,1,0));group.add(t)}
   }
   if(['winddose','windsep','windcutoff'].includes(windKey.value)||m.sep>=10){
    // Charged particles from space reach the atmosphere freely only where the field lines are open: over the caps.
    const color=m.sep>=10?0xff5fb0:0xd7a6ff
    for(const hemi of [1,-1])for(let k=0;k<6;k++){const p=onEarth(hemi*80,k*4,0),from=p.at.clone().add(p.dir.clone().multiplyScalar(3.5));group.add(flowArrow(p.dir.clone().negate(),from,3.1,color,.45,.28))}
    if(L.labels){const p=onEarth(90,0,4.2),t=textSprite(m.sep>=10?`solar protons ${sig(m.sep,2)} pfu: only over the polar caps`:'cosmic rays: the poles are unshielded',m.sep>=10?'#ff8fc8':'#d7a6ff',.85);t.position.copy(p.at).add(new THREE.Vector3(0,2.6,0));group.add(t)}
   }
   if(L.labels){
    const nose=V(sketch.nose),t=textSprite(`magnetopause ${m.standoff.toFixed(1)} Earth radii${m.inside_geostationary?' · inside geostationary orbit':''}`,m.inside_geostationary?'#ff8a7a':'#c9a6ff',.85);t.position.copy(nose).add(new THREE.Vector3(1,-4.5,0));group.add(t)
    const cap=onEarth(90,0,1.6),tc=textSprite('polar cap: open field lines, H⁺ polar wind','#ffb37a',.85);tc.position.copy(cap.at).add(new THREE.Vector3(-5,1.4,0));group.add(tc)
    const ov=onEarth(90-ovalBoundary(w.map.oval,0,'equatorward'),0,1.2),to=textSprite(`auroral oval down to ${m.oval_mlat.toFixed(0)}° magnetic`,'#7ff0a0',.85);to.position.copy(ov.at).add(new THREE.Vector3(-5.5,-.6,0));group.add(to)
    if(L.field){const tail=textSprite('magnetotail (open field lines)','#ffb37a',.9);tail.position.set(-21,C.y+9,0);group.add(tail)}
   }
  }else{
   // Flat map: the oval's edges projected onto the equirectangular grid (drawn on top).
   const at=(lat,lon,y)=>new THREE.Vector3(((lon+180)/360*g.nx-g.nx/2)*dx,y,-((lat+90)/180*g.ny-g.ny/2)*dz)
   const mapLine=(vectors,color)=>{let pts=[],last=null;const flush=()=>{if(pts.length>1){const l=new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts),mat(color,.95,true));l.renderOrder=5;group.add(l)}pts=[]}
    for(const v of vectors){const {lat,lon}=vectorToLatLon(v);if(last!==null&&Math.abs(lon-last)>180)flush();pts.push(at(lat,lon,3.2));last=lon}flush()}
   if(L.ovals)for(const hemi of [1,-1]){mapLine(ovalRing(w,hemi,'poleward',192),0xff9f5a);mapLine(ovalRing(w,hemi,'equatorward',192),0x58e07a)}
   if(L.labels){const d=w.dipole,n=glyphSprite('⊕','#ffb37a',1.2);n.position.copy(at(d.north_pole_latitude_deg,d.north_pole_longitude_deg,3.6));group.add(n)
    const sp=vectorToLatLon(geographicVector(-d.north_pole_latitude_deg,d.north_pole_longitude_deg+180)),so=glyphSprite('⊕','#ffb37a',1.2);so.position.copy(at(sp.lat,sp.lon,3.6));group.add(so)
    const sun=glyphSprite('☀','#ffb347',1.4);sun.position.copy(at(w.map.subsolar.latitude_deg,w.map.subsolar.longitude_deg,3.6));group.add(sun)
    const t=textSprite('geomagnetic north pole','#ffb37a',.6);t.position.copy(at(d.north_pole_latitude_deg-6,d.north_pole_longitude_deg,4.4));group.add(t)}
  }
 }
 if(solarDome){
  const s=data.value.solar,L=solarLayers.value,R=solarDome.R,f=solarDome.f
  const at=(p,lift)=>new THREE.Vector3(p.x*R,.2+p.mu*R*f+lift,-p.y*R)
  // Heliographic latitude circles on the visible hemisphere (equator brighter).
  if(L.grid)for(const lat of [-30,-15,0,15,30]){const pts=[];for(let lon=-90;lon<=90;lon+=5)pts.push(at(regionPosition({latitude_deg:lat,longitude_deg:lon}),.08));group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts),new THREE.LineBasicMaterial({color:lat===0?0xffe3a3:0x9a7b4f,transparent:true,opacity:lat===0?.75:.45})))}
  // The month's flares in their regions, coloured by the NOAA radio-blackout level.
  if(L.flares)s.flares.forEach(fl=>{const r=s.regions[fl.region];if(!r)return;const p=regionPosition(r),big=fl.r_scale>=3,sprite=glyphSprite('⚡',R_COLORS[fl.r_scale],big?1.5:.9);sprite.position.copy(at(p,big?1.4:.9));group.add(sprite)
   if(L.labels&&big){const label=textSprite(flareLabel(fl.peak_flux_w_m2),R_COLORS[fl.r_scale],.6);label.position.copy(at(p,2.3));group.add(label)}})
  if(L.labels)[...s.regions].sort((a,b)=>b.area_uhem-a.area_uhem).slice(0,3).forEach(r=>{const label=textSprite(`cycle ${r.cycle} · ${r.area_uhem.toFixed(0)} µHem`,cycleColor(r.cycle,solarMetrics.value?.cycle??r.cycle),.55);label.position.copy(at(regionPosition(r),1.9));group.add(label)})
 }
 if(view.value==='field'?vectors.value:mapLayers.value.wind){ const stride=Math.max(1,Math.ceil(Math.sqrt(g.nx*g.ny/90)))
 for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x,u=forecast.value.fields.wind_east.values[i],v=forecast.value.fields.wind_north.values[i],speed=Math.hypot(u,v);if(speed<.01)continue; const arrow=flowArrow(new THREE.Vector3(u,0,-v).normalize(),new THREE.Vector3((x+.5-g.nx/2)*dx,(surface(i)??2.4)+1,-(y+.5-g.ny/2)*dz),Math.min(2.5,.3+speed*.18),0xb7e8de,.3,.15);group.add(arrow)} }
 if(view.value==='map'){
  const L=mapLayers.value
  for(const level of isobarLines.value)for(const line of level.lines){
   const points=line.map(([u,v])=>new THREE.Vector3((u-.5)*g.nx*dx,.48,-(v-.5)*g.ny*dz))
   const major=Math.round(level.level_hpa)%(L.isobarIntervalHpa*2)===0
   group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color:major?0xf3f7f9:0xa9bcc6,transparent:true,opacity:major?.95:.6})))
   if(major){const label=glyphSprite(String(Math.round(level.level_hpa)),'#f3f7f9',1.4);label.position.copy(points[Math.floor(points.length/2)]).setY(.9);group.add(label)}
  }
  if(L.symbols&&data.value.precipitation){const p=data.value.precipitation,stride=symbolStride.value
   for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x;if(p.type[i]==='none')continue;const info=precipitationTypeInfo(p.type[i]);const sprite=glyphSprite(info.symbol,p.phase[i]==='solid'?'#ffffff':p.phase[i]==='mixed'?'#d6c8ff':'#9fd0ff',.9+Math.min(.6,p.rate[i]/30));sprite.position.set((x+.5-g.nx/2)*dx,.75,-(y+.5-g.ny/2)*dz);group.add(sprite)}}
  if(L.storms&&data.value.storms){
   for(const s of data.value.storms.list){
    const cx=(s.east_m-g.origin_east_m)/g.spacing_east_m-g.nx/2,cy=(s.north_m-g.origin_north_m)/g.spacing_north_m-g.ny/2
    const ring=new THREE.Mesh(new THREE.RingGeometry(Math.max(.2,s.radius_max_wind_m/g.spacing_east_m*dx*.8),Math.max(.3,s.radius_max_wind_m/g.spacing_east_m*dx),32),new THREE.MeshBasicMaterial({color:new THREE.Color(categoryInfo(s.category).color),side:THREE.DoubleSide,transparent:true,opacity:.9}))
    ring.rotation.x=-Math.PI/2;ring.position.set(cx*dx,.6,-cy*dz);group.add(ring)
    const label=glyphSprite(categoryInfo(s.category).short,categoryInfo(s.category).color,1.6);label.position.set(cx*dx,1.5,-cy*dz);group.add(label)
    if(s.track.length>1){const points=s.track.map(([e,n])=>new THREE.Vector3(((e-g.origin_east_m)/g.spacing_east_m-g.nx/2)*dx,.55,-((n-g.origin_north_m)/g.spacing_north_m-g.ny/2)*dz));group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color:0xffffff,transparent:true,opacity:.7})))}
   }
   for(const e of visibleEvents.value){if(e.kind!=='landfall'&&e.kind!=='merger'&&e.kind!=='genesis')continue;const m=glyphSprite(e.kind==='landfall'?'✖':e.kind==='merger'?'⊕':'✦',e.kind==='landfall'?'#ff5c5c':'#ffffff',1.1);m.position.set(((e.east_m-g.origin_east_m)/g.spacing_east_m-g.nx/2)*dx,1.0,-((e.north_m-g.origin_north_m)/g.spacing_north_m-g.ny/2)*dz);group.add(m)}
  }
  if(data.value.orography){const o=data.value.orography
   for(let i=0;i<g.nx*g.ny;i++){const ground=(surface(i)??.2)+.2
    if(L.foehn&&o.foehn[i]){const sprite=glyphSprite('F','#ff8c42',1.1);sprite.position.set(((i%g.nx)+.5-g.nx/2)*dx,ground+.6,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(sprite)}
    if(L.coldPools&&o.coldPoolFlag[i]){const sprite=glyphSprite('❄','#dff3ff',1.0);sprite.position.set(((i%g.nx)+.5-g.nx/2)*dx,ground+.5,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(sprite)}
    if(L.oroPrecip&&o.precipRate[i]>0.2&&(i%g.nx)%symbolStride.value===0&&Math.floor(i/g.nx)%symbolStride.value===0){const sprite=glyphSprite('●','#9fd0ff',.7+Math.min(.5,o.precipRate[i]/5));sprite.position.set(((i%g.nx)+.5-g.nx/2)*dx,ground+.55,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(sprite)}
   }}
  if(L.tornado&&data.value.severe){const v=data.value.severe
   for(let i=0;i<g.nx*g.ny;i++){if(!v.possible[i])continue;const sprite=glyphSprite('🌪','#ffffff',1.3);sprite.position.set(((i%g.nx)+.5-g.nx/2)*dx,1.3,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(sprite)}}
  if(L.hazards&&data.value.precipitation){const p=data.value.precipitation
   for(let i=0;i<g.nx*g.ny;i++){if(!p.lightning[i])continue;const sprite=glyphSprite(p.hail[i]>0?'⚡◆':'⚡','#ffd166',1.2);sprite.position.set(((i%g.nx)+.5-g.nx/2)*dx,1.15,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(sprite)}}
 }
 if(view.value==='ocean'&&data.value.ocean){
  const o=data.value.ocean,L=oceanLayers.value,up=new THREE.Vector3(0,1,0),base=.25
  const at=(fx,fy,lift=base)=>new THREE.Vector3((fx-.5)*g.nx*dx,lift,-(fy-.5)*g.ny*dz)
  const centre=(i,lift=base)=>at(((i%g.nx)+.5)/g.nx,(Math.floor(i/g.nx)+.5)/g.ny,lift)
  if(L.streams)for(const st of o.streams){
   const radius=Math.min(dx,dz)*(.06+.18*Math.min(1,st.transport_sv/40)),tone=new THREE.Color(speedColor(st.max_speed_m_s))
   for(let k=0;k+1<st.path.length;k++){const a=at(st.path[k][0],st.path[k][1],base+.08),b=at(st.path[k+1][0],st.path[k+1][1],base+.08),length=a.distanceTo(b);if(length<1e-6)continue
    const seg=new THREE.Mesh(new THREE.CylinderGeometry(radius,radius,length,6),new THREE.MeshBasicMaterial({color:tone}));seg.position.copy(a).lerp(b,.5);seg.quaternion.setFromUnitVectors(up,b.clone().sub(a).normalize());group.add(seg)}
   if(L.labels){const label=textSprite(streamLabel(st,g),'#f3f7f9',1.0);label.position.copy(centre(st.core_cell,1.1));group.add(label)}
  }
  if(L.arrows){const stride=Math.max(1,Math.ceil(Math.sqrt(g.nx*g.ny/200)))
   for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x;if(!o.mask[i]||o.speed[i]<.02)continue;group.add(flowArrow(new THREE.Vector3(o.u[i],0,-o.v[i]).normalize(),centre(i,base+.3),Math.min(dx,dz)*(.4+Math.min(1.2,o.speed[i]*2)),new THREE.Color(speedColor(o.speed[i])).getHex(),Math.min(dx,dz)*.3,Math.min(dx,dz)*.18))}}
  if(L.rivers)for(const p of o.exchangePoints){const marker=glyphSprite('⊕',p.discharge_m3_s>0?'#7fffd4':'#9aa7b0',1.2);marker.position.copy(centre(p.mouth_cell,base+.6));group.add(marker)
   if(L.labels&&p.discharge_m3_s>0){const label=textSprite(`${p.name}: ${p.discharge_m3_s>=1e5?(p.discharge_m3_s/1e6).toFixed(2)+' Sv':Math.round(p.discharge_m3_s).toLocaleString()+' m³/s'} · ${p.minimum_salinity_psu.toFixed(1)} psu`,'#dffcf0',.9);label.position.copy(centre(p.mouth_cell,base+1.2));group.add(label)}}
  if(L.formation)for(let i=0;i<g.nx*g.ny;i++){if(o.sinking[i]<=0||(i%g.nx)%2||Math.floor(i/g.nx)%2)continue;const s=glyphSprite('⇩','#e0c8ff',1.0);s.position.copy(centre(i,base+.5));group.add(s)}
  if(selectedStream.value){const st=selectedStream.value;const ring=new THREE.Mesh(new THREE.RingGeometry(Math.min(dx,dz)*.45,Math.min(dx,dz)*.6,24),new THREE.MeshBasicMaterial({color:0xffffff,side:THREE.DoubleSide}));ring.rotation.x=-Math.PI/2;ring.position.copy(centre(st.core_cell,base+.12));group.add(ring)}
  // Coastal runoff: arrows from draining coastal land to the sea cell that receives it (length grows with the store).
  if(L.runoff&&o.rain)for(let i=0;i<g.nx*g.ny;i++){const r=o.rain.receiver[i];if(r<0||o.rain.store[i]<.5)continue;const a=centre(i,base+.35),b=centre(r,base+.35),dir=b.clone().sub(a);const length=dir.length();if(length<1e-6)continue;group.add(flowArrow(dir.normalize(),a,length*Math.min(1,.45+o.rain.store[i]/60),0x4fc3f7,Math.min(dx,dz)*.3,Math.min(dx,dz)*.2))}
  // Surface lenses: label the largest ones at their freshest cell.
  if(L.lenses&&L.labels)for(const l of o.lenses.slice(0,4)){if(l.area_km2<2*g.spacing_east_m*g.spacing_north_m/1e6)continue;const label=textSprite(lensLabel(l),l.kind==='river'?'#7fffd4':l.kind==='rain'?'#9fd0ff':'#ffd59a',.85);label.position.copy(centre(l.freshest_cell,base+1.6));group.add(label)}
  // Sea-ice drift: white arrows on ice-covered cells (length grows with the speed).
  if(o.seaIce&&L.seaIce&&L.iceDrift)for(let i=0;i<g.nx*g.ny;i++){const a=o.seaIce.concentration[i],ue=o.seaIce.driftEast[i],un=o.seaIce.driftNorth[i],sp=Math.hypot(ue,un);if(a<.15||sp<.01)continue;group.add(flowArrow(new THREE.Vector3(ue,0,-un).normalize(),centre(i,base+.45),Math.min(dx,dz)*(.35+Math.min(1,sp*4)),0xf4fbff,Math.min(dx,dz)*.25,Math.min(dx,dz)*.16))}
  // Hosing: the ring where the extra meltwater enters the sea, labelled with the current rate.
  const hose=o.forcing?.hosing
  if(hose&&L.hosing){const fx=(hose.center_east_m-g.origin_east_m)/(g.nx*g.spacing_east_m),fy=(hose.center_north_m-g.origin_north_m)/(g.ny*g.spacing_north_m),r=hose.radius_m*scale
   const ring=new THREE.Mesh(new THREE.RingGeometry(r*.92,r,48),new THREE.MeshBasicMaterial({color:0xffd166,side:THREE.DoubleSide,transparent:true,opacity:.85}));ring.rotation.x=-Math.PI/2;ring.position.copy(at(fx,fy,base+.1));group.add(ring)
   if(L.labels){const label=textSprite(`meltwater hosing ${(o.hosing??0).toFixed(2)} Sv`,'#ffd166',.9);label.position.copy(at(fx,fy,base+1.4));group.add(label)}}
  // The coupled ice sheet: its meltwater and icebergs enter at the front.
  if(o.iceSheet&&L.iceSheet&&L.labels){let best=-1,hmax=0;o.iceSheet.thickness.forEach((h,i)=>{if(h>hmax){hmax=h;best=i}});if(best>=0){const label=textSprite(`ice sheet ${hmax.toFixed(0)} m · SMB ${o.iceSheet.smbGt.toFixed(0)} Gt/yr · +${(o.iceSheet.seaLevelRise*1000).toFixed(1)} mm sea level`,'#e8f4ff',.85);label.position.copy(centre(best,base+1.8));group.add(label)}}
  // The cell shown in the water-column panel.
  if(columnCell.value!==null){const ring=new THREE.Mesh(new THREE.RingGeometry(Math.min(dx,dz)*.3,Math.min(dx,dz)*.42,24),new THREE.MeshBasicMaterial({color:0xffd166,side:THREE.DoubleSide}));ring.rotation.x=-Math.PI/2;ring.position.copy(centre(columnCell.value,base+.14));group.add(ring)}
 }
 if(view.value==='coast'&&data.value.coast){
  const c=data.value.coast,b=c.block,L=coastLayers.value,m=coastSummary.value
  const centre=(i,lift)=>new THREE.Vector3(((i%g.nx)+.5-g.nx/2)*dx,(cellHeights[i]??.2)+lift,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz)
  if(L.labels){
   // Region labels at their land centroid: the share of land lost and the people affected.
   b.region_names.forEach((name,r)=>{let sx=0,sy=0,n=0;for(let i=0;i<c.count;i++)if(b.region[i]===r&&!c.sea[i]){sx+=i%g.nx;sy+=Math.floor(i/g.nx);n++}
    if(!n)return;const i=Math.round(sy/n)*g.nx+Math.round(sx/n),reg=m.regions[r]
    const label=textSprite(`${name} · ${reg.lost_pct.toFixed(1)} % lost`,reg.lost_pct>10?'#ffb4a8':'#f3f7f9',.8);label.position.copy(centre(i,2.2));group.add(label)})
   if(L.rivers)b.rivers.forEach((r,k)=>{const i=r.cells[Math.min(r.cells.length-1,Math.floor(r.cells.length*.35))],rv=m.rivers[k]
    const label=textSprite(`${r.name} · surge ${rv.surge_reach_km.toFixed(0)} km up`,'#9fe0ff',.6);label.position.copy(centre(i,1.4));group.add(label)})
  }
  if(L.wind){
   // The design storm: an arrow over the sea pointing where the wind blows.
   const d=b.design_storm,t=d.wind_toward_deg*Math.PI/180,start=new THREE.Vector3(-g.nx*dx*.3,1.2,g.ny*dz*.32)
   group.add(flowArrow(new THREE.Vector3(Math.sin(t),0,-Math.cos(t)),start,Math.min(g.nx*dx,g.ny*dz)*.22,0xffd166,1.2,.7))
   const label=textSprite(`${d.return_period_years}-yr storm · ${d.wind_speed_m_s} m/s · setup ≤ ${m.max_setup_m.toFixed(1)} m`,'#ffd166',.7);label.position.copy(start.clone().add(new THREE.Vector3(0,1.4,0)));group.add(label)
  }
 }
 if(view.value==='carbon'&&data.value.carbon){
  const c=data.value.carbon,L=carbonLayers.value,m=carbonMetrics.value
  const centre=(i,lift)=>new THREE.Vector3(((i%g.nx)+.5-g.nx/2)*dx,(cellHeights[i]??.2)+lift,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz)
  // Outline a set of cells: the edges they do not share with each other, as one line set.
  const outline=(inside,color,lift,opacity=1)=>{const pts=[];for(let i=0;i<g.nx*g.ny;i++){if(!inside(i))continue;const x=i%g.nx,y=Math.floor(i/g.nx),cx=(x+.5-g.nx/2)*dx,cz=-(y+.5-g.ny/2)*dz,hx=dx/2,hz=dz/2,yy=(cellHeights[i]??.2)+lift
    if(x+1>=g.nx||!inside(i+1))pts.push(new THREE.Vector3(cx+hx,yy,cz-hz),new THREE.Vector3(cx+hx,yy,cz+hz))
    if(x<=0||!inside(i-1))pts.push(new THREE.Vector3(cx-hx,yy,cz-hz),new THREE.Vector3(cx-hx,yy,cz+hz))
    if(y+1>=g.ny||!inside(i+g.nx))pts.push(new THREE.Vector3(cx-hx,yy,cz-hz),new THREE.Vector3(cx+hx,yy,cz-hz))
    if(y<=0||!inside(i-g.nx))pts.push(new THREE.Vector3(cx-hx,yy,cz+hz),new THREE.Vector3(cx+hx,yy,cz+hz))}
   if(pts.length)group.add(new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(pts),new THREE.LineBasicMaterial({color,transparent:opacity<1,opacity})))}
  // Dead zones: a red outline and a dark-red plate just above the water (hypoxic bottom water: a dead zone).
  if(L.dead&&c.deadCells.length){outline(i=>!!c.dead[i],0xff3b30,.5)
   // A translucent red volume over each dead-zone cell (one instanced mesh), taller where less oxygen is left.
   const volume=new THREE.InstancedMesh(new THREE.BoxGeometry(dx*.96,1,dz*.96),new THREE.MeshBasicMaterial({color:0xff2a2a,transparent:true,opacity:.28,depthWrite:false}),c.deadCells.length),place=new THREE.Matrix4()
   c.deadCells.forEach((i,k)=>{const hgt=.2+.3*Math.max(0,1-c.o2Bottom[i]/c.habitat.hypoxia_umol_kg),base=cellHeights[i]??.2;place.makeScale(1,hgt,1).setPosition(((i%g.nx)+.5-g.nx/2)*dx,base+hgt/2,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);volume.setMatrixAt(k,place)})
   volume.instanceMatrix.needsUpdate=true;group.add(volume)}
  // Corrosive water (aragonite undersaturated at the surface or the bottom): a magenta outline.
  if(L.corrosive)outline(i=>c.mask[i]&&(c.omega[i]<c.habitat.corrosive_omega||c.omegaBottom[i]<c.habitat.corrosive_omega),0xff4fd8,.2)
  // Anoxic columns: a skull over each.
  if(L.anoxic)for(let i=0;i<g.nx*g.ny;i++)if(c.mask[i]&&c.columnClass[i]===3){const s=glyphSprite('☠','#f1e3e6',.9);s.position.copy(centre(i,.6));group.add(s)}
  if(L.labels&&L.dead&&m)for(const p of m.patches.slice(0,2)){if(p.cells.length<3)continue;const label=textSprite(`Dead zone · ${fmtKm2(p.area_km2)} km² · bottom O₂ ${p.min_bottom_oxygen_umol_kg.toFixed(0)} µmol/kg${p.nearest_river&&p.river_distance_km<250?` · off ${p.nearest_river}`:''}`,'#ffb4a8',.8);label.position.copy(centre(p.centre_cell,1.6));group.add(label)}
  // CO2 sources: a chimney marker, labelled with the emission and the CO2 above the background there.
  if(L.sources)for(const src of c.sources){const i=src.peak_cell,mark=glyphSprite('🏭','#ffd166',1.2);mark.position.copy(centre(i,.9));group.add(mark)
   if(L.labels){const label=textSprite(`${src.name} · ${src.mt_co2_per_year} Mt/yr · ${signed(c.ppm[i]-c.background,1)} ppm`,'#ffd9a0',.7);label.position.copy(centre(i,2.4));group.add(label)}}
  // River mouths with their nitrate load.
  if(L.rivers)for(const r of c.rivers){if(r.cell<0)continue;const mark=glyphSprite('⊕','#7fffd4',1.1);mark.position.copy(centre(r.cell,.7));group.add(mark)
   if(L.labels){const n=r.chemistry?.nitrate_umol_kg;const label=textSprite(`${r.name}${n!=null?` · ${(r.discharge*n*1.025e-3*14.007e-6*86400).toFixed(0)} t N/day`:''}`,'#bff5e6',.65);label.position.copy(centre(r.cell,1.2));group.add(label)}}
  // The wind that carries the CO2 plumes.
  if(L.wind){const stride=Math.max(1,Math.ceil(Math.sqrt(g.nx*g.ny/160)))
   for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x,u=forecast.value.fields.wind_east.values[i],v=forecast.value.fields.wind_north.values[i],sp=Math.hypot(u,v);if(sp<.3)continue;group.add(flowArrow(new THREE.Vector3(u,0,-v).normalize(),centre(i,.9),Math.min(dx,dz)*(.4+Math.min(1,sp/12)),0xdfe9f2,Math.min(dx,dz)*.25,Math.min(dx,dz)*.15))}}
  // The water column shown in the panel.
  if(carbonCell.value!==null){const ring=new THREE.Mesh(new THREE.RingGeometry(Math.min(dx,dz)*.3,Math.min(dx,dz)*.42,24),new THREE.MeshBasicMaterial({color:0xffd166,side:THREE.DoubleSide}));ring.rotation.x=-Math.PI/2;ring.position.copy(centre(carbonCell.value,.16));group.add(ring)}
 }
 if((view.value==='water'||view.value==='hazards')&&data.value.hydrology){
  const h=data.value.hydrology,n=h.network,L=waterLayers.value,H=hazardLayers.value,up=new THREE.Vector3(0,1,0)
  const centre=(i,lift=.25)=>new THREE.Vector3(((i%g.nx)+.5-g.nx/2)*dx,(surface(i)??.2)+lift,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz)
  if(view.value==='hazards'||L.rivers)for(const seg of riverLines.value){
   const q=h.discharge[seg.cell],w=riverWidth(seg.area_km2),a=centre(seg.cell),b=seg.to>=0?centre(seg.to):centre(seg.cell).add(new THREE.Vector3((seg.end[0]-seg.from[0])*g.nx*dx,0,-(seg.end[1]-seg.from[1])*g.ny*dz))
   const length=a.distanceTo(b);if(length<1e-6)continue
   const radius=(.04+.14*w)*Math.min(dx,dz)*(1+Math.min(1.5,Math.log10(1+q)/2)),depth=h.channelDepth[seg.cell],bankfull=n.bankfull_depth_m[seg.cell]
   const tone=view.value==='hazards'&&bankfull>0&&depth>=bankfull?new THREE.Color('#ff7b4a'):new THREE.Color().setHSL(.58,.85,Math.min(.78,.32+Math.log10(1+q)/5))
   const reach=new THREE.Mesh(new THREE.CylinderGeometry(radius,radius,length,6),new THREE.MeshBasicMaterial({color:tone}))
   reach.position.copy(a).lerp(b,.5);reach.quaternion.setFromUnitVectors(up,b.clone().sub(a).normalize());group.add(reach)
  }
  if(view.value==='water'&&L.arrows){const stride=Math.max(1,Math.ceil(Math.sqrt(g.nx*g.ny/220)))
   for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x,d=n.flow_direction_deg[i];if(d===null||n.ocean[i])continue;const t=d*Math.PI/180;group.add(flowArrow(new THREE.Vector3(Math.cos(t),0,-Math.sin(t)),centre(i,.45),Math.min(dx,dz)*.8,n.channel[i]?0xbfe3ff:0x7fa6b8,Math.min(dx,dz)*.3,Math.min(dx,dz)*.18))}}
  if(view.value==='water'&&L.labels)n.lakes.forEach((l,k)=>{if(l.cell_count<2)return;let best=l.outlet_cell>=0?l.outlet_cell:0;for(let i=0;i<n.lake_id.length;i++)if(n.lake_id[i]===k){best=i;break}
   const label=textSprite(`L${k+1} · ${h.lakeLevel[k].toFixed(0)} m`,'#dff3ff',1.1);label.position.copy(centre(best,.9));group.add(label)})
  if(view.value==='hazards'){
   if(H.landslides)for(const e of visibleLandslides.value){const marker=glyphSprite('▲','#ff5c5c',1.1);marker.position.copy(centre(e.cell,.7));group.add(marker)
    if(H.runout&&e.runout_cell>=0){const points=[centre(e.cell,.4),centre(e.runout_cell,.4)];group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color:0xffa15c,transparent:true,opacity:.9})))}}
   if(H.threshold){const stride=Math.max(1,Math.ceil(Math.sqrt(g.nx*g.ny/260)))
    for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x;if(!h.threshold[i])continue;const s=glyphSprite('!','#ffd166',.9);s.position.copy(centre(i,.6));group.add(s)}}
  }
  if(selectedPath.value&&selectedPath.value.cells.length>1){const points=selectedPath.value.cells.map(i=>centre(i,.5));group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color:0xffffff,transparent:true,opacity:.95})))}
 }
 // Clouds in their forms (clouds3d.js): heaps, towers, anvils, layers, sheets, cloudlets and streaks between their base
 // and top heights, the anvils and streaks turned with the wind; rain or snow falls from the precipitating ones.
 if(data.value.clouds){
  const c=data.value.clouds,heightScale=6/Math.max(1,c.config.max_cloud_top_m)
  if(view.value==='field'?cloudVolumes.value:mapLayers.value.volumes){
   const puffs=[],shafts=[],p=data.value.precipitation,w=Math.min(dx,dz),fw=forecast.value.fields
   for(let i=0;i<g.nx*g.ny;i++){
    const type=c.type[i];if(type==='none'||!Number.isFinite(c.base[i])||!Number.isFinite(c.top[i]))continue
    const floor=surface(i)??.2,ground=floor+.3,base=ground+c.base[i]*heightScale,top=ground+c.top[i]*heightScale
    const cx=((i%g.nx)+.5-g.nx/2)*dx,cz=-(Math.floor(i/g.nx)+.5-g.ny/2)*dz
    const u=fw.wind_east?.values[i]??0,v=fw.wind_north?.values[i]??0,sp=Math.hypot(u,v),wind=sp>.01?[u/sp,-v/sp]:[1,0]
    const storm=!!c.thunderstorm[i]&&type==='cumulonimbus'
    for(const q of cloudPuffs({type,base,top,fraction:c.fraction[i],w,wind,seed:i+1}))puffs.push({...q,x:q.x+cx,z:q.z+cz,base,depth:top-base,storm,seed:(i*.618)%1})
    const rate=p?.rate?.[i]
    if(Number.isFinite(rate)&&rate>.1)shafts.push({x:cx,z:cz,top:base,bottom:floor,radius:w*(type==='cumulonimbus'?.3:.42),intensity:precipitationIntensity(rate),snow:p.phase?.[i]==='solid'})
   }
   addClouds(puffs,shafts)
  }
 }
 for(const o of data.value.surfaceWater?.outlets??[]){
  const index=o.y*g.nx+o.x
  const marker=new THREE.Mesh(new THREE.SphereGeometry(.35,10,8),new THREE.MeshBasicMaterial({color:0xffa15c}))
  marker.position.set((o.x+.5-g.nx/2)*dx,(surface(index)??2.4)+.6,-(o.y+.5-g.ny/2)*dz);group.add(marker)
 }
 if(data.value.atmosphere){
  const atmosphere=data.value.atmosphere
  if(cloudsEnabled.value){
   // The synthetic regional weather has a cloud cover but no cloud types: heaps, or a lumpy layer where the sky is
   // mostly covered. Every second cell, as before, each cloud twice as wide.
   const puffs=[]
   for(let y=0;y<g.ny;y+=2)for(let x=0;x<g.nx;x+=2){const i=y*g.nx+x,cover=atmosphere.cloud[i];if(cover<=.35)continue
    const type=cover>.75?'stratocumulus':'cumulus',base=5.2,top=base+.5+cover*1.4,cx=(x+.5-g.nx/2)*dx,cz=-(y+.5-g.ny/2)*dz
    for(const q of cloudPuffs({type,base,top,fraction:cover,w:Math.min(dx,dz)*2,seed:i+1}))puffs.push({...q,x:q.x+cx,z:q.z+cz,base,depth:top-base,seed:(i*.618)%1})}
   addClouds(puffs,[])
  }
  if(systemsEnabled.value)for(const system of atmosphere.systems){
   if(system.kind==='cold_front'){
    const points=system.points.map(([u,v])=>new THREE.Vector3((u-.5)*g.nx*dx,3.1,-(v-.5)*g.ny*dz))
    group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),new THREE.LineBasicMaterial({color:0x66bbff})))
   }else{
    const canvas=document.createElement('canvas');canvas.width=128;canvas.height=128
    const ctx=canvas.getContext('2d');ctx.font='bold 90px sans-serif';ctx.textAlign='center';ctx.fillStyle=system.kind==='low'?'#ffa28c':'#9fd7ff';ctx.fillText(system.kind==='low'?'L':'H',64,96)
    const marker=new THREE.Sprite(new THREE.SpriteMaterial({map:new THREE.CanvasTexture(canvas),depthTest:false}))
    marker.position.set((system.u-.5)*g.nx*dx,3.8,-(system.v-.5)*g.ny*dz);marker.scale.set(2.5,2.5,1);group.add(marker)
   }
  }
 }

 if(flowList.length){const field=arrowField(flowList,{moving:!prefersReducedMotion(),seed:g.nx*7+g.ny});animated.push(field.userData.uniforms);group.add(field)}
}
function reset(){if(!controls)return;const globe=view.value==='solarwind'&&data.value.solarWind&&windLayers.value.globe
 if(globe){camera.position.set(2,17,50);controls.target.set(0,7.6,0)}else{if(data.value.dust||data.value.solarWind)camera.position.set(0,25,21);else camera.position.set(26,27,31);controls.target.set(0,0,0)}controls.update()}
// The world maps of the cosmic dust and the solar wind are wide and flat: look at them from the south, from higher up;
// the solar wind's globe with its magnetosphere from the dawn side.
watch(()=>`${!!data.value.dust}|${!!data.value.solarWind}|${view.value==='solarwind'&&windLayers.value.globe}`,(key,was)=>{if(key!==was)reset()})
// Picking, hovering and the inspector: see "Surface interaction" below.
async function upload(e){error.value=''; const file=e.target.files[0]; if(!file)return;try{if(file.size>30*1024*1024)throw Error('Choose a JSON file smaller than 30 MB.');const doc=JSON.parse(await file.text());if(earthDocumentKind(doc)){earthDocument.value=doc;openEarth(null);return}loadFrames(doc);source.value=file.name}catch(err){error.value=err.message}finally{e.target.value=''}}
async function loadEngineExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}examples/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load engine example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · synthetic inputs';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
function useDemo(){loadFrames(demo());source.value='Synthetic demonstration';selected.value=null;error.value=''}
async function loadCoupledExample(){
 loading.value=true;error.value=''
 try{
  const response=await fetch(`${import.meta.env.BASE_URL}coupled/engine-timeline.json`)
  if(!response.ok)throw Error(`Could not load coupled wind example (${response.status}).`)
  loadFrames(await response.json());source.value='Engine output · coupled wind';layer.value='wind';selected.value=null
 }catch(err){error.value=err.message}finally{loading.value=false}
}
// The look of terrain (see "Realistic terrain" below): declared before the rebuild watch that depends on it.
const terrainStyle=ref({realistic:true,dataMix:35}),reliefInfo=ref(null),waveStyle=ref({on:true}),waveNote=ref(null)
watch([data,layer,vectors,terrainEnabled,exaggeration,adjustedTemperature,cloudsEnabled,systemsEnabled,cloudVolumes,frontOverlay,view,mapLayers,waterLayers,hazardLayers,oceanLayers,polarLayers,coastLayers,carbonLayers,solarLayers,dustLayers,windLayers,selectedPath,selectedStream,terrainStyle,waveStyle],draw,{deep:true})
watch([solarBlock,()=>solarLayers.value.window,butterflyCanvas],drawButterfly,{flush:'post'})
// ---- Surface interaction ------------------------------------------------------------------------------------------
// Hovering lights the data point under the pointer (and, in the field explorer, a line through every place with the
// same value); clicking opens the inspector, which explains the point in sentences. Hover only changes shader
// uniforms, so it never rebuilds the scene; a click that ends a drag (orbiting) is not a click.
// Lights: ambient and sky light keep the data colours true to the legend, the sun and a cool rim light model the relief.
const LIGHTS={ambient:1.3,sky:.25,sun:2.3,rim:.35}
let sunLight=null,shadowCatcher=null
// Uniform sets of the scene's animated parts (arrows, clouds, rain), advanced every frame.
let animated=[]
const motionQuery=window.matchMedia?.('(prefers-reduced-motion: reduce)'),prefersReducedMotion=()=>!!motionQuery?.matches
function addClouds(puffs,shafts){
 if(puffs.length){const mesh=cloudMesh(puffs,{sun:sunLight?.position??new THREE.Vector3(12,30,20),opacity:view.value==='field'?1:.55});if(prefersReducedMotion())mesh.userData.uniforms.uFlash.value=0;animated.push(mesh.userData.uniforms);group.add(mesh)}
 if(shafts.length){const mesh=precipitationMesh(shafts);animated.push(mesh.userData.uniforms);group.add(mesh)}
}
// ---- Realistic terrain ------------------------------------------------------------------------------------------------
// The look of the land (terrain.js, surface.js): relief exaggerated to a readable height, rock, strata, snow, ice and
// water from altitude, slope and the data, fine craggy relief on steep and high ground, shadows from a low sun.
const smoothstep=(a,b,v)=>{const t=Math.min(1,Math.max(0,(v-a)/(b-a)));return t*t*(3-2*t)}
// Per cell: elevation (m), water, ice and snow cover (0..1), from whatever the frame has: polar ice thickness, an ice
// sheet, sea ice (floes on the water), the hydrology snowpack, and the near-ground air temperature (the lapse-rate
// adjusted one where there is one): below freezing, the ground is white.
function terrainCells(elevations,waterCell){
 const d=data.value,g=grid.value,n=g.nx*g.ny
 const aElev=new Float32Array(n),aWater=new Float32Array(n),aIce=new Float32Array(n),aSnow=new Float32Array(n)
 const seaIce=d.ocean?.seaIce?.concentration,sheet=d.ocean?.iceSheet,snow=d.hydrology?.snow,polar=d.polar
 const cold=d.terrainTemperature?.values??(d.ocean||d.polar||d.coast?null:d.forecast.fields.air_temperature?.values)
 for(let i=0;i<n;i++){
  aElev[i]=elevations[i]??0
  let w=waterCell(i)?1:0
  if(w&&seaIce&&Number.isFinite(seaIce[i])){aIce[i]=seaIce[i];w=1-seaIce[i]}
  if(polar?.land[i]&&Number.isFinite(polar.thickness[i]))aIce[i]=smoothstep(5,120,polar.thickness[i])
  if(sheet?.domain[i]&&Number.isFinite(sheet.thickness[i]))aIce[i]=Math.max(aIce[i],smoothstep(5,120,sheet.thickness[i]))
  if(snow&&Number.isFinite(snow[i]))aSnow[i]=smoothstep(5,60,snow[i])
  if(cold&&Number.isFinite(cold[i]))aSnow[i]=Math.max(aSnow[i],smoothstep(273.6,270.5,cold[i]))
  aWater[i]=w
 }
 return {extras:{aElev,aWater,aIce,aSnow},roughness:roughness({elevations,nx:g.nx,ny:g.ny,water:aWater,ice:aIce,channel:d.hydrology?.network?.channel})}
}
// ---- Waves ----------------------------------------------------------------------------------------------------------
// Open water of a frame: the coastal, polar, ocean or carbon sea, the storm runs' land mask, or terrain below sea level.
function seaCellsOf(d){
 const g=d.forecast.grid,n=g.nx*g.ny,out=new Uint8Array(n)
 if(d.coast)for(let i=0;i<n;i++)out[i]=d.coast.sea[i]||(view.value==='coast'&&coastLostAt(i)>=1)?1:0
 else if(d.polar)for(let i=0;i<n;i++)out[i]=d.polar.land[i]?0:1
 else if(d.ocean)for(let i=0;i<n;i++)out[i]=d.ocean.mask[i]?1:0
 else if(d.carbon)for(let i=0;i<n;i++)out[i]=d.carbon.mask[i]?1:0
 else if(d.surface?.land)for(let i=0;i<n;i++)out[i]=d.surface.land[i]?0:1
 else if(d.terrain)for(let i=0;i<n;i++)out[i]=d.terrain.values[i]<0?1:0
 else return null
 return out.some(Boolean)?out:null
}
// The wind sea of the frame (waves.js): from the 10 m wind, or a tropical cyclone's wind where it is stronger.
const waveInfo=computed(()=>{
 const d=data.value,g=grid.value,sea=seaCellsOf(d)
 if(!sea)return null
 const f=d.forecast.fields,terrain=d.terrain?.values,storms=d.storms?.list?.length?d.storms.list:null
 const depth=terrain?Array.from(terrain,(e,i)=>sea[i]?(e<0?-e:1):Infinity):null
 return {sea,...waveField({sea,nx:g.nx,ny:g.ny,dxm:g.spacing_east_m,dym:g.spacing_north_m,originEast:g.origin_east_m,originNorth:g.origin_north_m,u:f.wind_east?.values,v:f.wind_north?.values,
  depth,ice:d.ocean?.seaIce?.concentration??null,storms,stormSpeed:storms?d.storms.env.storm_wind_speed_m_s:null,northernHemisphere:(g.geographic_anchor_latitude_deg??1)>=0})}
})
// A lower sun from the west-south-west with soft shadows for terrain: slopes facing the viewer and away from it both
// read, and the shadows of ridges fall into the valleys. Data views keep the high, even light.
function setTerrainLighting(on){
 if(!sunLight)return
 sunLight.castShadow=on;shadowCatcher.visible=on
 if(on){sunLight.position.set(-30,19,10);sunLight.intensity=LIGHTS.sun*1.2}else{sunLight.position.set(12,30,20);sunLight.intensity=LIGHTS.sun}
}
let surfaceUniforms=null,surfaceInfo=null
const hoverCell=ref(null),hoverAt=ref(null),inspecting=ref(false),inspectAnchor=ref(null),viewportSize=ref({width:0,height:0})
const pointer={x:0,y:0,inside:false,moved:false,down:null,frames:0}
const raycaster=new THREE.Raycaster(),ndc=new THREE.Vector2()
// The floor: a soft pool of light under the scene instead of grid lines.
function softFloor(){
 const canvas=document.createElement('canvas');canvas.width=canvas.height=256
 const ctx=canvas.getContext('2d'),grad=ctx.createRadialGradient(128,128,0,128,128,128)
 grad.addColorStop(0,'rgba(64,120,140,.34)');grad.addColorStop(.45,'rgba(30,66,82,.18)');grad.addColorStop(1,'rgba(9,21,29,0)')
 ctx.fillStyle=grad;ctx.fillRect(0,0,256,256)
 const floor=new THREE.Mesh(new THREE.CircleGeometry(48,96),new THREE.MeshBasicMaterial({map:new THREE.CanvasTexture(canvas),transparent:true,depthWrite:false}))
 floor.rotation.x=-Math.PI/2;floor.position.y=-.06;floor.renderOrder=-1
 return floor
}
function cellAtPointer(x,y){
 if(!renderer||!surfaceInfo||!cells.length)return null
 const el=renderer.domElement;ndc.set(x/el.clientWidth*2-1,-(y/el.clientHeight)*2+1);raycaster.setFromCamera(ndc,camera)
 const hit=raycaster.intersectObjects(cells,false)[0]
 return hit?.uv?cellAtUv(hit.uv.x,hit.uv.y,surfaceInfo.nx,surfaceInfo.ny):null
}
function onPointerMove(e){const r=renderer.domElement.getBoundingClientRect();pointer.x=e.clientX-r.left;pointer.y=e.clientY-r.top;pointer.inside=true;pointer.moved=true}
function onPointerLeave(){pointer.inside=false;hoverCell.value=null;hoverAt.value=null}
function onPointerDown(e){pointer.down={x:e.clientX,y:e.clientY};hoverAt.value=null}
function onPointerUp(e){
 const d=pointer.down;pointer.down=null
 if(!d||e.button!==0||Math.hypot(e.clientX-d.x,e.clientY-d.y)>5)return
 const r=renderer.domElement.getBoundingClientRect(),i=cellAtPointer(e.clientX-r.left,e.clientY-r.top)
 if(i===null){inspecting.value=false;selected.value=null;return}
 selected.value=i;inspecting.value=true
}
// The 2D fallback: its cells are buttons, the inspector anchors at the clicked button.
function openInspector(i,e){
 selected.value=i;inspecting.value=true
 const b=e.currentTarget.getBoundingClientRect(),v=host.value.getBoundingClientRect()
 inspectAnchor.value={x:b.left+b.width/2-v.left,y:b.top+b.height/2-v.top,visible:true}
}
function closeInspector(){inspecting.value=false}
watch(selected,v=>{if(v===null)inspecting.value=false})
// Eased lighting: the hover light glides between nearby points and fades in and out; the selection ring breathes.
const glow={hover:0,x:-100,y:-100,iso:-1,isoOn:0,sel:0,sx:-100,sy:-100,time:0},tmp=new THREE.Vector3()
let lastSurfaceTick=0
function syncSurfaceUniforms(){
 const u=surfaceUniforms;if(!u)return
 u.uHoverOn.value=glow.hover;u.uHover.value.set(glow.x,glow.y);u.uIso.value=glow.iso;u.uIsoOn.value=glow.isoOn
 u.uSelOn.value=glow.sel;u.uSel.value.set(glow.sx,glow.sy);u.uTime.value=glow.time
}
function animateSurface(now){
 const dt=lastSurfaceTick?Math.min(.1,(now-lastSurfaceTick)/1000):0;lastSurfaceTick=now;glow.time+=dt
 if(!prefersReducedMotion())for(const u of animated)u.uTime.value=glow.time
 // Re-pick when the pointer moved, and now and then while the camera may still be gliding under a resting pointer.
 if(pointer.inside&&!pointer.down&&(pointer.moved||++pointer.frames%8===0)){
  pointer.moved=false;const i=cellAtPointer(pointer.x,pointer.y)
  if(i!==hoverCell.value)hoverCell.value=i
  hoverAt.value=i===null?null:{x:pointer.x,y:pointer.y}
 }
 const info=surfaceInfo,h=hoverCell.value,sel=selected.value
 if(info){
  const nx=info.nx,wrap=info.wrap
  if(h!==null&&h<nx*info.ny){
   const tx=h%nx,ty=Math.floor(h/nx)
   if(glow.hover<.02||Math.abs(tx-glow.x)>3||Math.abs(ty-glow.y)>3){glow.x=tx;glow.y=ty}
   else{glow.x=approach(glow.x,tx,dt,.05,.01);glow.y=approach(glow.y,ty,dt,.05,.01)}
   const v=values.value[h],[lo,hi]=range.value
   glow.iso=v===null||v===undefined||!Number.isFinite(v)?-1:Math.max(0,Math.min(1,(v-lo)/((hi-lo)||1)))
  }
  glow.hover=approach(glow.hover,h===null?0:1,dt,.08,.003)
  glow.isoOn=approach(glow.isoOn,h!==null&&info.colouredByValue?1:0,dt,.12,.003)
  if(sel!==null&&sel<nx*info.ny){glow.sx=sel%nx;glow.sy=Math.floor(sel/nx)}
  glow.sel=approach(glow.sel,sel===null?0:1,dt,.1,.003)
  syncSurfaceUniforms()
  // The inspector's tether follows the data point as the camera moves.
  if(inspecting.value&&sel!==null&&info.anchorOf&&renderer){
   const world=info.anchorOf(sel),el=renderer.domElement
   tmp.copy(world).project(camera)
   let visible=tmp.z>-1&&tmp.z<1&&Math.abs(tmp.x)<=1.05&&Math.abs(tmp.y)<=1.05
   if(visible&&info.centre)visible=world.clone().sub(info.centre).dot(camera.position.clone().sub(world))>0
   const a={x:(tmp.x+1)/2*el.clientWidth,y:(1-tmp.y)/2*el.clientHeight,visible},b=inspectAnchor.value
   if(!b||Math.abs(b.x-a.x)>.5||Math.abs(b.y-a.y)>.5||b.visible!==a.visible)inspectAnchor.value=a
  }
 }
}
// ---- The inspector's content ------------------------------------------------------------------------------------------
// One label format per run, chosen by its length, so that a time axis reads consistently from end to end.
function frameLabel(f,span=f.forecast.elapsed_seconds){
 if(f.polar)return String(f.polar.year)
 if(f.coast)return String(f.coast.year)
 const t=f.forecast.elapsed_seconds
 if(f.solar)return new Date(Date.parse(f.forecast.start_time_utc)+t*1000).toISOString().slice(0,7)
 if(span<7200)return `+${Math.round(t/60)} min`
 if(span<4*86400)return `+${Math.round(t/3600)} h`
 return new Date(Date.parse(f.forecast.start_time_utc)+t*1000).toISOString().slice(0,10)
}
const cellColourCss=(d,i)=>{const vals=values.value;return `#${(view.value!=='field'?mapCellColor(d,i):fieldColor(vals[i]??null)).getHexString()}`}
// The value at the point over the run: depends on the point and the layer, not on the frame shown.
const inspectSeries=computed(()=>{
 const i=selected.value;if(!inspecting.value||i===null)return []
 const frames=visibleFrames.value,span=frames.at(-1)?.forecast.elapsed_seconds??0
 return frames.map(f=>{let v=null;try{v=layerValuesFor(f,layer.value)[i]??null}catch{v=null};return {value:v,label:frameLabel(f,span)}})
})
// Layers whose values are classes, with their names (and colours, and what they mean where known).
function categoriesOf(key){
 if(key==='clouds')return CLOUD_TYPES.map(t=>({label:t.label,color:t.color,note:CLOUD_FORMS[t.key]?`In the scene: ${CLOUD_FORMS[t.key].note}, at ${CLOUD_FORMS[t.key].level==='deep'?'all levels, from low to the top of the weather':`${CLOUD_FORMS[t.key].level} level`}.`:t.key==='none'?'No cloud over this point.':undefined}))
 if(key==='habitat')return HABITAT_CLASSES.map(h=>({label:h.label,color:h.color,note:h.tooltip}))
 if(key==='windregion')return REGIONS.map(r=>({label:r.label,color:r.color}))
 return null
}
const domainOf=d=>d.solar?'the disk':d.solarWind||d.dust?'the world':'the map'
const ALSO_PRIORITY=['temperature','rainfall','precip','pressure','humidity','cloud','terrain','surfacewater','discharge','saturation','current','oceansst','sst','surfacesalinity','seaice','bottomo2','ph','co2air','icethickness','coastlost','coastpopulation','cape','stormwind','surge']
const inspectContent=computed(()=>{
 const i=selected.value,d=data.value,g=grid.value
 if(!inspecting.value||i===null||i>=g.nx*g.ny)return null
 const def=layerDef.value,key=layer.value,vals=values.value,series=inspectSeries.value,k=frameIndex.value
 const global=!!(d.dust||d.solarWind)
 const also=ALSO_PRIORITY.filter(a=>a!==key&&LAYERS.find(l=>l.key===a)?.needs(d)).slice(0,3).map(a=>{let v=null;try{v=layerValuesFor(d,a)[i]??null}catch{v=null};return {key:a,title:LAYERS.find(l=>l.key===a).title,unit:unitFor(a),value:v}})
 const u=d.forecast.fields.wind_east?.values[i],w=d.forecast.fields.wind_north?.values[i]
 const wind=!global&&!d.solar&&Number.isFinite(u)&&Number.isFinite(w)&&key!=='wind'?windAt(u,w):null
 const wi=waveInfo.value,waves=waveStyle.value.on&&wi?.sea[i]?{hs:wi.hs[i],tp:wi.tp[i],length:wi.length[i],speed:wi.speed[i],towardDeg:(Math.atan2(wi.dirEast[i],wi.dirNorth[i])*180/Math.PI+360)%360,state:seaState(wi.hs[i]),regime:wi.regime[i],fetchKm:wi.fetch[i]/1000}:null
 const content=explainCell({key,title:def?.title??key,tooltip:def?.tooltip,unit:unit.value,value:vals[i]??null,values:vals,i,grid:g,
  series,previous:k>0?series[k-1]?.value:null,previousLabel:k>0?series[k-1]?.label:null,wind,also,where:whereWords(g,i,{geographic:global}),categories:categoriesOf(key),domain:domainOf(d),waves})
 const x=i%g.nx,y=Math.floor(i/g.nx),near=[]
 for(let r=2;r>=-2;r--)for(let c=-2;c<=2;c++){const xx=x+c,yy=y+r;near.push(xx<0||yy<0||xx>=g.nx||yy>=g.ny?null:cellColourCss(d,yy*g.nx+xx))}
 const [lo,hi]=range.value,byValue=view.value==='field'&&key!=='clouds'
 return {content,wind,near,stops:byValue?[0,.2,.4,.6,.8,1].map(t=>`#${fieldColor(lo+t*(hi-lo)).getHexString()}`):null,typeKey:`${i}|${key}|${view.value}`}
})
// Free room for the inspector: the viewport, minus the fullscreen panels.
const inspectBounds=computed(()=>{
 const {width,height}=viewportSize.value
 if(!immersive.value)return {left:0,top:0,right:width,bottom:height}
 const l=hud.value;return {left:l.left+l.gap,top:l.top,right:width-l.right-l.gap,bottom:height-l.bottom-l.gap}
})
// The hover tip: the value and its meaning in a few words.
const hoverInfo=computed(()=>{
 const i=hoverCell.value,g=grid.value
 if(i===null||i>=g.nx*g.ny)return null
 const v=values.value[i]??null
 const c=explainCell({key:layer.value,title:layerDef.value?.title??layer.value,unit:unit.value,value:v,values:values.value,i,grid:null,categories:categoriesOf(layer.value),domain:domainOf(data.value)})
 return {title:layerDef.value?.title??layer.value,value:c.value,unit:c.unit,short:c.short}
})
const legendWords=computed(()=>senseOf(layer.value))
watch(selected,(v,was)=>{nextTick(()=>{if(lastRender&&lastRender.selected!==streamState.value.selected)recordRender(null)})})
// ---- Fullscreen stage -----------------------------------------------------------------------------------------------
// F switches between the page and a fullscreen stage. The stage is <main> itself: the scene fills the screen, the very
// same control panel (the aside with all its bindings) floats over the left side and the data stream over the right,
// so nothing has to be mirrored between the two modes. The browser's Fullscreen API is used when it is allowed; without
// it (e.g. in an iframe) the stage still covers the whole window.
const stage=ref(null),immersive=ref(false),hudControls=ref(true),hudStream=ref(true),hudHelp=ref(false),hudToast=ref('')
const screenSize=ref({width:window.innerWidth,height:window.innerHeight})
const hud=computed(()=>hudLayout({...screenSize.value,controls:hudControls.value,stream:hudStream.value,timeline:snapshots.value.length>1}))
const stageStyle=computed(()=>immersive.value?hudStyle(hud.value):null)
const returning=ref(false)
const stageClass=computed(()=>immersive.value?{immersive:true,'hud-no-controls':!hudControls.value,'hud-no-stream':!hudStream.value,'hud-timeline':snapshots.value.length>1,'hud-drawers':!hud.value.docked}:{'stage-return':returning.value})
let savedScroll=0,toastTimer=null,returnTimer=null,panelsSet=false
// The browser's fullscreen switches asynchronously (on some systems with a long animation) and its promises settle
// before its events arrive, so quick F presses can overtake it. syncNative() moves the browser towards the wanted state
// (the stage open or not), one switch at a time, and is called again whenever a switch completes. Every exit it makes
// is counted: each successful exit yields exactly one "left fullscreen" event, so such an event closes the stage only
// when none of ours is outstanding, i.e. when the user left through the browser (its Esc handling or its UI).
let requestPending=false,exitPending=false,ourExits=0
function syncNative(){
 const el=stage.value,current=document.fullscreenElement
 if(immersive.value){
  if(current||requestPending||!el?.requestFullscreen)return
  requestPending=true
  el.requestFullscreen({navigationUI:'hide'}).catch(()=>{}).finally(()=>{requestPending=false;if(!immersive.value)syncNative()})
 }else if(current&&!exitPending&&document.exitFullscreen){
  exitPending=true;ourExits++
  document.exitFullscreen().catch(()=>{ourExits=Math.max(0,ourExits-1)}).finally(()=>{exitPending=false})
 }
}
function toast(text){hudToast.value=text;clearTimeout(toastTimer);toastTimer=setTimeout(()=>{hudToast.value=''},2800)}
function enterImmersive(){
 if(immersive.value||mode.value!=='observatory')return
 savedScroll=window.scrollY
 // The panels start from the screen's defaults once; later entries keep the user's own panel setup (one drawer at a time
 // when the screen is narrow).
 if(!panelsSet){const panels=defaultPanels(window.innerWidth);hudControls.value=panels.controls;hudStream.value=panels.stream;panelsSet=true}
 else if(!hud.value.docked&&hudControls.value&&hudStream.value)hudStream.value=false
 hudHelp.value=false
 immersive.value=true;document.documentElement.classList.add('tf-immersive')
 const el=stage.value
 // Focus the stage, not the button that was clicked: a focused button would take Space (play) for itself.
 el?.focus({preventScroll:true})
 syncNative()
 toast('FULLSCREEN · F OR ESC TO RETURN · ? FOR ALL KEYS')
}
function exitImmersive(){
 if(!immersive.value)return
 immersive.value=false;hudHelp.value=false;hudToast.value='';document.documentElement.classList.remove('tf-immersive')
 if(stage.value?.contains(document.activeElement))document.activeElement.blur()
 syncNative()
 returning.value=true;clearTimeout(returnTimer);returnTimer=setTimeout(()=>{returning.value=false},700)
 nextTick(()=>window.scrollTo({top:savedScroll}))
}
function toggleImmersive(){if(immersive.value)exitImmersive();else enterImmersive()}
function togglePanel(which){if(which==='controls')hudControls.value=!hudControls.value;else hudStream.value=!hudStream.value}
// On narrow screens the panels are drawers over the scene: opening one closes the other.
watch(hudControls,on=>{if(on&&immersive.value&&!hud.value.docked)hudStream.value=false})
watch(hudStream,on=>{if(on&&immersive.value&&!hud.value.docked)hudControls.value=false})
function onKeydown(e){
 if(mode.value!=='observatory')return
 if(e.key==='Escape'&&inspecting.value&&!isEditable(e.target)&&!hudHelp.value){e.preventDefault();closeInspector();return}
 const action=shortcutAction(e,{immersive:immersive.value});if(!action)return
 e.preventDefault()
 if(action==='toggle')toggleImmersive()
 else if(action==='exit'){if(hudHelp.value)hudHelp.value=false;else exitImmersive()}
 else if(action==='controls'||action==='stream')togglePanel(action)
 else if(action==='help')hudHelp.value=!hudHelp.value
 else if(action==='reset')reset()
 else if(action==='play'){if(snapshots.value.length>1)togglePlayback()}
 else if(action==='previous'){if(frameIndex.value>0)seek(frameIndex.value-1)}
 else if(action==='next'){if(frameIndex.value<snapshots.value.length-1)seek(frameIndex.value+1)}
}
// Leaving the browser's fullscreen through the browser also leaves the stage; the end of one of our own exits instead
// re-syncs, in case the user reopened the stage in the meantime.
function onFullscreenChange(){
 if(document.fullscreenElement){if(!immersive.value)syncNative();return}
 if(ourExits>0){ourExits--;if(immersive.value)syncNative();return}
 if(immersive.value)exitImmersive()
}
function onWindowResize(){screenSize.value={width:window.innerWidth,height:window.innerHeight}}
watch(mode,m=>{if(m!=='observatory')exitImmersive()})
// The camera's view offset keeps the scene centred between the open panels; it eases there, so opening or closing a
// panel (or the stage itself) glides instead of jumping. Picking uses the same projection, so clicks stay exact.
// The zoom eases too, so that the scene keeps the page's framing in the free area and grows when panels close.
const viewShift={x:0,y:0,zoom:1};let lastTick=0
function applyViewOffset(now){
 const dt=lastTick?Math.min(.1,(now-lastTick)/1000):0;lastTick=now
 const target=immersive.value?sceneOffset(hud.value):{x:0,y:0},zoom=immersive.value?fitZoom(hud.value):1
 viewShift.x=approach(viewShift.x,target.x,dt);viewShift.y=approach(viewShift.y,target.y,dt)
 const z=approach(viewShift.zoom,zoom,dt,.12,.0005);if(z!==viewShift.zoom||camera.zoom!==z){viewShift.zoom=z;camera.zoom=z;camera.updateProjectionMatrix()}
 const el=renderer.domElement,w=el.clientWidth,h=el.clientHeight
 if((viewShift.x||viewShift.y)&&w&&h)camera.setViewOffset(w,h,-viewShift.x,-viewShift.y,w,h)
 else if(camera.view?.enabled)camera.clearViewOffset()
}
// ---- Data stream ----------------------------------------------------------------------------------------------------
// What the scene renders, the frame's data blocks, the pipeline numbers and one log line per scene rebuild.
const telemetry=shallowRef({fps:null,calls:null,triangles:null,objects:null,buildMs:null,resolution:null,renderer:'webgl'})
let fpsCount=0,fpsStart=0
function sampleTelemetry(now){
 fpsCount++;if(!fpsStart)fpsStart=now
 if(now-fpsStart<500)return
 const fps=Math.round(fpsCount*1000/(now-fpsStart));fpsCount=0;fpsStart=now
 if(!immersive.value)return
 const info=renderer.info.render,el=renderer.domElement
 telemetry.value={...telemetry.value,fps,calls:info.calls,triangles:info.triangles,resolution:`${el.width}×${el.height}`}
}
const layerDef=computed(()=>LAYERS.find(l=>l.key===layer.value)??null)
const streamChannels=computed(()=>frameChannels(data.value,layerDef.value))
const streamOverlays=computed(()=>{const d=data.value,v=view.value
 if(v==='field')return [...overlayChips({vectors:vectors.value,terrain:!!d.terrain&&terrainEnabled.value,lapseRate:!!d.terrainTemperature&&adjustedTemperature.value,clouds:!!d.atmosphere&&cloudsEnabled.value,systems:!!d.atmosphere&&systemsEnabled.value,cloudVolumes:!!d.clouds&&cloudVolumes.value,frontOverlay:!!d.clouds&&frontOverlay.value}),...(d.terrain&&terrainEnabled.value?[`relief ×${exaggeration.value}`]:[])]
 return overlayChips({map:mapLayers.value,water:waterLayers.value,hazards:hazardLayers.value,ocean:oceanLayers.value,polar:polarLayers.value,coast:coastLayers.value,carbon:carbonLayers.value,solar:solarLayers.value,dust:dustLayers.value,solarwind:windLayers.value}[v])})
const streamState=computed(()=>{const g=grid.value,sel=selected.value!==null&&selected.value<g.nx*g.ny?selected.value:null,def=layerDef.value
 let frameLabel='';try{frameLabel=playbackLabel.value}catch{frameLabel=''}
 return {view:view.value,viewLabel:VIEW_LABELS[view.value]??view.value,layer:layer.value,layerTitle:def?.title??layer.value,layerSubtitle:def?layerSubtitle(def,data.value):'',unit:unit.value,
  source:source.value,variant:comparisonFrames.value&&comparisonView.value==='variant'?comparisonName.value:'baseline',frame:frameIndex.value,frames:snapshots.value.length,frameLabel,validTime:time.value,
  nx:g.nx,ny:g.ny,selected:sel,selectedLabel:sel===null?'':`${sel%g.nx},${Math.floor(sel/g.nx)}`,selectedValue:sel===null?null:values.value[sel]??null,overlays:streamOverlays.value}})
// Cell colours exactly as the scene paints them (sRGB), computed only while the stream is visible.
const rgbOf=c=>{const h=c.getHex();return [h>>16&255,h>>8&255,h&255]}
const streamColors=computed(()=>{
 if(!immersive.value||!hudStream.value)return null
 const d=data.value,mapLike=view.value!=='field',n=grid.value.nx*grid.value.ny,vals=values.value
 try{return Array.from({length:n},(_,i)=>rgbOf(mapLike?mapCellColor(d,i):fieldColor(vals[i]??null)))}catch{return null}
})
// In the field explorer the colour is a function of the value, so the histogram bins wear the scene's colours; the map
// views compose several layers per cell, so there the bins stay neutral.
const streamColorFor=computed(()=>view.value==='field'&&layer.value!=='clouds'?v=>`#${fieldColor(v).getHexString()}`:null)
const streamLog=shallowRef([])
let lastRender=null
function recordRender(ms){
 let snap;try{snap=streamState.value}catch{return}
 const entries=describeChanges(lastRender,snap);lastRender=snap
 if(entries.length&&ms!==null)entries[0]={...entries[0],meta:`${ms.toFixed(1)} ms`}
 streamLog.value=appendEntries(streamLog.value,entries)
 let objects=0;group?.traverse(()=>{objects++})
 telemetry.value={...telemetry.value,buildMs:ms??telemetry.value.buildMs,objects,renderer:fallback.value?'2d':'webgl'}
}
watch(error,e=>{if(e)streamLog.value=appendEntries(streamLog.value,[{tag:'ERROR',text:e}])})
watch(loading,on=>{if(on)streamLog.value=appendEntries(streamLog.value,[{tag:'FETCH',text:'requesting dataset…'}])})
let sizeObserver=null
onMounted(()=>{sizeObserver=new ResizeObserver(()=>{const r=host.value?.getBoundingClientRect();if(r)viewportSize.value={width:r.width,height:r.height}});if(host.value)sizeObserver.observe(host.value);window.addEventListener('keydown',onKeydown);window.addEventListener('resize',onWindowResize);document.addEventListener('fullscreenchange',onFullscreenChange);nextTick(()=>{if(!lastRender)recordRender(null)})})
onBeforeUnmount(()=>{sizeObserver?.disconnect();window.removeEventListener('keydown',onKeydown);window.removeEventListener('resize',onWindowResize);document.removeEventListener('fullscreenchange',onFullscreenChange);clearTimeout(toastTimer);clearTimeout(returnTimer);document.documentElement.classList.remove('tf-immersive')})
onMounted(()=>{try{scene=new THREE.Scene();scene.background=new THREE.Color('#09151d');scene.fog=new THREE.Fog('#09151d',65,110);camera=new THREE.PerspectiveCamera(42,1,.1,200);renderer=new THREE.WebGLRenderer({antialias:true});renderer.shadowMap.enabled=true;renderer.shadowMap.type=THREE.PCFSoftShadowMap;renderer.setPixelRatio(Math.min(devicePixelRatio,2));host.value.appendChild(renderer.domElement);scene.add(new THREE.AmbientLight(0xd6efff,LIGHTS.ambient));sunLight=new THREE.DirectionalLight(0xffffff,LIGHTS.sun);sunLight.position.set(12,30,20);const sh=sunLight.shadow;sh.mapSize.set(2048,2048);Object.assign(sh.camera,{left:-26,right:26,top:26,bottom:-26,near:1,far:110});sh.bias=-.0004;sh.normalBias=.035;sh.radius=3;scene.add(sunLight);scene.add(sunLight.target);shadowCatcher=new THREE.Mesh(new THREE.PlaneGeometry(90,90),new THREE.ShadowMaterial({opacity:.42}));shadowCatcher.rotation.x=-Math.PI/2;shadowCatcher.position.y=-.04;shadowCatcher.receiveShadow=true;shadowCatcher.visible=false;scene.add(shadowCatcher);scene.add(softFloor());scene.add(new THREE.HemisphereLight(0xdff3ff,0x0b1820,LIGHTS.sky));const rim=new THREE.DirectionalLight(0x7fe0ff,LIGHTS.rim);rim.position.set(-22,14,-26);scene.add(rim);controls=new OrbitControls(camera,renderer.domElement);controls.enableDamping=true;controls.maxPolarAngle=Math.PI/2.05;controls.minDistance=8;controls.maxDistance=85;reset();const el=renderer.domElement;el.addEventListener('pointermove',onPointerMove);el.addEventListener('pointerleave',onPointerLeave);el.addEventListener('pointerdown',onPointerDown);el.addEventListener('pointerup',onPointerUp);observer=new ResizeObserver(()=>{const {width,height}=host.value.getBoundingClientRect();renderer.setSize(width,height);camera.aspect=width/height;camera.updateProjectionMatrix()});observer.observe(host.value);draw();const tick=now=>{frame=requestAnimationFrame(tick);controls.update();applyViewOffset(now??performance.now());animateSurface(now??performance.now());renderer.render(scene,camera);sampleTelemetry(now??performance.now())};tick()}catch(err){fallback.value=true;error.value='WebGL unavailable in this browser. Showing an interactive 2D field instead.'}})
onBeforeUnmount(()=>{stop();cancelAnimationFrame(frame);observer?.disconnect();controls?.dispose();disposeGroup();scene?.traverse(o=>{o.geometry?.dispose();o.material?.dispose()});renderer?.dispose()})
</script>
<template>
 <div class="shell">
  <header><a class="brand" href="#"><span class="mark">T</span><span>TERRAFLUX<small>PHYSICS OBSERVATORY</small></span></a><nav class="mode-switch" aria-label="Workspace"><button :class="{selected:mode==='observatory'}" @click="mode='observatory'">Observatory</button><button :class="{selected:mode==='earth'}" title="The coupled climate (drivers, CO₂) and the coupled 3–10 day forecast (weather, wind, severe weather)" @click="openEarth(null)">Coupled Earth</button></nav><span class="status"><i></i> LOCAL WORKSPACE</span><label class="upload">↥ Open JSON<input type="file" accept=".json,application/json" @change="upload"></label></header>
  <CoupledEarth v-if="earthOpened" v-show="mode==='earth'" :document="earthDocument" :goto="earthGoto"/>
  <main v-show="mode==='observatory'" ref="stage" tabindex="-1" :class="stageClass" :style="stageStyle"><aside class="control-panel" :class="{'hud-panel':immersive,'hud-collapsed':immersive&&!hudControls}" :inert="immersive&&!hudControls?true:null"><div v-if="immersive" class="hud-panel-head"><span class="stream-title">CONTROL DECK</span><span class="stream-status">{{availableLayerCount}} LAYERS</span><button class="hud-icon" title="Hide the control panel ( [ )" aria-label="Hide the control panel" @click="hudControls=false">[</button></div><div class="eyebrow">EXPERIMENT / 001</div><h1>Weather,<br>in perspective.</h1><p class="intro">Explore the fields behind the physics. Inspect temperature, wind and accumulated rainfall on a shared grid.</p>
   <section class="layer-groups"><div class="section-label">01 / FIELD LAYERS</div><label class="toggle layer-filter" title="Hide the layers (and groups) the loaded data cannot show."><input type="checkbox" v-model="onlyAvailable"> Only layers with data ({{availableLayerCount}} of {{LAYERS.length}})</label><details v-for="g in layerGroups" :key="g.id" class="layer-group" :open="openGroups.includes(g.id)" @toggle="onGroupToggle(g.id,$event)"><summary :title="g.tooltip"><span>{{g.title}}</span><small>{{g.available}}/{{g.layers.length}}{{g.active?' · ●':''}}</small></summary><button v-for="l in g.shown" :key="l.key" class="layer" :class="{active:layer===l.key}" :disabled="!l.available" :title="l.tooltip" @click="layer=l.key"><span>{{l.title}}<small>{{l.subtitle}}</small></span><span>{{layer===l.key?'●':'○'}}</span></button></details><template v-if="data.atmosphere"><label class="toggle"><input type="checkbox" v-model="cloudsEnabled"> Cloud overlay</label><label class="toggle"><input type="checkbox" v-model="systemsEnabled"> Weather-system markers</label><p class="hint">Synthetic pressure, clouds and front. Cloud height is schematic; these fields do not drive the local physics.</p></template><p v-if="data.coupled" class="hint">Coupled engine run: wind from the static pressure field, Coriolis and friction; density diagnosed from temperature and humidity feeds back into the wind; temperature and humidity are carried by the wind with configurable diffusion.<br>Diffusion T {{data.coupled.request.diffusion.temperature_m2_s}} · q {{data.coupled.request.diffusion.humidity_m2_s}} · momentum {{data.coupled.request.diffusion.momentum_m2_s}} m²/s · friction {{data.coupled.request.atmosphere.surface_friction_coefficient_s}} 1/s · Coriolis {{data.coupled.request.atmosphere.coriolis_mode}} · density {{data.coupled.request.density_mode}}<template v-if="data.coupled.columns"><br>Vertical columns: hydrostatic layers, convective adjustment and a lifted surface parcel give cloud base, equilibrium level and CAPE per cell. Diagnostic only; no surface fluxes resupply heat or moisture.</template></p><template v-if="data.clouds"><label class="toggle"><input type="checkbox" v-model="cloudVolumes"> Cloud volumes (base → top)</label><label class="toggle"><input type="checkbox" v-model="frontOverlay"> Front overlay (blue cold · red warm · purple stationary)</label><p class="hint">{{data.clouds.synthetic?'Synthetic mock in the documented cloud-formation format; illustration only.':'Engine diagnosis: fronts from the temperature gradient, advection and convergence; convection from the heated surface parcel; combined where frontal lifting releases it.'}}<br>Heating now {{data.clouds.heating.toFixed(1)}} K · lifting over {{(data.clouds.config.lifting_time_scale_s/3600).toFixed(0)}} h · warm slope 1:{{(1/data.clouds.config.warm_front_slope).toFixed(0)}} · cold slope 1:{{(1/data.clouds.config.cold_front_slope).toFixed(0)}}</p></template><label v-if="!data.dust&&!data.solar&&!data.solarWind" class="toggle"><input type="checkbox" v-model="vectors"> Wind direction vectors</label><label v-if="waveInfo" class="toggle" title="Waves on the open water, estimated from the wind (fetch-limited growth, depth and sea ice), drawn in 3D and animated in their direction and at their speed."><input type="checkbox" v-model="waveStyle.on"> Waves in 3D (estimated from the wind)</label><template v-if="data.terrain"><label class="toggle"><input type="checkbox" v-model="terrainEnabled"> Place fields on terrain</label><label class="toggle">Height scale {{exaggeration}}× <input aria-label="Terrain vertical exaggeration" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label><label class="toggle" title="Draw the land as land: meadow and forest, bare rock in strata on steep slopes, snow above the snow line, glacier and sea ice, water, with shadows from a low sun."><input type="checkbox" v-model="terrainStyle.realistic"> Realistic terrain (rock, snow, ice, water, shadows)</label><label v-if="terrainStyle.realistic" class="toggle" title="How strongly the colours of the selected data are laid over the realistic terrain.">Data colours over the terrain {{terrainStyle.dataMix}} % <input aria-label="Data colours over the terrain" type="range" min="0" max="100" step="5" v-model.number="terrainStyle.dataMix"></label><label v-if="data.terrainTemperature" class="toggle"><input type="checkbox" v-model="adjustedTemperature"> Terrain-adjusted temperature</label><p v-if="data.terrainTemperature" class="hint">Lapse rate {{data.terrainTemperature.lapse_rate_k_per_m*1000}} K/km · reference {{data.terrainTemperature.reference_elevation_m}} m<br>Diagnostic adjustment; no feedback into transport.</p><p class="hint">{{data.terrain.source_id}} · {{data.terrain.vertical_reference}}<br>Geometry is a display layer; lapse-rate adjustment is optional.</p></template></section>
   <section v-if="view==='map'" class="map-layers"><div class="section-label">01b / WEATHER MAP LAYERS</div><p class="hint">Switch layers on and off to compose the map. Engine or labelled mock output only; no rendering of external map tiles yet.</p>
    <label class="toggle">Base field <select v-model="mapLayers.base"><option value="temperature">Air temperature</option><option value="pressure">Surface pressure</option><option v-if="data.terrain" value="terrain">Terrain (hillshade)</option><option value="none">None (dark)</option></select></label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.relief" :disabled="!data.terrain"> Relief (3D terrain, height scale {{exaggeration}}×)</label>
    <label v-if="data.terrain" class="toggle">Height scale <input aria-label="Map relief exaggeration" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.lifting" :disabled="!data.orography"> Orographic lifting (blue up-slope · orange descent)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.foehn" :disabled="!data.orography"> Foehn cells (F)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.coldPools" :disabled="!data.orography"> Cold-air pools (❄)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.oroPrecip" :disabled="!data.orography"> Orographic precipitation (●)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.blocking" :disabled="!data.orography"> Blocked flow (Froude)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.isobars"> Isobars every <select v-model.number="mapLayers.isobarIntervalHpa"><option :value="1">1</option><option :value="2">2</option><option :value="4">4</option></select> hPa</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.fronts" :disabled="!data.clouds"> Fronts (cold blue · warm red · stationary purple)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.clouds" :disabled="!data.clouds"> Cloud cover by genus</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.volumes" :disabled="!data.clouds"> Cloud volumes (3D base → top)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.precipitation" :disabled="!data.precipitation"> Precipitation rate shading</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.symbols" :disabled="!data.precipitation"> Precipitation symbols (● rain ▽ showers ✱ snow ▲ sleet ’ drizzle)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.hazards" :disabled="!data.precipitation"> Thunderstorm ⚡ and hail ◆ markers</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.wind"> Wind vectors</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.landSea" :disabled="!data.surface"> Land / sea</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.storms" :disabled="!data.storms"> Tropical cyclones (centres, category, tracks, ✖ landfall ⊕ merger ✦ genesis)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.stormWind" :disabled="!data.storms"> Storm wind field (category colours)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.surge" :disabled="!data.storms"> Storm surge on the coast</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.tornado" :disabled="!data.severe"> Tornado risk (STP class) and 🌪 where possible</label>
    <p v-if="!data.clouds" class="hint">Load the cloud formation example or mock for fronts, clouds and precipitation layers.</p>
   </section>
   <section v-if="view==='water'" class="map-layers"><div class="section-label">01c / CATCHMENT LAYERS</div><p class="hint">Where the water goes: rivers sized by upstream area and coloured by discharge, lakes at their level, snow, overland water and rain over the relief. Click a cell to trace its water to the outlet.</p>
    <label class="toggle">Base <select v-model="waterLayers.base"><option value="terrain">Terrain (hillshade)</option><option value="basins">Drainage basins</option><option value="groundwater">Water-table depth (dark = deep)</option><option value="saturation">Soil saturation</option></select></label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.relief"> Relief (3D terrain, height scale {{exaggeration}}×)</label>
    <label class="toggle">Height scale <input aria-label="Catchment relief exaggeration" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label>
    <label class="toggle"><input type="checkbox" v-model="waterLayers.rivers"> River network (width ∝ upstream area · brightness ∝ discharge)</label>
    <label class="toggle"><input type="checkbox" v-model="waterLayers.lakes"> Lakes (depth above the lake floor)</label>
    <label class="toggle"><input type="checkbox" v-model="waterLayers.labels"> Lake labels with level</label>
    <label class="toggle"><input type="checkbox" v-model="waterLayers.surface"> Overland / floodplain water</label>
    <label class="toggle"><input type="checkbox" v-model="waterLayers.snow"> Snowpack</label>
    <label class="toggle"><input type="checkbox" v-model="waterLayers.rain"> Rainfall rate tint</label>
    <label class="toggle"><input type="checkbox" v-model="waterLayers.arrows"> Flow direction arrows (D8)</label>
   </section>
   <section v-if="view==='hazards'" class="map-layers"><div class="section-label">01d / HAZARD LAYERS</div><p class="hint">What massive rain does: flood depth × velocity classes, channels above bankfull in orange, erosion (red) and deposition (tan), landslides ▲ with debris runout, rainfall intensity–duration warnings (!) and slopes still unstable.</p>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.relief"> Relief (3D terrain)</label>
    <label class="toggle"><input type="checkbox" v-model="hazardLayers.flood"> Flooding (<span v-for="c in FLOOD_CLASSES.slice(1)" :key="c.key" :style="{color:c.color}">■ {{c.key}} </span>)</label>
    <label class="toggle"><input type="checkbox" v-model="hazardLayers.sediment"> Erosion / deposition</label>
    <label class="toggle"><input type="checkbox" v-model="hazardLayers.landslides"> Landslides ▲ (up to the current time)</label>
    <label class="toggle"><input type="checkbox" v-model="hazardLayers.runout"> Debris runout paths</label>
    <label class="toggle"><input type="checkbox" v-model="hazardLayers.unstable"> Unstable slopes (failed, not yet drained)</label>
    <label class="toggle"><input type="checkbox" v-model="hazardLayers.threshold"> Rainfall intensity–duration threshold exceeded (!)</label>
    <label class="toggle"><input type="checkbox" v-model="hazardLayers.stability"> Factor of safety shading (red < 1 · orange < 1.3 · yellow < 2 · green)</label>
   </section>
   <section v-if="view==='ocean'" class="map-layers"><div class="section-label">01e / OCEAN STREAM LAYERS</div><p class="hint">The streams (core streamlines sized by transport, coloured by speed), the current field, the saltiness of the water, the river exchange points ⊕ with their plumes, the mixing fronts and the deep-water formation sites ⇩ where the overturning sinks. Click a cell for the local state and its stream.</p>
    <label class="toggle">Water colour <select v-model="oceanLayers.base"><option value="surface">Surface salinity (fresh ↔ salty)</option><option value="salinity">Layer salinity</option><option value="speed">Current speed</option><option value="temperature">Layer temperature</option><option value="thickness">Thermocline depth</option><option value="freshwater">Freshwater fraction</option><option v-if="data.ocean?.processes?.length" value="sst">Sea-surface temperature (run range)</option><option v-if="data.ocean?.thermal" value="mld">Mixed-layer depth (pale shallow → deep blue)</option><option v-if="data.ocean?.airSea" value="netheat">Net air–sea heat flux (blue loss ↔ red gain)</option><option v-if="data.ocean?.airSea" value="evaporation">Evaporation (dark → yellow 15 mm/day)</option><option v-if="data.ocean?.oxygen" value="oxygen">Surface oxygen capacity (purple low ↔ green high)</option><option value="none">None (deep blue)</option></select></label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.streams"> Streams (core streamlines)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.arrows"> Current arrows (length ∝ speed)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.rivers"> River mouths ⊕ (exchange points)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.plumes"> Freshwater plumes (pale tint)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.fronts"> Mixing fronts (white)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.formation"> Deep-water formation ⇩ (purple)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.sponge"> Open-boundary sponge rows</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.labels"> Labels (stream names, river discharge and salinity)</label>
    <template v-if="data.ocean?.processes?.length"><p class="hint">Rain, evaporation and heat: rain tints sea and land by rate; the coastal band (tan) drains straight to the sea (blue arrows, length ∝ water still stored on land), land beyond it is left to the rivers. Lenses are fresh surface water by source. The yellow ring marks the water column shown below the map.</p>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.rain" :disabled="!data.ocean.rain"> Rain on sea and land (rate tint)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.coast" :disabled="!data.ocean.rain"> Coastal band draining directly to the sea (tan)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.runoff" :disabled="!data.ocean.rain"> Coastal runoff → sea (arrows, stored water on land)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.lenses"> Surface lens labels (rain · coastal runoff · river · glacial)</label></template>
    <template v-if="data.ocean?.polar"><p class="hint">Polar processes: sea ice drawn white over the water (opacity = concentration, brightness = thickness), the coupled ice sheet white on land (reddish where it melts faster than it snows), the yellow ring where extra meltwater is hosed into the sea.</p>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.seaIce" :disabled="!data.ocean.seaIce"> Sea ice (white, concentration)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.iceDrift" :disabled="!data.ocean.seaIce"> Sea-ice drift (white arrows: current + 2 % of the wind)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.iceSheet" :disabled="!data.ocean.iceSheet"> Ice sheet on land (thickness · melting margin)</label>
    <label class="toggle"><input type="checkbox" v-model="oceanLayers.hosing" :disabled="!data.ocean.forcing?.hosing"> Meltwater hosing region (yellow ring)</label></template>
   </section>
   <section v-if="view==='polar'&&data.polar" class="map-layers"><div class="section-label">01f / POLAR MELT LAYERS</div><p class="hint">Greenland of the “{{data.polar.block.map_scenario}}” scenario in {{data.polar.year}}: relief of bed + ice (height scale {{exaggeration}}×). The charts and the table below compare all scenarios; choose which one the metrics, the freshwater chart and the equilibrium diagram follow.</p>
    <label class="toggle">Ice colour <select v-model="polarLayers.base"><option value="thickness">Ice thickness (pale thin → white thick)</option><option value="smb">Surface mass balance (red melt ↔ blue snow)</option><option value="melt">Surface melt</option><option value="temperature">Surface air temperature (annual)</option></select></label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.relief"> Relief (bed + ice)</label>
    <label class="toggle">Height scale <input aria-label="Polar relief exaggeration" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label>
    <label class="toggle">Scenario in focus <select v-model="polarLayers.scenario"><option value="">{{data.polar.block.map_scenario}} (map)</option><option v-for="s in data.polar.block.scenarios" :key="s.name" :value="s.name">{{s.name}}</option></select></label>
   </section>
   <section v-if="view==='coast'&&data.coast" class="map-layers"><div class="section-label">01g / SEA LEVEL &amp; COAST LAYERS</div><p class="hint">The coast in {{data.coast.year}} under “{{coastScenario}}”: <span style="color:#ff8a8a">red</span> land lost to the sea (permanently under high water or eroded), <span style="color:#8fd3ff">blue</span> flooded by today's 100-year storm on the risen sea, <span style="color:#ffb867">orange</span> below high water but kept dry by defences, black defences, blue rivers (white: salt water). {{coastIsMap?'Depths from the engine maps.':'Other scenarios show when each cell is first lost or flooded (the depth maps are for '+data.coast.block.map_scenario+').'}}</p>
    <label class="toggle" title="The colour of the land (the sea is coloured by depth).">Land colour <select v-model="coastLayers.base"><option value="terrain">Elevation (hillshaded)</option><option value="year">Year the land is lost (red soon → yellow late)</option><option value="frequency">Today's 100-year flood: how often now (red yearly → green rare)</option><option value="population">Population density</option><option value="subsidence">Land subsidence</option><option value="setup">Sea: wind setup of the design storm</option></select></label>
    <label class="toggle" title="Which scenario the metrics, tables, charts and first-year maps follow.">Scenario in focus <select v-model="coastLayers.scenario"><option value="">{{data.coast.block.map_scenario}} (maps)</option><option v-for="x in data.coast.block.scenarios" :key="x.name" :value="x.name">{{x.name}}</option></select></label>
    <label class="toggle" title="Land under the daily high water of the risen sea (reached by the sea) or eroded from sandy shores by the Bruun rule."><input type="checkbox" v-model="coastLayers.lost"> Land lost to the sea (red)</label>
    <label class="toggle" title="The 100-year storm: surge on top of high tide and the risen sea plus the wind setup, losing 0.2 m per km inland and far less along rivers."><input type="checkbox" v-model="coastLayers.storm"> Flooded by the 100-year storm (blue by depth)</label>
    <label class="toggle" title="Land lying below the daily high water that only the dikes keep dry: it depends on the defences."><input type="checkbox" v-model="coastLayers.protected"> Below high water behind defences (orange)</label>
    <label class="toggle" title="Dikes and flood walls, drawn at their crest height; they hold until the water overtops them."><input type="checkbox" v-model="coastLayers.defences"> Defences (black, raised)</label>
    <label class="toggle" title="River channels; white where the water is salty (salt intrusion from the sea)."><input type="checkbox" v-model="coastLayers.rivers"> Rivers and salt intrusion</label>
    <label class="toggle" title="The onshore wind of the design storm that piles water up over the shallow shelf."><input type="checkbox" v-model="coastLayers.wind"> Design-storm wind</label>
    <label class="toggle"><input type="checkbox" v-model="coastLayers.labels"> Labels (regions, rivers)</label>
    <label class="toggle">Height scale <input aria-label="Coast relief exaggeration" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label>
   </section>
   <section v-if="view==='carbon'&&data.carbon" class="map-layers"><div class="section-label">01h / CARBON &amp; OCEAN LIFE LAYERS</div><p class="hint">{{data.carbon.year.toFixed(2)}}: the sea coloured by the chosen quantity, the land by the CO₂ in the air above it (orange–red above the {{data.carbon.background.toFixed(0)}} ppm background, green where plants draw it down). <span style="color:#ff6b60">Red outlines</span> mark dead zones (hypoxic bottom water), ☠ anoxic columns, <span style="color:#ff7be0">magenta outlines</span> corrosive water (Ω &lt; 1). The yellow ring is the water column shown below the map; click a sea cell to move it.</p>
    <label class="toggle" title="What colours the sea.">Sea colour <select v-model="carbonLayers.base"><option v-for="b in CARBON_BASES" :key="b.key" :value="b.key">{{b.label}}</option></select></label>
    <label v-if="carbonLayers.base==='depth'" class="toggle" title="The depth level whose oxygen colours the sea (grey: below the seafloor).">Depth <select v-model.number="carbonLayers.level"><option v-for="(z,k) in data.carbon.levels" :key="k" :value="k">{{data.carbon.tops[k]}}–{{z}} m</option></select></label>
    <label class="toggle" title="The CO₂ of the boundary-layer air over land: cities and industry raise it, the wind carries the plumes, vegetation draws it down in daylight."><input type="checkbox" v-model="carbonLayers.air"> CO₂ in the air over land</label>
    <label class="toggle" title="Cells whose bottom water is hypoxic (below 60 µmol/kg, about 2 mg/L) or whose column is hypoxic over a fifth of its depth."><input type="checkbox" v-model="carbonLayers.dead"> Dead zones (red outline)</label>
    <label class="toggle" title="Columns with practically no oxygen left (below 5 µmol/kg)."><input type="checkbox" v-model="carbonLayers.anoxic"> Anoxic columns ☠</label>
    <label class="toggle" title="Water undersaturated in aragonite (Ω &lt; 1) at the surface or at the bottom: shells and corals dissolve."><input type="checkbox" v-model="carbonLayers.corrosive"> Corrosive water (magenta outline)</label>
    <label class="toggle" title="Cities, industry and ports with their emission and the CO₂ above the background they cause."><input type="checkbox" v-model="carbonLayers.sources"> CO₂ sources 🏭</label>
    <label class="toggle" title="River mouths with their discharge and nitrate load: the fertiliser that feeds the blooms behind the dead zones."><input type="checkbox" v-model="carbonLayers.rivers"> River mouths ⊕ (nitrate load)</label>
    <label class="toggle" title="The near-surface wind that carries the CO₂ plumes."><input type="checkbox" v-model="carbonLayers.wind"> Wind arrows</label>
    <label class="toggle"><input type="checkbox" v-model="carbonLayers.labels"> Labels (dead zones, sources, rivers)</label>
   </section>
   <section v-if="view==='solar'&&data.solar" class="map-layers"><div class="section-label">01i / SUN &amp; SOLAR CYCLE LAYERS</div><p class="hint">The visible Sun on an illustrative day of {{solarMetrics.month}} ({{solarMetrics.source==='record'?(solarBlock.record_kind==='cycle_table'?'synthetic record':'record'):'forecast median'}}): spot groups at the latitudes of the cycles' spot zones, the limb-darkened photosphere and faculae around the groups. The dome is the solar hemisphere (height scale 4 = true shape); ⚡ marks the month's flares by NOAA radio-blackout level. Positions, sizes and flares are random draws consistent with the cycle, not observations.</p>
    <label class="toggle">Disk colour <select v-model="solarLayers.base"><option v-for="b in SOLAR_BASES" :key="b.key" :value="b.key">{{b.label}}</option></select></label>
    <label class="toggle" title="The window of the charts, the tables and the butterfly diagram.">Chart window <select v-model="solarLayers.window"><option value="focus">Last two cycles + forecast</option><option value="modern">Since 1950</option><option value="all">Whole record</option></select></label>
    <label class="toggle"><input type="checkbox" v-model="solarLayers.relief"> Dome (solar hemisphere)</label>
    <label class="toggle">Height scale <input aria-label="Solar dome height" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label>
    <label class="toggle" title="The month's flares of M class and above: synthetic draws from the rate model (none in forecast months)."><input type="checkbox" v-model="solarLayers.flares"> Flares ⚡ (colour: R scale)</label>
    <label class="toggle"><input type="checkbox" v-model="solarLayers.grid"> Latitude lines (equator, ±15°, ±30°)</label>
    <label class="toggle"><input type="checkbox" v-model="solarLayers.labels"> Labels (largest groups, X flares)</label>
   </section>
   <section v-if="view==='dust'&&data.dust" class="map-layers"><div class="section-label">01j / COSMIC DUST LAYERS</div><p class="hint">The world on {{dustBlock.map.date_utc}} at {{String(Math.floor(data.dust.hour)).padStart(2,'0')}}:00 UTC: the dust entering the atmosphere per square metre (as a daily rate), for the {{dustLayers.scenario}} total of {{dustScenario.influx_t_per_day}} t/day. Brighter and higher: more. The morning side of the Earth faces its direction of motion and sweeps up more. The night side is darker; land is tinted green. Equirectangular map, 5° cells.</p>
    <label class="toggle">Colour <select v-model="dustLayers.base"><option value="hour">Influx this hour</option><option value="daily">Daily mean</option></select></label>
    <label class="toggle">Total influx <select v-model="dustLayers.scenario"><option v-for="x in DUST_SCENARIOS" :key="x.key" :value="x.key">{{x.label}}: {{dustBlock.scenarios[x.key].influx_t_per_day}} t/day</option></select></label>
    <label class="toggle"><input type="checkbox" v-model="dustLayers.relief"> Height by influx</label>
    <label class="toggle"><input type="checkbox" v-model="dustLayers.night"> Night side darker</label>
    <label class="toggle" title="Where each sporadic meteoroid source stands overhead now, and the Sun."><input type="checkbox" v-model="dustLayers.sources"> Sources overhead ☄ ◐ ◑ △ ▽ and the Sun ☀</label>
    <label class="toggle"><input type="checkbox" v-model="dustLayers.labels"> Labels</label>
   </section>
   <section v-if="view==='solarwind'&&data.solarWind" class="map-layers"><div class="section-label">01k / SOLAR WIND &amp; POLAR CAPS</div><p class="hint">{{windMetrics.time.replace('T',' ').replace(':00Z',' UTC')}} · {{windMetrics.phase}}. The field lines poleward of the auroral ovals are open to the solar wind: there particles rain in (aurora), ions flow out to space and cosmic rays and solar protons enter. Choose what to colour in <em>Sun &amp; space weather</em> above. {{windLayers.globe?'The Sun is to the right (+x); distances beyond the Earth are compressed.':'Equirectangular map, 4° cells.'}}</p>
    <label class="toggle" title="The Earth as a globe in a Sun-fixed frame with the magnetosphere around it, or a flat world map."><input type="checkbox" v-model="windLayers.globe"> Globe with the magnetosphere</label>
    <label class="toggle"><input type="checkbox" v-model="windLayers.relief"> Height by value</label>
    <label class="toggle"><input type="checkbox" v-model="windLayers.night"> Night side darker</label>
    <label class="toggle" title="The edge of the open polar cap (orange) and the equatorward edge of the auroral oval (green), both hemispheres."><input type="checkbox" v-model="windLayers.ovals"> Oval and polar-cap edges</label>
    <label class="toggle" :class="{muted:!windLayers.globe}" title="Closed dipole field lines (blue, cut at the magnetopause on the day side) and open field lines from the polar caps swept into the magnetotail (orange): a sketch of the topology."><input type="checkbox" v-model="windLayers.field" :disabled="!windLayers.globe"> Field lines (sketch)</label>
    <label class="toggle" :class="{muted:!windLayers.globe}" title="The magnetopause of Shue et al. (1998) for this hour's solar-wind pressure and Bz."><input type="checkbox" v-model="windLayers.magnetopause" :disabled="!windLayers.globe"> Magnetopause</label>
    <label class="toggle" :class="{muted:!windLayers.globe}"><input type="checkbox" v-model="windLayers.sunwind" :disabled="!windLayers.globe"> Solar wind and the Sun</label>
    <label class="toggle" :class="{muted:!windLayers.globe}" title="Oxygen ions escaping from the cusp and the night-side oval (orange), the H+ polar wind (cyan)."><input type="checkbox" v-model="windLayers.outflow" :disabled="!windLayers.globe"> Escaping ions</label>
    <label class="toggle"><input type="checkbox" v-model="windLayers.labels"> Labels</label>
   </section>
   <section><div class="section-label">02 / DATA SOURCE</div><strong class="source">{{source}}</strong><p class="hint">{{source==='Synthetic demonstration'?'Illustrative fields only. These values are not engine output or live observations.':source==='Regional weather · synthetic'?'Analytic 1,200 × 900 km weather illustration. Low/high pressure, a moving front and cloud bands; not solver output.':source==='Engine output · synthetic inputs'?'C++ dry transport + radar rain estimation + accumulation + gridding. Synthetic inputs; not live weather.':source==='Engine output · coupled wind'?'C++ coupled wind / moist-layer model on synthetic Gaussian pressure systems. Pressure is prescribed; not live weather or a forecast.':source==='Engine output · cloud formation'?'C++ coupled model with column and cloud-formation diagnostics: cold/warm fronts, frontal and convective cloud, afternoon thunderstorms. Synthetic inputs; rule-of-thumb cloud types.':source==='Cloud formation · synthetic mock'?'Hand-built mock in the documented cloud-formation format (moving warm and cold fronts, diurnal convection, a synthetic hurricane and tornado risk). Not solver output.':source==='Engine output · Alps foehn'?'C++ coupled model over a synthetic east-west Alpine ridge with a foreland basin: southerly flow, orographic precipitation on the south side, foehn warming and drying to the north, nocturnal cold pools in the basin. Synthetic terrain; parametrised orography, not a forecast.':source==='Engine output · polar melt & overturning'?'C++ Greenland ice sheet (shallow-ice flow, degree-day melt with the melt-elevation feedback, ocean-driven frontal melt), a central-Arctic sea-ice column and a two-box Atlantic overturning calibrated to 17 Sv, under three warming scenarios 1850–2300 with a 9-member uncertainty ensemble. Synthetic geometry, conceptual overturning: read dates as scenario-relative, not as a forecast.':source==='Engine output · ocean carbon today'||source==='Engine output · ocean carbon 2100'?`C++ ocean carbon and oxygen model coupled to the layered ocean of a synthetic Gulf-like sea (30 × 22 cells of 36 km, a 16,000 m³/s delta river carrying nitrate, an open southern boundary) and to a boundary-layer CO₂ model of the air over land and sea with four coastal sources (180 Mt CO₂/yr). Carbonate chemistry (pH, aragonite saturation), air–sea CO₂ and O₂ exchange (Wanninkhof 2014), plankton production, sinking and respiration, seafloor respiration on the shelf, habitat classes and dead zones. ${source.endsWith('2100')?'The 2100 slice: about 1,010 ppm in the air, the sea 3 K warmer, anthropogenic carbon in the thermocline.':'Today: about 427 ppm.'} One year after a one-year spin-up; a process demonstration, not a forecast.`:source==='Engine output · solar wind & polar caps'?'C++ solar wind model: a synthetic severe storm hour by hour (a solar proton event, then a fast CME) on a tilted dipole: the Newell coupling and Kp, the O\'Brien–McPherron ring current, the Shue magnetopause, the open polar cap of Milan et al. with the auroral oval, Jacchia thermosphere, diffusion-limited hydrogen escape (Jeans, charge exchange, polar wind), O⁺ outflow, galactic cosmic rays (force field, Störmer cutoff) and solar protons with their NOx and ozone loss; the solar cycle from the solar-cycle example; scenarios of storms, proton events and a weaker dipole.':source==='Engine output · cosmic dust'?'C++ cosmic dust model: the Grün (1985) size distribution of interplanetary dust scaled to a total influx (30, 60 or 100 t/day), the entry of every size, speed and angle through the standard atmosphere (drag, heating, radiation, Langmuir evaporation, dehydration), the budgets of mass, water, energy, iron and meteoric smoke against their terrestrial counterparts, and hourly maps of the influx from the sporadic meteoroid sources. The entry model is calibrated to the Antarctic micrometeorite collections; the comparison quantities are rounded literature values.':source==='Engine output · solar cycle'?`C++ solar-cycle model. ${solarBlock?.record_kind==='monthly'?'A monthly sunspot record':'A monthly sunspot record synthesised with noise from an approximate cycle table (SILSO v2 minima and maxima, hand-entered and not verified against the SILSO files)'} since 1850; Hathaway cycle curves fitted to it; a forecast of the cycle in progress (onset-prior fit weighed against an AR(1) prior by a backtest on the past cycles) and of the next cycles, with a 200-member ensemble to 2050. F10.7, Mg II, TSI and UV bands as sunspot proxies, ozone and the noon UV index, a flare-rate model with synthetic flares, and a two-layer energy balance for the climate response. A statistical forecast of a proxy model, not an operational space-weather prediction.`:source==='Engine output · sea level & coasts'?'C++ sea-level budget (thermal expansion, glaciers, Antarctica, land water, and Greenland from the polar-melt run) turned into coastal impacts on a synthetic 420 × 252 km coast: a subsiding delta with polders and dikes, an estuary city, barrier islands with a lagoon, cliffs and low islands. Land lost at high water (bathtub with connectivity), 100-year storm floods with wind setup, Bruun erosion, rivers pushed back and salt intrusion, year by year to 2300. Synthetic coast; a demonstration, not a local projection.':source==='Engine output · polar ocean'?'C++ layered ocean with sea ice and a coupled Greenland-like ice sheet in a synthetic 3,400 × 4,200 km North-Atlantic-like basin with seasons: air warming ramps up by 0.4 K/yr to +4 K (2.5× in the north) while extra meltwater is hosed near the sinking region (up to 0.5 Sv). Watch the winter sea ice shrink, the ice sheet melt and the deep-water formation weaken. Ten model years after a spin-up; a process demonstration, not a projection.':source==='Engine output · ocean heat & rain'?'C++ ocean with rain, evaporation, air–sea heat exchange and a 10-layer temperature/salinity column in a synthetic late-summer subtropical basin: a tropical rain band crosses the sea and makes landfall, a storm floods the coastal plain, a cold dry outbreak draws heat and water out of the sea, the sun drives a daily warm layer. Synthetic forcing; not an ocean forecast.':source==='Engine output · Atlantic streams'?'C++ reduced-gravity ocean layer on a beta-plane in a synthetic Atlantic-like basin: a double-gyre wind drives a Gulf-Stream-like western boundary current that separates at a cape, a cold north-western patch forms deep water (the overturning), rivers and a meltwater pulse pour fresh water in. Idealised and closed; not an ocean forecast.':source==='Engine output · alpine flood'?'C++ catchment hydrology on a synthetic pre-alpine range draining to a southern sea: two days of stratiform rain, an eight-hour cloudburst on the range, then a shower. Rivers, lakes, groundwater, floods, erosion and landslides from the kernel; synthetic terrain and forcing, not a forecast.':source==='Engine output · hurricane landfall'?'C++ coupled model with tropical-cyclone objects: two seeds merge, the storm intensifies toward its potential intensity over a 29 °C sea, makes landfall on the western coast and decays inland. Synthetic inputs; parametrised storm physics, not a forecast.':'Loaded in this browser only. No upload service or database.'}}</p><label v-if="data.surfaceWater" class="compare-upload" title="Load a second timeline on the same grid and forcing to compare against the current one.">Open comparison JSON<input type="file" accept=".json,application/json" @change="uploadComparison"></label><details v-for="g in EXAMPLE_GROUPS" :key="g.id" class="example-group" :open="openExamples.includes(g.id)" @toggle="onExampleToggle(g.id,$event)"><summary :title="`${g.examples.length} examples`"><span>{{g.title}}</span><small>{{g.examples.length}}</small></summary><button v-for="e in g.examples" :key="e.action" class="example-button" :disabled="loading&&e.action.startsWith('load')" :title="e.tooltip" @click="runExample(e.action)">{{e.action==='loadEngineExample'&&loading?'Loading…':e.label}}</button></details><button class="text-button" @click="useDemo">↻ Load synthetic demo</button></section>
   <section class="inspection"><div class="section-label">03 / CELL INSPECTOR</div><template v-if="chosen"><h2>{{values[chosen.i]===null?(layer==='aspect'?'Undefined (flat)':'No data'):values[chosen.i]?.toFixed(2)}} <small>{{unit}}</small></h2><p>Column {{chosen.x}} · Row {{chosen.y}} · Index {{chosen.i}}</p><p v-if="!data.dust&&!data.solarWind">East {{(grid.origin_east_m+(chosen.x+.5)*grid.spacing_east_m).toFixed(0)}} m · North {{(grid.origin_north_m+(chosen.y+.5)*grid.spacing_north_m).toFixed(0)}} m</p><p v-if="data.atmosphere">Cloud {{(data.atmosphere.cloud[chosen.i]*100).toFixed(1)}}% · Pressure {{data.atmosphere.pressure[chosen.i].toFixed(1)}} hPa<br>Relative humidity {{data.atmosphere.humidity[chosen.i].toFixed(1)}}%</p><p v-if="data.dust">{{Math.abs(dustLatLon(chosen.i).lat).toFixed(1)}}° {{dustLatLon(chosen.i).lat<0?'S':'N'}}, {{Math.abs(dustLatLon(chosen.i).lon).toFixed(1)}}° {{dustLatLon(chosen.i).lon<0?'W':'E'}} · {{data.dust.land[chosen.i]?'land':'sea'}} · local solar time {{localSolarTime(dustLatLon(chosen.i).lon,data.dust.subsolar.longitude_deg).toFixed(1)}} h ({{data.dust.daylight[chosen.i]?'day':'night'}})<br>Influx now {{formatNumber(data.dust.influx[chosen.i]*dustMetrics.scale,3)}} µg/m²/day · daily mean {{formatNumber(data.dust.daily[chosen.i]*dustMetrics.scale,3)}} · a year: {{formatNumber(data.dust.daily[chosen.i]*dustMetrics.scale*365.25,1)}} µg/m²</p><p v-if="data.solarWind">{{Math.abs(windLatLon(chosen.i).lat).toFixed(0)}}° {{windLatLon(chosen.i).lat<0?'S':'N'}}, {{Math.abs(windLatLon(chosen.i).lon).toFixed(0)}}° {{windLatLon(chosen.i).lon<0?'W':'E'}} · {{data.solarWind.static.land_mask?.[chosen.i]?'land':'sea'}} · magnetic latitude {{data.solarWind.static.magnetic_latitude_deg[chosen.i].toFixed(1)}}° · MLT {{magneticLocalTime(data.solarWind.static.magnetic_longitude_deg[chosen.i],data.solarWind.map.subsolar.magnetic_longitude_deg).toFixed(1)}} h · {{REGIONS[data.solarWind.region[chosen.i]].label.toLowerCase()}}<br>Cutoff {{(data.solarWind.static.cutoff_gv[chosen.i]*data.solarWind.map.cutoff_factor).toFixed(2)}} GV · dose at 12 km {{cellDose(data.solarWind,chosen.i).toFixed(2)}} µSv/h · aurora {{formatWind(data.solarWind.aurora[chosen.i],2)}} mW/m² · O⁺ upflow {{formatWind(data.solarWind.outflow[chosen.i]*1e7,2)}} /cm²/s · solar-proton ionisation {{formatWind(cellSep(data.solarWind,chosen.i)*1e9,2)}} /cm²/s · thermosphere {{data.solarWind.thermo[chosen.i]}} K</p><p v-if="data.solar&&solarGeometry"><template v-if="solarGeometry[chosen.i].mu!==null">Heliographic latitude {{solarGeometry[chosen.i].latitude_deg.toFixed(1)}}° · {{Math.abs(solarGeometry[chosen.i].longitude_deg).toFixed(1)}}° {{solarGeometry[chosen.i].longitude_deg<0?'east':'west'}} of the central meridian · μ {{solarGeometry[chosen.i].mu.toFixed(2)}}<br>Brightness {{data.solar.brightness[chosen.i].toFixed(0)}} K (quiet Sun {{data.solar.quiet[chosen.i].toFixed(0)}} K) · contrast {{solarSigned(contrastPct(data.solar.brightness[chosen.i],data.solar.quiet[chosen.i]),1)}} %</template><template v-else>Off the solar disk.</template></p><p v-if="data.coupled">Pressure {{(data.coupled.pressure[chosen.i]/100).toFixed(1)}} hPa · Density {{data.coupled.density[chosen.i].toFixed(4)}} kg/m³<br>Specific humidity {{(data.coupled.specificHumidity[chosen.i]*1000).toFixed(2)}} g/kg · Relative humidity {{data.coupled.humidity[chosen.i].toFixed(1)}}%<br>Coriolis f {{data.coupled.coriolis[chosen.i].toExponential(3)}} 1/s</p><p v-if="data.clouds">Cloud: <strong>{{cloudTypeInfo(data.clouds.type[chosen.i]).label}}</strong><template v-if="data.clouds.type[chosen.i]!=='none'"> · base {{data.clouds.base[chosen.i].toFixed(0)}} m · top {{data.clouds.top[chosen.i].toFixed(0)}} m · cover {{(data.clouds.fraction[chosen.i]*100).toFixed(0)}}%<br>Mechanism {{data.clouds.mechanism[chosen.i]}} · trigger {{data.clouds.trigger[chosen.i]}}{{data.clouds.thunderstorm[chosen.i]?' · thunderstorm':''}}</template><br>Front {{data.clouds.frontType[chosen.i]}} · |∇T| {{(data.clouds.gradient[chosen.i]*1e5).toFixed(2)}} K/100 km · advection {{(data.clouds.advection[chosen.i]*3600).toFixed(2)}} K/h · divergence {{data.clouds.divergence[chosen.i].toExponential(2)}} 1/s<br>{{data.clouds.distance[chosen.i]===null?'No front in the domain':`Nearest front ${(data.clouds.distance[chosen.i]/1000).toFixed(0)} km · ${data.clouds.side[chosen.i]===1?'warm side':data.clouds.side[chosen.i]===-1?'cold side':'on the front'}`}} · lifting {{data.clouds.lifting[chosen.i].toFixed(0)}} m · heated CAPE {{data.clouds.cape[chosen.i].toFixed(0)}} / CIN {{data.clouds.cin[chosen.i].toFixed(0)}} J/kg</p><p v-if="data.precipitation">Precipitation: <strong>{{precipitationTypeInfo(data.precipitation.type[chosen.i]).label}}</strong><template v-if="data.precipitation.type[chosen.i]!=='none'"> · {{data.precipitation.intensity[chosen.i]}} {{data.precipitation.character[chosen.i]}} · {{data.precipitation.rate[chosen.i].toFixed(2)}} mm/h for {{(data.precipitation.duration[chosen.i]*100).toFixed(0)}}% of the interval → {{data.precipitation.accumulation[chosen.i].toFixed(1)}} mm · phase {{data.precipitation.phase[chosen.i]}}</template><template v-if="data.precipitation.virga[chosen.i]"> · virga (evaporates aloft)</template><br>{{data.precipitation.hazards[chosen.i].length?data.precipitation.hazards[chosen.i].map(h=>HAZARD_LABELS[h]).join(' · '):'no hazards'}}<template v-if="data.precipitation.hail[chosen.i]>0"> · hail up to {{data.precipitation.hail[chosen.i].toFixed(1)}} cm</template><br>Wet-bulb {{(data.precipitation.wetBulb[chosen.i]-273.15).toFixed(1)}} °C · freezing level {{data.precipitation.freezingLevel[chosen.i]===null?'none':data.precipitation.freezingLevel[chosen.i].toFixed(0)+' m'}} · precipitable water {{data.precipitation.precipitableWater[chosen.i].toFixed(1)}} mm<template v-if="data.precipitation.baseTemperature[chosen.i]!==null"> · cloud {{(data.precipitation.baseTemperature[chosen.i]-273.15).toFixed(0)}} °C at base, {{(data.precipitation.topTemperature[chosen.i]-273.15).toFixed(0)}} °C at top · lifting {{data.precipitation.updraft[chosen.i].toFixed(2)}} m/s</template></p><p v-if="data.orography">Orography: elevation {{data.terrain.values[chosen.i].toFixed(0)}} m · surface pressure {{(data.orography.surfacePressure[chosen.i]/100).toFixed(1)}} hPa · lifting {{data.orography.lifting[chosen.i].toFixed(3)}} m/s ({{data.orography.lifting[chosen.i]>0.01?'up-slope':data.orography.lifting[chosen.i]<-0.01?'descending':'level'}}) · contour {{data.orography.orientation[chosen.i]===null?'flat':data.orography.orientation[chosen.i].toFixed(0)+'° from north'}}<br>Cross-barrier {{data.orography.cross[chosen.i].toFixed(1)}} m/s · along-barrier {{data.orography.along[chosen.i].toFixed(1)}} m/s · obstacle ahead {{data.orography.obstacle[chosen.i].toFixed(0)}} m · Froude {{data.orography.froude[chosen.i]===null?'—':data.orography.froude[chosen.i].toFixed(2)}} · blocked {{(data.orography.blocked[chosen.i]*100).toFixed(0)}}%<br>Valley depth {{data.orography.valley[chosen.i].toFixed(0)}} m · cold pool {{data.orography.coldPool[chosen.i].toFixed(1)}} K{{data.orography.coldPoolFlag[chosen.i]?' ❄':''}} · foehn {{data.orography.foehn[chosen.i]?`+${data.orography.foehnWarming[chosen.i].toFixed(1)} K`:'no'}} · orographic precipitation {{data.orography.precipRate[chosen.i].toFixed(2)}} mm/h ({{data.orography.cumulative[chosen.i].toFixed(1)}} mm so far)</p><p v-if="data.hydrology">Catchment: <strong>{{data.hydrology.network.ocean[chosen.i]?'sea':data.hydrology.network.lake_id[chosen.i]>=0?`lake ${data.hydrology.network.lake_id[chosen.i]+1}`:data.hydrology.network.channel[chosen.i]?`river reach (Strahler ${data.hydrology.network.strahler_order[chosen.i]})`:'hillslope'}}</strong><template v-if="!data.hydrology.network.ocean[chosen.i]"> · upstream {{data.hydrology.network.upstream_area_km2[chosen.i].toFixed(0)}} km² · {{pathSummary}}<br>Rain {{data.hydrology.rain[chosen.i].toFixed(1)}} mm/h · snow {{data.hydrology.snow[chosen.i].toFixed(0)}} mm · surface water {{data.hydrology.surface[chosen.i].toFixed(1)}} mm · soil {{(data.hydrology.saturation[chosen.i]*100).toFixed(0)}} % saturated · water table {{data.hydrology.waterTable[chosen.i].toFixed(1)}} m down<template v-if="data.hydrology.network.channel[chosen.i]"><br>Discharge {{data.hydrology.discharge[chosen.i].toFixed(2)}} m³/s · depth {{data.hydrology.channelDepth[chosen.i].toFixed(2)}} / bankfull {{data.hydrology.network.bankfull_depth_m[chosen.i].toFixed(2)}} m · width {{data.hydrology.network.channel_width_m[chosen.i].toFixed(0)}} m · sediment {{data.hydrology.sediment[chosen.i].toFixed(2)}} kg/s</template><template v-if="data.hydrology.network.lake_id[chosen.i]>=0"><br>Lake level {{data.hydrology.lakeLevel[data.hydrology.network.lake_id[chosen.i]].toFixed(1)}} m · depth here {{data.hydrology.lakeDepth[chosen.i].toFixed(1)}} m · volume {{(data.hydrology.lakeVolume[data.hydrology.network.lake_id[chosen.i]]/1e6).toFixed(2)}} / {{(data.hydrology.network.lakes[data.hydrology.network.lake_id[chosen.i]].capacity_m3/1e6).toFixed(2)}} hm³</template><br>Hazards: flood <strong :style="{color:floodClassInfo(data.hydrology.floodClass[chosen.i]).color||'inherit'}">{{floodClassInfo(data.hydrology.floodClass[chosen.i]).key}}</strong> · erosion {{data.hydrology.erosion[chosen.i].toFixed(2)}} mm · deposition {{data.hydrology.deposition[chosen.i].toFixed(2)}} mm · factor of safety {{data.hydrology.fs[chosen.i]===null?'n/a (flat or water)':data.hydrology.fs[chosen.i].toFixed(2)}}{{data.hydrology.landslide[chosen.i]?' · ▲ landslide this step':data.hydrology.unstable[chosen.i]?' · unstable':''}}{{data.hydrology.threshold[chosen.i]?' · ! rainfall threshold exceeded':''}} · regolith {{data.hydrology.network.regolith_depth_m[chosen.i].toFixed(2)}} m · slope {{(Math.atan(data.hydrology.network.slope[chosen.i])*180/Math.PI).toFixed(1)}}°</template></p><p v-if="data.coast"><template v-if="data.coast.sea[chosen.i]">Sea · {{(-data.terrain.values[chosen.i]).toFixed(1)}} m deep · today's design-storm wind setup {{data.coast.block.today.setup_m[chosen.i].toFixed(2)}} m</template><template v-else><strong>{{data.coast.block.region[chosen.i]>=0?data.coast.block.region_names[data.coast.block.region[chosen.i]]:'Other land'}}</strong> · {{COAST_TYPES[data.coast.block.coast_type[chosen.i]]}} · {{data.terrain.values[chosen.i].toFixed(2)}} m above today's mean sea level{{coastStatic.defence.has(chosen.i)?` · defence crest ${coastStatic.defence.get(chosen.i).toFixed(1)} m`:''}}{{coastStatic.river.has(chosen.i)?` · river ${data.coast.block.rivers[coastStatic.river.get(chosen.i)].name}`:''}}<br>{{fmtPeople(data.coast.block.population[chosen.i])}} people · sinking {{data.coast.block.subsidence_mm_yr[chosen.i].toFixed(1)}} mm/yr · relative rise by {{data.coast.year}} {{cellFrequency(data.coast,coastScenario,chosen.i).relative_rise_m.toFixed(2)}} m<template v-if="coastCoastal[chosen.i]"> · today's 100-year flood now {{fmtYears(cellFrequency(data.coast,coastScenario,chosen.i).return_years)}}</template><br>Under “{{coastScenario}}”: {{coastYears.lost[chosen.i]!==null?`lost in ${coastYears.lost[chosen.i]}`:'not lost by '+data.coast.block.output_years.at(-1)}} · {{coastYears.storm[chosen.i]===data.coast.block.reference_year?'flooded by the 100-year storm already today':coastYears.storm[chosen.i]!==null?`first flooded by the 100-year storm in ${coastYears.storm[chosen.i]}`:'not reached by the 100-year storm'}}<template v-if="coastIsMap"><br>{{data.coast.year}}: {{data.coast.lost[chosen.i]>=1?`under high water (${data.coast.inundation[chosen.i].toFixed(2)} m deep)`:data.coast.lost[chosen.i]>0?`${(data.coast.lost[chosen.i]*100).toFixed(1)} % eroded (shore retreat ${data.coast.erosion[chosen.i].toFixed(0)} m)`:data.coast.protectedLow.has(chosen.i)?'below high water, kept dry by defences':'dry at high water'}}{{data.coast.storm[chosen.i]>0?` · ${data.coast.storm[chosen.i].toFixed(2)} m deep in the 100-year storm`:''}}{{data.coast.salt.has(chosen.i)?' · salt water in the river':''}}</template></template></p><p v-if="data.ocean">Ocean: <strong>{{data.ocean.mask[chosen.i]?(selectedStream?streamLabel(selectedStream,grid):'open water'):'land'}}</strong><template v-if="data.ocean.mask[chosen.i]"><br>Current {{data.ocean.speed[chosen.i].toFixed(2)}} m/s (u {{data.ocean.u[chosen.i].toFixed(2)}}, v {{data.ocean.v[chosen.i].toFixed(2)}}) · thermocline {{data.ocean.thickness[chosen.i].toFixed(0)}} m · {{data.ocean.thermal?'layer-mean temperature':'temperature'}} {{(data.ocean.temperature[chosen.i]-273.15).toFixed(1)}} °C<br>Salinity {{data.ocean.surfaceSalinity[chosen.i].toFixed(2)}} psu at the surface · {{data.ocean.salinity[chosen.i].toFixed(2)}} psu in the layer · fresh water {{(data.ocean.freshwater[chosen.i]*100).toFixed(1)}} %{{data.ocean.front[chosen.i]?' · mixing front':''}}{{data.ocean.sinking[chosen.i]>0?` · deep-water formation ${(data.ocean.sinking[chosen.i]*86400).toFixed(2)} m/day`:''}}{{data.ocean.spongeStrength[chosen.i]>0?' · sponge row':''}}<template v-if="selectedStream"><br>Stream: {{selectedStream.direction}} · {{selectedStream.cell_count}} cells · max {{selectedStream.max_speed_m_s.toFixed(2)}} m/s · {{selectedStream.transport_sv.toFixed(1)}} Sv · {{selectedStream.mean_salinity_psu.toFixed(2)}} psu · {{(selectedStream.mean_temperature_k-273.15).toFixed(1)}} °C</template><template v-for="p in data.ocean.exchangePoints.filter(p=>p.mouth_cell===chosen.i)" :key="p.name"><br>Exchange point {{p.name}}: {{p.discharge_m3_s.toFixed(0)}} m³/s of fresh water · plume {{p.plume_area_km2.toFixed(0)}} km² down to {{p.minimum_salinity_psu.toFixed(1)}} psu · extent {{p.plume_extent_km.toFixed(0)}} km · bearing {{p.deflection_deg.toFixed(0)}}°</template></template></p><p v-if="data.ocean?.processes?.length"><template v-if="data.ocean.mask[chosen.i]">Surface <strong>{{((data.ocean.thermal?data.ocean.thermal.sst[chosen.i]:data.ocean.temperature[chosen.i])-273.15).toFixed(2)}} °C</strong><template v-if="data.ocean.thermal"> · mixed layer {{data.ocean.thermal.mld[chosen.i].toFixed(1)}} m · isothermal layer {{data.ocean.thermal.ild[chosen.i].toFixed(1)}} m{{data.ocean.thermal.ild[chosen.i]>data.ocean.thermal.mld[chosen.i]+1?` · barrier layer ${(data.ocean.thermal.ild[chosen.i]-data.ocean.thermal.mld[chosen.i]).toFixed(1)} m`:''}}</template><template v-if="data.ocean.airSea"><br>Heat into the ocean {{data.ocean.airSea.net[chosen.i].toFixed(0)}} W/m² = sun {{data.ocean.airSea.shortwave[chosen.i].toFixed(0)}} + longwave {{data.ocean.airSea.longwave[chosen.i].toFixed(0)}} + sensible {{data.ocean.airSea.sensible[chosen.i].toFixed(0)}} + latent {{data.ocean.airSea.latent[chosen.i].toFixed(0)}}{{data.ocean.seaIce&&Math.abs(data.ocean.seaIce.toIce[chosen.i])>=1?` − to the sea ice ${data.ocean.seaIce.toIce[chosen.i].toFixed(0)} (open water ${(100-data.ocean.seaIce.concentration[chosen.i]*100).toFixed(0)} %)`:''}}{{Math.abs(data.ocean.airSea.rainHeat[chosen.i])>=1?` · rain ${data.ocean.airSea.rainHeat[chosen.i].toFixed(0)} (rain colder than the sea)`:''}}<br>Evaporation {{data.ocean.airSea.evaporation[chosen.i].toFixed(2)}} mm/day · air humidity {{(data.ocean.airSea.humidity[chosen.i]*100).toFixed(0)}} % · cloud {{(data.ocean.airSea.cloud[chosen.i]*100).toFixed(0)}} %</template><template v-if="data.ocean.rain"><br>Rain {{data.ocean.rain.rate[chosen.i].toFixed(2)}} mm/h{{data.ocean.rain.inflow[chosen.i]>0?` · coastal runoff arriving ${fmtFlow(data.ocean.rain.inflow[chosen.i])}`:''}}</template><template v-for="l in data.ocean.lenses.filter(l=>l.freshest_cell===chosen.i)" :key="'lens'+l.freshest_cell"><br>Freshest point of a {{lensLabel(l)}} (rain {{(l.source_shares.rain*100).toFixed(0)}} % · runoff {{(l.source_shares.coastal_runoff*100).toFixed(0)}} % · river {{(l.source_shares.river*100).toFixed(0)}} %{{l.source_shares.glacial!==undefined?` · glacial ${(l.source_shares.glacial*100).toFixed(0)} %`:''}})</template><template v-if="data.ocean.seaIce"><br>Sea ice {{(data.ocean.seaIce.concentration[chosen.i]*100).toFixed(0)}} %{{data.ocean.seaIce.concentration[chosen.i]>0?` · ${data.ocean.seaIce.thickness[chosen.i].toFixed(2)} m thick · ice surface ${(data.ocean.seaIce.surfaceTemperature[chosen.i]-273.15).toFixed(1)} °C · drifting ${(Math.hypot(data.ocean.seaIce.driftEast[chosen.i],data.ocean.seaIce.driftNorth[chosen.i])*86.4).toFixed(1)} km/day · the water gives it ${data.ocean.seaIce.toIce[chosen.i].toFixed(0)} W/m²`:' · open water'}}</template><template v-if="data.ocean.oxygen"><br>Dissolved-oxygen capacity at the surface {{data.ocean.oxygen[chosen.i].toFixed(0)}} µmol/kg (saturation at this temperature and salinity)</template></template><template v-else-if="data.ocean.iceSheet&&data.ocean.iceSheet.domain[chosen.i]">Ice sheet · {{data.ocean.iceSheet.thickness[chosen.i].toFixed(0)}} m of ice · surface {{data.ocean.iceSheet.surface[chosen.i].toFixed(0)}} m · bed {{data.ocean.iceSheet.bed[chosen.i].toFixed(0)}} m<br>Surface mass balance {{data.ocean.iceSheet.smb[chosen.i].toFixed(2)}} m ice/yr · melt {{data.ocean.iceSheet.melt[chosen.i].toFixed(2)}} m ice/yr</template><template v-else-if="data.ocean.rain">Land · rain {{data.ocean.rain.rate[chosen.i].toFixed(2)}} mm/h · {{data.ocean.rain.receiver[chosen.i]>=0?`coastal band: drains straight to sea cell ${data.ocean.rain.receiver[chosen.i]} · ${data.ocean.rain.store[chosen.i].toFixed(1)} mm still on the land`:'inland: its rain is left to the rivers and the catchment model'}}</template></p><p v-if="data.carbon">Air CO₂ {{data.carbon.ppm[chosen.i].toFixed(1)}} ppm ({{signed(data.carbon.ppm[chosen.i]-data.carbon.background,1)}} from the background)<template v-if="data.carbon.emission[chosen.i]*grid.spacing_east_m*grid.spacing_north_m*365.25*86400*44.01e-12>=0.01"> · emits {{(data.carbon.emission[chosen.i]*grid.spacing_east_m*grid.spacing_north_m*365.25*86400*44.01e-12).toFixed(2)}} Mt CO₂/yr</template><template v-if="data.carbon.mask[chosen.i]"><br>Seafloor {{data.carbon.seafloor[chosen.i].toFixed(0)}} m · <strong :style="{color:habitatInfo(data.carbon.columnClass[chosen.i]).color==='#2a0410'?'#c98b9b':habitatInfo(data.carbon.columnClass[chosen.i]).color}">{{data.carbon.dead[chosen.i]?'DEAD ZONE · ':''}}{{habitatInfo(data.carbon.columnClass[chosen.i]).label}}</strong> · habitable {{(data.carbon.habitable[chosen.i]*100).toFixed(0)}} % of the column<br>O₂ bottom {{data.carbon.o2Bottom[chosen.i].toFixed(1)}} µmol/kg · lowest {{data.carbon.o2Min[chosen.i].toFixed(1)}} at {{data.carbon.o2MinDepth[chosen.i].toFixed(0)}} m<br>Surface pH {{data.carbon.ph[chosen.i].toFixed(3)}} · Ω surface {{data.carbon.omega[chosen.i].toFixed(2)}} / bottom {{data.carbon.omegaBottom[chosen.i].toFixed(2)}} · pCO₂ {{data.carbon.pco2[chosen.i].toFixed(0)}} µatm<br>CO₂ flux {{signed(data.carbon.flux[chosen.i],2)}} mol/m²/yr ({{data.carbon.flux[chosen.i]>=0?'uptake':'outgassing'}}) · production {{data.carbon.production[chosen.i].toFixed(2)}} g C/m²/day · nitrate {{data.carbon.nitrate[chosen.i].toFixed(2)}} µmol/kg</template></p><p v-if="data.polar">{{data.polar.land[chosen.i]?'Greenland':'Sea'}}<template v-if="data.polar.land[chosen.i]"> · ice {{data.polar.thickness[chosen.i].toFixed(0)}} m · surface {{data.polar.surface[chosen.i].toFixed(0)}} m · bed {{data.terrain.values[chosen.i].toFixed(0)}} m<br>Surface mass balance {{data.polar.smb[chosen.i].toFixed(2)}} m ice/yr · melt {{data.polar.melt[chosen.i].toFixed(2)}} m ice/yr · annual air {{(forecast.fields.air_temperature.values[chosen.i]-273.15).toFixed(1)}} °C</template> · {{data.polar.block.map_scenario}}, {{data.polar.year}}</p><p v-if="data.storms">Storm environment: {{data.surface?.land[chosen.i]?'land':'ocean'}}{{data.surface&&!data.surface.land[chosen.i]?` · SST ${(data.surface.sst[chosen.i]-273.15).toFixed(1)} °C`:''}} · potential intensity {{data.storms.env.potential_intensity_m_s[chosen.i].toFixed(0)}} m/s · genesis index {{data.storms.env.genesis_potential_index[chosen.i].toFixed(1)}} · shear {{data.storms.env.vertical_shear_m_s[chosen.i].toFixed(1)}} m/s · vorticity {{data.storms.env.relative_vorticity_s[chosen.i].toExponential(2)}} 1/s<br>Storm wind here {{data.storms.env.storm_wind_speed_m_s[chosen.i].toFixed(1)}} m/s · pressure anomaly {{(data.storms.env.storm_pressure_anomaly_pa[chosen.i]/100).toFixed(1)}} hPa · storm rain {{data.storms.env.storm_rain_rate_mm_h[chosen.i].toFixed(1)}} mm/h<template v-if="data.storms.env.storm_surge_m[chosen.i]>0"> · surge {{data.storms.env.storm_surge_m[chosen.i].toFixed(2)}} m</template></p><p v-if="data.severe">Severe: STP {{data.severe.stp[chosen.i].toFixed(2)}} ({{data.severe.risk[chosen.i]}}){{data.severe.possible[chosen.i]?' · 🌪 tornado possible':''}} · 0–1 km shear {{data.severe.shear1[chosen.i].toFixed(1)}} m/s · deep shear {{data.severe.shear6[chosen.i].toFixed(1)}} m/s · helicity {{data.severe.helicity[chosen.i].toFixed(0)}} m²/s²<br>Heated CAPE {{data.severe.cape[chosen.i].toFixed(0)}} / CIN {{data.severe.cin[chosen.i].toFixed(0)}} J/kg · cap {{data.severe.cap[chosen.i].toFixed(0)}} J/kg {{data.severe.capped[chosen.i]?(data.severe.capBroken[chosen.i]?'(broken)':'(holding)'):'(none)'}} · storm motion ({{data.severe.motionEast[chosen.i].toFixed(1)}}, {{data.severe.motionNorth[chosen.i].toFixed(1)}}) m/s</p><p v-if="data.coupled?.columns">Column ({{data.coupled.columns.config.layer_count}} × {{data.coupled.columns.config.layer_thickness_m}} m): LCL {{data.coupled.columns.lcl[chosen.i]===null?'none':data.coupled.columns.lcl[chosen.i].toFixed(0)+' m'}} · LFC {{data.coupled.columns.lfc[chosen.i]===null?'none':data.coupled.columns.lfc[chosen.i].toFixed(0)+' m'}} · EL {{data.coupled.columns.el[chosen.i]===null?'none':data.coupled.columns.el[chosen.i].toFixed(0)+' m'}}<br>CAPE {{data.coupled.columns.cape[chosen.i].toFixed(0)}} J/kg · CIN {{data.coupled.columns.cin[chosen.i].toFixed(0)}} J/kg · potential {{data.coupled.columns.potential[chosen.i]}} · condensed {{data.coupled.columns.condensed[chosen.i].toFixed(3)}} kg/m²</p><p v-if="data.terrainTemperature && layer==='temperature'">Reference temperature {{(forecast.fields.air_temperature.values[chosen.i]-273.15).toFixed(2)}} °C<br>Terrain correction {{(data.terrainTemperature.values[chosen.i]-forecast.fields.air_temperature.values[chosen.i]).toFixed(2)}} K</p><p v-if="data.terrainDerivatives">Slope {{data.terrainDerivatives.slope_deg[chosen.i].toFixed(2)}}° · Aspect {{data.terrainDerivatives.aspect_deg[chosen.i]===null?'flat / undefined':data.terrainDerivatives.aspect_deg[chosen.i].toFixed(1)+'°'}}</p><p v-if="data.terrain">Elevation {{data.terrain.values[chosen.i].toFixed(2)}} m · {{data.terrain.vertical_reference}}</p><p v-if="layer==='rainfall'">Quality: {{data.rain.quality[chosen.i]}}</p></template><p v-else class="hint">Select a cell in the scene to inspect its value and local coordinates.</p></section>
   <section class="request"><details class="request-fold" :open="requestOpen" @toggle="requestOpen=$event.target.open"><summary title="Build a request for the C++ coupled-wind CLI: grid, time steps, atmosphere, diffusion and pressure systems."><span class="section-label">04 / COUPLED WIND REQUEST</span><small>{{requestOpen?'fold':'unfold'}}</small></summary><p class="hint">Edit the time steps, grid, atmosphere, diffusion and pressure systems, download the request and run <code>terraflux-run-coupled-wind</code>. No physics runs in the browser; checks mirror the solver bounds.</p>
    <div class="request-form">
     <label class="wide">Start time (UTC)<input v-model="coupledForm.start_time_utc"></label>
     <label>Timestep s<input type="number" min="1" v-model.number="coupledForm.timestep_seconds"></label><label>Steps<input type="number" min="1" step="1" v-model.number="coupledForm.steps"></label>
     <label>Snapshot every N steps<input type="number" min="1" step="1" v-model.number="coupledForm.snapshot_every_steps"></label><label>Density mode<select v-model="coupledForm.density_mode"><option v-for="m in DENSITY_MODES" :key="m" :value="m">{{m}}</option></select></label>
     <label>Cells east (nx)<input type="number" min="2" step="1" v-model.number="coupledForm.nx"></label><label>Cells north (ny)<input type="number" min="2" step="1" v-model.number="coupledForm.ny"></label>
     <label>Spacing east m<input type="number" min="1" v-model.number="coupledForm.spacing_east_m"></label><label>Spacing north m<input type="number" min="1" v-model.number="coupledForm.spacing_north_m"></label>
     <label>Anchor latitude °<input type="number" step="0.1" v-model.number="coupledForm.geographic_anchor_latitude_deg"></label><label>Anchor longitude °<input type="number" step="0.1" v-model.number="coupledForm.geographic_anchor_longitude_deg"></label>
     <label>Coriolis mode<select v-model="coupledForm.coriolis_mode"><option v-for="m in CORIOLIS_MODES" :key="m" :value="m">{{m}}</option></select></label><label>Prescribed f 1/s<input type="number" step="1e-5" :disabled="coupledForm.coriolis_mode!=='prescribed'" v-model.number="coupledForm.coriolis_parameter_s"></label>
     <label>Air density kg/m³<input type="number" step="0.001" v-model.number="coupledForm.air_density_kg_m3"></label><label>Friction k 1/s<input type="number" step="1e-6" v-model.number="coupledForm.surface_friction_coefficient_s"></label>
     <label>Temperature diffusion m²/s<input type="number" min="0" v-model.number="coupledForm.temperature_diffusivity_m2_s"></label><label>Humidity diffusion m²/s<input type="number" min="0" v-model.number="coupledForm.humidity_diffusivity_m2_s"></label>
     <label>Momentum diffusion m²/s<input type="number" min="0" v-model.number="coupledForm.momentum_diffusivity_m2_s"></label><label>Scalar boundary<select v-model="coupledForm.scalar_boundary"><option v-for="m in SCALAR_BOUNDARIES" :key="m" :value="m">{{m}}</option></select></label>
     <label>Gradient boundary<select v-model="coupledForm.pressure_gradient_boundary"><option v-for="m in GRADIENT_BOUNDARIES" :key="m" :value="m">{{m}}</option></select></label><label>Background pressure Pa<input type="number" v-model.number="coupledForm.background_pressure_pa"></label>
     <label>Background temperature K<input type="number" step="0.1" v-model.number="coupledForm.background_temperature_k"></label><label>Background humidity kg/kg<input type="number" step="0.001" v-model.number="coupledForm.background_humidity_kg_kg"></label>
    </div>
    <label class="toggle"><input type="checkbox" v-model="coupledForm.column_enabled"> Vertical column diagnostics (layers, cloud base, CAPE)</label>
    <div v-if="coupledForm.column_enabled" class="request-form">
     <label>Layers<input type="number" min="2" step="1" v-model.number="coupledForm.column_layer_count"></label><label>Layer thickness m<input type="number" min="1" v-model.number="coupledForm.column_layer_thickness_m"></label>
     <label>Environment lapse rate K/m<input type="number" step="0.0005" v-model.number="coupledForm.column_lapse_rate_k_m"></label><label>Tropopause height m<input type="number" min="0" v-model.number="coupledForm.column_tropopause_height_m"></label>
     <label>Humidity scale height m<input type="number" min="1" v-model.number="coupledForm.column_humidity_scale_height_m"></label><label>Parcel step m<input type="number" min="1" v-model.number="coupledForm.column_parcel_step_m"></label>
    </div>
    <label v-if="coupledForm.column_enabled" class="toggle"><input type="checkbox" v-model="coupledForm.inversion_enabled"> Temperature inversion (capping layer storing convective energy)</label>
    <div v-if="coupledForm.column_enabled&&coupledForm.inversion_enabled" class="request-form">
     <label>Inversion base m<input type="number" min="0" v-model.number="coupledForm.inversion_base_m"></label><label>Inversion depth m<input type="number" min="1" v-model.number="coupledForm.inversion_depth_m"></label>
     <label>Inversion strength K<input type="number" min="0" step="0.5" v-model.number="coupledForm.inversion_strength_k"></label><label>Lapse rate above K/m<input type="number" step="0.0005" v-model.number="coupledForm.lapse_rate_above_inversion_k_m"></label>
    </div>
    <div v-for="(c,i) in coupledForm.centers" :key="i" class="center-row"><div class="center-head"><strong>{{c.kind==='low'?'L':'H'}} · Pressure system {{i+1}}</strong><button class="text-button" @click="removeCenter(i)">remove</button></div><div class="request-form">
     <label>Kind<select v-model="c.kind"><option value="high">high</option><option value="low">low</option></select></label><label>Radius m<input type="number" min="1" v-model.number="c.radius_m"></label>
     <label>East m<input type="number" v-model.number="c.east_m"></label><label>North m<input type="number" v-model.number="c.north_m"></label>
     <label>Δ pressure Pa<input type="number" v-model.number="c.pressure_anomaly_pa"></label><label>Δ temperature K<input type="number" step="0.1" v-model.number="c.temperature_anomaly_k"></label>
     <label class="wide">Δ specific humidity kg/kg<input type="number" step="0.001" v-model.number="c.humidity_anomaly_kg_kg"></label>
    </div></div>
    <button class="text-button" @click="addCenter">+ add pressure system</button>
    <div v-for="(a,i) in coupledForm.airmasses" :key="'a'+i" class="center-row"><div class="center-head"><strong>Air mass {{i+1}} (temperature / humidity anomaly)</strong><button class="text-button" @click="removeAirmass(i)">remove</button></div><div class="request-form">
     <label>East m<input type="number" v-model.number="a.east_m"></label><label>North m<input type="number" v-model.number="a.north_m"></label>
     <label>Radius m<input type="number" min="1" v-model.number="a.radius_m"></label><label>Δ temperature K<input type="number" step="0.1" v-model.number="a.temperature_anomaly_k"></label>
     <label class="wide">Δ specific humidity kg/kg<input type="number" step="0.001" v-model.number="a.humidity_anomaly_kg_kg"></label>
    </div></div>
    <button class="text-button" @click="addAirmass">+ add air mass (warm sector / cold pool)</button>
    <label class="toggle"><input type="checkbox" v-model="coupledForm.cloud_enabled" :disabled="!coupledForm.column_enabled"> Cloud formation (fronts, convection, thunderstorms; needs the column)</label>
    <div v-if="coupledForm.cloud_enabled" class="request-form">
     <label>Diurnal heating amplitude K<input type="number" min="0" step="0.5" v-model.number="coupledForm.cloud_heating_amplitude_k"></label><label>Heating phase s (sunrise offset)<input type="number" step="600" v-model.number="coupledForm.cloud_heating_phase_seconds"></label>
     <label v-for="[key,label] in CLOUD_CONFIG_KEYS" :key="key">{{label}}<input type="number" step="any" v-model.number="coupledForm.cloud[key]"></label>
    </div>
    <label v-if="coupledForm.cloud_enabled" class="toggle"><input type="checkbox" v-model="coupledForm.severe_enabled"> Severe convection (tornado parameter from the inversion, low-level shear and helicity)</label>
    <label v-if="coupledForm.cloud_enabled" class="toggle"><input type="checkbox" v-model="coupledForm.precip_enabled"> Precipitation outcomes (rain, snow, sleet, hail, thunderstorms)</label>
    <div v-if="coupledForm.cloud_enabled&&coupledForm.precip_enabled" class="request-form">
     <label v-for="[key,label] in PRECIP_CONFIG_KEYS" :key="key">{{label}}<input type="number" step="any" v-model.number="coupledForm.precip[key]"></label>
    </div>
    <label class="toggle"><input type="checkbox" v-model="coupledForm.orography_enabled"> Orography: mountain ridges (orientation!), basins, foehn, blocking, valley cold pools</label>
    <template v-if="coupledForm.orography_enabled">
     <div class="request-form"><label>Base elevation m<input type="number" v-model.number="coupledForm.terrain_base_elevation_m"></label><label v-for="[key,label] in OROGRAPHY_KEYS" :key="key">{{label}}<input type="number" step="any" v-model.number="coupledForm.orography[key]"></label></div>
     <div v-for="(r,i) in coupledForm.ridges" :key="'ridge'+i" class="center-row"><div class="center-head"><strong>Ridge {{i+1}} · {{r.orientation_deg%180===0?'north–south (blocks zonal flow)':Math.abs(r.orientation_deg%180)===90?'east–west (blocks north–south exchange)':r.orientation_deg+'°'}}</strong><button class="text-button" @click="removeRidge(i)">remove</button></div><div class="request-form">
      <label>Axis orientation ° (0 = N–S, 90 = E–W)<input type="number" step="15" v-model.number="r.orientation_deg"></label><label>Height m<input type="number" min="0" v-model.number="r.height_m"></label>
      <label>Centre east m<input type="number" v-model.number="r.center_east_m"></label><label>Centre north m<input type="number" v-model.number="r.center_north_m"></label>
      <label>Half width m<input type="number" min="1" v-model.number="r.half_width_m"></label><label>Length m<input type="number" min="1" v-model.number="r.length_m"></label>
     </div></div>
     <button class="text-button" @click="addRidge">+ add ridge</button>
     <div v-for="(b,i) in coupledForm.basins" :key="'basin'+i" class="center-row"><div class="center-head"><strong>Basin / valley {{i+1}}</strong><button class="text-button" @click="removeBasin(i)">remove</button></div><div class="request-form">
      <label>Centre east m<input type="number" v-model.number="b.center_east_m"></label><label>Centre north m<input type="number" v-model.number="b.center_north_m"></label>
      <label>Depth m<input type="number" min="0" v-model.number="b.depth_m"></label><label>Radius m<input type="number" min="1" v-model.number="b.radius_m"></label>
     </div></div>
     <button class="text-button" @click="addBasin">+ add basin</button>
    </template>
    <label class="toggle"><input type="checkbox" v-model="coupledForm.surface_enabled"> Surface: straight coastline and sea-surface temperature</label>
    <div v-if="coupledForm.surface_enabled" class="request-form">
     <label>Coast position east m<input type="number" v-model.number="coupledForm.coast_east_m"></label><label>Land side<select v-model="coupledForm.land_side"><option value="west">west</option><option value="east">east</option></select></label>
     <label>Sea-surface temperature K<input type="number" step="0.1" v-model.number="coupledForm.sea_surface_temperature_k"></label><label>SST gradient north K/m<input type="number" step="1e-6" v-model.number="coupledForm.sst_gradient_north_k_per_m"></label>
    </div>
    <label v-if="coupledForm.surface_enabled&&coupledForm.column_enabled" class="toggle"><input type="checkbox" v-model="coupledForm.storms_enabled"> Tropical cyclones (genesis, intensity, Fujiwhara, merger, landfall) with default storm physics</label>
    <template v-if="coupledForm.surface_enabled&&coupledForm.column_enabled&&coupledForm.storms_enabled">
     <div v-for="(x,i) in coupledForm.storm_seeds" :key="'seed'+i" class="center-row"><div class="center-head"><strong>Storm seed {{i+1}}</strong><button class="text-button" @click="removeSeed(i)">remove</button></div><div class="request-form">
      <label>East m<input type="number" v-model.number="x.east_m"></label><label>North m<input type="number" v-model.number="x.north_m"></label>
      <label>Max wind m/s<input type="number" min="1" v-model.number="x.max_wind_m_s"></label><label>Radius of max wind m<input type="number" min="1" v-model.number="x.radius_max_wind_m"></label>
     </div></div>
     <button class="text-button" @click="addSeed">+ add storm seed</button>
    </template>
    <ul class="report"><li v-for="e in coupledReport.errors" :key="e" class="report-error">✕ {{e}}</li><li v-for="w in coupledReport.warnings" :key="w" class="report-warn">△ {{w}}</li><li v-if="!coupledReport.errors.length" class="report-ok">✓ Request passes the configuration checks</li></ul>
    <p v-if="!coupledReport.errors.length" class="hint">f {{coupledReport.estimates.coriolis_parameter_s?.toExponential(3)}} 1/s · inertial period {{fmt(coupledReport.estimates.inertial_period_h,1)}} h · friction time {{fmt(coupledReport.estimates.friction_time_h,1)}} h · cross-isobar angle {{fmt(coupledReport.estimates.cross_isobar_angle_deg,1)}}°<br>Δt(|f|+k) {{fmt(coupledReport.estimates.coriolis_friction_number,3)}} · momentum diffusion number {{fmt(coupledReport.estimates.momentum_diffusion_number,3)}} · scalar stability estimate at spin-up peak {{fmt(coupledReport.estimates.scalar_stability_estimate,2)}}<br>Balanced wind ≈ {{fmt(coupledReport.estimates.balanced_speed_estimate_m_s,1)}} m/s · peak ≈ {{fmt(coupledReport.estimates.peak_speed_estimate_m_s,1)}} m/s · upwind numerical diffusion ≈ {{fmt(coupledReport.estimates.numerical_diffusivity_estimate_m2_s,0)}} m²/s<template v-if="coupledReport.estimates.column_top_m"><br>Column top {{fmt(coupledReport.estimates.column_top_m/1000,1)}} km</template></p>
    <div class="comparison-switch"><button :disabled="coupledReport.errors.length>0" @click="downloadCoupledRequest">⇩ Download request JSON</button><button v-if="data.coupled" @click="useLoadedCoupledSettings">Use loaded run's settings</button><button @click="resetCoupledForm">Reset to example</button></div>
    <p v-if="coupledNote" class="hint">{{coupledNote}}</p>
   </details></section>
  </aside><article><div class="scene-head"><div><span class="eyebrow">FIELD EXPLORER</span><h2>{{WIND_LAYER_KEYS.includes(layer)?WIND_TITLES[layer]:layer==='dustinflux'?'Cosmic dust entering the atmosphere (this hour)':layer==='dustdaily'?'Cosmic dust entering the atmosphere (daily mean)':layer==='sunbrightness'?'Photosphere brightness temperature':layer==='suncontrast'?'Contrast to the quiet Sun':layer==='temperature'?(adjustedTemperature?'Terrain-adjusted temperature':'Air temperature'):layer==='wind'?'Wind speed':layer==='terrain'?'Terrain elevation':layer==='slope'?'Terrain slope':layer==='aspect'?'Downhill aspect':layer==='water'?'Surface water depth':layer==='soil'?'Soil water storage':layer==='cloud'?'Cloud cover':layer==='pressure'?'Sea-level pressure':layer==='humidity'?'Relative humidity':layer==='density'?'Air density':layer==='moisture'?'Specific humidity':layer==='cape'?'Convective available potential energy':layer==='cloudbase'?'Cloud base (lifting condensation level)':layer==='cloudtop'?'Equilibrium level':layer==='clouds'?'Cloud formation · fronts and convection':layer==='precip'?'Precipitation rate at the ground':layer==='accum'?'Precipitation accumulated over the snapshot interval':layer==='stormwind'?'Tropical cyclone wind field':layer==='mpi'?'Maximum potential intensity':layer==='gpi'?'Genesis potential index':layer==='surge'?'Storm surge estimate':layer==='stp'?'Significant tornado parameter':layer==='helicity'?'Storm-relative helicity':layer==='lifting'?'Orographic lifting (up-slope positive)':layer==='blocked'?'Froude blocking of the cross-barrier flow':layer==='foehn'?'Foehn warming on the lee':layer==='coldpool'?'Cold-air pool deficit':layer==='oroprecip'?'Orographic precipitation rate':layer==='valley'?'Valley depth':layer==='discharge'?'River discharge':layer==='surfacewater'?'Surface and floodplain water':layer==='saturation'?'Soil saturation':layer==='watertable'?'Water-table depth below ground':layer==='snowpack'?'Snowpack water equivalent':layer==='erosion'?'Cumulative erosion':layer==='deposition'?'Cumulative deposition':layer==='stability'?'Slope factor of safety':layer==='floodclass'?'Flood hazard class':layer==='current'?'Ocean current speed':layer==='surfacesalinity'?'Surface salinity':layer==='salinity'?'Upper-ocean layer salinity':layer==='sst'?'Upper-ocean layer temperature':layer==='thermocline'?'Thermocline depth (layer thickness)':layer==='freshwater'?'Surface freshwater fraction':layer==='sinking'?'Deep-water formation rate':layer==='oceansst'?'Sea-surface temperature (top layer)':layer==='mld'?'Ocean mixed-layer depth':layer==='netheat'?'Net air–sea heat flux (into the ocean)':layer==='latent'?'Latent heat flux (evaporative cooling)':layer==='evaporation'?'Evaporation from the sea':layer==='searain'?'Rain on sea and land':layer==='coastalstore'?'Coastal runoff store':layer==='icethickness'?'Greenland ice thickness':layer==='icesmb'?'Greenland surface mass balance':layer==='icemelt'?'Greenland surface melt':layer==='seaice'?'Sea-ice concentration':layer==='seaicethick'?'Sea-ice thickness':layer==='oxygen'?'Surface dissolved-oxygen capacity':layer==='coastlost'?'Land lost to the sea':layer==='coastyear'?'Year the land is lost':layer==='coaststorm'?'Design-storm flood depth':layer==='coastinundation'?'Permanent inundation':layer==='coastfrequency'?'Today\'s 100-year flood: now every … years':layer==='coastsetup'?'Wind setup of the design storm':layer==='coastsubsidence'?'Land subsidence':layer==='coastpopulation'?'Population density':layer==='co2air'?'CO₂ in the air (boundary layer)':layer==='co2flux'?'Air–sea CO₂ flux (into the sea)':layer==='ph'?'Surface pH':layer==='omega'?'Surface aragonite saturation Ω':layer==='omegabottom'?'Bottom-water aragonite saturation Ω':layer==='bottomo2'?'Bottom-water oxygen':layer==='habitat'?'Habitat class of the water column':layer==='habitable'?'Habitable share of the water column':layer==='production'?'Plankton net production':layer==='nitrate'?'Surface nitrate':'Rainfall accumulation'}}</h2></div><div class="view-switch"><button :class="{selected:view==='field'}" @click="view='field'">Field explorer</button><button :class="{selected:view==='map'}" :disabled="!data.coupled" @click="view='map'">Weather map</button><button :class="{selected:view==='water'}" :disabled="!data.hydrology" @click="view='water'">Catchment</button><button :class="{selected:view==='hazards'}" :disabled="!data.hydrology" @click="view='hazards'">Hazards</button><button :class="{selected:view==='ocean'}" :disabled="!data.ocean" @click="view='ocean'">Ocean streams</button><button :class="{selected:view==='polar'}" :disabled="!data.polar" @click="view='polar'">Polar melt</button><button :class="{selected:view==='coast'}" :disabled="!data.coast" @click="view='coast'" title="Sea-level rise on the coast: land lost, storm floods, defences, rivers and islands.">Sea level &amp; coasts</button><button :class="{selected:view==='carbon'}" :disabled="!data.carbon" @click="view='carbon'" title="CO₂ in the air and the sea: uptake, acidification, oxygen and the dead zones where sea life cannot live.">Carbon &amp; ocean life</button><button :class="{selected:view==='solar'}" :disabled="!data.solar" @click="view='solar'" title="The sunspot cycle with its minima, maxima and forecast, and what it does to the UV, the ozone, the flares and the climate.">Sun &amp; solar cycle</button><button :class="{selected:view==='dust'}" :disabled="!data.dust" @click="view='dust'" title="Cosmic dust and extraterrestrial water entering the Earth every day.">Cosmic dust</button><button :class="{selected:view==='solarwind'}" :disabled="!data.solarWind" @click="view='solarwind'" title="The solar wind at the magnetosphere's polar caps: aurora, ions escaping to space, cosmic radiation and solar protons, and their global effects.">Solar wind &amp; poles</button></div><div class="scene-actions"><button class="reset" @click="reset" :title="immersive?'Reset the camera (R)':'Reset the camera'">↺ Reset view</button><template v-if="immersive"><button class="reset hud-toggle" :class="{selected:hudControls}" :aria-pressed="hudControls" title="Show or hide the control panel ( [ )" @click="togglePanel('controls')">◧ Controls</button><button class="reset hud-toggle" :class="{selected:hudStream}" :aria-pressed="hudStream" title="Show or hide the data stream ( ] )" @click="togglePanel('stream')">Stream ◨</button></template><button class="reset fullscreen-toggle" :title="immersive?'Return to the page (F or Esc)':'Fullscreen scene with the controls and a live data stream (F)'" @click="toggleImmersive"><svg class="icon" viewBox="0 0 16 16" aria-hidden="true"><path :d="immersive?'M5 1v4H1M15 5h-4V1M11 15v-4h4M1 11h4v4':'M1 5V1h4M11 1h4v4M15 11v4h-4M5 15H1v-4'"/></svg>{{immersive?'Exit':'Fullscreen'}} <kbd>{{immersive?'Esc':'F'}}</kbd></button></div></div><div class="viewport" ref="host" aria-label="Interactive 3D weather grid"><div v-if="fallback" class="fallback-grid" :style="{gridTemplateColumns:`repeat(${grid.nx},1fr)`}"><button v-for="(value,i) in values" :key="i" :aria-label="`Cell ${i}: ${value===null?'no data':value.toFixed(2)} ${unit}`" :style="{gridRow:grid.ny-Math.floor(i/grid.nx),gridColumn:i%grid.nx+1,background:view!=='field'?mapCellStyles[i]:value===null?'#283b49':layer==='clouds'?CLOUD_TYPES[value].color:`hsl(${hue(value)} ${layer==='cloud'?15:70}% ${layer==='cloud'?18+value*.72:50}%)`}" @click="openInspector(i,$event)"></button></div><svg v-if="fallback && view==='dust' && data.dust && dustLayers.sources" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Meteoroid sources"><text v-for="pnt in data.dust.points" :key="pnt.name" :x="(pnt.longitude_deg+180)/360*100" :y="(90-pnt.latitude_deg)/180*100" font-size="4" text-anchor="middle" :fill="pnt.name==='apex'?'#ffd166':'#e8f4ff'">{{SOURCE_SYMBOLS[pnt.name]??'•'}}</text><text :x="(data.dust.subsolar.longitude_deg+180)/360*100" :y="(90-data.dust.subsolar.latitude_deg)/180*100" font-size="4" text-anchor="middle" fill="#ffb347">☀</text></svg><svg v-if="fallback && view==='solar' && data.solar" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Solar flares"><template v-for="(fl,k) in data.solar.flares" :key="'sf'+k"><text v-if="data.solar.regions[fl.region]" :x="(regionPosition(data.solar.regions[fl.region]).x/solarBlock.disk.extent_radii+1)*50" :y="(1-regionPosition(data.solar.regions[fl.region]).y/solarBlock.disk.extent_radii)*50" font-size="3.5" text-anchor="middle" :fill="R_COLORS[fl.r_scale]">⚡</text></template></svg><svg v-if="fallback && data.atmosphere" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Synthetic cloud and weather-system overlay"><template v-if="cloudsEnabled"><ellipse v-for="(c,i) in data.atmosphere.cloud" :key="'cloud'+i" v-show="i%3===0 && c>.35" :cx="((i%grid.nx)+.5)/grid.nx*100" :cy="(1-(Math.floor(i/grid.nx)+.5)/grid.ny)*100" :rx="2.1" :ry="1.7" fill="#e9f5ff" :opacity="c*.32"/></template><template v-if="systemsEnabled"><template v-for="(s,i) in data.atmosphere.systems" :key="i"><polyline v-if="s.kind==='cold_front'" :points="s.points.map(([u,v])=>`${u*100},${(1-v)*100}`).join(' ')" fill="none" stroke="#85ccff" stroke-width=".7" stroke-dasharray="2 1"/><text v-else :x="s.u*100" :y="(1-s.v)*100" :fill="s.kind==='low'?'#ffb39c':'#c5e9ff'" font-size="7" font-weight="bold" text-anchor="middle">{{s.kind==='low'?'L':'H'}}</text></template></template></svg><svg v-if="fallback && view==='map'" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Weather map overlays"><template v-for="level in isobarLines" :key="level.level_pa"><polyline v-for="(line,li) in level.lines" :key="li" :points="line.map(([u,v])=>`${u*100},${(1-v)*100}`).join(' ')" fill="none" :stroke="Math.round(level.level_hpa)%(mapLayers.isobarIntervalHpa*2)===0?'#f3f7f9':'#a9bcc6'" :stroke-width="Math.round(level.level_hpa)%(mapLayers.isobarIntervalHpa*2)===0?.5:.3"/><text v-if="Math.round(level.level_hpa)%(mapLayers.isobarIntervalHpa*2)===0&&level.lines[0]" :x="level.lines[0][0][0]*100" :y="(1-level.lines[0][0][1])*100" font-size="2.6" fill="#f3f7f9">{{Math.round(level.level_hpa)}}</text></template><template v-if="mapLayers.fronts&&data.clouds"><template v-for="(t,i) in data.clouds.frontType" :key="'f'+i"><rect v-if="t!=='none'" :x="(i%grid.nx)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+1)/grid.ny)*100" :width="100/grid.nx" :height="100/grid.ny" :fill="FRONT_COLORS[t]" opacity=".5"/></template></template><template v-if="mapLayers.symbols&&data.precipitation"><template v-for="(t,i) in data.precipitation.type" :key="'p'+i"><text v-if="t!=='none'&&(i%grid.nx)%symbolStride===0&&Math.floor(i/grid.nx)%symbolStride===0" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.3)/grid.ny)*100" font-size="2.8" text-anchor="middle" :fill="data.precipitation.phase[i]==='solid'?'#ffffff':'#bfe0ff'">{{precipitationTypeInfo(t).symbol}}</text></template></template><template v-if="mapLayers.storms&&data.storms"><template v-for="s in data.storms.list" :key="'st'+s.id"><polyline v-if="s.track.length>1" :points="s.track.map(([e,n])=>`${(e-grid.origin_east_m)/(grid.nx*grid.spacing_east_m)*100},${(1-(n-grid.origin_north_m)/(grid.ny*grid.spacing_north_m))*100}`).join(' ')" fill="none" stroke="#ffffff" stroke-width=".4" opacity=".7"/><circle :cx="(s.east_m-grid.origin_east_m)/(grid.nx*grid.spacing_east_m)*100" :cy="(1-(s.north_m-grid.origin_north_m)/(grid.ny*grid.spacing_north_m))*100" :r="Math.max(1.5,s.radius_max_wind_m/(grid.nx*grid.spacing_east_m)*100)" fill="none" :stroke="categoryInfo(s.category).color" stroke-width=".8"/><text :x="(s.east_m-grid.origin_east_m)/(grid.nx*grid.spacing_east_m)*100" :y="(1-(s.north_m-grid.origin_north_m)/(grid.ny*grid.spacing_north_m))*100-2.5" font-size="3" text-anchor="middle" :fill="categoryInfo(s.category).color">{{categoryInfo(s.category).short}}</text></template><text v-for="(e,i) in visibleEvents.filter(e=>['landfall','merger','genesis'].includes(e.kind))" :key="'ev'+i" :x="(e.east_m-grid.origin_east_m)/(grid.nx*grid.spacing_east_m)*100" :y="(1-(e.north_m-grid.origin_north_m)/(grid.ny*grid.spacing_north_m))*100+1" font-size="3" text-anchor="middle" :fill="e.kind==='landfall'?'#ff5c5c':'#ffffff'">{{e.kind==='landfall'?'✖':e.kind==='merger'?'⊕':'✦'}}</text></template><template v-if="data.orography"><template v-for="(b,i) in data.orography.foehn" :key="'fo'+i"><text v-if="mapLayers.foehn&&b" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.4)/grid.ny)*100" font-size="3" text-anchor="middle" fill="#ff8c42">F</text></template><template v-for="(b,i) in data.orography.coldPoolFlag" :key="'cp'+i"><text v-if="mapLayers.coldPools&&b" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.4)/grid.ny)*100" font-size="3" text-anchor="middle" fill="#dff3ff">❄</text></template></template><template v-if="mapLayers.tornado&&data.severe"><template v-for="(b,i) in data.severe.possible" :key="'t'+i"><text v-if="b" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.5)/grid.ny)*100" font-size="3.2" text-anchor="middle">🌪</text></template></template><template v-if="mapLayers.hazards&&data.precipitation"><template v-for="(b,i) in data.precipitation.lightning" :key="'h'+i"><text v-if="b" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.7)/grid.ny)*100" font-size="3" text-anchor="middle" fill="#ffd166">{{data.precipitation.hail[i]>0?'⚡◆':'⚡'}}</text></template></template></svg><svg v-if="fallback && view==='field' && data.clouds && frontOverlay" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Front overlay"><template v-for="(t,i) in data.clouds.frontType" :key="i"><rect v-if="t!=='none'" :x="(i%grid.nx)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+1)/grid.ny)*100" :width="100/grid.nx" :height="100/grid.ny" :fill="FRONT_COLORS[t]" opacity=".55"/></template><template v-for="(s,i) in data.clouds.thunderstorm" :key="'s'+i"><text v-if="s" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.3)/grid.ny)*100" font-size="3" text-anchor="middle" fill="#ffd166">⚡</text></template></svg><svg v-if="fallback && data.surfaceWater?.outlets?.length" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Configured surface outlet locations"><circle v-for="(o,i) in data.surfaceWater.outlets" :key="i" :cx="(o.x+.5)/grid.nx*100" :cy="(1-(o.y+.5)/grid.ny)*100" r="1.6" fill="#ffa15c" stroke="#fff2e6" stroke-width=".4"/></svg><div class="scene-note">{{view==='solarwind'&&data.solarWind?(fallback?'2D FALLBACK / NORTH UP':windLayers.globe?'SUN +X · NORTH UP · GLOBE IN A SUN-FIXED FRAME':'NORTH −Z / EAST +X · WORLD MAP'):view==='dust'&&data.dust?(fallback?'2D FALLBACK / NORTH UP':'NORTH −Z / EAST +X · WORLD MAP'):view==='solar'&&data.solar?(fallback?'2D FALLBACK / SOLAR NORTH UP':'SOLAR NORTH −Z / WEST LIMB +X'):fallback?'2D FALLBACK / NORTH UP':'NORTH −Z / EAST +X'}}<br><span>{{view==='solarwind'&&data.solarWind?(windLayers.globe&&!fallback?`Earth radius 6 units; beyond it distances are compressed (r^0.45): magnetopause ${windMetrics.standoff.toFixed(1)} R_E`:'Equirectangular 4° grid, 180° W to 180° E')+' · colour: '+WIND_UNITS[windKey]:view==='dust'&&data.dust?'Equirectangular 5° grid, 180° W to 180° E · height and colour: dust influx':view==='solar'&&data.solar?'The visible hemisphere as seen from the Earth (orthographic) · colour: photosphere':fallback?'Color encodes field value · select a cell':data.terrain&&(terrainEnabled||layer==='terrain')?'Height: relative terrain · color: selected field':'Height encodes field value · flat reference surface'}}</span></div><div v-if="view==='carbon'&&carbonKey" class="cloud-key map-key" v-wobbly="'key'" title="Drag to move · double-click to put back"><span v-if="carbonKey.sea"><i :style="{background:carbonKey.sea.css}"></i>{{carbonKey.sea.label}}</span><template v-if="carbonLayers.base==='habitat'"><span v-for="h in HABITAT_CLASSES" :key="h.key"><i :style="{background:h.color}"></i>{{h.label}}</span></template><span v-if="carbonKey.air"><i :style="{background:carbonKey.air.css}"></i>{{carbonKey.air.label}}</span><span v-if="carbonLayers.dead"><i style="background:transparent;border:2px solid #ff3b30"></i>dead zone (hypoxic bottom water)</span><span v-if="carbonLayers.corrosive"><i style="background:transparent;border:2px solid #ff4fd8"></i>corrosive water (Ω &lt; 1)</span><span v-if="carbonLayers.anoxic">☠ anoxic column</span></div><div v-else-if="view==='solar'&&data.solar" class="cloud-key map-key" v-wobbly="'key'" title="Drag to move · double-click to put back"><span v-if="solarLayers.base==='contrast'"><i :style="{background:`linear-gradient(90deg,${contrastColor(-40)},${contrastColor(0)},${contrastColor(4)})`}"></i>spot −40 % · quiet Sun · facula +4 %</span><template v-else-if="solarLayers.base==='cycle'"><span><i style="background:#6fa8ff"></i>groups of the previous cycle</span><span><i style="background:#ffb347"></i>groups of the cycle in progress</span><span><i style="background:#9fe870"></i>groups of the next cycle</span></template><span v-else><i :style="{background:`linear-gradient(90deg,${brightnessColor(4800)},${brightnessColor(5500)},${brightnessColor(6200)})`}"></i>4800 · 5500 · 6200 K (limb → centre)</span><span v-if="solarLayers.flares"><i :style="{background:R_COLORS[1]}"></i>M flare (R1–R2) <i :style="{background:R_COLORS[3]}"></i>X flare (R3+)</span><span>{{data.solar.regions.length}} groups · {{data.solar.flares.length}} flares ≥ M1 this month</span></div><div v-else-if="view==='solarwind'&&data.solarWind" class="cloud-key map-key" v-wobbly="'key'" title="Drag to move · double-click to put back"><span v-if="windKey!=='windregion'"><i :style="{background:`linear-gradient(90deg,${windColor(windKey,windRange[0],windRange[0],windRange[1])},${windColor(windKey,windRange[0]+.25*(windRange[1]-windRange[0]),windRange[0],windRange[1])},${windColor(windKey,windRange[0]+.6*(windRange[1]-windRange[0]),windRange[0],windRange[1])},${windColor(windKey,windRange[1],windRange[0],windRange[1])})`}"></i>{{formatWind(windRange[0],2)}} … {{formatWind(windRange[1],2)}} {{WIND_UNITS[windKey]}}</span><template v-else><span v-for="r in REGIONS" :key="r.key"><i :style="{background:r.color}"></i>{{r.label}}</span></template><span v-if="windLayers.ovals&&!fallback"><b style="color:#ff9f5a">—</b> polar-cap edge · <b style="color:#58e07a">—</b> oval edge</span><span v-if="windLayers.globe&&windLayers.field&&!fallback"><b style="color:#7fb8ff">—</b> closed · <b style="color:#ffa65a">—</b> open field lines</span><span v-if="windLayers.night">darker: night side</span></div><div v-else-if="view==='dust'&&data.dust" class="cloud-key map-key" v-wobbly="'key'" title="Drag to move · double-click to put back"><span><i :style="{background:`linear-gradient(90deg,${dustRampColor(0,0,1)},${dustRampColor(.5,0,1)},${dustRampColor(1,0,1)})`}"></i>{{formatNumber(dustRange[0],3)}} … {{formatNumber(dustRange[1],3)}} µg/m²/day</span><span v-if="dustLayers.night&&dustLayers.base==='hour'">darker: night side</span><span>green tint: land</span><span v-if="dustLayers.sources">☄ apex · ◐ helion · ◑ antihelion · △▽ toroidal · ☀ Sun</span></div><div v-else-if="view==='map'" class="cloud-key map-key" v-wobbly="'key'" title="Drag to move · double-click to put back"><span v-if="mapLayers.base!=='none'"><i :style="{background:mapLayers.base==='temperature'?'linear-gradient(90deg,hsl(223 55% 30%),hsl(11 55% 42%))':'linear-gradient(90deg,hsl(260 35% 26%),hsl(140 35% 44%))'}"></i>{{mapLayers.base==='temperature'?`Temperature ${(temperatureRange[0]-273.15).toFixed(0)}–${(temperatureRange[1]-273.15).toFixed(0)} °C`:`Pressure ${(pressureRange[0]/100).toFixed(0)}–${(pressureRange[1]/100).toFixed(0)} hPa`}}</span><span v-if="mapLayers.isobars">━ isobars {{mapLayers.isobarIntervalHpa}} hPa (bright = {{mapLayers.isobarIntervalHpa*2}} hPa)</span><template v-if="mapLayers.precipitation&&data.precipitation"><span v-for="r in [0.5,3,15,40]" :key="r"><i :style="{background:rateColor(r)}"></i>{{r}} mm/h</span></template><template v-if="mapLayers.clouds&&data.clouds"><span v-for="t in CLOUD_TYPES.slice(1)" :key="t.key"><i :style="{background:t.color}"></i>{{t.abbreviation}}</span></template><span v-if="mapLayers.fronts&&data.clouds"><i style="background:#4da3ff"></i>cold <i style="background:#ff6b6b"></i>warm <i style="background:#c792ea"></i>stationary</span><template v-if="(mapLayers.storms||mapLayers.stormWind)&&data.storms"><span v-for="c in CATEGORIES" :key="c.key"><i :style="{background:c.color}"></i>{{c.short}} {{c.label}}</span></template><span v-if="mapLayers.surge&&data.storms"><i style="background:#00e5ff"></i>storm surge</span><template v-if="mapLayers.tornado&&data.severe"><span><i style="background:#ffe066"></i>STP low <i style="background:#ff7f50"></i>moderate <i style="background:#c724b1"></i>significant</span></template><template v-if="data.orography"><span v-if="mapLayers.lifting"><i style="background:#3da5ff"></i>up-slope <i style="background:#ff8c42"></i>descent</span><span v-if="mapLayers.coldPools"><i style="background:#9ad7ff"></i>cold pool ❄</span><span v-if="mapLayers.foehn">F foehn</span><span v-if="mapLayers.blocking"><i style="background:#f0e68c"></i>blocked flow</span></template></div><div v-else-if="layer==='clouds'" class="cloud-key" v-wobbly="'key'" title="Drag to move · double-click to put back"><span v-for="t in CLOUD_TYPES" :key="t.key"><i :style="{background:t.color}"></i>{{t.abbreviation}} {{t.label}}</span></div><div v-else class="legend" v-wobbly="'key'" title="Drag to move · double-click to put back"><span>{{range[0].toFixed(1)}} {{unit}}<small>← {{legendWords.less}}</small></span><div :style="layer==='aspect'?{background:'linear-gradient(90deg,red,yellow,lime,cyan,blue,magenta,red)'}:['rainfall','water','soil'].includes(layer)?{background:'linear-gradient(90deg,#2675cc,#26ccb2)'}:{}"></div><span>{{range[1].toFixed(1)}} {{unit}}<small>{{legendWords.more}} →</small></span></div><div v-if="layer==='aspect'" class="aspect-key">0° N · 90° E · 180° S · 270° W · gray = flat</div><div v-if="waveNote&&!fallback" class="wave-note" :class="{second:reliefInfo}" title="The engine has no wave model: heights, periods and directions are estimated from the wind with standard growth laws. Waves are drawn enlarged; their direction is kept, longer and higher waves are drawn longer and higher, and the crests move in proportion to their real speed.">WAVES UP TO {{waveNote.highest.toFixed(1)}} M ({{waveNote.state.toUpperCase()}}) · FROM THE WIND · DRAWN ENLARGED, CRESTS MOVING IN PROPORTION TO THEIR REAL SPEED</div><div v-if="reliefInfo&&!fallback" class="relief-note" :title="reliefInfo.sqrt?'Heights grow with the square root of the elevation, so that metres of lowland stay visible next to hills.':'Mountains drawn at true scale would be nearly flat at this map size, so heights are stretched by this factor.'">{{reliefInfo.sqrt?'HEIGHTS √-SCALED TO SHOW THE LOWLAND':`HEIGHTS ×${reliefInfo.factor>=10?Math.round(reliefInfo.factor):reliefInfo.factor.toFixed(1)} (VERTICAL EXAGGERATION)`}}<template v-if="reliefInfo.realistic"> · SNOW LINE ≈ {{Math.round(reliefInfo.snowline/50)*50}} M</template></div><div class="gestures">{{fallback?'CLICK A POINT TO HAVE IT EXPLAINED':'HOVER TO LIGHT UP A POINT · CLICK TO HAVE IT EXPLAINED · DRAG TO ORBIT · SCROLL TO ZOOM'}}</div><div v-if="hoverInfo&&hoverAt&&!fallback&&!(inspecting&&hoverCell===selected)" class="hover-tip" :class="{flip:hoverAt.x>viewportSize.width-260}" :style="{left:`${hoverAt.x}px`,top:`${hoverAt.y}px`}" aria-hidden="true"><b>{{hoverInfo.value}}</b><em>{{hoverInfo.unit}}</em><span>{{hoverInfo.short}}</span><small>click to have it explained</small></div><InspectOverlay v-if="inspectContent&&inspectAnchor" :content="inspectContent.content" :type-key="inspectContent.typeKey" :anchor="inspectAnchor" :bounds="inspectBounds" :series="inspectSeries" :series-index="frameIndex" :wind="inspectContent.wind" :neighbourhood="inspectContent.near" :scale-stops="inspectContent.stops" :unit="unit" @close="closeInspector"/></div><div v-if="snapshots.length>1" class="timeline"><div class="playback"><button @click="seek(frameIndex-1)" :disabled="frameIndex===0" aria-label="Previous frame">‹</button><button @click="togglePlayback">{{playing?'Pause':'Play'}}</button><button @click="seek(frameIndex+1)" :disabled="frameIndex===snapshots.length-1" aria-label="Next frame">›</button><span>Frame {{frameIndex+1}} / {{snapshots.length}} · {{playbackLabel}}</span></div><input aria-label="Timeline frame" type="range" min="0" :max="snapshots.length-1" :value="frameIndex" @input="seek($event.target.value)"><p>{{data.solarWind?`Snapshots every ${windBlock.storm.frame_every_hours} h of a synthetic storm · colour scale fixed over the storm · Sun-fixed frame: the Earth turns beneath the magnetosphere`:data.dust?`Hourly snapshots over one day · colour scale fixed over the day · total influx is a scenario (estimates 5–300 t/day)`:data.orography?`Engine orography · ${data.orography.foehnCells} foehn cells · ${data.orography.poolCells} cold pools · surface ${(data.orography.tendency*3600).toFixed(2)} K/h`:data.storms?`${data.storms.synthetic?'Synthetic mock':'Engine'} storms · ${data.storms.list.length} active · events so far: ${visibleEvents.map(e=>e.kind).join(', ')||'none'}`+(data.severe?` · tornado possible in ${data.severe.possibleCells} cells`:''):data.precipitation?`${data.precipitation.synthetic?'Synthetic mock':'Engine'} outcomes · ${data.precipitation.precipitating} precipitating cells · ${data.precipitation.lightningCells} thunderstorms · ${data.precipitation.hailCells} hail · ${data.precipitation.snowCells} snow · max ${data.precipitation.maxRate.toFixed(1)} mm/h`:data.clouds?`${data.clouds.synthetic?'Synthetic mock':'Engine'} cloud snapshots · heating ${data.clouds.heating.toFixed(1)} K · ${data.clouds.cloudy} cloudy cells · ${data.clouds.storms} thunderstorm cells · ${data.clouds.fronts} frontal cells`:data.coupled?`Coupled engine snapshots · fixed color scale · pressure static · scalar stability number ${data.coupled.stability.toFixed(2)} · supersaturated cells ${data.coupled.supersaturated}`:data.atmosphere?'Synthetic regional snapshots · fixed color scale · no coupling to local engine physics':'Discrete solver snapshots · fixed color scale · rainfall accumulates from the start · unavailable at time zero'}}</p></div><p v-if="layer==='rainfall' && data.rain" class="rain-interval">Accumulation interval: {{data.rain.period_start_utc.replace('T',' ').replace('Z',' UTC')}} → {{data.rain.valid_time_utc.replace('T',' ').replace('Z',' UTC')}}</p><p v-if="['water','soil'].includes(layer) && data.surfaceWater" class="rain-interval">Surface {{data.surfaceWater.stored_volume_m3.toFixed(2)}} m³ · Soil {{data.surfaceWater.soil_volume_m3.toFixed(2)}} m³ · Rain input {{data.surfaceWater.cumulative_rainfall_volume_m3.toFixed(2)}} m³ · Evaporated {{data.surfaceWater.evaporated_volume_m3.toFixed(2)}} m³ · Drained {{data.surfaceWater.drained_volume_m3.toFixed(2)}} m³ · Discharged {{data.surfaceWater.discharged_volume_m3.toFixed(2)}} m³ · Balance error {{data.surfaceWater.balance_error_m3.toExponential(2)}} m³<br>{{data.surfaceWater.boundary==='configured_outlets'?'Configured surface outlets':'Closed boundaries'}} · unknown rainfall cells are impermeable masks · prototype routing<br>Step {{data.surfaceWater.parameters.timestep_seconds}} s · Infiltration {{(data.surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h · Evaporation {{(data.surfaceWater.parameters.evaporation_rate_m_s*3600000).toFixed(2)}} mm/h · Drainage {{(data.surfaceWater.parameters.drainage_rate_m_s*3600000).toFixed(2)}} mm/h<span v-if="data.surfaceWater.soil_capacity_m!==null"> · soil capacity {{data.surfaceWater.soil_capacity_m*1000}} mm</span></p><section v-if="comparisonFrames" class="comparison"><div class="section-label">SCENARIO COMPARISON · SAME FORCING / TIME</div><div class="comparison-switch"><button @click="exportComparison">Export all frames (CSV)</button><button :class="{selected:comparisonView==='baseline'}" @click="comparisonView='baseline'">Show baseline</button><button :class="{selected:comparisonView==='variant'}" @click="comparisonView='variant'">Show {{comparisonName}}</button></div><p>Viewing {{comparisonView==='baseline'?'baseline':comparisonName}} · Δ = variant − baseline · volumes in m³</p><table><thead><tr><th>Metric</th><th>Baseline</th><th>{{comparisonName}}</th><th>Δ</th></tr></thead><tbody><tr v-for="row in comparisonRows" :key="row.label"><th>{{row.label}}</th><td>{{row.baseline.toFixed(2)}}</td><td>{{row.variant.toFixed(2)}}</td><td>{{row.delta>=0?'+':''}}{{row.delta.toFixed(2)}}</td></tr></tbody></table><div class="comparison-chart"><small>SURFACE STORAGE OVER TIME · full timeline, shared scale<br>Green = baseline · Blue = variant · dashed line = selected time</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Surface water volume comparison"><polyline :points="chartPath(snapshots)" fill="none" stroke="#b1d9a4" stroke-width=".6"/><polyline :points="chartPath(comparisonFrames)" fill="none" stroke="#75bbfa" stroke-width=".6"/><line :x1="frameIndex/(snapshots.length-1)*100" :x2="frameIndex/(snapshots.length-1)*100" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0 → {{snapshots.at(-1).forecast.elapsed_seconds/60}} minutes · 0–{{chartMaximum.toFixed(0)}} m³</small></div><p>Baseline infiltration {{(snapshots[0].surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h · Variant {{(comparisonFrames[0].surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h</p></section><section v-if="outletSeries.length" class="comparison outlet-report"><div class="section-label">SURFACE OUTLET DISCHARGE</div><p>Orange markers identify configured boundary outlets. Discharged water leaves the modeled domain and remains in the mass budget.</p><div class="outlet-charts"><div class="comparison-chart"><small>CUMULATIVE DISCHARGE · m³</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Cumulative outlet discharge"><polyline :points="outletPath('cumulative_m3',outletVolumeMax)" fill="none" stroke="#ffb77a" stroke-width=".7"/><line :x1="outletTimePosition" :x2="outletTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{outletVolumeMax.toFixed(1)}} m³ · 0–{{visibleFrames.at(-1).forecast.elapsed_seconds/60}} min</small></div><div class="comparison-chart"><small>INTERVAL-MEAN DISCHARGE · m³/s</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Interval mean outlet discharge"><polyline :points="outletPath('mean_rate_m3_s',outletRateMax)" fill="none" stroke="#75bbfa" stroke-width=".7"/><line :x1="outletTimePosition" :x2="outletTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{outletRateMax.toFixed(2)}} m³/s · averages between snapshots</small></div></div><p>Selected time: discharged {{outletSeries[frameIndex].cumulative_m3.toFixed(2)}} m³ · interval mean {{outletSeries[frameIndex].mean_rate_m3_s===null?'no preceding interval':outletSeries[frameIndex].mean_rate_m3_s.toFixed(3)+' m³/s'}}</p></section><div class="metrics extra-metrics"><div v-if="data.solarWind"><small>GLOBAL MAP · CELL</small><strong class="date">{{grid.nx}} × {{grid.ny}} cells of {{(360/grid.nx).toFixed(0)}}°</strong></div><div v-else-if="data.dust"><small>GLOBAL MAP · CELL</small><strong class="date">{{grid.nx}} × {{grid.ny}} cells of {{(360/grid.nx).toFixed(0)}}°</strong></div><div v-else-if="data.solar"><small>SOLAR DISK · GRID CELL</small><strong class="date">radius {{(solarBlock.disk.solar_radius_m/1e3).toLocaleString()}} km · cell {{(grid.spacing_east_m/1e3).toLocaleString(undefined,{maximumFractionDigits:0})}} km</strong></div><div v-else><small>DOMAIN AREA</small><strong>{{metrics.area_km2.toLocaleString()}} <em>km²</em></strong></div><div v-if="!data.coast&&!data.solar&&!data.dust&&!data.solarWind"><small>TEMPERATURE MIN / MEAN / MAX</small><strong class="date">{{metrics.temperature_min_c.toFixed(1)}} / {{metrics.temperature_mean_c.toFixed(1)}} / {{metrics.temperature_max_c.toFixed(1)}} °C</strong></div><div v-if="!data.coast&&!data.solar&&!data.dust&&!data.solarWind"><small>WIND MEAN / MAX</small><strong class="date">{{metrics.wind_mean_m_s.toFixed(1)}} / {{metrics.wind_max_m_s.toFixed(1)}} m/s</strong></div><template v-if="data.atmosphere"><div><small>MEAN CLOUD FRACTION</small><strong>{{metrics.cloud_pct.toFixed(1)}} <em>%</em></strong></div><div><small>PRESSURE MIN / MAX</small><strong class="date">{{metrics.pressure_min_hpa.toFixed(1)}} / {{metrics.pressure_max_hpa.toFixed(1)}} hPa</strong></div><div><small>MEAN RELATIVE HUMIDITY</small><strong>{{metrics.humidity_mean_pct.toFixed(1)}} <em>%</em></strong></div></template><template v-if="data.rain"><div><small>RAINFALL COVERAGE</small><strong>{{metrics.rain_coverage_pct.toFixed(1)}} <em>%</em></strong></div><div><small>MEAN RAIN · COVERED CELLS</small><strong>{{metrics.rain_mean_mm?.toFixed(2)??'Unknown'}} <em>mm</em></strong></div></template><template v-if="data.surfaceWater"><div><small>WET AREA · DEPTH &gt; 0.1 MM</small><strong>{{metrics.wet_area_km2.toFixed(2)}} <em>km²</em></strong></div><div><small>MAX SURFACE WATER</small><strong>{{metrics.water_max_mm?.toFixed(2)}} <em>mm</em></strong></div><div><small>MEAN SOIL CAPACITY USED</small><strong>{{metrics.soil_fill_pct?.toFixed(1)??'Unknown'}} <em>%</em></strong></div></template></div><div v-if="orographyMetrics" class="metrics extra-metrics"><div><small>FOEHN CELLS · MAX WARMING</small><strong class="date">{{orographyMetrics.foehn_cells}} · +{{orographyMetrics.max_foehn_warming_k.toFixed(1)}} K</strong></div><div><small>COLD POOLS · MAX DEFICIT</small><strong class="date">{{orographyMetrics.cold_pool_cells}} · −{{orographyMetrics.max_cold_pool_k.toFixed(1)}} K</strong></div><div><small>RISING / SINKING AIR</small><strong class="date">{{orographyMetrics.rising_pct.toFixed(0)}} % / {{orographyMetrics.sinking_pct.toFixed(0)}} %</strong></div><div><small>MAX BLOCKED · OROGRAPHIC RAIN</small><strong class="date">{{(orographyMetrics.max_blocked_fraction*100).toFixed(0)}} % · {{orographyMetrics.max_precipitation_rate.toFixed(1)}} mm/h</strong></div><div><small>RIDGE ORIENTATION</small><strong class="date">{{orographyMetrics.orientation_dominant_deg===null?'—':orographyMetrics.orientation_dominant_deg.toFixed(0)+'° (0 = N–S, 90 = E–W)'}}</strong></div><div><small>SURFACE TENDENCY</small><strong class="date">{{orographyMetrics.surface_tendency_k_h>=0?'+':''}}{{orographyMetrics.surface_tendency_k_h.toFixed(2)}} K/h {{orographyMetrics.surface_tendency_k_h<0?'(night)':orographyMetrics.surface_tendency_k_h>0?'(day)':''}}</strong></div></div><div v-if="hydrologyMetrics" class="metrics extra-metrics"><div><small>PEAK DISCHARGE · FLOODED AREA</small><strong class="date">{{hydrologyMetrics.max_discharge_m3_s.toFixed(1)}} m³/s · {{hydrologyMetrics.flooded_area_km2.toFixed(0)}} km²</strong></div><div><small>FLOOD CLASSES L / M / H / X</small><strong class="date">{{hydrologyMetrics.class_cells.low}} / {{hydrologyMetrics.class_cells.medium}} / {{hydrologyMetrics.class_cells.high}} / {{hydrologyMetrics.class_cells.extreme}}</strong></div><div><small>LANDSLIDES · DEBRIS</small><strong class="date">{{hydrologyMetrics.landslides_total}} ({{hydrologyMetrics.landslides_now}} now) · {{(hydrologyMetrics.landslide_volume_m3/1000).toFixed(0)}} ×10³ m³</strong></div><div><small>SEDIMENT YIELD · MAX EROSION</small><strong class="date">{{hydrologyMetrics.sediment_yield_t.toFixed(0)}} t · {{hydrologyMetrics.max_erosion_mm.toFixed(2)}} mm</strong></div><div><small>RAINING · SOIL SATURATION</small><strong class="date">{{hydrologyMetrics.raining_pct.toFixed(0)}} % · {{(hydrologyMetrics.mean_saturation*100).toFixed(0)}} %</strong></div><div><small>RUNOFF RATIO · WARNINGS</small><strong class="date">{{hydrologyMetrics.runoff_ratio===null?'—':(hydrologyMetrics.runoff_ratio*100).toFixed(0)+' %'}} · {{hydrologyMetrics.threshold_cells}} cells</strong></div><div><small>STORES · hm³ (snow / surface / soil / gw / rivers / lakes)</small><strong class="date">{{(hydrologyMetrics.storage.snow/1e6).toFixed(1)}} / {{(hydrologyMetrics.storage.surface/1e6).toFixed(1)}} / {{(hydrologyMetrics.storage.soil/1e6).toFixed(0)}} / {{(hydrologyMetrics.storage.groundwater/1e6).toFixed(0)}} / {{(hydrologyMetrics.storage.channels/1e6).toFixed(1)}} / {{(hydrologyMetrics.storage.lakes/1e6).toFixed(1)}}</strong></div><div><small>CUMULATIVE · hm³ (rain → sea / edge / ET)</small><strong class="date">{{(hydrologyMetrics.cumulative.precipitation/1e6).toFixed(1)}} → {{(hydrologyMetrics.cumulative.ocean_discharge/1e6).toFixed(1)}} / {{(hydrologyMetrics.cumulative.edge_outflow/1e6).toFixed(1)}} / {{(hydrologyMetrics.cumulative.evapotranspiration/1e6).toFixed(1)}}</strong></div><div><small>NETWORK</small><strong class="date">{{hydrologyMetrics.basins}} basins · {{hydrologyMetrics.lakes}} lakes · {{hydrologyMetrics.channel_km}} river cells</strong></div></div><template v-if="view==='carbon'&&carbonMetrics"><div class="metrics extra-metrics"><div title="The background CO₂ of the scenario (the global concentration that flows in and that the boundary layer mixes with) and the highest value in the region, next to the strongest source."><small>CO₂ IN THE AIR · BACKGROUND / HIGHEST</small><strong class="date">{{carbonMetrics.background_ppm.toFixed(1)}} / {{carbonMetrics.max_ppm.toFixed(1)}} ppm <em>(+{{carbonMetrics.max_excess_ppm.toFixed(1)}} near {{carbonMetrics.peak_source}})</em></strong></div><div title="CO₂ the sea takes up from the air (positive) or releases (negative), averaged since the previous frame. Warm water holds less CO₂: in summer the sea can breathe it out, in winter it takes it up."><small>OCEAN CO₂ UPTAKE · MONTH MEAN</small><strong class="date">{{signed(carbonMetrics.uptake_mt_co2_yr)}} Mt CO₂/yr <em>({{signed(carbonMetrics.mean_flux_mol_m2_yr,2)}} mol/m²/yr · sink over {{carbonMetrics.uptake_share_pct.toFixed(0)}} % of the sea)</em></strong></div><div title="Acidification: the CO₂ the sea takes up lowers its pH and the saturation of aragonite, the mineral of shells and corals (corrosive below Ω = 1)."><small>SURFACE pH · ARAGONITE Ω (MEAN)</small><strong class="date">pH {{carbonMetrics.mean_ph.toFixed(3)}} · Ω {{carbonMetrics.mean_omega.toFixed(2)}} <em>(lowest bottom Ω {{carbonMetrics.min_bottom_omega.toFixed(2)}})</em></strong></div><div title="Sea area whose bottom water is hypoxic (below 60 µmol/kg, about 2 mg/L: fish and shrimp leave or die), or whose column is hypoxic over a fifth of its depth."><small>DEAD ZONES · SHARE OF THE SEA</small><strong class="date">{{fmtKm2(carbonMetrics.dead_zone_km2)}} km² · {{carbonMetrics.dead_zone_pct.toFixed(1)}} % <em>({{carbonMetrics.patches.length}} patch{{carbonMetrics.patches.length===1?'':'es'}})</em></strong></div><div title="Anoxic: practically no oxygen left anywhere in the column. Corrosive: aragonite undersaturated at the surface or the bottom."><small>ANOXIC · CORROSIVE AREA</small><strong class="date">{{fmtKm2(carbonMetrics.anoxic_km2)}} · {{fmtKm2(carbonMetrics.corrosive_km2)}} km²</strong></div><div title="Share of the water volume (to the seafloor or 600 m) that is habitable: enough oxygen, not corrosive and not metabolically stressed."><small>HABITABLE WATER VOLUME</small><strong class="date">{{carbonMetrics.habitable_volume_pct.toFixed(2)}} % <em>({{carbonMetrics.hypoxic_volume_km3.toFixed(0)}} km³ hypoxic · {{fmtKm2(carbonMetrics.stressed_km2)}} km² stressed)</em></strong></div><div title="The lowest oxygen at the seafloor anywhere, and the mean over the shelf (shallower than 100 m)."><small>BOTTOM OXYGEN · LOWEST / SHELF MEAN</small><strong class="date">{{carbonMetrics.min_bottom_oxygen_umol_kg.toFixed(0)}} / {{carbonMetrics.shelf_bottom_oxygen_umol_kg.toFixed(0)}} µmol/kg</strong></div><div title="Net production of the plankton, the mean since the previous frame: it makes oxygen at the surface, but what sinks is respired at depth and on the seafloor."><small>PLANKTON PRODUCTION</small><strong class="date">{{carbonMetrics.production_mt_c_yr.toFixed(0)}} Mt C/yr <em>({{carbonMetrics.mean_production_g_m2_day.toFixed(2)}} g C/m²/day)</em></strong></div><div title="Emissions of the sources in the region."><small>EMISSIONS IN THE REGION</small><strong class="date">{{carbonMetrics.emissions_mt_yr.toFixed(0)}} Mt CO₂/yr <em>(air over the sea {{carbonMetrics.ocean_ppm.toFixed(1)}}, over land {{carbonMetrics.land_ppm.toFixed(1)}} ppm)</em></strong></div></div>
<section class="comparison outlet-report"><div class="section-label">THE YEAR IN THE SEA · {{carbonSeriesData[0]?.year.toFixed(0)}}</div><p>Summer warms and stratifies the shelf: the plankton fed by the river nitrate sinks and is respired at the seafloor, where no new oxygen arrives — the dead zones grow until the autumn storms and winter cooling mix the water again. Warm water holds less CO₂: {{carbonSeriesData.every(p=>p.uptake_mt_co2_yr>0)?'here the rising atmosphere pushes CO₂ into the sea all year, least in the warm months, and the pH keeps falling':carbonSeriesData.every(p=>p.uptake_mt_co2_yr<0)?'here the sea releases CO₂ all year':'so the sea takes CO₂ up in the cool months and releases some in the warm ones'}}. The dashed line is the current frame.</p><div class="outlet-charts"><div v-if="carbonChartDead" class="comparison-chart"><small>DEAD ZONES · ANOXIC · CORROSIVE · thousand km²</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Dead zone area over the year"><line x1="0" x2="100" :y1="carbonChartDead.zero" :y2="carbonChartDead.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in carbonChartDead.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-dasharray="l.dash" stroke-width=".7"/><line :x1="carbonChartDead.x" :x2="carbonChartDead.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{carbonChartDead.lo.toFixed(0)}}–{{carbonChartDead.hi.toFixed(0)}} ×10³ km²<template v-if="carbonChartDead.lines.length>1"> · <span v-for="l in carbonChartDead.lines" :key="l.name" :style="{color:l.color,marginRight:'.6em'}">■ {{l.name}}</span></template></small></div><div v-if="carbonChartUptake" class="comparison-chart"><small>OCEAN CO₂ UPTAKE · Mt CO₂/yr (negative: outgassing)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Ocean uptake over the year"><line x1="0" x2="100" :y1="carbonChartUptake.zero" :y2="carbonChartUptake.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in carbonChartUptake.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-dasharray="l.dash" stroke-width=".7"/><line :x1="carbonChartUptake.x" :x2="carbonChartUptake.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{carbonChartUptake.lo.toFixed(0)}}–{{carbonChartUptake.hi.toFixed(0)}} Mt CO₂/yr<template v-if="carbonChartUptake.lines.length>1"> · <span v-for="l in carbonChartUptake.lines" :key="l.name" :style="{color:l.color,marginRight:'.6em'}">■ {{l.name}}</span></template></small></div><div v-if="carbonChartPh" class="comparison-chart"><small>MEAN SURFACE pH</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Surface pH over the year"><line x1="0" x2="100" :y1="carbonChartPh.zero" :y2="carbonChartPh.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in carbonChartPh.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-dasharray="l.dash" stroke-width=".7"/><line :x1="carbonChartPh.x" :x2="carbonChartPh.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{carbonChartPh.lo.toFixed(3)}}–{{carbonChartPh.hi.toFixed(3)}} <template v-if="carbonChartPh.lines.length>1"> · <span v-for="l in carbonChartPh.lines" :key="l.name" :style="{color:l.color,marginRight:'.6em'}">■ {{l.name}}</span></template></small></div><div v-if="carbonChartOmega" class="comparison-chart"><small>MEAN SURFACE ARAGONITE SATURATION Ω</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Aragonite saturation over the year"><line x1="0" x2="100" :y1="carbonChartOmega.zero" :y2="carbonChartOmega.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in carbonChartOmega.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-dasharray="l.dash" stroke-width=".7"/><line :x1="carbonChartOmega.x" :x2="carbonChartOmega.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{carbonChartOmega.lo.toFixed(2)}}–{{carbonChartOmega.hi.toFixed(2)}} <template v-if="carbonChartOmega.lines.length>1"> · <span v-for="l in carbonChartOmega.lines" :key="l.name" :style="{color:l.color,marginRight:'.6em'}">■ {{l.name}}</span></template></small></div><div v-if="carbonChartAir" class="comparison-chart"><small>CO₂ IN THE AIR · ppm</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Atmospheric CO2 over the year"><line x1="0" x2="100" :y1="carbonChartAir.zero" :y2="carbonChartAir.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in carbonChartAir.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-dasharray="l.dash" stroke-width=".7"/><line :x1="carbonChartAir.x" :x2="carbonChartAir.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{carbonChartAir.lo.toFixed(0)}}–{{carbonChartAir.hi.toFixed(0)}} ppm<template v-if="carbonChartAir.lines.length>1"> · <span v-for="l in carbonChartAir.lines" :key="l.name" :style="{color:l.color,marginRight:'.6em'}">■ {{l.name}}</span></template></small></div><div v-if="carbonChartProduction" class="comparison-chart"><small>PLANKTON PRODUCTION · Mt C/yr</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Production over the year"><line x1="0" x2="100" :y1="carbonChartProduction.zero" :y2="carbonChartProduction.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in carbonChartProduction.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-dasharray="l.dash" stroke-width=".7"/><line :x1="carbonChartProduction.x" :x2="carbonChartProduction.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{carbonChartProduction.lo.toFixed(0)}}–{{carbonChartProduction.hi.toFixed(0)}} Mt C/yr<template v-if="carbonChartProduction.lines.length>1"> · <span v-for="l in carbonChartProduction.lines" :key="l.name" :style="{color:l.color,marginRight:'.6em'}">■ {{l.name}}</span></template></small></div></div></section>
<section v-if="carbonProfile" class="comparison outlet-report"><div class="section-label">WATER COLUMN · {{chosen&&data.carbon.mask[chosen.i]?`SELECTED CELL ${carbonCell}`:`LOWEST BOTTOM OXYGEN OF THE RUN (CELL ${carbonCell})`}}</div><p>The {{carbonProfile.levels.length}} levels from the surface to {{carbonProfile.shelf?`the seafloor at ${carbonProfile.seafloor_m.toFixed(0)} m`:`600 m (seafloor ${carbonProfile.seafloor_m.toFixed(0)} m)`}}: habitat class on the left (<span v-for="h in HABITAT_CLASSES" :key="h.key" :style="{color:h.color==='#2a0410'?'#c98b9b':h.color,marginRight:'.5em'}">■ {{h.label}}</span>), then oxygen (red line: 60 µmol/kg hypoxia), pH and aragonite saturation (magenta line: Ω = 1). Right: the oxygen of this column over the year. {{carbonProfile.dead_zone?`A dead zone now: ${carbonProfile.hypoxic_m.toFixed(0)} m of hypoxic water above the seafloor.`:carbonProfile.column_class>=2?'Hypoxic water at depth.':'No hypoxic water now.'}} Depth on a square-root scale; click a sea cell to move the column.</p><div class="outlet-charts"><div class="comparison-chart"><small>PROFILE · O₂ 0–300 µmol/kg · pH 7.4–8.3 · Ω 0–5</small><svg viewBox="0 0 100 60" preserveAspectRatio="none" aria-label="Oxygen, pH and saturation profile"><rect v-for="(b,k) in carbonProfileBands" :key="k" x="0" :y="b.y" width="4" :height="b.h" :fill="b.fill"><title>{{b.title}}</title></rect><line x1="6" x2="6" y1="0" y2="60" stroke="#2b3e49" stroke-width=".3"/><line x1="37" x2="37" y1="0" y2="60" stroke="#2b3e49" stroke-width=".3"/><line x1="68" x2="68" y1="0" y2="60" stroke="#2b3e49" stroke-width=".3"/><line :x1="6+60/300*29" :x2="6+60/300*29" y1="0" y2="60" stroke="#e4572e" stroke-dasharray="1 .6" stroke-width=".4"/><line :x1="68+1/5*30" :x2="68+1/5*30" y1="0" y2="60" stroke="#e066c8" stroke-dasharray="1 .6" stroke-width=".4"/><polyline :points="carbonStep('oxygen_umol_kg',0,300,6,29)" fill="none" stroke="#7fd1ff" stroke-width=".8"/><polyline :points="carbonStep('ph',7.4,8.3,37,29)" fill="none" stroke="#9fe870" stroke-width=".8"/><polyline :points="carbonStep('omega',0,5,68,30)" fill="none" stroke="#f4a261" stroke-width=".8"/></svg><small>surface O₂ {{carbonProfile.levels[0].oxygen_umol_kg.toFixed(0)}} → bottom {{carbonProfile.levels.at(-1).oxygen_umol_kg.toFixed(0)}} µmol/kg · pH {{carbonProfile.levels[0].ph.toFixed(2)}} → {{carbonProfile.levels.at(-1).ph.toFixed(2)}} · Ω {{carbonProfile.levels[0].omega.toFixed(2)}} → {{carbonProfile.levels.at(-1).omega.toFixed(2)}} · habitable {{(carbonProfile.habitable_fraction*100).toFixed(0)}} %</small></div><div v-if="carbonHistory" class="comparison-chart"><small>OXYGEN SECTION OVER THE YEAR</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Oxygen time-depth section"><rect v-for="(r,k) in carbonSectionRects" :key="k" :x="r.x" :y="r.y" :width="r.w" :height="r.h" :fill="r.fill"/><line :x1="carbonSectionTime" :x2="carbonSectionTime" y1="0" y2="50" stroke="#f3f7f9" stroke-dasharray="1 1" stroke-width=".4"/></svg><small>{{((carbonHistory.end-carbonHistory.start)/86400).toFixed(0)}} days · √depth 0–{{carbonHistory.max_depth_m.toFixed(0)}} m · black/red = hypoxic, blue = rich</small></div></div></section>
<section class="comparison outlet-report"><div class="section-label">DEAD ZONES NOW · {{time.slice(0,10)}}</div><p v-if="!carbonMetrics.patches.length">No dead zone this month: the water column is mixed or the bottom water still holds enough oxygen.</p><template v-else><p>Connected areas of hypoxic bottom water, largest first, with the nearest river mouth. Click a row to show its worst column.</p><table class="event-table"><thead><tr><th>#</th><th>Area</th><th title="Lowest oxygen at the seafloor in the patch">Lowest bottom O₂</th><th title="Columns with practically no oxygen">Anoxic cells</th><th title="Cells whose bottom water is also corrosive (Ω &lt; 1)">Corrosive bottom</th><th>Mean depth</th><th>Nearest river</th></tr></thead><tbody><tr v-for="(p,k) in carbonMetrics.patches" :key="k" :class="{selected:carbonCell===p.worst_cell}" @click="selected=p.worst_cell"><td>{{k+1}}</td><td>{{Math.round(p.area_km2).toLocaleString()}} km²</td><td>{{p.min_bottom_oxygen_umol_kg.toFixed(1)}} µmol/kg</td><td>{{p.anoxic_cells}}</td><td>{{p.corrosive_cells}}</td><td>{{p.mean_depth_m.toFixed(0)}} m</td><td>{{p.nearest_river?`${p.nearest_river} (${p.river_distance_km.toFixed(0)} km)`:'—'}}</td></tr></tbody></table></template></section>
<section v-if="carbonBudgetData" class="comparison outlet-report"><div class="section-label">CARBON BUDGETS SINCE {{new Date(Date.parse(forecast.start_time_utc)).toISOString().slice(0,10)}} · Mt CO₂</div><p>Where the carbon went: the sea's dissolved inorganic carbon, and the CO₂ above the background in the region's boundary layer. Both budgets close (the rows add up to the change).</p><div class="outlet-charts"><table class="event-table"><thead><tr><th>Sea</th><th>Mt CO₂</th></tr></thead><tbody><tr v-for="r in carbonBudgetData.ocean" :key="r.key"><td>{{r.label}}</td><td>{{signed(r.value,2)}}</td></tr><tr><td><strong>Change of the sea's carbon</strong></td><td><strong>{{signed(carbonBudgetData.ocean_change,2)}}</strong></td></tr></tbody></table><table class="event-table"><thead><tr><th>Air</th><th>Mt CO₂</th></tr></thead><tbody><tr v-for="r in carbonBudgetData.air" :key="r.key"><td>{{r.label}}</td><td>{{signed(r.value,2)}}</td></tr><tr><td><strong>Change of the excess CO₂</strong></td><td><strong>{{signed(carbonBudgetData.air_change,2)}}</strong></td></tr></tbody></table></div><p>Rivers delivered {{carbonBudgetData.river_kt_n.toFixed(0)}} kt of nitrate nitrogen; {{carbonBudgetData.denitrified_kt_n.toFixed(0)}} kt were lost to denitrification where the oxygen ran out.</p></section>
<section class="comparison outlet-report"><div class="section-label">TODAY ↔ 2100 · WHAT CHANGES</div><p>The same sea under about 427 ppm today and about 1,010 ppm in 2100 (high emissions, 3 K warmer, anthropogenic carbon in the thermocline). Numbers are whole-year means and maxima of each run.</p><button v-if="carbonScenarioKey&&!carbonCompare" class="text-button" @click="compareCarbonScenarios">⇄ Load the {{carbonScenarioKey==='future'?'today':'2100'}} run and compare</button><table v-if="carbonCompareRows.length" class="event-table"><thead><tr><th></th><th>{{CARBON_EXAMPLES.today.label}}</th><th>{{CARBON_EXAMPLES.future.label}}</th><th>Change by 2100</th></tr></thead><tbody><tr v-for="r in carbonCompareRows" :key="r.label"><td>{{r.label}}</td><td>{{r.today}}</td><td>{{r.future}}</td><td>{{r.change}}</td></tr></tbody></table></section></template><template v-if="view!=='carbon'"><div v-if="oceanMetrics" class="metrics extra-metrics"><div><small>MAIN STREAM</small><strong class="date">{{oceanMetrics.main_stream?oceanMetrics.main_stream.label:'none above threshold'}}</strong></div><div><small>FASTEST CURRENT · STREAMS</small><strong class="date">{{oceanMetrics.max_speed_m_s.toFixed(2)}} m/s · {{oceanMetrics.stream_count}}</strong></div><div><small>OVERTURNING · FORMATION AREA</small><strong class="date">{{oceanMetrics.overturning_sv.toFixed(2)}} Sv · {{oceanMetrics.formation_area_km2.toLocaleString()}} km²</strong></div><div><small>FRESH WATER IN · NET RAIN−EVAPORATION</small><strong class="date">{{(oceanMetrics.river_input_m3_s/1000).toFixed(1)}} ×10³ m³/s ({{oceanMetrics.river_input_sv.toFixed(3)}} Sv) · {{(oceanMetrics.net_precipitation_m3_s/1000).toFixed(1)}} ×10³ m³/s</strong></div><div><small>SALINITY MEAN · MIN SURFACE</small><strong class="date">{{oceanMetrics.mean_salinity_psu.toFixed(2)}} · {{oceanMetrics.min_surface_salinity_psu.toFixed(1)}} psu</strong></div><div><small>PLUMES · FRESHWATER HELD · FRONTS</small><strong class="date">{{oceanMetrics.plume_area_km2.toLocaleString()}} km² · {{oceanMetrics.plume_freshwater_km3.toFixed(0)}} km³ · {{oceanMetrics.front_cells}} cells</strong></div><div><small>CUMULATIVE RIVER · SUNK</small><strong class="date">{{oceanMetrics.cumulative_river_km3.toFixed(0)}} km³ · {{oceanMetrics.cumulative_sinking_km3.toFixed(0)}} km³</strong></div></div><section v-if="oceanSeries.length" class="comparison outlet-report"><div class="section-label">CIRCULATION OVER TIME</div><p>The overturning (deep-water formation), the transport of the strongest stream and the fresh water entering from rivers, over the run.</p><div class="outlet-charts"><div class="comparison-chart"><small>OVERTURNING · Sv</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Overturning time series"><polyline :points="oceanPath('overturning_sv',oceanSeriesMax.overturning)" fill="none" stroke="#b388ff" stroke-width=".7"/><line :x1="oceanTimePosition" :x2="oceanTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{oceanSeriesMax.overturning.toFixed(2)}} Sv · 0–{{(visibleFrames.at(-1).forecast.elapsed_seconds/86400).toFixed(0)}} days</small></div><div class="comparison-chart"><small>MAIN STREAM TRANSPORT · Sv</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Stream transport time series"><polyline :points="oceanPath('stream_sv',oceanSeriesMax.stream)" fill="none" stroke="#75bbfa" stroke-width=".7"/><line :x1="oceanTimePosition" :x2="oceanTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{oceanSeriesMax.stream.toFixed(1)}} Sv</small></div><div class="comparison-chart"><small>RIVER FRESH WATER · Sv</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="River input time series"><polyline :points="oceanPath('river_sv',oceanSeriesMax.river)" fill="none" stroke="#7fffd4" stroke-width=".7"/><line :x1="oceanTimePosition" :x2="oceanTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{oceanSeriesMax.river.toFixed(3)}} Sv</small></div></div><p v-if="data.ocean">Exchange points: <template v-for="(p,i) in data.ocean.exchangePoints" :key="p.name">{{i?' · ':''}}{{p.name}} {{p.discharge_m3_s>=1e5?(p.discharge_m3_s/1e6).toFixed(2)+' Sv':Math.round(p.discharge_m3_s).toLocaleString()+' m³/s'}} → plume {{p.plume_area_km2.toLocaleString()}} km², min {{p.minimum_salinity_psu.toFixed(1)}} psu</template></p></section><div v-if="oceanHeatMetrics" class="metrics extra-metrics"><div><small>SEA SURFACE · MEAN (MIN–MAX)</small><strong class="date">{{oceanHeatMetrics.mean_sst_c.toFixed(2)}} °C ({{oceanHeatMetrics.min_sst_c.toFixed(1)}}–{{oceanHeatMetrics.max_sst_c.toFixed(1)}})</strong></div><div v-if="oceanHeatMetrics.mean_mld_m!==null"><small>MIXED LAYER · MEAN / DEEPEST · BARRIER-LAYER CELLS</small><strong class="date">{{oceanHeatMetrics.mean_mld_m.toFixed(1)}} / {{oceanHeatMetrics.max_mld_m.toFixed(0)}} m · {{oceanHeatMetrics.barrier_layer_cells}}</strong></div><div><small>NET HEAT INTO THE SEA<template v-if="oceanHeatMetrics.fluxes_w_m2"> · SUN / LONGWAVE / SENSIBLE / LATENT</template></small><strong class="date">{{oceanHeatMetrics.net_heat_w_m2.toFixed(0)}} W/m²<template v-if="oceanHeatMetrics.fluxes_w_m2"> · {{oceanHeatMetrics.fluxes_w_m2.shortwave.toFixed(0)}} / {{oceanHeatMetrics.fluxes_w_m2.longwave.toFixed(0)}} / {{oceanHeatMetrics.fluxes_w_m2.sensible.toFixed(0)}} / {{oceanHeatMetrics.fluxes_w_m2.latent.toFixed(0)}}</template></strong></div><div v-if="oceanHeatMetrics.fluxes_w_m2"><small>EVAPORATION · MEAN / MAX</small><strong class="date">{{fmtFlow(oceanHeatMetrics.evaporation_m3_s)}} · {{oceanHeatMetrics.mean_evaporation_mm_day.toFixed(2)}} / {{oceanHeatMetrics.max_evaporation_mm_day.toFixed(1)}} mm/day</strong></div><div v-if="data.ocean.rain"><small>RAIN ON SEA (NEAR COAST) · COAST · INLAND</small><strong class="date">{{fmtFlow(oceanHeatMetrics.rain_sea_m3_s)}} ({{fmtFlow(oceanHeatMetrics.rain_coastal_sea_m3_s)}}) · {{fmtFlow(oceanHeatMetrics.rain_coast_m3_s)}} · {{fmtFlow(oceanHeatMetrics.rain_inland_m3_s)}}</strong></div><div v-if="data.ocean.rain"><small>COASTAL RUNOFF → SEA · STILL ON LAND · PEAK RAIN</small><strong class="date">{{fmtFlow(oceanHeatMetrics.coastal_runoff_m3_s)}} · {{(oceanHeatMetrics.coastal_storage_m3/1e9).toFixed(2)}} km³ · {{oceanHeatMetrics.max_rain_mm_h.toFixed(1)}} mm/h</strong></div><div><small>RAIN − EVAPORATION OVER THE SEA</small><strong>{{oceanHeatMetrics.precipitation_minus_evaporation_mm_day.toFixed(2)}} <em>mm/day</em></strong></div><div><small>SURFACE LENSES (RAIN) · LARGEST</small><strong class="date">{{oceanHeatMetrics.lens_count}} ({{oceanHeatMetrics.rain_lens_count}}) · {{oceanHeatMetrics.largest_lens?`${oceanHeatMetrics.largest_lens.kind.replace('_',' ')} ${Math.round(oceanHeatMetrics.largest_lens.area_km2).toLocaleString()} km², ${oceanHeatMetrics.largest_lens.min_salinity_psu.toFixed(1)} psu`:'none'}}</strong></div><div><small>HEAT GAINED SINCE START · BUDGET RESIDUAL</small><strong class="date">{{oceanHeatMetrics.heat_change_ej.toFixed(1)}} EJ · {{oceanHeatMetrics.heat_balance_error_j.toExponential(1)}} J</strong></div><div><small>CUMULATIVE RAIN SEA / COAST · RUNOFF · EVAPORATION</small><strong class="date">{{oceanHeatMetrics.cumulative.rain_sea_km3.toFixed(1)}} / {{oceanHeatMetrics.cumulative.rain_coast_km3.toFixed(1)}} · {{oceanHeatMetrics.cumulative.runoff_km3.toFixed(1)}} · {{oceanHeatMetrics.cumulative.evaporation_km3.toFixed(1)}} km³</strong></div></div><section v-if="oceanHeatSeries.length" class="comparison outlet-report"><div class="section-label">OCEAN HEAT &amp; WATER EXCHANGE OVER TIME</div><p>Basin means over the run: the sea surface and its mixed layer, the net heat the air and the sun give the sea (above the line the sea warms), and the fresh water it exchanges with the air and the land.</p><div class="outlet-charts"><div class="comparison-chart"><small>SEA SURFACE °C (orange) · MIXED LAYER m (blue)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Sea-surface temperature and mixed-layer depth time series"><polyline :points="heatPath('sst_c',v=>45-(v-oceanHeatMax.sst[0])/((oceanHeatMax.sst[1]-oceanHeatMax.sst[0])||1)*40)" fill="none" stroke="#ffb366" stroke-width=".7"/><polyline :points="heatPath('mld_m',v=>45-v/oceanHeatMax.mld*40)" fill="none" stroke="#75bbfa" stroke-width=".7"/><line :x1="oceanTimePosition" :x2="oceanTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{oceanHeatMax.sst[0].toFixed(2)}}–{{oceanHeatMax.sst[1].toFixed(2)}} °C · 0–{{oceanHeatMax.mld.toFixed(0)}} m</small></div><div class="comparison-chart"><small>NET HEAT INTO THE SEA · W/m²</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Net air-sea heat flux time series"><line x1="0" x2="100" y1="25" y2="25" stroke="#4a5a66" stroke-width=".3"/><polyline :points="heatPath('net_heat_w_m2',v=>25-v/oceanHeatMax.net*22)" fill="none" stroke="#ff7b6b" stroke-width=".7"/><line :x1="oceanTimePosition" :x2="oceanTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>±{{oceanHeatMax.net.toFixed(0)}} W/m² (day and night, outbreaks)</small></div><div class="comparison-chart"><small>FRESH WATER · rain on sea (blue) · coastal runoff (tan) · evaporation (orange)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Freshwater exchange time series"><polyline :points="heatPath('rain_sea_m3_s',v=>45-v/oceanHeatMax.water*40)" fill="none" stroke="#75bbfa" stroke-width=".7"/><polyline :points="heatPath('runoff_m3_s',v=>45-v/oceanHeatMax.water*40)" fill="none" stroke="#d8b77a" stroke-width=".7"/><polyline :points="heatPath('evaporation_m3_s',v=>45-v/oceanHeatMax.water*40)" fill="none" stroke="#ffb366" stroke-width=".7"/><line :x1="oceanTimePosition" :x2="oceanTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{fmtFlow(oceanHeatMax.water)}}</small></div></div></section><div v-if="oceanPolarMetrics" class="metrics extra-metrics"><div><small>OVERTURNING NOW · ANNUAL MEAN OF YEAR 1 → LATEST</small><strong class="date">{{oceanPolarMetrics.overturning_sv.toFixed(1)}} Sv<template v-if="oceanPolarEvents"> · {{oceanPolarEvents.reference_sv.toFixed(1)}} → {{oceanPolarYears.at(-1).overturning_mean_sv.toFixed(1)}} Sv ({{oceanPolarEvents.last_pct.toFixed(0)}} %)</template></strong></div><div><small>EXTRA FRESH WATER · HOSING / ALL GLACIAL (HOSING + MELT + ICEBERGS)</small><strong class="date">{{oceanPolarMetrics.hosing_sv.toFixed(3)}} / {{oceanPolarMetrics.glacial_sv.toFixed(3)}} Sv · {{oceanPolarMetrics.cumulative_glacial_km3.toFixed(0)}} km³ so far</strong></div><div v-if="oceanWarming"><small>AIR WARMING · SOUTH / NORTH</small><strong class="date">+{{oceanWarming.south.toFixed(2)}} / +{{oceanWarming.north.toFixed(2)}} K</strong></div><div v-if="oceanPolarMetrics.sea_ice_area_mkm2!==null"><small>SEA ICE · AREA / EXTENT / VOLUME</small><strong class="date">{{oceanPolarMetrics.sea_ice_area_mkm2.toFixed(2)}} / {{oceanPolarMetrics.sea_ice_extent_mkm2.toFixed(2)}} M km² · {{oceanPolarMetrics.sea_ice_volume_km3.toFixed(0)}} km³ · {{oceanPolarMetrics.sea_ice_water_m3_s>=0?'melting':'freezing'}} {{fmtFlow(Math.abs(oceanPolarMetrics.sea_ice_water_m3_s))}} · drift up to {{(oceanPolarMetrics.sea_ice_max_drift_m_s*86.4).toFixed(0)}} km/day</strong></div><div v-if="oceanPolarMetrics.greenland_smb_gt_yr!==null"><small>ICE SHEET · SMB / DISCHARGE / SEA LEVEL</small><strong class="date">{{oceanPolarMetrics.greenland_smb_gt_yr.toFixed(0)}} Gt/yr · {{fmtFlow(oceanPolarMetrics.greenland_discharge_m3_s)}} · +{{oceanPolarMetrics.greenland_sea_level_rise_mm.toFixed(2)}} mm</strong></div><div><small>NORTH · SEA SURFACE / MIXED LAYER / O₂ CAPACITY</small><strong class="date">{{oceanPolarMetrics.north_sst_c.toFixed(2)}} °C · {{oceanPolarMetrics.north_mld_m===null?'–':oceanPolarMetrics.north_mld_m.toFixed(0)+' m'}} · {{oceanPolarMetrics.north_oxygen_umol_kg===null?'–':oceanPolarMetrics.north_oxygen_umol_kg.toFixed(0)+' µmol/kg'}}</strong></div></div><section v-if="oceanPolarSeries.length>1" class="comparison outlet-report"><div class="section-label">POLAR OCEAN OVER TIME · SEA ICE, MELTWATER AND THE OVERTURNING</div><p>The deep-water formation has a strong seasonal cycle (winter convection), so the dots mark each model year's mean. The fresh water from the ice sheet and the hosing caps the sinking region with light water; warming thins the winter sea ice and shrinks the area where the water gets cold and dense enough to sink.</p><div class="outlet-charts"><div v-if="poChartOverturning" class="comparison-chart"><small>OVERTURNING · Sv (dots: annual means)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Overturning time series"><line x1="0" x2="100" :y1="poChartOverturning.zero" :y2="poChartOverturning.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in poChartOverturning.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" stroke-width=".7"/><line :x1="poChartOverturning.x" :x2="poChartOverturning.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/><circle v-for="(y,k) in poChartOverturning.years" :key="k" :cx="y.x" :cy="y.y" r=".9" fill="#e0c8ff"/></svg><small>{{poChartOverturning.lo.toFixed(0)}}–{{poChartOverturning.hi.toFixed(0)}} Sv</small></div><div v-if="poChartFresh" class="comparison-chart"><small>EXTRA FRESH WATER · all glacial incl. hosing (teal) · hosing alone (yellow) · Sv</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Glacial and hosing freshwater time series"><line x1="0" x2="100" :y1="poChartFresh.zero" :y2="poChartFresh.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in poChartFresh.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" stroke-width=".7"/><line :x1="poChartFresh.x" :x2="poChartFresh.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{poChartFresh.hi.toFixed(2)}} Sv</small></div><div v-if="poChartSeaIce" class="comparison-chart"><small>SEA-ICE AREA · million km²</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Sea-ice area time series"><line x1="0" x2="100" :y1="poChartSeaIce.zero" :y2="poChartSeaIce.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in poChartSeaIce.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" stroke-width=".7"/><line :x1="poChartSeaIce.x" :x2="poChartSeaIce.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{poChartSeaIce.hi.toFixed(2)}} M km²</small></div><div v-if="poChartGreenland" class="comparison-chart"><small>ICE-SHEET SURFACE MASS BALANCE · Gt/yr</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Ice-sheet surface mass balance time series"><line x1="0" x2="100" :y1="poChartGreenland.zero" :y2="poChartGreenland.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in poChartGreenland.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" stroke-width=".7"/><line :x1="poChartGreenland.x" :x2="poChartGreenland.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{poChartGreenland.lo.toFixed(0)}}–{{poChartGreenland.hi.toFixed(0)}} Gt/yr (below the line it loses mass)</small></div><div v-if="poChartNorth" class="comparison-chart"><small>NORTHERN SEA SURFACE · °C</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Northern sea-surface temperature time series"><line x1="0" x2="100" :y1="poChartNorth.zero" :y2="poChartNorth.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in poChartNorth.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" stroke-width=".7"/><line :x1="poChartNorth.x" :x2="poChartNorth.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{poChartNorth.lo.toFixed(1)}}–{{poChartNorth.hi.toFixed(1)}} °C</small></div><div v-if="poChartOxygen" class="comparison-chart"><small>NORTHERN O₂ CAPACITY · µmol/kg</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Northern surface oxygen time series"><line x1="0" x2="100" :y1="poChartOxygen.zero" :y2="poChartOxygen.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in poChartOxygen.lines" :key="l.key" :points="l.points" fill="none" :stroke="l.color" stroke-width=".7"/><line :x1="poChartOxygen.x" :x2="poChartOxygen.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{poChartOxygen.lo.toFixed(0)}}–{{poChartOxygen.hi.toFixed(0)}} µmol/kg (warmer water holds less)</small></div></div></section><section v-if="oceanPolarEvents" class="comparison outlet-report"><div class="section-label">WHEN DOES THE OVERTURNING WEAKEN? · YEAR BY YEAR</div><p>Annual means of each complete model year (year 0 is the first year after the spin-up). Weakened / collapsed: the first year whose mean overturning falls below 75 %, 50 % or 20 % of year 0. <strong>25 %: {{oceanPolarEvents.weakened_25pct===null?'not yet':'year '+oceanPolarEvents.weakened_25pct}} · 50 %: {{oceanPolarEvents.weakened_50pct===null?'not yet':'year '+oceanPolarEvents.weakened_50pct}} · collapse: {{oceanPolarEvents.collapsed===null?'not within the run':'year '+oceanPolarEvents.collapsed}} · first ice-free summer: {{oceanPolarEvents.ice_free_summer===null?'none':'year '+oceanPolarEvents.ice_free_summer}}{{oceanPolarEvents.greenland_negative_smb===null?'':' · ice sheet loses mass at the surface from year '+oceanPolarEvents.greenland_negative_smb}}</strong></p><table class="event-table"><thead><tr><th>Year</th><th>Overturning mean / winter max (Sv)</th><th>Glacial water incl. hosing (Sv)</th><th>Sea ice max / min (M km²)</th><th>North SST (°C)</th><th>North O₂ (µmol/kg)</th><th>Deepest north mixing (m)</th><th>Ice-sheet SMB (Gt/yr)</th><th>Sea level (mm)</th></tr></thead><tbody><tr v-for="y in oceanPolarYears" :key="y.year" :class="{selected:y.year===Math.floor(forecast.elapsed_seconds/31557600+1e-9)}" @click="seekYear(y.year)"><td>{{y.year}}</td><td>{{y.overturning_mean_sv.toFixed(1)}} / {{y.overturning_max_sv.toFixed(1)}}</td><td>{{y.glacial_sv.toFixed(3)}}</td><td>{{y.sea_ice_max_mkm2===null?'–':y.sea_ice_max_mkm2.toFixed(2)+' / '+y.sea_ice_min_mkm2.toFixed(2)}}</td><td>{{y.north_sst_c.toFixed(2)}}</td><td>{{y.north_oxygen_umol_kg===null?'–':y.north_oxygen_umol_kg.toFixed(0)}}</td><td>{{y.north_mld_max_m===null?'–':y.north_mld_max_m.toFixed(0)}}</td><td>{{y.greenland_smb_gt_yr===null?'–':y.greenland_smb_gt_yr.toFixed(0)}}</td><td>{{y.greenland_sea_level_rise_mm===null?'–':y.greenland_sea_level_rise_mm.toFixed(2)}}</td></tr></tbody></table><p v-if="oceanSameSeason.length>1">Same time of year as the current frame ({{time.slice(5,10)}}), year by year: <span v-for="r in oceanSameSeason" :key="r.k" :class="{'selected-year':r.k===frameIndex}" style="margin-right:.9em">year {{r.year.toFixed(0)}}: {{r.overturning_sv.toFixed(1)}} Sv{{r.sea_ice_area_mkm2===null?'':' · ice '+r.sea_ice_area_mkm2.toFixed(2)+' M km²'}}</span></p></section><section v-if="columnNow" class="comparison outlet-report"><div class="section-label">WATER COLUMN · {{chosen&&data.ocean.mask[chosen.i]?`SELECTED CELL ${columnCell}`:`STRONGEST COOLING OVER THE RUN (CELL ${columnCell})`}}</div><p>Temperature (red) and salinity (green) of the {{columnNow.layers.length}} layers from the surface down to the thermocline ({{columnNow.thickness_m.toFixed(0)}} m) at the current time; dashed: mixed-layer depth, dotted: isothermal-layer depth (a gap between them is a barrier layer). The section shows the same column over the whole run with the mixed layer in white. Depth on a square-root scale. Click a sea cell to move the column.</p><div class="outlet-charts"><div class="comparison-chart"><small>PROFILE · {{(columnTemperatureRange[0]-273.15).toFixed(1)}}–{{(columnTemperatureRange[1]-273.15).toFixed(1)}} °C · {{columnSalinityRange[0].toFixed(2)}}–{{columnSalinityRange[1].toFixed(2)}} psu</small><svg viewBox="0 0 100 60" preserveAspectRatio="none" aria-label="Temperature and salinity profile"><polyline :points="columnStep('temperature_k',columnTemperatureRange)" fill="none" stroke="#ff7b6b" stroke-width=".8"/><polyline :points="columnStep('salinity_psu',columnSalinityRange)" fill="none" stroke="#7fe08a" stroke-width=".8"/><line x1="0" x2="100" :y1="columnDepthY(columnNow.mixed_layer_depth_m,columnNow.thickness_m)" :y2="columnDepthY(columnNow.mixed_layer_depth_m,columnNow.thickness_m)" stroke="#f3f7f9" stroke-dasharray="2 1" stroke-width=".4"/><line x1="0" x2="100" :y1="columnDepthY(columnNow.isothermal_layer_depth_m,columnNow.thickness_m)" :y2="columnDepthY(columnNow.isothermal_layer_depth_m,columnNow.thickness_m)" stroke="#ffd166" stroke-dasharray=".5 1" stroke-width=".4"/></svg><small>surface {{(columnNow.sst_k-273.15).toFixed(2)}} °C · mixed layer {{columnNow.mixed_layer_depth_m.toFixed(1)}} m · isothermal {{columnNow.isothermal_layer_depth_m.toFixed(1)}} m{{columnNow.barrier_layer_m>1?` · barrier ${columnNow.barrier_layer_m.toFixed(1)} m`:''}}</small></div><div v-if="columnHistory" class="comparison-chart"><small>TEMPERATURE SECTION · {{(columnHistory.temperature_range_k[0]-273.15).toFixed(1)}}–{{(columnHistory.temperature_range_k[1]-273.15).toFixed(1)}} °C (blue → red)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Time-depth temperature section"><rect v-for="(r,k) in sectionRects" :key="k" :x="r.x" :y="r.y" :width="r.w+.02" :height="r.h+.02" :fill="r.fill"/><polyline :points="sectionMld" fill="none" stroke="#f3f7f9" stroke-width=".5"/><line :x1="sectionTimePosition" :x2="sectionTimePosition" y1="0" y2="50" stroke="#ffd166" stroke-dasharray="1 1" stroke-width=".4"/></svg><small>{{((columnHistory.end-columnHistory.start)/86400).toFixed(1)}} days · √depth 0–{{columnHistory.max_depth_m.toFixed(0)}} m</small></div></div></section></template><template v-if="polarMetrics"><div class="metrics extra-metrics"><div><small>SCENARIO · YEAR · GLOBAL WARMING</small><strong class="date">{{polarMetrics.scenario}} · {{polarMetrics.year}} · +{{fmt(polarMetrics.global_warming_k,2)}} K</strong></div><div><small>ATLANTIC OVERTURNING (AMOC)</small><strong class="date">{{fmt(polarMetrics.overturning_sv,1)}} Sv · {{fmt(polarMetrics.overturning_pct,0)}} % of today · heat {{fmt(polarMetrics.heat_transport_pw,2)}} PW</strong></div><div><small>FRESH WATER INTO THE NORTH ATLANTIC · TIPPING POINT</small><strong class="date">{{fmt(polarMetrics.freshwater_sv,3)}} / {{fmt(polarMetrics.threshold_sv,3)}} Sv{{polarMetrics.freshwater_sv>polarMetrics.threshold_sv?' · past the tipping point':''}}</strong></div><div><small>EXTRA FRESH WATER · GREENLAND / ARCTIC / RAIN &amp; RIVERS</small><strong class="date">{{fmt(polarMetrics.freshwater_components_sv.greenland,3)}} / {{fmt(polarMetrics.freshwater_components_sv.arctic,3)}} / {{fmt(polarMetrics.freshwater_components_sv.hydrological,3)}} Sv</strong></div><div><small>RECOVERY TIME (CRITICAL SLOWING DOWN)</small><strong class="date">{{polarMetrics.recovery_time_years===null?'no stable state left':fmt(polarMetrics.recovery_time_years,0)+' years'}}</strong></div><div><small>GREENLAND · SEA LEVEL (VS CONTROL) · SMB</small><strong class="date">{{fmt(polarMetrics.greenland_sle_m,2)}} m left · +{{fmt((polarMetrics.sea_level_rise_vs_control_m??polarMetrics.sea_level_rise_m)*100,1)}} cm · {{fmt(polarMetrics.greenland_smb_gt_yr,0)}} Gt/yr</strong></div><div><small>ARCTIC SEA ICE · MARCH / SEPTEMBER · ICE-FREE DAYS</small><strong class="date">{{fmt(polarMetrics.arctic_ice_march_m,2)}} / {{fmt(polarMetrics.arctic_ice_september_m,2)}} m · {{fmt(polarMetrics.arctic_ice_free_days,0)}}</strong></div><div><small>OCEAN TEMPERATURE CHANGE · NORTH ATLANTIC / TROPICS</small><strong class="date">{{fmt(polarMetrics.north_atlantic_change_k,2)}} / {{fmt(polarMetrics.tropical_atlantic_change_k,2)}} K</strong></div><div><small>DISSOLVED-OXYGEN CAPACITY · NORTH / TROPICS</small><strong class="date">{{fmt(polarMetrics.north_oxygen_change_pct,1)}} / {{fmt(polarMetrics.south_oxygen_change_pct,1)}} %</strong></div></div>
<section class="comparison outlet-report"><div class="section-label">WHEN DO THE CURRENTS COLLAPSE?</div><p>Per scenario: the year (and global warming) of each event in the central run, and across the uncertainty ensemble (present salinity contrast = distance to the tipping point × Greenland warming sensitivity). The tipping point is crossed when the fresh water into the North Atlantic exceeds what the weakened circulation can carry; the collapse follows later because the circulation lingers near the vanished state. Conceptual overturning model: read the dates relative to each other and to the scenarios, not as a forecast.</p><table class="comparison-table"><thead><tr><th>Scenario</th><th>Ice-free Sept.</th><th>Greenland SMB &lt; 0</th><th>Tipping point</th><th>−50 %</th><th>Collapse</th><th>Ensemble: tipped / collapsed (range)</th><th>Sea level 2100 · end</th></tr></thead><tbody><tr v-for="r in polarEvents" :key="r.scenario" :class="{selected:r.scenario===polarScenario}" @click="polarLayers.scenario=r.scenario"><td>{{r.scenario}}</td><td>{{eventCell(r.events.first_ice_free_september)}}</td><td>{{eventCell(r.events.greenland_negative_smb)}}</td><td>{{eventCell(r.events.threshold_crossed)}}</td><td>{{eventCell(r.events.overturning_weakened_50pct)}}</td><td><strong>{{eventCell(r.events.overturning_collapsed)}}</strong>{{r.lag_years!==null?` · ${r.lag_years} yr after tipping`:''}}</td><td>{{r.ensemble?`${r.ensemble.crossed}/${r.ensemble.members} · ${r.ensemble.collapsed}/${r.ensemble.members}${r.ensemble.collapsed?` (${r.ensemble.earliest}–${r.ensemble.latest})`:''}`:'—'}}</td><td>+{{fmt(r.sea_level_2100_m*100,0)}} · +{{fmt(r.sea_level_end_m*100,0)}} cm</td></tr></tbody></table><p v-for="r in polarEvents.filter(r=>r.early_warning)" :key="'ews'+r.scenario">Early warnings in the noisy {{r.scenario}} record ({{r.early_warning.window}}-year windows until {{r.early_warning.until}}): trend of the variance τ = {{r.early_warning.variance_tau.toFixed(2)}}, of the lag-1 autocorrelation τ = {{r.early_warning.autocorrelation_tau.toFixed(2)}} (Kendall; near 1 = rising steadily, near 0 = no trend).</p></section>
<section class="comparison outlet-report"><div class="section-label">SCENARIOS OVER TIME · {{polarBlock.output_years[0]}}–{{polarBlock.output_years.at(-1)}}</div><p><span v-for="(s,k) in polarBlock.scenarios" :key="s.name" :style="{color:scenarioColor(k-(polarBlock.scenarios[0].control?1:0),s.control),marginRight:'1em'}">■ {{s.name}}</span> · the dotted grey line is the control (model drift); the dashed vertical line is the current frame.</p><div class="outlet-charts"><div class="comparison-chart"><small>ATLANTIC OVERTURNING · Sv</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Overturning by scenario"><line x1="0" x2="100" :y1="chartOverturning.zero" :y2="chartOverturning.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in chartOverturning.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.control?'1 1':null"/><line :x1="chartOverturning.x" :x2="chartOverturning.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{chartOverturning.lo.toFixed(1)}}–{{chartOverturning.hi.toFixed(1)}} Sv</small></div><div class="comparison-chart"><small>SEA LEVEL FROM GREENLAND (VS CONTROL) · cm</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Sea-level rise by scenario"><line x1="0" x2="100" :y1="chartSeaLevel.zero" :y2="chartSeaLevel.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in chartSeaLevel.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.control?'1 1':null"/><line :x1="chartSeaLevel.x" :x2="chartSeaLevel.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{chartSeaLevel.lo.toFixed(1)}}–{{chartSeaLevel.hi.toFixed(1)}} cm</small></div><div class="comparison-chart"><small>ARCTIC SEPTEMBER ICE · m (thickness × cover)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="September sea ice by scenario"><line x1="0" x2="100" :y1="chartSeptemberIce.zero" :y2="chartSeptemberIce.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in chartSeptemberIce.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.control?'1 1':null"/><line :x1="chartSeptemberIce.x" :x2="chartSeptemberIce.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{chartSeptemberIce.lo.toFixed(2)}}–{{chartSeptemberIce.hi.toFixed(2)}} m</small></div><div class="comparison-chart"><small>NORTH ATLANTIC TEMPERATURE CHANGE · K</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="North Atlantic temperature change by scenario"><line x1="0" x2="100" :y1="chartNorthAtlantic.zero" :y2="chartNorthAtlantic.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in chartNorthAtlantic.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.control?'1 1':null"/><line :x1="chartNorthAtlantic.x" :x2="chartNorthAtlantic.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{chartNorthAtlantic.lo.toFixed(1)}}–{{chartNorthAtlantic.hi.toFixed(1)}} K</small></div><div class="comparison-chart"><small>NORTH ATLANTIC OXYGEN CAPACITY · %</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Oxygen solubility change by scenario"><line x1="0" x2="100" :y1="chartOxygen.zero" :y2="chartOxygen.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in chartOxygen.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.control?'1 1':null"/><line :x1="chartOxygen.x" :x2="chartOxygen.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{chartOxygen.lo.toFixed(1)}}–{{chartOxygen.hi.toFixed(1)}} %</small></div><div v-if="chartThreshold" class="comparison-chart"><small>FRESH WATER (BLUE) VS TIPPING POINT (RED) · {{polarScenario}}</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Freshwater forcing against the tipping point"><polyline :points="chartThreshold.threshold" fill="none" stroke="#ff6b6b" stroke-width=".7"/><polyline :points="chartThreshold.forcing" fill="none" stroke="#75bbfa" stroke-width=".7"/><line :x1="chartThreshold.x" :x2="chartThreshold.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{chartThreshold.lo.toFixed(3)}}–{{chartThreshold.hi.toFixed(3)}} Sv · the circulation tips where blue crosses red</small></div></div></section>
<section v-if="equilibrium" class="comparison outlet-report"><div class="section-label">EQUILIBRIUM DIAGRAM OF THE OVERTURNING</div><p>Steady states against the fresh water into the North Atlantic: strong sinking-north circulation (solid, stable), the unstable middle branch (dashed) and the reversed salinity-driven state (lower, stable), for the start climate (bright) and at +{{equilibrium.warmest.warming.toFixed(1)}} K (faded: warming shifts the tipping point left). The coloured path is the “{{polarScenario}}” run; the dot is the current frame; the grey line today's forcing. Once the path runs past the end of the strong branch there is no way back short of net evaporation.</p><svg class="equilibrium" viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Overturning equilibrium diagram"><line x1="0" x2="100" :y1="equilibrium.zero" :y2="equilibrium.zero" stroke="#4a5a66" stroke-width=".3"/><line :x1="equilibrium.present" :x2="equilibrium.present" y1="0" y2="50" stroke="#93a5af" stroke-width=".3"/><polyline :points="equilibrium.warmest.strong" fill="none" stroke="#7fd1ff" stroke-opacity=".35" stroke-width=".6"/><polyline :points="equilibrium.warmest.unstable" fill="none" stroke="#7fd1ff" stroke-opacity=".35" stroke-dasharray="1 1" stroke-width=".5"/><polyline :points="equilibrium.warmest.reversed" fill="none" stroke="#b388ff" stroke-opacity=".35" stroke-width=".6"/><polyline :points="equilibrium.start.strong" fill="none" stroke="#7fd1ff" stroke-width=".8"/><polyline :points="equilibrium.start.unstable" fill="none" stroke="#7fd1ff" stroke-dasharray="1 1" stroke-width=".6"/><polyline :points="equilibrium.start.reversed" fill="none" stroke="#b388ff" stroke-width=".8"/><polyline :points="equilibrium.trajectory" fill="none" stroke="#ffd166" stroke-width=".6"/><circle :cx="equilibrium.now.x" :cy="equilibrium.now.y" r="1.1" fill="#ffd166"/></svg><small>fresh water 0–{{equilibrium.fmax.toFixed(2)}} Sv (right) · overturning {{equilibrium.qlo.toFixed(0)}}–{{equilibrium.qhi.toFixed(0)}} Sv (up)</small></section></template><template v-if="view==='solar'&&solarMetrics">
<div class="metrics extra-metrics">
 <div title="Monthly mean sunspot number (v2) of this month: from the record before the forecast date, the ensemble median (and its 90 % range) after it."><small>SUNSPOT NUMBER · {{solarMetrics.month}} · {{solarMetrics.source==='record'?(solarBlock.record_kind==='cycle_table'?'SYNTHETIC RECORD':'RECORD'):'FORECAST'}}</small><strong class="date">{{solarMetrics.sunspot.toFixed(0)}} <em v-if="solarMetrics.source==='forecast'">(90 %: {{solarMetrics.sunspot_p5.toFixed(0)}}–{{solarMetrics.sunspot_p95.toFixed(0)}})</em><em v-else-if="solarMetrics.smoothed!==null">(smoothed {{solarMetrics.smoothed.toFixed(0)}})</em></strong></div>
 <div title="The cycle this month belongs to (counted from its minimum) and whether the smoothed sunspot number is rising or falling."><small>CYCLE · YEARS SINCE MINIMUM · PHASE</small><strong class="date">{{solarMetrics.cycle}} · {{solarMetrics.phase_years.toFixed(1)}} yr · {{solarMetrics.trend}}</strong></div>
 <div title="The 10.7 cm radio flux, the standard index of solar activity for the upper atmosphere (satellite drag)."><small>F10.7 RADIO FLUX</small><strong class="date">{{solarMetrics.f107_sfu.toFixed(0)}} sfu <em v-if="solarMetrics.f107_p5!==null">({{solarMetrics.f107_p5.toFixed(0)}}–{{solarMetrics.f107_p95.toFixed(0)}})</em></strong></div>
 <div title="Total solar irradiance at 1 AU: faculae brighten the Sun more than the spots darken it, so it is brightest at maximum."><small>TOTAL SOLAR IRRADIANCE</small><strong class="date">{{solarMetrics.tsi_w_m2.toFixed(2)}} W/m² <em>({{solarSigned(solarMetrics.tsi_change_w_m2,2)}} against the quiet Sun)</em></strong></div>
 <div title="Change from the quiet Sun in the band that splits oxygen molecules and so makes ozone, and in UV-B."><small>UV · 200–242 nm / UV-B</small><strong class="date">{{solarSigned(solarUvPick.herzberg.change_pct,1)}} % / {{solarSigned(solarUvPick.uvb.change_pct,2)}} % <em>(Lyman-α {{solarSigned(solarUvPick.lyman.change_pct,0)}} %)</em></strong></div>
 <div title="The solar cycle's effect on the clear-sky noon UV index at the ground: more UV at the top of the atmosphere, but also more ozone absorbing it."><small>SURFACE UV INDEX · SOLAR-CYCLE PART</small><strong class="date">{{solarSigned(solarMetrics.uv_index_change_pct,2)}} % <em>(ozone {{solarSigned(solarMetrics.ozone_pct,2)}} %)</em></strong></div>
 <div title="Expected number of flares of class X1 and above this month from the rate model, and the chance of at least one."><small>X-CLASS FLARES · EXPECTED / MONTH · P(≥ 1)</small><strong class="date">{{solarMetrics.flares.x1.toFixed(2)}} · {{(solarMetrics.flares.p_x1*100).toFixed(0)}} % <em>(M1+: {{solarMetrics.flares.m1.toFixed(1)}})</em></strong></div>
 <div title="Global-mean surface temperature anomaly caused by the solar cycle alone (two-layer energy balance), relative to the record's mean Sun."><small>GLOBAL SURFACE · SOLAR PART</small><strong class="date">{{solarSigned(solarMetrics.surface_k,3)}} K <em v-if="solarMetrics.surface_p5!==null">({{solarSigned(solarMetrics.surface_p5,3)}} … {{solarSigned(solarMetrics.surface_p95,3)}})</em></strong></div>
 <div title="The rendered disk: what the spots take away and the faculae add, integrated over the visible hemisphere (parts per million of the total)."><small>DISK TODAY · SPOTS / FACULAE / NET</small><strong class="date">{{solarMetrics.disk.spot_ppm.toFixed(0)}} / +{{solarMetrics.disk.facular_ppm.toFixed(0)}} / {{solarSigned(solarMetrics.disk.net_ppm,0)}} ppm <em>({{solarMetrics.disk.regions}} groups)</em></strong></div>
 <div v-if="longNow" title="The slow activity of the last decades (15-year moving average of the sunspot number) and the production of cosmogenic isotopes it allows, against the satellite era."><small>LONG-TERM CONTEXT · BACKGROUND ACTIVITY · ¹⁴C/¹⁰Be</small><strong class="date">{{longNow.background.toFixed(0)}} · {{longNow.production.toFixed(2)}}× <em>(Maunder Minimum: {{longTerm.series.background_sunspot[1680-longTerm.start_year]?.toFixed(0)??'—'}} · {{longTerm.series.cosmogenic_production[1680-longTerm.start_year]?.toFixed(2)??'—'}}×)</em></strong></div>
</div>
<section class="comparison outlet-report"><div class="section-label">THE SOLAR CYCLE · SUNSPOT NUMBER {{solarSunspot.x[0].toFixed(0)}}–{{solarSunspot.x[1].toFixed(0)}}</div>
 <p>Monthly sunspot number (thin grey), its 13-month smoothed mean (yellow) and the fitted cycle curves (dotted blue); from the green line on, the forecast: the ensemble median (dashed orange) with its 50 % and 90 % ranges. ▼ minima and ▲ maxima of the smoothed series; hollow ▽ △ are predicted, with the 90 % range of their date. {{solarBlock.record_kind==='cycle_table'?'The record is synthesised from an approximate table of the SILSO cycles (hand-entered, not verified) with monthly noise: its minima and maxima follow the table only within that noise.':'Record: '+solarBlock.data_origin}}</p>
 <div class="comparison-switch"><button v-for="w in [['focus','Last two cycles + forecast'],['modern','Since 1950'],['all','Whole record']]" :key="w[0]" :class="{selected:solarLayers.window===w[0]}" @click="solarLayers.window=w[0]">{{w[1]}}</button></div>
 <div class="comparison-chart"><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Sunspot number: record and forecast" style="height:230px">
  <line v-for="l in solarSunspot.levels" :key="'l'+l.v" x1="0" x2="100" :y1="l.y" :y2="l.y" stroke="#1d3340" stroke-width=".2"/>
  <line v-for="t in solarSunspot.ticks" :key="'t'+t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".2"/>
  <polygon :points="solarSunspot.paths.outer" fill="#ffb347" opacity=".16"/>
  <polygon :points="solarSunspot.paths.inner" fill="#ffb347" opacity=".26"/>
  <polyline :points="solarSunspot.paths.monthly" fill="none" stroke="#7d8f99" stroke-width=".18" opacity=".85"/>
  <polyline :points="solarSunspot.paths.model" fill="none" stroke="#7fd1ff" stroke-width=".3" stroke-dasharray=".8 .6"/>
  <polyline :points="solarSunspot.paths.smoothed" fill="none" stroke="#ffd166" stroke-width=".55"/>
  <polyline :points="solarSunspot.paths.median" fill="none" stroke="#ffb347" stroke-width=".55" stroke-dasharray="1.2 .6"/>
  <line :x1="solarSunspot.issue" :x2="solarSunspot.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/>
  <line :x1="solarSunspot.cursor" :x2="solarSunspot.cursor" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/>
  <template v-for="(e,k) in solarSunspot.marks" :key="'m'+k"><line v-if="e.predicted" :x1="solarX(e.year_p5,solarSunspot.x)" :x2="solarX(e.year_p95,solarSunspot.x)" :y1="e.y" :y2="e.y" :stroke="e.kind==='minimum'?'#7fd1ff':'#ff8a65'" stroke-width=".45" opacity=".85"/><text :x="e.x" :y="e.kind==='minimum'?e.y-.8:e.y-1.2" font-size="2.6" text-anchor="middle" :fill="e.kind==='minimum'?'#7fd1ff':'#ff8a65'"><title>{{e.predicted?'Predicted ':''}}{{e.kind}} of cycle {{e.cycle}}: {{fmtYear(e.year)}}, sunspot number {{e.value.toFixed(0)}}</title>{{e.kind==='minimum'?(e.predicted?'▽':'▼'):(e.predicted?'△':'▲')}}</text></template>
 </svg><small>sunspot number 0–{{solarSunspot.y[1].toFixed(0)}} (grid lines every 100) · years {{solarSunspot.ticks.map(t=>t.year).join(' · ')}} · forecast issued {{fmtYear(solarBlock.forecast.issue_year)}} · dashed grey: this frame</small></div>
 <p v-if="solarBlock.forecast.current_cycle"><strong>Cycle {{solarBlock.forecast.current_cycle.cycle}}</strong> began at the minimum of {{fmtYear(solarBlock.forecast.current_cycle.minimum_year)}} and is {{solarBlock.forecast.current_cycle.phase_years.toFixed(1)}} years old at the forecast date; {{solarBlock.forecast.current_cycle.maximum_observed?'its maximum has passed':'its maximum is still ahead'}}. Its fitted amplitude {{solarBlock.forecast.current_cycle.fit_amplitude.toFixed(0)}} (backtest error at this age ±{{solarBlock.forecast.current_cycle.fit_sigma===null?'—':solarBlock.forecast.current_cycle.fit_sigma.toFixed(0)}}) is weighed against the prior {{solarBlock.forecast.current_cycle.prior_amplitude.toFixed(0)}} ± {{solarBlock.forecast.current_cycle.prior_sigma.toFixed(0)}} from the earlier cycles: {{solarBlock.forecast.current_cycle.amplitude.toFixed(0)}} ± {{solarBlock.forecast.current_cycle.amplitude_sigma.toFixed(0)}}.
  <template v-if="solarNext.current"> Its maximum: {{fmtYear(solarNext.current.year_median)}} at {{solarNext.current.value_median.toFixed(0)}} ({{solarNext.current.value_p5.toFixed(0)}}–{{solarNext.current.value_p95.toFixed(0)}}).</template>
  <template v-if="solarNext.minimum"> <strong>Next minimum</strong> {{fmtYear(solarNext.minimum.year_median)}} (90 %: {{fmtYear(solarNext.minimum.year_p5)}} – {{fmtYear(solarNext.minimum.year_p95)}}), starting cycle {{solarNext.minimum.cycle}}. </template>
  <template v-if="solarNext.maximum"> <strong>Cycle {{solarNext.maximum.cycle}} maximum</strong> around {{fmtYear(solarNext.maximum.year_median)}} ({{fmtYear(solarNext.maximum.year_p5)}} – {{fmtYear(solarNext.maximum.year_p95)}}) with a smoothed sunspot number of {{solarNext.maximum.value_median.toFixed(0)}} (90 %: {{solarNext.maximum.value_p5.toFixed(0)}}–{{solarNext.maximum.value_p95.toFixed(0)}}).</template>
  A cycle that has not started is forecast from the cycle lengths and amplitudes of the record only (mean period {{solarBlock.statistics.mean_period_years.toFixed(1)}} ± {{solarBlock.statistics.sd_period_years.toFixed(1)}} years, amplitude {{solarBlock.statistics.mean_amplitude.toFixed(0)}} ± {{solarBlock.statistics.sd_amplitude.toFixed(0)}}, year-to-year memory {{solarBlock.statistics.amplitude_autocorrelation.toFixed(2)}}): hence the wide range. Physical precursors such as the Sun's polar magnetic field at minimum are not in this model.</p>
</section>
<section class="comparison outlet-report"><div class="section-label">MINIMA &amp; MAXIMA · CYCLE BY CYCLE</div>
 <p>From the 13-month smoothed sunspot number: the minimum that starts each cycle, its maximum, its length (minimum to minimum) and rise time. Strong cycles tend to rise faster (Waldmeier effect; correlation of rise time and maximum here {{solarBlock.statistics.waldmeier_correlation===null?'—':solarBlock.statistics.waldmeier_correlation.toFixed(2)}}). Italic rows: forecast (median, 90 % range).<template v-if="solarBlock.record_kind==='cycle_table'"> The last column is the input table the record was synthesised from.</template></p>
 <table class="event-table"><thead><tr><th>Cycle</th><th>Minimum</th><th>Sunspots at minimum</th><th>Maximum</th><th>Sunspots at maximum</th><th>Length</th><th>Rise</th><th v-if="solarBlock.record_kind==='cycle_table'">Table: min · max</th></tr></thead><tbody>
  <tr v-for="r in solarCycleRowsShown" :key="r.cycle" :style="r.predicted?{fontStyle:'italic',color:'#ffcf8a'}:r.in_progress?{color:'#c8e8bd'}:{}">
   <template v-if="!r.predicted"><td>{{r.cycle}}{{r.in_progress?' (in progress)':''}}</td><td>{{fmtYear(r.minimum_year)}}</td><td>{{r.minimum.toFixed(1)}}</td><td>{{r.maximum_year===null?(r.predicted_maximum?fmtYear(r.predicted_maximum.year_median)+' (forecast)':'—'):fmtYear(r.maximum_year)}}</td><td>{{r.maximum===null?(r.predicted_maximum?r.predicted_maximum.value_median.toFixed(0)+' (forecast)':'—'):r.maximum.toFixed(1)}}</td><td>{{r.length_years===null?'—':r.length_years.toFixed(1)+' yr'}}</td><td>{{r.rise_years===null?'—':r.rise_years.toFixed(1)+' yr'}}</td><td v-if="solarBlock.record_kind==='cycle_table'">{{r.table?`${r.table.minimum_year.toFixed(1)} · ${r.table.maximum.toFixed(0)}`:'—'}}</td></template>
   <template v-else><td>{{r.cycle}} (forecast)</td><td>{{r.minimum?`${fmtYear(r.minimum.year_median)} (${fmtYear(r.minimum.year_p5)} – ${fmtYear(r.minimum.year_p95)})`:'—'}}</td><td>{{r.minimum?`${r.minimum.value_median.toFixed(0)} (${r.minimum.value_p5.toFixed(0)}–${r.minimum.value_p95.toFixed(0)})`:'—'}}</td><td>{{r.maximum?`${fmtYear(r.maximum.year_median)} (${fmtYear(r.maximum.year_p5)} – ${fmtYear(r.maximum.year_p95)})${r.maximum.beyond_horizon?' · partly beyond the horizon':''}`:'after the horizon'}}</td><td>{{r.maximum?`${r.maximum.value_median.toFixed(0)} (${r.maximum.value_p5.toFixed(0)}–${r.maximum.value_p95.toFixed(0)})`:'—'}}</td><td>—</td><td>—</td><td v-if="solarBlock.record_kind==='cycle_table'">—</td></template>
  </tr></tbody></table>
</section>
<section class="comparison outlet-report"><div class="section-label">HOW GOOD IS THE FORECAST? · BACKTEST ON THE PAST CYCLES</div>
 <p>Every past cycle with three complete cycles before it was forecast again with only the data available at its minimum + the given number of years, and compared with the maximum that followed (mean absolute error of the smoothed sunspot number at maximum, and of its date). Until about 1.5 years after a minimum it cannot yet be detected in the smoothed series, so only the prior (earlier cycles) is available; afterwards the fit to the new cycle takes over and improves quickly. The combined column uses error estimates from these same cycles, so it is partly in-sample.</p>
 <table class="event-table"><thead><tr><th>Years after minimum</th><th>Forecasts</th><th title="Mean absolute error of the fit to the cycle so far">Fit error</th><th title="Mean absolute error of the prior from the earlier cycles">Prior error</th><th title="Fit and prior weighted by their backtest errors">Combined error</th><th title="Mean absolute error of the date of the maximum">Maximum date error</th><th title="Mean absolute error of the predicted minimum date (before it is detectable)">Minimum date error</th></tr></thead><tbody>
  <tr v-for="r in solarBlock.backtest.summary" :key="r.phase_years"><td>{{r.phase_years}}</td><td>{{r.forecasts}}</td><td>{{r.fit_mae===null?'not yet detectable':r.fit_mae.toFixed(0)}}</td><td>{{r.prior_mae===null?'—':r.prior_mae.toFixed(0)}}</td><td>{{r.combined_mae===null?'—':r.combined_mae.toFixed(0)}}</td><td>{{r.timing_mae_years===null?'—':r.timing_mae_years.toFixed(1)+' yr'}}</td><td>{{r.minimum_mae_years===null?'—':r.minimum_mae_years.toFixed(1)+' yr'}}</td></tr>
 </tbody></table>
</section>
<section class="comparison outlet-report"><div class="section-label">UV &amp; IRRADIANCE · {{solarMetrics.month}}</div>
 <p>The shorter the wavelength, the stronger the cycle: against the quiet Sun (bars; ticks: at a maximum of sunspot number {{solarBlock.irradiance.reference.sunspot_number}}). At that maximum the total irradiance rises by {{solarBlock.irradiance.reference.tsi_change_w_m2.toFixed(2)}} W/m² (faculae +{{solarBlock.irradiance.reference.facular_w_m2.toFixed(2)}}, spots −{{solarBlock.irradiance.reference.spot_w_m2.toFixed(2)}}), {{(solarBlock.irradiance.reference.uv_share_of_tsi_change*100).toFixed(0)}} % of it below 400 nm. Measured UV variability differs by about a factor of two between instruments and reconstructions.</p>
 <div class="solar-spectrum" role="list" aria-label="UV change by band (logarithmic scale)"><div v-for="u in solarSpectrum" :key="u.key" role="listitem"><span>{{u.label}}</span><div class="bar"><i :style="{width:u.w+'%',background:u.color}"></i><b :style="{left:u.ref+'%'}" :title="`At the reference maximum: +${u.reference_pct.toFixed(u.reference_pct<1?2:1)} %`"></b></div><strong>{{solarSigned(u.change_pct,u.change_pct<1?2:1)}} %<em v-if="u.p5!==null"> ({{u.p5.toFixed(u.p5<1?2:1)}}–{{u.p95.toFixed(u.p95<1?2:1)}})</em></strong></div><small>logarithmic scale 0.05 % … 100 % · white tick: at the reference maximum</small></div>
 <div class="outlet-charts">
  <div v-if="solarTsiChart" class="comparison-chart"><small>TOTAL SOLAR IRRADIANCE · W/m²</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Total solar irradiance"><line v-for="t in solarTsiChart.ticks" :key="t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".2"/><polygon :points="solarTsiChart.band" fill="#ffd166" opacity=".2"/><polyline :points="solarTsiChart.record" fill="none" stroke="#ffd166" stroke-width=".35"/><polyline :points="solarTsiChart.median" fill="none" stroke="#ffb347" stroke-width=".5" stroke-dasharray="1.2 .6"/><line :x1="solarTsiChart.issue" :x2="solarTsiChart.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/><line :x1="solarTsiChart.cursor" :x2="solarTsiChart.cursor" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{solarTsiChart.lo.toFixed(2)}}–{{solarTsiChart.hi.toFixed(2)}} W/m² · monthly, forecast median and 90 % range</small></div>
  <div v-if="solarUvChart" class="comparison-chart"><small>UV AT THE TOP vs AT THE GROUND · % CHANGE FROM THE QUIET SUN</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="UV at the top of the atmosphere and UV index at the ground"><line v-for="t in solarUvChart.ticks" :key="t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".2"/><line x1="0" x2="100" :y1="solarUvChart.zero" :y2="solarUvChart.zero" stroke="#3a505d" stroke-width=".25"/><polyline :points="solarUvChart.top.record" fill="none" stroke="#b388ff" stroke-width=".35"/><polyline :points="solarUvChart.top.median" fill="none" stroke="#b388ff" stroke-width=".5" stroke-dasharray="1.2 .6"/><polyline :points="solarUvChart.ground.record" fill="none" stroke="#7fffd4" stroke-width=".45"/><polyline :points="solarUvChart.ground.median" fill="none" stroke="#7fffd4" stroke-width=".5" stroke-dasharray="1.2 .6"/><line :x1="solarUvChart.issue" :x2="solarUvChart.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/><line :x1="solarUvChart.cursor" :x2="solarUvChart.cursor" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small><span style="color:#b388ff">■ {{solarUvChart.label}}</span> · <span style="color:#7fffd4">■ noon UV index at the ground</span> · {{solarUvChart.lo.toFixed(1)}} … +{{solarUvChart.hi.toFixed(1)}} %</small></div>
 </div>
 <p>At the ground the cycle nearly cancels: more UV at the top also makes more ozone (+{{(solarBlock.atmosphere.ozone_change_at_reference*100).toFixed(0)}} % at the reference maximum), which absorbs UV-B. With these settings the clear-sky UV index changes by {{solarSigned(solarBlock.atmosphere.uv_index_change_at_reference_pct,1)}} % between the quiet Sun and the maximum, far less than the seasons, clouds or the ozone layer's own variations; the sign depends on the assumed ozone response. Noon UV index this month, clear sky:</p>
 <table class="event-table"><thead><tr><th>Site</th><th>Latitude</th><th>Ozone</th><th>Quiet Sun</th><th>This month</th><th>Solar-cycle change</th><th>Yearly maximum (quiet)</th></tr></thead><tbody>
  <tr v-for="x in solarSites" :key="x.name"><td>{{x.name}}</td><td>{{x.latitude_deg.toFixed(0)}}°</td><td>{{x.ozone_du.toFixed(0)}} DU</td><td>{{x.quiet.toFixed(2)}}</td><td>{{x.now.toFixed(2)}}</td><td>{{solarSigned(x.change_pct,2)}} %</td><td>{{x.year_max.toFixed(1)}}</td></tr>
 </tbody></table>
</section>
<section class="comparison outlet-report"><div class="section-label">SOLAR FLARES · RADIO BLACKOUTS AND THEIR ENERGY</div>
 <p>Flares follow the sunspot number: {{solarBlock.flares.config.x_rate_per_year}} flares of class X1 and above per year at a sunspot number of {{solarBlock.flares.config.reference_sunspot_number}}, scaling as SN<sup>{{solarBlock.flares.config.rate_exponent}}</sup>, with a power-law size distribution (index {{solarBlock.flares.config.size_index}}: about {{(solarBlock.flares.rates[0].m1_per_year/solarBlock.flares.rates[0].x1_per_year).toFixed(0)}} M-class flares per X-class flare). Bars: synthetic yearly counts of the record (random draws, not a flare list); line: their expectation; dashed: forecast median with its 90 % range.</p>
 <div class="outlet-charts">
  <div v-if="solarFlareChart" class="comparison-chart"><small>X-CLASS FLARES PER YEAR</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="X-class flares per year"><line v-for="t in solarFlareChart.ticks" :key="t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".2"/><rect v-for="b in solarFlareChart.bars" :key="b.year" :x="b.x" :y="b.y" :width="b.w" :height="b.h" fill="#ff7b4a" opacity=".55"><title>{{b.year}}: {{b.count}} synthetic X flares (expected {{b.expected.toFixed(1)}})</title></rect><polyline :points="solarFlareChart.expected" fill="none" stroke="#ffd166" stroke-width=".4"/><polygon :points="solarFlareChart.band" fill="#ff7b4a" opacity=".18"/><polyline :points="solarFlareChart.median" fill="none" stroke="#ff7b4a" stroke-width=".5" stroke-dasharray="1.2 .6"/><line :x1="solarFlareChart.issue" :x2="solarFlareChart.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/><line :x1="solarFlareChart.cursor" :x2="solarFlareChart.cursor" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{solarFlareChart.hi.toFixed(0)}} per year</small></div>
  <div><table class="event-table"><thead><tr><th>NOAA scale · {{solarMetrics.month}}</th><th>Threshold</th><th>Expected this month</th><th>P(at least one)</th></tr></thead><tbody>
   <tr v-for="l in solarMetrics.flares.r_scale" :key="l.level"><td :style="{color:R_COLORS[l.level]}">{{l.label}}</td><td>≥ {{l.threshold}}</td><td>{{l.expected<.01?l.expected.toExponential(1):l.expected.toFixed(2)}}</td><td>{{(l.probability*100).toFixed(l.probability<.01?2:0)}} %</td></tr>
  </tbody></table><p v-if="solarMetrics.source==='record'">This month's synthetic draw: {{solarMetrics.flares.m1_count}} flares ≥ M1, {{solarMetrics.flares.x1_count}} ≥ X1{{solarMetrics.flares.largest_w_m2?`, the largest ${flareLabel(solarMetrics.flares.largest_w_m2)}`:''}}.</p></div>
 </div>
 <table class="event-table"><thead><tr><th>Year (forecast)</th><th>Sunspot number</th><th>X flares expected</th><th>M flares expected</th><th title="Chance of at least one flare of X10 or above (R4 severe radio blackout) in the year">P(X10+ in the year)</th></tr></thead><tbody>
  <tr v-for="y in solarOutlook" :key="y.year"><td>{{y.year}}</td><td>{{y.sunspot_p50.toFixed(0)}} ({{y.sunspot_p5.toFixed(0)}}–{{y.sunspot_p95.toFixed(0)}})</td><td>{{y.x1_expected_p50.toFixed(1)}} ({{y.x1_expected_p5.toFixed(1)}}–{{y.x1_expected_p95.toFixed(1)}})</td><td>{{y.m1_expected_p50.toFixed(0)}}</td><td>{{(y.x10_probability_p50*100).toFixed(0)}} % ({{(y.x10_probability_p5*100).toFixed(0)}}–{{(y.x10_probability_p95*100).toFixed(0)}} %)</td></tr>
 </tbody></table>
 <p>Energy: an X10 flare radiates about {{solarBlock.flares.energy.x10_j.toExponential(1)}} J, {{solarBlock.flares.energy.x10_fluence_at_earth_j_m2.toFixed(0)}} J/m² at the Earth over its duration. All flares of C1 and above together average {{solarBlock.flares.energy.mean_irradiance_at_reference_w_m2.toExponential(1)}} W/m² at a sunspot number of {{solarBlock.irradiance.reference.sunspot_number}}: {{(solarBlock.flares.energy.ratio_to_tsi_change*100).toFixed(2)}} % of the cycle's change in total irradiance. Flares matter for the ionosphere (radio blackouts on the day side), the thermosphere (EUV heating, satellite drag) and, through the particles of the eruptions that accompany them, for polar ozone, but not for the energy of weather and climate. Largest synthetic flares of the record: {{solarBlock.flares.record_largest.slice(0,5).map(f=>`${f.class} (${f.month})`).join(', ')}}.</p>
</section>
<section class="comparison outlet-report"><div class="section-label">EARTH'S ATMOSPHERE &amp; CLIMATE · THE SOLAR PART</div>
 <div class="outlet-charts">
  <div v-if="solarClimateChart" class="comparison-chart"><small>GLOBAL SURFACE TEMPERATURE · SOLAR-CYCLE ANOMALY · K</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Solar-cycle surface temperature anomaly"><line v-for="t in solarClimateChart.ticks" :key="t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".2"/><line x1="0" x2="100" :y1="solarClimateChart.zero" :y2="solarClimateChart.zero" stroke="#3a505d" stroke-width=".25"/><polygon :points="solarClimateChart.band" fill="#ff8a65" opacity=".2"/><polyline :points="solarClimateChart.record" fill="none" stroke="#ff8a65" stroke-width=".45"/><polyline :points="solarClimateChart.median" fill="none" stroke="#ff8a65" stroke-width=".5" stroke-dasharray="1.2 .6"/><line :x1="solarClimateChart.issue" :x2="solarClimateChart.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/><line :x1="solarClimateChart.cursor" :x2="solarClimateChart.cursor" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{solarClimateChart.lo.toFixed(3)}} … {{solarClimateChart.hi.toFixed(3)}} K · relative to the record's mean Sun</small></div>
  <div><table class="event-table"><thead><tr><th>Cycle</th><th>Maximum</th><th>Surface response, peak to peak</th></tr></thead><tbody><tr v-for="c in solarBlock.climate.cycles.slice(-6)" :key="c.cycle"><td>{{c.cycle}}</td><td>{{c.maximum.toFixed(0)}}</td><td>{{c.peak_to_peak_k.toFixed(3)}} K</td></tr></tbody></table></div>
 </div>
 <p>The forcing is the irradiance change spread over the sphere minus what is reflected: ΔS (1 − {{solarBlock.climate.config.albedo}}) / 4, {{solarBlock.climate.forcing_at_reference_w_m2.toFixed(2)}} W/m² between the quiet Sun and a maximum of {{solarBlock.irradiance.reference.sunspot_number}}. A two-layer energy balance (upper ocean and atmosphere {{solarBlock.climate.config.upper_heat_capacity}}, deep ocean {{solarBlock.climate.config.deep_heat_capacity}} W yr m⁻² K⁻¹, feedback {{solarBlock.climate.config.feedback_w_m2_k}} W m⁻² K⁻¹) damps and delays the 11-year swing: a few hundredths of a kelvin, about what two to three years of today's warming trend (roughly 0.2 K per decade) add. Observational analyses find up to about 0.1 K; the difference may come from the UV–ozone pathway in the stratosphere (top-down) and ocean dynamics, which this model leaves out. This month: total ozone {{solarSigned(solarMetrics.ozone_pct,2)}} %, upper stratosphere {{solarSigned(solarMetrics.stratosphere_k,2)}} K against the quiet Sun.</p>
</section>
<section class="comparison outlet-report"><div class="section-label">BUTTERFLY DIAGRAM · WHERE ON THE SUN THE SPOTS APPEAR</div>
 <p>Expected spot area by time and latitude. Each cycle starts with spots at about ±{{solarBlock.butterfly.drift.onset_latitude_deg}}° that move toward the equator as it ages (Spörer's law, e-folding in {{solarBlock.butterfly.drift.efold_months}} months); around a minimum the old cycle's low-latitude spots and the new cycle's high-latitude ones coexist. Modelled from the cycle curves and the forecast: the record holds no spot positions.</p>
 <div v-if="solarButterfly" class="solar-butterfly"><canvas ref="butterflyCanvas" aria-label="Butterfly diagram"></canvas><svg viewBox="0 0 100 50" preserveAspectRatio="none"><line x1="0" x2="100" y1="25" y2="25" stroke="#3a505d" stroke-width=".25"/><line v-for="t in solarButterfly.ticks" :key="t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#ffffff" stroke-width=".12" opacity=".25"/><line :x1="solarButterfly.issue" :x2="solarButterfly.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/><line :x1="solarButterfly.cursor" :x2="solarButterfly.cursor" y1="0" y2="50" stroke="#e8f4ff" stroke-dasharray="1 1" stroke-width=".3"/></svg></div>
 <small v-if="solarButterfly" class="solar-caption">latitude {{solarButterfly.latitude[0]}}° (bottom) … +{{solarButterfly.latitude[1]}}° (top) · years {{solarButterfly.ticks.map(t=>t.year).join(' · ')}}</small>
</section>
<template v-if="longTerm">
<section class="comparison outlet-report"><div class="section-label">GRAND MINIMA &amp; MAXIMA · SOLAR ACTIVITY {{longWindow[0]}}–{{longWindow[1]-1}}</div>
 <p>Beyond the 11-year cycle the Sun's activity changes over centuries. For decades at a time it almost stops making spots (grand minima: the Oort, Wolf, Spörer and Maunder Minima) or stays high (grand maxima: the Medieval and the Modern Maximum). Blue bands mark grand minima and orange bands grand maxima from the literature (paler for weaker minima and maxima). The bars along the bottom are the episodes found in the series: decadal mean below {{longTerm.episode_definition.grand_minimum_mean}} or above {{longTerm.episode_definition.grand_maximum_mean}} for at least {{longTerm.episode_definition.min_years}} years. Before {{Math.round(longTerm.envelope.first_known_onset)}} the cycles are synthesised around this catalogue (illustrative, not a reconstruction), then comes the cycle table, the record and, after the green line, the forecast continued with average cycles.<template v-if="longLayers.scenario"> Dashed blue: a new Maunder-like minimum from cycle {{longTerm.future.grand_minimum.first_cycle}} on.</template></p>
 <div class="comparison-switch"><button v-for="w in LONG_TERM_WINDOWS" :key="w.key" :class="{selected:longLayers.window===w.key}" @click="longLayers.window=w.key">{{w.label}}</button><button :class="{selected:longLayers.scenario}" @click="longLayers.scenario=!longLayers.scenario" title="A new grand minimum (Maunder-like) starting with the next cycle.">{{longLayers.scenario?'✓ ':''}}New grand minimum scenario</button></div>
 <div class="comparison-chart"><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Long-term solar activity" style="height:230px">
  <rect v-for="b in longActivity.bands" :key="'b'+b.name" :x="b.x" y="0" :width="b.w" height="50" :fill="b.color" :opacity="b.kind.startsWith('grand')?.2:.1"><title>{{b.name}} ({{Math.round(b.start_year)}}–{{Math.round(b.end_year)}}): {{b.evidence}}</title></rect>
  <line v-for="t in longActivity.ticks" :key="'t'+t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".2"/>
  <line v-for="t in longActivity.thresholds" :key="'h'+t.v" x1="0" x2="100" :y1="t.y" :y2="t.y" stroke="#93a5af" stroke-width=".2" stroke-dasharray=".6 .6"/>
  <polyline v-for="l in longActivity.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/>
  <rect v-for="(e,k) in longActivity.episodes" :key="'e'+k" :x="e.x" :y="e.kind==='grand_minimum'?47.6:46.4" :width="e.w" height="1.1" :fill="e.kind==='grand_minimum'?'#4f8bd6':'#ff9f43'"><title>{{e.kind==='grand_minimum'?'Grand minimum':'Grand maximum'}} {{e.start_year}}–{{e.end_year}}: mean sunspot number {{e.mean_sunspot}}{{e.epoch?' ('+e.epoch+')':''}}</title></rect>
  <line :x1="longActivity.issue" :x2="longActivity.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/>
  <line :x1="longActivity.cursor" :x2="longActivity.cursor" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/>
  <text v-for="b in longActivity.bands.filter(b=>b.w>2.5)" :key="'n'+b.name" :x="b.x+b.w/2" :y="b.kind.startsWith('grand')?4:8.5" font-size="2.2" text-anchor="middle" fill="#c7d6de">{{longLabel(b)}}</text>
 </svg><small>sunspot number 0–{{longActivity.hi.toFixed(0)}}: annual mean (grey), decadal mean (yellow); dotted: the episode thresholds · years {{longActivity.ticks.map(t=>t.year).join(' · ')}}</small></div>
 <div class="outlet-charts">
  <div class="comparison-chart"><small>TOTAL SOLAR IRRADIANCE · W/m² · band: low–high slow background</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Long-term TSI"><rect v-for="b in longTsi.bands" :key="b.name" :x="b.x" y="0" :width="b.w" height="50" :fill="b.color" :opacity="b.kind.startsWith('grand')?.16:.08"/><template v-for="l in longTsi.lines" :key="l.name"><polygon v-if="l.band" :points="l.band" :fill="l.color" opacity=".18"/><polyline :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/></template><line :x1="longTsi.issue" :x2="longTsi.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/></svg><small>{{longTsi.lo.toFixed(2)}}–{{longTsi.hi.toFixed(2)}} W/m²</small></div>
  <div class="comparison-chart"><small>COSMIC RAYS · ¹⁴C AND ¹⁰Be PRODUCTION (1 = satellite era)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Cosmogenic production"><rect v-for="b in longProduction.bands" :key="b.name" :x="b.x" y="0" :width="b.w" height="50" :fill="b.color" :opacity="b.kind.startsWith('grand')?.16:.08"/><polyline v-for="l in longProduction.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width"/><line :x1="longProduction.issue" :x2="longProduction.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/></svg><small>{{longProduction.lo.toFixed(2)}}–{{longProduction.hi.toFixed(2)}} · a weak solar wind lets in more cosmic rays: grand minima are written into tree rings (¹⁴C) and ice (¹⁰Be)</small></div>
 </div>
</section>
<section class="comparison outlet-report"><div class="section-label">WHAT DID IT DO TO THE CLIMATE? · GLOBAL SURFACE TEMPERATURE {{longWindow[0]}}–{{longWindow[1]-1}}</div>
 <p>The same two-layer energy balance, driven separately by the Sun, by volcanic eruptions and by CO₂ (anomalies about the pre-industrial mean before 1850). The solar band spans the uncertain slow brightening of the quiet Sun. The volcanic list is short and rounded: it shows the order of magnitude, not a reconstruction. <label class="toggle" style="display:inline-flex;margin:0 0 0 8px"><input type="checkbox" v-model="longLayers.regional"> {{longTerm.regional_winter.name}}</label><label class="toggle" style="display:inline-flex;margin:0 0 0 8px" title="The solar and volcanic contributions are small next to CO₂: zoom to ±0.45 K (lines beyond are cut)."><input type="checkbox" v-model="longLayers.zoom"> Zoom to ±0.45 K</label></p>
 <div class="comparison-chart"><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Long-term temperature contributions" style="height:200px">
  <rect v-for="b in longClimate.bands" :key="b.name" :x="b.x" y="0" :width="b.w" height="50" :fill="b.color" :opacity="b.kind.startsWith('grand')?.16:.08"/>
  <line v-for="t in longClimate.ticks" :key="'t'+t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".2"/>
  <line x1="0" x2="100" :y1="longClimate.zero" :y2="longClimate.zero" stroke="#3a505d" stroke-width=".25"/>
  <template v-for="l in longClimate.lines" :key="l.name"><polygon v-if="l.band" :points="l.band" :fill="l.color" opacity=".22"/><polyline :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/></template>
  <line :x1="longClimate.issue" :x2="longClimate.issue" y1="0" y2="50" stroke="#9fe870" stroke-width=".3"/>
 </svg><small>{{longClimate.lo.toFixed(2)}} … +{{longClimate.hi.toFixed(2)}} K · <span v-for="l in longClimate.lines" :key="l.name" :style="{color:l.color,marginRight:'.8em'}">■ {{l.name}}</span></small></div>
 <table class="event-table"><thead><tr><th>Epoch</th><th>Years</th><th title="Mean annual sunspot number (v2) in the series">Sunspots</th><th title="Found in the series as an episode">Detected</th><th title="Mean TSI against the satellite era 1976–2019: central (low … high slow background)">TSI vs today, W/m²</th><th title="Change of the 200–242 nm UV (ozone production) against the pre-industrial mean">UV 200–242 nm</th><th title="¹⁴C/¹⁰Be production against the pre-industrial mean">¹⁴C / ¹⁰Be</th><th title="Global surface temperature from the Sun: central (low … high)">Solar, K</th><th>Volcanic, K</th><th>CO₂, K</th><th>Total, K</th><th :title="longTerm.regional_winter.name">Regional winter, K</th></tr></thead><tbody>
  <tr v-for="e in longEpochs" :key="e.name" :title="e.evidence"><td :style="{color:e.color}">{{e.name}} <small style="display:inline">({{e.label}})</small></td><td style="white-space:nowrap">{{Math.round(e.start_year)}}–{{Math.round(e.end_year)}}</td><td>{{e.mean_sunspot.toFixed(0)}}</td><td>{{e.detected??'—'}}</td><td style="white-space:nowrap">{{solarSigned(e.tsi_vs_reference_w_m2.central,2)}} <small style="display:inline">({{solarSigned(e.tsi_vs_reference_w_m2.low,1)}}…{{solarSigned(e.tsi_vs_reference_w_m2.high,1)}})</small></td><td>{{solarSigned(e.uv_200_242_pct,1)}} %</td><td>{{solarSigned(e.cosmogenic_production_pct,0)}} %</td><td style="white-space:nowrap">{{solarSigned(e.temperature_k.solar_central,2)}} <small style="display:inline">({{solarSigned(e.temperature_k.solar_low,2)}}…{{solarSigned(e.temperature_k.solar_high,2)}})</small></td><td>{{solarSigned(e.temperature_k.volcanic,2)}}</td><td>{{solarSigned(e.temperature_k.co2,2)}}</td><td>{{solarSigned(e.temperature_k.total,2)}}</td><td>{{solarSigned(e.temperature_k.regional_winter,2)}}</td></tr>
 </tbody></table>
 <p>Temperatures and UV against the pre-industrial mean ({{longTerm.start_year}}–{{longTerm.preindustrial_end_year}}), which itself contains several grand minima. Against the Modern Maximum the solar difference doubles. A grand minimum cools the globe by a few hundredths to about a tenth of a kelvin in this model; eruptions, chance variability and, in the industrial era, CO₂ are larger. The winter column illustrates that the regional effects can be several times larger (top-down pathway through the stratosphere); it is a parametrisation, not a circulation model.</p>
</section>
<section v-if="longCase" class="comparison outlet-report"><div class="section-label">CASE STUDY · THE {{longCase.row.name.toUpperCase()}} {{Math.round(longCase.row.start_year)}}–{{Math.round(longCase.row.end_year)}}</div>
 <div class="outlet-charts">
  <div class="comparison-chart"><small>SUNSPOT NUMBER (yellow) AND ¹⁴C/¹⁰Be PRODUCTION × 100 (green dashed) · {{longCase.xr[0]}}–{{longCase.xr[1]}}</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Case study chart"><rect v-for="b in longCase.chart.bands" :key="b.name" :x="b.x" y="0" :width="b.w" height="50" :fill="b.color" opacity=".18"/><line v-for="t in longCase.chart.ticks" :key="t.year" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".2"/><polyline v-for="l in longCase.chart.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/></svg><small>0–{{longCase.chart.hi.toFixed(0)}} · years {{longCase.chart.ticks.map(t=>t.year).join(' · ')}} · model cycles (synthetic before 1755): {{longCase.cycles.map(c=>`${c.number}: ${c.amplitude.toFixed(0)}`).join(', ')}}</small></div>
  <div><table class="event-table"><thead><tr><th>The model, against the satellite era</th><th></th></tr></thead><tbody>
   <tr><td>Total solar irradiance</td><td>{{solarSigned(longCase.row.tsi_vs_reference_w_m2.central,2)}} W/m² (low {{solarSigned(longCase.row.tsi_vs_reference_w_m2.low,2)}}, high {{solarSigned(longCase.row.tsi_vs_reference_w_m2.high,2)}})</td></tr>
   <tr><td>Against the pre-industrial mean</td><td>{{solarSigned(longCase.row.tsi_vs_preindustrial_w_m2.central,2)}} W/m²</td></tr>
   <tr><td>UV 200–242 nm · ozone · upper stratosphere</td><td>{{solarSigned(longCase.row.uv_200_242_pct,1)}} % · {{solarSigned(longCase.row.ozone_pct,1)}} % · {{solarSigned(longCase.row.stratosphere_k,2)}} K</td></tr>
   <tr><td>¹⁴C / ¹⁰Be production</td><td>{{solarSigned(longCase.row.cosmogenic_production_pct,0)}} % (pre-industrial mean)</td></tr>
   <tr><td>Global temperature: solar · volcanic · CO₂</td><td>{{solarSigned(longCase.row.temperature_k.solar_central,2)}} · {{solarSigned(longCase.row.temperature_k.volcanic,2)}} · {{solarSigned(longCase.row.temperature_k.co2,2)}} K</td></tr>
   <tr><td>{{longTerm.regional_winter.name}}</td><td>{{solarSigned(longCase.row.temperature_k.regional_winter,2)}} K</td></tr>
  </tbody></table></div>
 </div>
 <p v-if="longCase.row.name==='Maunder Minimum'"><strong>What was observed.</strong> Telescopes had watched the Sun since 1610, yet between 1645 and 1715 observers such as Hevelius, Cassini, Picard, La Hire and Flamsteed saw only a few dozen spot groups in seventy years, often none for years, mostly in the southern hemisphere. Aurorae became rare in Europe. Spörer and Maunder noticed the gap in the 1880s–90s, and Eddy (1976) confirmed it with the ¹⁴C of tree rings. A reassessment of all data sets (Usoskin et al. 2015) concludes that it was a real grand minimum, not a lack of observers. The ¹⁰Be of the ice cores shows that a weak 11-year magnetic cycle went on throughout (Beer, Tobias &amp; Weiss 1998): the dynamo did not stop, it stopped making sunspots.</p>
 <p v-if="longCase.row.name==='Maunder Minimum'"><strong>What caused it.</strong> The solar dynamo regenerates the Sun's polar field from the decay of tilted sunspot groups (the Babcock–Leighton mechanism), and this source scatters from cycle to cycle. A bad draw can leave the toroidal field too weak to rise as spots; without spots the Babcock–Leighton source switches off. Only a weak turbulent (mean-field α) effect then keeps a faint magnetic cycle alive, until chance lifts the field back over the threshold. The dynamo map below reproduces how often and how long this happens. The cause was internal to the Sun; there is no external trigger and no regular clock.</p>
 <p v-if="longCase.row.name==='Maunder Minimum'"><strong>What it did.</strong> In this model the dimmer Sun, at {{solarSigned(longCase.row.tsi_vs_reference_w_m2.central,1)}} W/m² or about {{Math.abs(longCase.row.tsi_vs_reference_w_m2.central/13.61).toFixed(2)}} %, cools the globe by {{Math.abs(longCase.row.temperature_k.solar_central).toFixed(2)}} K against the pre-industrial mean, and by about {{Math.abs(longCase.row.temperature_k.solar_central-(longEpochs.find(e=>e.name==='Modern Maximum')?.temperature_k.solar_central??0)).toFixed(2)}} K against the Modern Maximum. It coincided with the coldest phase of the Little Ice Age in Europe: frost fairs on the Thames and the Great Frost of 1683–84. The Little Ice Age, however, began earlier and was global only in part. Reconstructions and models attribute most of its cold to clusters of volcanic eruptions, with the Sun a secondary driver (e.g. Schurer et al. 2014). The harsh European winters fit a regional pathway: less UV, less stratospheric ozone and heating, a weaker polar vortex and more blocking over the North Atlantic (Shindell et al. 2001; Ineson et al. 2015). The UV fell by {{Math.abs(longCase.row.uv_200_242_pct).toFixed(1)}} % in the ozone-making band here, and the illustrative regional column gives {{solarSigned(longCase.row.temperature_k.regional_winter,2)}} K.</p>
</section>
<section v-if="longDynamoBars" class="comparison outlet-report"><div class="section-label">HOW DOES A GRAND MINIMUM HAPPEN? · A STOCHASTIC DYNAMO MAP</div>
 <p>Each cycle's toroidal field b comes from the previous one. The Babcock–Leighton source (it needs spots) varies from cycle to cycle; a weak mean-field α effect (α = {{longTerm.dynamo.config.weak_alpha}}) works without spots. Only fields above the emergence threshold ({{longTerm.dynamo.config.emergence_threshold}}) rise as spots. Below, one grand minimum of the model: bars show the cycle amplitudes (blue inside the episode), the line the share of the field that emerges as spots, and the dotted line the grand-minimum threshold.<template v-if="longDynamo.trigger"> It begins after cycle {{longDynamo.trigger.cycle}}, whose Babcock–Leighton source ran at {{longDynamo.trigger.efficiency.toFixed(2)}} of its mean{{longDynamo.trigger.efficiency<0?', reversed':''}} (badly tilted groups cancel the polar field). The next field is too weak to emerge, spots vanish for {{longDynamo.cycles}} cycles, yet a weak magnetic cycle goes on, until the field grows back over the threshold<template v-if="longDynamo.recovery"> (cycle {{longDynamo.recovery.cycle}}: {{longDynamo.recovery.amplitude.toFixed(0)}})</template>.</template></p>
 <div class="outlet-charts">
  <div class="comparison-chart"><small>CYCLE AMPLITUDES OF THE EXAMPLE · emergence (line, 0–1)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Dynamo example"><rect :x="longDynamoBars.episode.x" y="0" :width="longDynamoBars.episode.w" height="50" fill="#4f8bd6" opacity=".15"/><rect v-for="b in longDynamoBars.bars" :key="b.k" :x="b.x" :y="b.y" :width="b.w" :height="b.h" :fill="b.inside?'#4f8bd6':'#ffd166'" opacity=".8"><title>cycle {{b.cycle}}: amplitude {{b.amplitude.toFixed(1)}} · field {{b.field.toFixed(2)}} · emergence {{(b.emergence*100).toFixed(0)}} % · Babcock–Leighton source {{b.efficiency.toFixed(2)}}</title></rect><line x1="0" x2="100" :y1="longDynamoBars.threshold" :y2="longDynamoBars.threshold" stroke="#93a5af" stroke-dasharray=".8 .8" stroke-width=".3"/><polyline :points="longDynamoBars.emergence" fill="none" stroke="#9fe870" stroke-width=".5"/></svg><small>amplitude 0–{{longDynamoBars.max.toFixed(0)}}</small></div>
  <div><table class="event-table"><thead><tr><th>Holocene statistics</th><th>Dynamo map</th><th>Isotope records</th></tr></thead><tbody><tr v-for="r in longTargets" :key="r.label"><td>{{r.label}}</td><td>{{r.model}}</td><td>{{r.target}}</td></tr></tbody></table>
   <small class="solar-caption">Records: {{longTerm.dynamo.targets?.source??'—'}}. The map is calibrated to the grand minima. It makes grand maxima too short and too rare: lasting high activity needs a slower memory (for example of the meridional flow) that this one-dimensional map lacks.</small></div>
 </div>
 <div v-if="longHolocene" class="comparison-chart"><small>A SYNTHETIC HOLOCENE · {{longHolocene.years.toFixed(0)}} years of the map, {{longHolocene.minima}} grand minima (blue) and {{longHolocene.maxima}} grand maxima (orange)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Synthetic Holocene" style="height:110px"><rect v-for="(b,k) in longHolocene.bands" :key="k" :x="b.x" y="0" :width="b.w" height="50" :fill="b.kind==='grand_minimum'?'#4f8bd6':'#ff9f43'" opacity=".35"/><polyline :points="longHolocene.line" fill="none" stroke="#ffd166" stroke-width=".15"/></svg></div>
</section>
<section class="comparison outlet-report"><div class="section-label">A NEW GRAND MINIMUM? · OUTLOOK AND WHAT IT WOULD CHANGE</div>
 <p>The dynamo map started from the last two cycles ({{longTerm.dynamo.outlook.observed_amplitudes.join(' and ')}}) in {{longTerm.dynamo.outlook.realizations.toLocaleString()}} realisations. A grand minimum counts as begun once two consecutive cycles stay below the threshold. By {{longTerm.horizon_year}}<template v-if="longHorizonRow"> (cycle {{longHorizonRow.cycle}}, the last to peak before it)</template> it has begun in <strong>{{(longTerm.dynamo.outlook.grand_minimum_by_horizon*100).toFixed(0)}} %</strong> of them; a constant rate of {{longTerm.dynamo.statistics.grand_minima_per_11400_years.toFixed(0)}} per 11,400 years would give {{(longTerm.dynamo.outlook.poisson_by_horizon*100).toFixed(0)}} %. The map's next cycle is close to climatology: it has no polar-field precursor, which is why the statistical forecast above is used for the next cycles.</p>
 <div class="outlet-charts">
  <table class="event-table"><thead><tr><th>Cycle</th><th>Peak around</th><th>Amplitude (map, 90 %)</th><th>Below threshold</th><th>Grand minimum begun</th></tr></thead><tbody><tr v-for="c in longTerm.dynamo.outlook.cycles" :key="c.cycle"><td>{{c.cycle}}</td><td>{{c.peak_year.toFixed(0)}}</td><td>{{c.amplitude_p50.toFixed(0)}} ({{c.amplitude_p5.toFixed(0)}}–{{c.amplitude_p95.toFixed(0)}})</td><td>{{(c.below_threshold*100).toFixed(0)}} %</td><td>{{(c.grand_minimum_begun*100).toFixed(1)}} %</td></tr></tbody></table>
  <div><p>If a Maunder-like minimum (cycle amplitude {{longTerm.future.grand_minimum.amplitude}}) began with cycle {{longTerm.future.grand_minimum.first_cycle}} and lasted {{longTerm.future.grand_minimum.cycles}} cycles, against the regular continuation:</p><table class="event-table"><thead><tr><th>Year</th><th>TSI, W/m²</th><th>Global, K</th><th>Winter (regional), K</th><th title="Warming from CO₂ alone since the forecast date, illustrative path">CO₂ since {{Math.floor(longTerm.issue_year)}}, K</th></tr></thead><tbody><tr v-for="r in longTerm.future.rows" :key="r.year"><td>{{r.year}}</td><td>{{solarSigned(r.tsi_difference_w_m2,2)}}</td><td>{{solarSigned(r.global_difference_k,2)}}</td><td>{{solarSigned(r.regional_winter_difference_k,2)}}</td><td>{{solarSigned(r.co2_warming_since_issue_k,2)}}</td></tr></tbody></table>
   <small class="solar-caption">Differences against the regular continuation; at most {{solarSigned(longTerm.future.largest_global_difference_k,2)}} K ({{longTerm.future.largest_difference_year}}). A new grand minimum would slow the warming a little, not stop it (compare Feulner &amp; Rahmstorf 2010).</small></div>
 </div>
</section>
</template>
</template>
<template v-if="view==='dust'&&dustMetrics">
<div class="metrics extra-metrics">
 <div title="Total mass of dust and small meteoroids entering the atmosphere per day (the scenario chosen in the sidebar)."><small>ENTERING EVERY DAY · {{dustLayers.scenario.toUpperCase()}}</small><strong class="date">{{dustScenario.influx_t_per_day}} t <em>({{formatNumber(dustScenario.influx_t_per_year,0)}} t/yr; estimates 5–300 t/day)</em></strong></div>
 <div title="Evaporated in the upper atmosphere: metal atoms that form the sodium and iron layers and recondense as meteoric smoke."><small>BURNS UP · MEDIAN ALTITUDE</small><strong class="date">{{formatNumber(dustScenario.ablated_t_per_day,1)}} t/day ({{(dustMetrics.ablated_share*100).toFixed(0)}} %) · {{dustBlock.fate.median_ablation_altitude_km.toFixed(0)}} km</strong></div>
 <div title="Micrometeorites reaching the ground: unmelted, partly melted and melted (cosmic spherules)."><small>REACHES THE GROUND</small><strong class="date">{{formatNumber(dustScenario.surviving_t_per_day,1)}} t/day <em>({{formatNumber(dustScenario.surface_deposition_ug_m2_yr,1)}} µg/m² per year; {{formatNumber(dustScenario.spherules_over_100um_per_m2_yr,2)}} spherules &gt; 100 µm per m² per year)</em></strong></div>
 <div title="Water bound in hydrated minerals and implanted by the solar wind, arriving with the dust."><small>EXTRATERRESTRIAL WATER · RELEASED ABOVE 80 km</small><strong class="date">{{formatNumber(dustScenario.water.in_t_per_day,2)}} t/day · {{(dustScenario.water.released_above_80km_t_per_day/dustScenario.water.in_t_per_day*100).toFixed(1)}} % <em>({{formatNumber(dustScenario.water.to_surface_kg_per_day,1)}} kg/day reach the ground)</em></strong></div>
 <div title="The kinetic energy of the dust, all turned into heat in the upper atmosphere."><small>ENERGY · AGAINST SUNLIGHT</small><strong class="date">{{formatNumber(dustScenario.energy.kinetic_power_w/1e9,2)}} GW · {{ratioLabel(dustScenario.energy.share_of_sunlight)}}</strong></div>
 <div title="Dust gained against hydrogen and helium escaping to space."><small>THE EARTH'S MASS · NET</small><strong class="date">{{formatNumber(dustScenario.mass.net_t_per_year,0)}} t/yr <em>({{dustScenario.mass.net_t_per_year<0?'losing':'gaining'}})</em></strong></div>
 <div title="Influx per square metre this hour: lowest and highest on the map."><small>THIS HOUR · LOWEST / HIGHEST</small><strong class="date">{{formatNumber(dustMetrics.min_ug_m2_day,3)}} / {{formatNumber(dustMetrics.max_ug_m2_day,3)}} µg/m²/day</strong></div>
 <div title="Meteoric smoke: a bound on its radiative forcing and the global temperature response."><small>CLIMATE · FORCING / TEMPERATURE (AT MOST)</small><strong class="date">±{{formatNumber(dustScenario.smoke.forcing_bound_w_m2,3)}} W/m² · ±{{formatNumber(dustScenario.smoke.equilibrium_temperature_bound_k,3)}} K</strong></div>
</div>
<section class="comparison outlet-report"><div class="section-label">WHAT COMES IN EVERY DAY · AND WHAT HAPPENS TO IT</div>
 <p>Interplanetary dust, mostly from comets and asteroid collisions, enters the atmosphere at 11–72 km/s. Most of its mass arrives in grains of 50–500 µm (the Grün distribution). Every grain is heated by the air it hits: small and slow grains radiate the heat away and survive unmelted; larger ones melt into cosmic spherules; the largest and fastest evaporate between about 80 and 110 km. The atoms they release (sodium, iron, magnesium...) form the metal layers seen by lidar and recondense into nanometre "meteoric smoke". The shares do not depend on the total, which is uncertain: estimates range from about 5 to 300 t/day.</p>
 <div class="comparison-switch"><button v-for="x in DUST_SCENARIOS" :key="x.key" :class="{selected:dustLayers.scenario===x.key}" @click="dustLayers.scenario=x.key">{{x.label}}: {{dustBlock.scenarios[x.key].influx_t_per_day}} t/day</button></div>
 <svg viewBox="0 0 100 8" preserveAspectRatio="none" style="display:block;width:100%;height:34px;margin:12px 0 6px" aria-label="Fate of the incoming mass"><rect v-for="x in dustShares" :key="x.key" :x="x.x" y="0" :width="x.w" height="8" :fill="x.color"><title>{{x.label}}: {{(x.share*100).toFixed(1)}} %</title></rect></svg>
 <p><span v-for="x in dustShares" :key="x.key" :style="{color:x.color,marginRight:'1.2em'}">■ {{x.label}} {{(x.share*100).toFixed(0)}} % ({{formatNumber(x.t_per_day*dustMetrics.scale,1)}} t/day)</span></p>
 <div class="outlet-charts">
  <div v-if="dustSizes" class="comparison-chart"><small>SIZES · share of the incoming mass (bars) and share reaching the ground (line, 0–100 %)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Size distribution and survival"><line v-for="t in dustSizes.ticks" :key="t.d" :x1="t.x" :x2="t.x" y1="0" y2="50" stroke="#1d3340" stroke-width=".25"/><rect v-for="(b,k) in dustSizes.bars" :key="k" :x="b.x" :y="b.y" :width="b.w" :height="b.h" :fill="b.fate.vaporised>.5?'#ff8a65':'#ffd166'" opacity=".8"><title>{{b.diameter_um.toFixed(1)}} µm: {{(b.mass_share*100).toFixed(1)}} % of the mass, {{(b.surviving*100).toFixed(0)}} % survives, peak {{b.peak_k.toFixed(0)}} K</title></rect><polyline :points="dustSizes.surviving" fill="none" stroke="#9fe870" stroke-width=".6"/></svg><small>diameter {{dustSizes.ticks.map(t=>t.d>=1000?(t.d/1000)+' mm':t.d+' µm').join(' · ')}} (logarithmic) · orange bars: mostly evaporated</small></div>
  <div v-if="dustProfile" class="comparison-chart"><small>WHERE IN THE ATMOSPHERE · {{dustProfile.h0.toFixed(0)}}–{{dustProfile.h1.toFixed(0)}} km, each profile scaled to its maximum</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Altitude profiles"><rect x="0" :y="dustProfile.metal.y0" width="100" :height="dustProfile.metal.y1-dustProfile.metal.y0" fill="#ffd166" opacity=".07"/><line x1="0" x2="100" :y1="dustProfile.nlc" :y2="dustProfile.nlc" stroke="#7fd1ff" stroke-width=".25" stroke-dasharray="1 1"/><line v-for="l in dustProfile.levels" :key="l.h" x1="0" x2="100" :y1="l.y" :y2="l.y" stroke="#1d3340" stroke-width=".2"/><polyline :points="dustProfile.energy" fill="none" stroke="#93a5af" stroke-width=".4"/><polyline :points="dustProfile.ablation" fill="none" stroke="#ff8a65" stroke-width=".6"/><polyline :points="dustProfile.water" fill="none" stroke="#4fc3f7" stroke-width=".6"/></svg><small><span style="color:#ff8a65">■ evaporated mass (peak {{dustProfile.peaks.ablation.toFixed(0)}} km)</span> · <span style="color:#4fc3f7">■ water released (peak {{dustProfile.peaks.water.toFixed(0)}} km)</span> · <span style="color:#93a5af">■ heat given to the air ({{dustProfile.peaks.energy.toFixed(0)}} km)</span> · shaded: metal layers 80–105 km · dotted: noctilucent clouds (83 km) · grid {{dustProfile.levels.map(l=>l.h).join(', ')}} km</small></div>
 </div>
</section>
<section class="comparison outlet-report"><div class="section-label">EXTRATERRESTRIAL WATER AND THE BUDGETS · {{dustLayers.scenario.toUpperCase()}} ({{dustScenario.influx_t_per_day}} t/day)</div>
 <p>The dust carries about {{(dustBlock.population.composition.water*100).toFixed(0)}} % water, bound in hydrated minerals of carbonaceous material and implanted in grain rims by the solar wind. It leaves the grains when they heat past {{dustBlock.entry.dehydration_start_k}}–{{dustBlock.entry.dehydration_end_k}} K, so {{(dustScenario.water.released_t_per_day/dustScenario.water.in_t_per_day*100).toFixed(1)}} % is released as vapour high in the atmosphere (median {{dustBlock.fate.median_water_altitude_km.toFixed(0)}} km), where sunlight splits it within weeks. Only about {{formatNumber(dustScenario.water.to_surface_kg_per_day,0)}} kg a day reach the ground inside cool grains. Ratios compare the dust with the quantity in the third column.</p>
 <table class="event-table dust-table"><thead><tr><th>Quantity</th><th>Cosmic dust</th><th>Compared with</th><th>Ratio</th></tr></thead><tbody>
  <template v-for="(r,k) in dustBudget" :key="k"><tr v-if="k===0||dustBudget[k-1].group!==r.group"><td colspan="4" style="text-align:left;color:#a7d497;padding-top:14px">{{r.group}}</td></tr><tr><td>{{r.label}}</td><td>{{r.value}}</td><td>{{r.against}}</td><td>{{ratioLabel(r.ratio)}}</td></tr></template>
 </tbody></table>
 <p>In short: the Earth gains about {{formatNumber(dustScenario.mass.gain_t_per_year,0)}} t of dust a year but loses about {{formatNumber(dustScenario.mass.hydrogen_loss_t_per_year+dustScenario.mass.helium_loss_t_per_year,0)}} t of hydrogen and helium to space. It also loses hundreds of times more water (as escaping hydrogen) than the dust brings. For the weather and climate the extraterrestrial water is negligible. It is <em>not</em> added to the water budgets of the rain, river and ocean models: it would change them by about {{ratioLabel(dustScenario.water.share_of_precipitation)}} of the rain. Its per-area fluxes are exported ({{dustBlock.deposition_export.water_kg_m2_s.toExponential(1)}} kg/m²/s) for any model that wants them.</p>
</section>
<section v-if="dustPlace" class="comparison outlet-report"><div class="section-label">WHERE AND WHEN IT ARRIVES · {{dustBlock.map.date_utc}}</div>
 <p>The Earth moves around the Sun at 30 km/s and sweeps up more dust on its leading side, toward the <strong>apex</strong> of its way, which stands overhead in the local morning. Further sporadic sources lie near the directions of the Sun (helion) and opposite it (antihelion) and toward the ecliptic poles (toroidal); ☄ and the other symbols on the map mark where each stands overhead now. Meteor showers are not included.<template v-if="dustBlock.map.land_share_of_influx!==null"> Land receives {{(dustBlock.map.land_share_of_influx*100).toFixed(0)}} % of the daily influx.</template> Click a cell to follow it through the day.</p>
 <div class="outlet-charts">
  <div class="comparison-chart"><small>OVER THE DAY AT {{Math.abs(dustPlace.lat).toFixed(1)}}° {{dustPlace.lat<0?'S':'N'}}, {{Math.abs(dustPlace.lon).toFixed(1)}}° {{dustPlace.lon<0?'W':'E'}} · against local solar time (0–24 h)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Influx over the day"><line v-for="h in [6,12,18]" :key="h" :x1="h/24*100" :x2="h/24*100" y1="0" y2="50" stroke="#1d3340" stroke-width=".25"/><polyline :points="dustPlace.line" fill="none" stroke="#ffd166" stroke-width=".6"/><circle v-for="(r,k) in dustPlace.dots" :key="k" :cx="r.x" :cy="r.y" r=".9" :fill="r.day?'#ffd166':'#6f8899'"/><line :x1="dustPlace.cursor" :x2="dustPlace.cursor" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{formatNumber(dustPlace.max,3)}} µg/m²/day · highest at {{dustPlace.peak.local.toFixed(1)}} h local time, lowest at {{dustPlace.low.local.toFixed(1)}} h ({{(dustPlace.peak.value/dustPlace.low.value).toFixed(1)}}×) · grey dots: night</small></div>
  <div v-if="dustLatitude" class="comparison-chart"><small>DAILY MEAN BY LATITUDE · 90° S – 90° N</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Daily mean by latitude"><line v-for="l in [-60,-30,0,30,60]" :key="l" :x1="(l+90)/180*100" :x2="(l+90)/180*100" y1="0" y2="50" stroke="#1d3340" stroke-width=".25"/><polyline :points="dustLatitude.line" fill="none" stroke="#ffd166" stroke-width=".6"/></svg><small>0–{{formatNumber(dustLatitude.max,3)}} µg/m²/day · the hemisphere tilted toward the apex gets more, and that changes with the seasons</small></div>
 </div>
</section>
<section class="comparison outlet-report"><div class="section-label">WHAT WOULD IT TAKE? · MORE DUST AND THE CLIMATE</div>
 <p>Meteoric smoke stays for years in the middle atmosphere before it settles. Its burden (evaporated mass × {{dustBlock.comparisons.smoke_residence_years}} years) sets an optical depth and at most ±{{dustBlock.comparisons.forcing_per_optical_depth_w_m2}} W/m² per unit optical depth of forcing; the sign is uncertain, because the particles absorb as well as scatter. Today's dust is far too little to matter. It would take about {{formatNumber(dustBlock.thresholds.multiplier_for_0_1_w_m2,0)}}× today's influx for 0.1 W/m² and {{formatNumber(dustBlock.thresholds.multiplier_for_1_w_m2,0)}}× for 1 W/m². Such increases are thought to have happened after the break-up of asteroids: the fragments of the L-chondrite parent body 466 million years ago may have contributed to an Ordovician cooling (Schmitz et al. 2019).</p>
 <table class="event-table"><thead><tr><th>× today</th><th>t/day</th><th>Smoke optical depth</th><th>Forcing (at most)</th><th>Temperature (at most)</th><th>Water, t/yr</th></tr></thead><tbody><tr v-for="w in dustBlock.what_if" :key="w.multiplier"><td>{{w.multiplier.toLocaleString('en-US')}}</td><td>{{formatSig(w.influx_t_per_day,3)}}</td><td>{{formatSig(w.smoke_optical_depth)}}</td><td>±{{formatSig(w.forcing_bound_w_m2)}} W/m²</td><td>±{{formatSig(w.temperature_bound_k)}} K</td><td>{{formatSig(w.water_t_per_year,3)}}</td></tr></tbody></table>
 <p v-if="dustBlock.solar_comparison">Against the Sun (from the solar-cycle run): the dust's {{dustBlock.solar_comparison.dust_kinetic_power_w_m2.toExponential(1)}} W/m² is {{dustBlock.solar_comparison.dust_vs_solar_cycle.toExponential(1)}} of the 11-year cycle's change in sunlight ({{formatSig(dustBlock.solar_comparison.tsi_cycle_change_global_mean_w_m2)}} W/m² averaged over the globe) and {{(dustBlock.solar_comparison.dust_vs_flares*100).toFixed(1)}} % of the average energy of solar flares, themselves negligible for the climate.</p>
</section>
</template>
<template v-if="view==='solarwind'&&windMetrics">
<div class="metrics extra-metrics">
 <div title="The solar wind arriving at the Earth this hour: speed, proton density and the north-south component of the interplanetary magnetic field (southward, negative, opens the magnetosphere)."><small>SOLAR WIND · SPEED / DENSITY / Bz</small><strong class="date">{{windMetrics.speed.toFixed(0)}} km/s · {{windMetrics.density.toFixed(0)}} /cm³ · {{windSigned(windMetrics.bz)}} nT</strong></div>
 <div title="Kp (0-9) from the Newell coupling function; NOAA storm scale G1-G5; Dst from the ring current (O'Brien & McPherron)."><small>GEOMAGNETIC STORM · Kp / Dst</small><strong class="date">Kp {{windMetrics.kp.toFixed(1)}}{{windMetrics.g?' · '+G_SCALE[windMetrics.g]:''}} · Dst {{windMetrics.dst.toFixed(0)}} nT</strong></div>
 <div title="Where the solar wind's pressure balances the Earth's field on the day side (Shue et al. 1998)."><small>MAGNETOPAUSE · DAY SIDE</small><strong class="date">{{windMetrics.standoff.toFixed(1)}} Earth radii <em>{{windMetrics.inside_geostationary?'(inside geostationary orbit: satellites there meet the solar wind)':'(geostationary orbit: 6.6)'}}</em></strong></div>
 <div title="The edge of the open polar cap (mean) and the equatorward edge of the auroral oval at midnight: there the aurora stands overhead."><small>POLAR CAP EDGE · AURORA OVERHEAD TO</small><strong class="date">{{windMetrics.cap_mlat.toFixed(0)}}° · {{windMetrics.oval_mlat.toFixed(0)}}° magnetic latitude <em>(open flux {{windMetrics.open_flux.toFixed(2)}} GWb)</em></strong></div>
 <div title="Particle energy of the aurora and the Joule heating of the ionospheric currents, each per hemisphere."><small>ENERGY INTO EACH POLAR ATMOSPHERE</small><strong class="date">{{windMetrics.hp.toFixed(0)}} GW aurora · {{windMetrics.joule.toFixed(0)}} GW Joule <em>(cross-cap potential {{windMetrics.potential.toFixed(0)}} kV)</em></strong></div>
 <div title="Global mean exospheric temperature (Jacchia) and the air density at 400 km against the quiet start: the drag on satellites in low orbit."><small>THERMOSPHERE · DENSITY AT 400 km</small><strong class="date">{{windMetrics.exo.toFixed(0)}} K · ×{{windMetrics.density_ratio.toFixed(2)}}</strong></div>
 <div title="Atoms and ions leaving the Earth for good this hour."><small>ESCAPING NOW · H / O / He</small><strong class="date">{{windMetrics.h_kg.toFixed(2)}} / {{sig(windMetrics.o_kg,2)}} / {{sig(windMetrics.he_kg,2)}} kg/s <em>(hydrogen: {{(windMetrics.jeans_share*100).toFixed(0)}} % thermal)</em></strong></div>
 <div title="Galactic cosmic rays at the ground (neutron-monitor-like) against the quiet start, over the pole and the magnetic equator; dose at 12 km."><small>COSMIC RAYS · POLE / EQUATOR · DOSE AT 12 km</small><strong class="date">{{windSigned(windMetrics.neutron_pole,1)}} / {{windSigned(windMetrics.neutron_equator,1)}} % · {{windMetrics.dose_pole.toFixed(1)}} / {{windMetrics.dose_equator.toFixed(1)}} µSv/h</strong></div>
 <div title="Solar protons above 10 MeV (pfu = particles per cm², s and sr) and the ozone of the polar upper stratosphere (35-50 km) against no event."><small>SOLAR PROTONS · POLAR OZONE 35–50 km</small><strong class="date">{{sig(windMetrics.sep,2)}} pfu · {{windMetrics.ozone_north.toFixed(1)}} % N / {{windMetrics.ozone_south.toFixed(1)}} % S</strong></div>
</div>
<section class="comparison outlet-report"><div class="section-label">WHY THE POLES? · OPEN FIELD LINES, NOT A WEAKER FIELD</div>
 <p>The Earth's field is in fact <em>strongest</em> at the magnetic poles: about {{windBlock.dipole.polar_field_ut.toFixed(0)}} µT at the surface against {{windBlock.dipole.equatorial_field_ut.toFixed(0)}} µT at the magnetic equator. What makes the polar caps special is the direction and the topology of the field lines. Near the equator they run horizontally and close in the other hemisphere: a charged particle from space has to cross them and is turned back unless its rigidity exceeds about {{windBlock.dipole.cutoff_equator_gv.toFixed(0)}} GV. Poleward of the auroral ovals the field lines stand nearly vertical and are <em>open</em>, connected to the solar wind. Along them cosmic rays and solar protons come straight in, solar-wind energy rains down as aurora, and ions of the upper atmosphere flow out to space. The dials show the current layer around each magnetic pole, with the polar-cap edge (orange) and the oval's equatorward edge (green).</p>
 <div class="outlet-charts">
  <div v-for="dl in windDials" :key="dl.hemisphere" class="comparison-chart"><small>{{dl.hemisphere>0?'NORTH':'SOUTH'}} · MAGNETIC LATITUDE 40–90° · NOON (SUN) AT THE TOP</small><svg viewBox="0 0 100 100" style="height:260px" aria-label="Polar dial"><circle cx="50" cy="50" r="46" fill="#09151d" stroke="#293e4b" stroke-width=".4"/><circle v-for="c in dl.cells" :key="c.i" :cx="c.x" :cy="c.y" :r="c.size/2" :fill="c.color"/><circle v-for="r in [9,18,27,36,45]" :key="r" cx="50" cy="50" :r="r" fill="none" stroke="#425561" stroke-width=".2" stroke-dasharray="1 1"/><polyline :points="dl.pole" fill="none" stroke="#ff9f5a" stroke-width=".7"/><polyline :points="dl.equator" fill="none" stroke="#58e07a" stroke-width=".7"/><text x="50" y="3" font-size="3" text-anchor="middle" fill="#ffd166">12 ☀</text><text x="50" y="99.5" font-size="3" text-anchor="middle" fill="#aebec9">00</text><text x="2" y="51" font-size="3" fill="#aebec9">18</text><text x="94" y="51" font-size="3" fill="#aebec9">06</text></svg><small>rings every 10° of magnetic latitude (90° at the centre) · magnetic local time around · {{WIND_UNITS[windKey]||'regions'}}</small></div>
 </div>
 <div v-if="windProfile" class="outlet-charts">
  <div class="comparison-chart"><small>SHIELDING · VERTICAL CUTOFF RIGIDITY 0–{{windProfile.cutoff.hi.toFixed(0)}} GV</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Cutoff by latitude"><polyline v-for="(l,k) in windProfile.cutoff.lines" :key="k" :points="l.points" fill="none" :stroke="l.color" stroke-width=".7"/><line x1="0" x2="100" :y1="windProfile.cutoff.y(1)" :y2="windProfile.cutoff.y(1)" stroke="#425561" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>magnetic latitude 0° (left) → 90° · dotted: 1 GV, below which almost every cosmic ray gets in</small></div>
  <div class="comparison-chart"><small>COSMIC-RAY DOSE AT 12 km · 0–{{windProfile.dose.hi.toFixed(1)}} µSv/h</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Dose by latitude"><polyline v-for="(l,k) in windProfile.dose.lines" :key="k" :points="l.points" fill="none" :stroke="l.color" stroke-width=".7"/></svg><small>violet: solar minimum · blue: solar maximum · pink: at the storm's proton peak (its soft protons add nothing at 12 km)</small></div>
 </div>
</section>
<section class="comparison outlet-report"><div class="section-label">THE STORM HOUR BY HOUR · {{windBlock.storm.start_utc.slice(0,10)}} + {{windBlock.storm.hours}} h</div>
 <p>A flare at hour {{windBlock.request.storm.sep?.onset_h??'—'}} sends solar protons (peak {{sig(windBlock.request.storm.sep?.peak_pfu??0,2)}} pfu) into the polar caps within hours; they reach down to about {{windSepReach.toFixed(0)}}° magnetic latitude, make NOx and thin the polar ozone above 35 km by up to {{Math.abs(windBlock.budgets.storm.min_ozone_upper_pct).toFixed(0)}} % ({{Math.abs(windBlock.budgets.storm.min_ozone_column_pct).toFixed(1)}} % of the column). The CME arrives at hour {{windBlock.request.storm.cme?.arrival_h??'—'}}: its shock compresses the magnetosphere, its magnetic cloud turns the field southward and opens it. Dst falls to {{windBlock.budgets.storm.min_dst_nt.toFixed(0)}} nT ({{G_SCALE[windBlock.budgets.storm.g_scale]}}); the magnetopause is pushed to {{windBlock.budgets.storm.min_standoff_re.toFixed(1)}} Earth radii{{windInGeo?`, inside geostationary orbit for ${windInGeo} hours`:''}}; the polar cap widens to {{windBlock.budgets.storm.min_cap_boundary_mlat_deg.toFixed(0)}}° and the aurora stands overhead down to {{windBlock.budgets.storm.min_oval_equatorward_mlat_deg.toFixed(0)}}° magnetic latitude. The thermosphere heats from {{windBlock.budgets.storm.quiet_exospheric_k.toFixed(0)}} to {{windBlock.budgets.storm.max_exospheric_k.toFixed(0)}} K and the air at 400 km becomes {{windBlock.budgets.storm.max_density_400km_ratio.toFixed(1)}} times denser. The CME also sweeps cosmic rays away: {{windBlock.budgets.storm.min_neutron_pole_change_pct.toFixed(0)}} % at the poles (a Forbush decrease).</p>
 <p>Over the {{windBlock.budgets.storm.days.toFixed(0)}} days the Earth loses {{sig(windBlock.budgets.storm.oxygen_lost_kg/1000,2)}} t of oxygen (quiet: {{sig(windBlock.budgets.storm.oxygen_quiet_kg/1000,2)}} t; {{windBlock.budgets.storm.oxygen_vs_mean_days.toFixed(0)}} extra days of the average loss), but hardly more hydrogen: {{sig(windBlock.budgets.storm.hydrogen_lost_kg/1000,3)}} t against {{sig(windBlock.budgets.storm.hydrogen_quiet_kg/1000,3)}} t. The hydrogen is limited by what diffuses up from below: the storm empties the reservoir above the exobase faster, then it refills.</p>
 <div class="outlet-charts">
  <div v-for="c in windCharts" :key="c.title" class="comparison-chart"><small>{{c.title}}</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" :aria-label="c.title"><polyline v-for="(l,k) in c.lines" :key="k" :points="l.points" fill="none" :stroke="l.color" :stroke-width="k===c.lines.length-1&&c.lines.length>1&&l.color==='#425561'?.3:.6"/><line :x1="windCursor" :x2="windCursor" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{windSpan(c)}} · 0–{{windBlock.storm.hours}} h{{c.note?' · '+c.note:''}}</small></div>
 </div>
</section>
<section class="comparison outlet-report"><div class="section-label">WHAT THE EARTH LOSES TO SPACE · MEAN {{windCycle?`${windCycle.first_year}–${windCycle.last_year}`:'OF THE QUIET BACKGROUND'}}</div>
 <p>The Earth loses about {{windBlock.budgets.mean_escape.total_kg_s.toFixed(1)}} kg of gas a second ({{sig(windBlock.budgets.mean_escape.total_t_yr,2)}} t a year). By mass it is almost all hydrogen, and most of that leaves everywhere above the exobase, thermally or by charge exchange. Heavy ions are different: oxygen cannot escape thermally at all and leaves only along the open and stretched field lines of the polar caps, the cusp and the night-side oval. Through the poles go {{(windBlock.budgets.mean_escape.polar_share*100).toFixed(0)}} % of the mass, and all of the oxygen and helium.</p>
 <table class="event-table wind-table wind-route"><thead><tr><th>Escaping</th><th>Particles/s</th><th>kg/s</th><th>t/yr</th><th>Through the poles</th><th>Route</th></tr></thead><tbody><tr v-for="r in windEscape" :key="r.species"><td>{{r.species}}</td><td>{{sig(r.per_s,2)}}</td><td>{{sig(r.kg_s,2)}}</td><td>{{sig(r.t_yr,2)}}</td><td>{{(r.polar_share*100).toFixed(0)}} %</td><td>{{r.route}}</td></tr></tbody></table>
 <p>In proportion: at this rate the atmosphere would lose {{sig(windBlock.budgets.reservoirs.atmosphere_share_per_million_years*100,2)}} % of its mass in a million years. The oxygen lost over the Earth's age would be {{(windBlock.budgets.reservoirs.oxygen_share_over_earth_history*100).toFixed(1)}} % of today's O₂ (which photosynthesis renews far faster). The hydrogen lost is water split high up: {{windBlock.budgets.reservoirs.water_equivalent_kg_s.toFixed(0)}} kg of water a second, {{(windBlock.budgets.reservoirs.ocean_share_over_earth_history*100).toFixed(2)}} % of the oceans over 4.5 billion years. The cold tropopause keeps the water below: only about 4 ppm of water vapour reaches the stratosphere. Methane now adds as much hydrogen as that water:</p>
 <table class="event-table wind-table"><thead><tr><th>Methane</th><th>ppm</th><th>Hydrogen escape</th><th>Against today</th></tr></thead><tbody><tr v-for="r in windBlock.methane_scenarios" :key="r.name"><td>{{r.name}}</td><td>{{r.methane_ppmv}}</td><td>{{r.hydrogen_kg_s.toFixed(2)}} kg/s</td><td>{{r.relative_to_today_pct.toFixed(0)}} %</td></tr></tbody></table>
</section>
<section v-if="windCycle" class="comparison outlet-report"><div class="section-label">OVER THE SOLAR CYCLE · {{windCycle.first_year}}–{{windCycle.last_year}} (ANNUAL MEANS)</div>
 <p>Driven by the monthly sunspot number and F10.7 of the solar-cycle example (record, then forecast). At solar maximum the solar wind's field is stronger and storms are more frequent: the polar caps are wider, more oxygen escapes and more solar protons reach the polar stratosphere. The hotter thermosphere lets more hydrogen escape thermally, but the total stays set by the supply. Cosmic rays move the other way: the stronger heliospheric field keeps them out, most noticeably over the poles.</p>
 <div class="outlet-charts">
  <div v-for="c in windCycleCharts" :key="c.title" class="comparison-chart"><small>{{c.title}}</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" :aria-label="c.title"><polyline v-for="(l,k) in c.lines" :key="k" :points="l.points" fill="none" :stroke="l.color" stroke-width=".6"/></svg><small>{{windSpan(c)}} · {{windCycle.first_year}}–{{windCycle.last_year}}{{c.note?' · '+c.note:''}}</small></div>
 </div>
</section>
<section class="comparison outlet-report"><div class="section-label">COSMIC RADIATION, SOLAR PROTONS AND US</div>
 <p>At cruising altitude over the poles a passenger receives about {{windBlock.budgets.aviation.dose_pole_min_usv_h.toFixed(1)}} µSv an hour near solar minimum ({{windBlock.budgets.aviation.dose_pole_max_usv_h.toFixed(1)}} at maximum), against {{windBlock.budgets.aviation.dose_equator_min_usv_h.toFixed(1)}} near the magnetic equator. A ten-hour polar flight thus adds {{windBlock.budgets.aviation.polar_flight_10h_usv.toFixed(0)}} µSv, an equatorial one {{windBlock.budgets.aviation.equatorial_flight_10h_usv.toFixed(0)}} µSv; the natural background is about {{windBlock.budgets.aviation.natural_background_usv_yr.toLocaleString('en-US')}} µSv a year. Aircrew on polar routes reach about {{sig(windBlock.budgets.aviation.aircrew_usv_yr_polar,2)}} µSv a year. Solar protons rarely matter at flight altitude: most are too soft. Only hard events, ground-level enhancements, raise the dose over the poles many times. All of them ionise the polar middle atmosphere and make NOx, which destroys ozone there for weeks.</p>
 <table class="event-table wind-table"><thead><tr><th>Solar proton event</th><th>&gt;10 MeV</th><th>&gt;1 GV</th><th>Reaches down to</th><th>NOx per hemisphere</th><th>Ozone 35–50 km</th><th>Dose at 12 km, pole</th><th>At the ground</th></tr></thead><tbody><tr v-for="r in windBlock.sep_scenarios" :key="r.name"><td>{{r.name}}</td><td>{{sig(r.peak_pfu,2)}} pfu</td><td>{{sig(r.flux_above_1gv_pfu,2)}} pfu</td><td>{{r.reach_mlat_deg.toFixed(0)}}° magnetic</td><td>{{sig(r.nox_per_hemisphere_mol/1e9,2)}} Gmol</td><td>{{Math.abs(r.ozone_upper_pct)<1?r.ozone_upper_pct.toFixed(1):r.ozone_upper_pct.toFixed(0)}} %</td><td>×{{r.dose_pole_ratio.toFixed(1)}}</td><td>{{r.neutron_pole_increase_pct>=1?'+'+r.neutron_pole_increase_pct.toFixed(0)+' %':'—'}}</td></tr></tbody></table>
 <p v-if="windBlock.budgets.chemistry.sep_nox_mol_yr">Averaged over the cycle, solar protons make {{sig(windBlock.budgets.chemistry.sep_nox_mol_yr/1e9,2)}} Gmol of NOx a year and descending auroral NOx adds {{sig(windBlock.budgets.chemistry.epp_nox_mol_yr/1e9,2)}} Gmol, against about {{sig(windBlock.budgets.chemistry.n2o_noy_source_mol_yr/1e9,2)}} Gmol from N₂O oxidation: important for polar ozone, minor globally.</p>
 <p v-if="windBlock.budgets.clouds.global_ionisation_cycle_change_pct!==null">Cosmic rays and clouds: the ionisation of the lower atmosphere changes by about {{windBlock.budgets.clouds.global_ionisation_cycle_change_pct.toFixed(0)}} % over the cycle (annual means, global). If aerosol nucleation and clouds followed it as weakly as the CLOUD chamber experiments and aerosol models suggest, the forcing would be about ±{{sig(windBlock.budgets.clouds.forcing_constrained_w_m2,1)}} W/m², {{sig(windBlock.budgets.clouds.forcing_constrained_w_m2/windBlock.budgets.clouds.tsi_cycle_forcing_w_m2,1)}} of the forcing from the cycle's change in sunlight (about {{windBlock.budgets.clouds.tsi_cycle_forcing_w_m2.toFixed(2)}} W/m²). Only under the disputed strong-coupling hypothesis would it reach about {{sig(windBlock.budgets.clouds.forcing_hypothesis_w_m2,1)}} W/m². Both numbers are illustrative sensitivities, not model results.</p>
</section>
<section class="comparison outlet-report"><div class="section-label">STORMS · FROM QUIET TO CARRINGTON</div>
 <p>Steady solar-wind driving for a few hours. The Kp index saturates at 9, so the index-based heating and oxygen escape level off for the strongest storms: for those they are lower bounds. Aurora: overhead at the oval's equatorward edge, low on the horizon about {{windBlock.comparisons.aurora_visibility_deg}}° further towards the equator.</p>
 <table class="event-table wind-table"><thead><tr><th>Storm</th><th>Wind · Bz</th><th>Kp · scale</th><th>Dst</th><th>Magnetopause</th><th>Aurora overhead · seen to</th><th>Aurora · Joule</th><th>Thermosphere · density 400 km</th><th>O escape</th></tr></thead><tbody><tr v-for="r in windBlock.storm_scenarios" :key="r.name"><td>{{r.name}}</td><td>{{r.speed_km_s}} km/s · {{r.bz_nt}} nT</td><td>{{r.kp.toFixed(1)}} · {{r.g_scale?'G'+r.g_scale:'—'}}</td><td>{{r.dst_nt.toFixed(0)}} nT</td><td>{{r.standoff_re.toFixed(1)}} R<sub>E</sub></td><td>{{r.aurora_overhead_mlat_deg.toFixed(0)}}° · {{r.aurora_visible_mlat_deg.toFixed(0)}}°</td><td>{{r.hemispheric_power_gw.toFixed(0)}} · {{r.joule_heating_gw.toFixed(0)}} GW</td><td>{{r.exospheric_k.toFixed(0)}} K · ×{{r.density_400km_ratio.toFixed(1)}}</td><td>{{sig(r.oxygen_escape_kg_s,2)}} kg/s</td></tr></tbody></table>
</section>
<section class="comparison outlet-report"><div class="section-label">WHERE YOU LIVE · AT THE PEAK OF THIS STORM</div>
 <p>The magnetic latitude decides, not the geographic one: the dipole is tilted towards North America, so New York lies further "north" magnetically than Rome. At the storm's peak the oval reaches {{windBlock.budgets.storm.min_oval_equatorward_mlat_deg.toFixed(0)}}° magnetic latitude. Under the auroral electrojet the ground's magnetic field changes fastest, which drives currents in power lines and pipelines. Solar-proton ionisation at the peak of the event (column, per cm² and s) shows where the polar cap's window is open.</p>
 <table class="event-table wind-table"><thead><tr><th>Place</th><th>Magnetic latitude</th><th>Cutoff</th><th>Dose at 12 km (min · max)</th><th>Aurora</th><th>Electrojet</th><th>Solar protons</th></tr></thead><tbody><tr v-for="c in windCity" :key="c.name"><td>{{c.name}}</td><td>{{c.magnetic_latitude_deg.toFixed(0)}}°</td><td>{{c.cutoff_gv.toFixed(1)}} GV</td><td>{{c.dose_min_usv_h.toFixed(1)}} · {{c.dose_max_usv_h.toFixed(1)}} µSv/h</td><td>{{c.aurora_at_storm_peak}}</td><td>{{c.electrojet_at_storm_peak}}</td><td>{{c.sep_ion_pairs_peak_cm2_s>=1e6?c.sep_ion_pairs_peak_cm2_s.toExponential(1):'—'}}</td></tr></tbody></table>
</section>
<section class="comparison outlet-report"><div class="section-label">IF THE MAGNETIC FIELD WEAKENS</div>
 <p>The dipole has weakened by about 9 % since 1840, and 41,000 years ago, in the Laschamp excursion, it fell to about a tenth. A weaker field shrinks the magnetosphere and widens the polar caps. The shielding falls everywhere, because the cutoff scales with the moment. More cosmic rays reach the atmosphere, and the cosmogenic isotopes in ice and sediments (¹⁰Be, ¹⁴C) record it. The atmosphere is not stripped away, however. The hydrogen escape is limited by the supply from below; the oxygen escape grows with the area of the caps, to about twice today's at a tenth of the field.</p>
 <table class="event-table wind-table"><thead><tr><th>Dipole</th><th>× today</th><th>Magnetopause</th><th>Polar cap edge · oval</th><th>Cutoff equator · 50°</th><th>Polar window (&lt; 1 GV)</th><th>Ionisation (Pfotzer)</th><th>¹⁰Be/¹⁴C production</th><th>Dose 12 km, equator</th><th>O escape</th></tr></thead><tbody><tr v-for="r in windBlock.dipole_scenarios" :key="r.name" :title="r.note"><td>{{r.name}}</td><td>{{r.moment_ratio}}</td><td>{{r.standoff_re.toFixed(1)}} R<sub>E</sub></td><td>{{r.cap_boundary_mlat_deg.toFixed(0)}}° · {{r.oval_equatorward_midnight_mlat_deg.toFixed(0)}}°</td><td>{{r.cutoff_equator_gv.toFixed(1)}} · {{r.cutoff_50deg_gv.toFixed(1)}} GV</td><td>{{(r.polar_window_share*100).toFixed(0)}} % of the Earth</td><td>{{r.global_ionisation_pct.toFixed(0)}} %</td><td>{{r.production_pct.toFixed(0)}} %</td><td>{{r.dose_equator_usv_h.toFixed(1)}} µSv/h</td><td>{{r.oxygen_escape_pct.toFixed(0)}} %</td></tr></tbody></table>
</section>
<section class="comparison outlet-report"><div class="section-label">HOW IT AFFECTS US GLOBALLY</div>
 <ul class="wind-summary">
  <li><strong>Air and water are not at risk.</strong> The Earth loses about {{windBlock.budgets.mean_escape.total_kg_s.toFixed(1)}} kg/s, mostly hydrogen that the polar caps hardly matter for. Oxygen, the one gas that leaves only through the poles, goes at about {{sig(windBlock.budgets.mean_escape.oxygen_kg_s,2)}} kg/s: {{(windBlock.budgets.reservoirs.oxygen_share_over_earth_history*100).toFixed(1)}} % of today's O₂ over the Earth's age.</li>
  <li><strong>Weather and climate: negligible.</strong> The energy entering through the polar caps ({{sig(windBlock.budgets.energy.magnetospheric_input_w/1e9,2)}} GW on average) is {{sig(windBlock.budgets.energy.share_of_sunlight,1)}} of the absorbed sunlight; cosmic rays bring {{windBlock.budgets.energy.gcr_share_of_sunlight!==null?sig(windBlock.budgets.energy.gcr_share_of_sunlight,1):'even less'}}. Effects on clouds via ionisation are at most a fraction of the solar cycle's 0.2 W/m². Polar ozone loss from particles can shift the winter polar vortex regionally; the global mean stays unaffected.</li>
  <li><strong>The upper atmosphere: strongly.</strong> The thermosphere heats by hundreds of kelvin in storms and expands: satellites in low orbit meet several times more drag. Solar protons remove tens of percent of the polar ozone above 35 km for weeks.</li>
  <li><strong>Technology and people.</strong> Storms push the magnetopause inside geostationary orbit, bring the auroral electrojet over Europe and North America (currents in power grids and pipelines), black out HF radio over the poles and degrade satellite navigation. Polar flights receive 2–3 times the cosmic radiation of equatorial ones, and far more in rare hard proton events.</li>
  <li><strong>Over millennia.</strong> The magnetic field's strength, not the escape, shapes the long-term signal: a weaker dipole lets in more cosmic rays everywhere (recorded in ¹⁰Be and ¹⁴C), but does not strip the atmosphere.</li>
 </ul>
</section>
</template>
<template v-if="coastSummary"><div class="metrics extra-metrics"><div title="Regional rise at this coast (the global mean with the fingerprints of the ice sources) and the global mean, counted from today."><small>SEA LEVEL · HERE / GLOBAL · {{coastSummary.year}} · {{coastSummary.warming_k.toFixed(1)}} K WARMER</small><strong class="date">+{{coastSummary.regional_rise_m.toFixed(2)}} m / +{{coastSummary.global_rise_m.toFixed(2)}} m</strong></div><div title="Land permanently under the daily high water (compared with today) plus land eroded from sandy shores."><small>LAND LOST · PEOPLE WHO LIVED ON IT</small><strong class="date">{{coastSummary.lost_km2.toFixed(0)}} km² · {{fmtPeople(coastSummary.people_on_lost_land)}}</strong></div><div title="People whose homes the 100-year storm floods: today and in this year."><small>PEOPLE UNDER THE 100-YEAR STORM · TODAY → NOW</small><strong class="date">{{fmtPeople(coastSummary.people_storm_today)}} → {{fmtPeople(coastSummary.people_storm_flooded)}}</strong></div><div title="Land below the daily high water that only dikes and walls keep dry: its future depends on the defences."><small>BELOW HIGH WATER, BEHIND DEFENCES</small><strong class="date">{{coastSummary.protected_low_km2.toFixed(0)}} km² · {{fmtPeople(coastSummary.people_protected_low)}} people</strong></div><div title="Return period of the flood that comes once a century today, per region (lowest to highest)."><small>TODAY'S 100-YEAR FLOOD NOW COMES</small><strong class="date">{{fmtYears(Math.min(...coastSummary.regions.map(r=>r.design_flood_return_years)))}} – {{fmtYears(Math.max(...coastSummary.regions.map(r=>r.design_flood_return_years)))}}</strong></div><div title="Water piled up by the design storm's onshore wind: the most anywhere and the mean along the coast."><small>WIND SETUP · MAX / COAST MEAN</small><strong class="date">{{coastSummary.max_setup_m.toFixed(2)}} / {{coastSummary.mean_setup_m.toFixed(2)}} m</strong></div></div>
<section class="comparison outlet-report"><div class="section-label">WHAT RAISES THE SEA · {{coastSummary.scenario}} · {{coastSummary.year}}</div><p>The rise still to come since {{data.coast.block.reference_year}}, by source (global mean). Greenland comes from the polar-melt run; here, far from Greenland, a fingerprint weights each ice source (Greenland ×{{data.coast.block.sea_level_source.fingerprints.greenland}}, Antarctica ×{{data.coast.block.sea_level_source.fingerprints.antarctic}}, glaciers ×{{data.coast.block.sea_level_source.fingerprints.glaciers}}), and sinking land adds its own rise on top.</p><svg class="component-bar" viewBox="0 0 100 8" preserveAspectRatio="none" aria-label="Sea-level rise by source"><rect v-for="c in coastComponentBar" :key="c.key" :x="c.x" y="0" :width="c.w" height="8" :fill="c.color"><title>{{c.label}}: {{c.value.toFixed(3)}} m</title></rect></svg><p><span v-for="c in coastComponentBar" :key="c.key" :style="{color:c.color,marginRight:'1.2em'}">■ {{c.label}} {{(c.value*100).toFixed(0)}} cm</span></p></section>
<section class="comparison outlet-report"><div class="section-label">WHERE DOES IT HIT HARDEST? · {{coastSummary.year}}</div><p>The regions of the coast ranked by <select v-model="coastLayers.rank" aria-label="Ranking metric"><option v-for="(m,k) in RANK_METRICS" :key="k" :value="k">{{m.label}}</option></select> under “{{coastSummary.scenario}}”. Click a row to fly there on the map.</p><table class="event-table"><thead><tr><th>#</th><th>Region</th><th title="Share of the region's land lost to the sea">Land lost</th><th title="People who lived on the land now lost">People displaced</th><th title="People whose homes the 100-year storm floods (today → now)">Under the 100-yr storm</th><th title="Land below high water kept dry only by defences">Behind defences</th><th title="How often today's 100-year flood comes now">100-yr flood now</th><th title="Mean relative rise at the region's coast, including land subsidence">Relative rise</th></tr></thead><tbody><tr v-for="r in coastRank" :key="r.name" :class="{selected:coastFocus===r.index}" @click="focusRegion(r.index)"><td>{{r.rank}}</td><td>{{r.name}}</td><td><strong v-if="r.lost_pct>=10">{{r.lost_pct.toFixed(1)}} %</strong><template v-else>{{r.lost_pct.toFixed(1)}} %</template></td><td>{{fmtPeople(r.people_on_lost_land)}}</td><td>{{fmtPeople(r.people_storm_today)}} → {{fmtPeople(r.people_storm_flooded)}}</td><td>{{r.protected_low_km2.toFixed(0)}} km² · {{fmtPeople(r.people_protected_low)}}</td><td>{{fmtYears(r.design_flood_return_years)}}</td><td>+{{r.relative_rise_m.toFixed(2)}} m</td></tr></tbody></table></section>
<section class="comparison outlet-report"><div class="section-label">WHEN? · EVENTS PER REGION AND SCENARIO</div><p>The first year each threshold is passed (— = not by {{data.coast.block.output_years.at(-1)}}). Defences are overtopped when the 100-year storm reaches land that lies below high water behind them.</p><table class="event-table"><thead><tr><th>Region</th><th>Scenario</th><th v-for="e in COAST_EVENTS" :key="e" :title="COAST_EVENT_LABELS[e]">{{COAST_EVENT_LABELS[e]}}</th></tr></thead><tbody><template v-for="r in coastEvents" :key="r.region"><tr v-for="(x,k) in r.scenarios" :key="r.region+x.name" :class="{selected:x.name===coastScenario}"><td>{{k===0?r.region:''}}</td><td :style="{color:scenarioTint(k)}">{{x.name}}</td><td v-for="e in COAST_EVENTS" :key="e">{{x.events[e]??'—'}}</td></tr></template></tbody></table></section>
<section class="comparison outlet-report"><div class="section-label">OVER TIME · {{data.coast.block.output_years[0]}}–{{data.coast.block.output_years.at(-1)}}</div><p><span v-for="(x,k) in data.coast.block.scenarios" :key="x.name" :style="{color:scenarioTint(k),marginRight:'1em'}">■ {{x.name}}</span> · bold: the scenario in focus; dashed in the sea-level chart: its global mean.</p><div class="outlet-charts"><div v-if="coastChartSea" class="comparison-chart"><small>SEA LEVEL HERE · m above today</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Regional sea-level rise per scenario"><line x1="0" x2="100" :y1="coastChartSea.zero" :y2="coastChartSea.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in coastChartSea.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/><line :x1="coastChartSea.x" :x2="coastChartSea.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>{{coastChartSea.lo.toFixed(1)}}–{{coastChartSea.hi.toFixed(1)}} m</small></div><div v-if="coastChartLost" class="comparison-chart"><small>LAND LOST · km²</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Land lost per scenario"><line x1="0" x2="100" :y1="coastChartLost.zero" :y2="coastChartLost.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in coastChartLost.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/><line :x1="coastChartLost.x" :x2="coastChartLost.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{coastChartLost.hi.toFixed(0)}} km²</small></div><div v-if="coastChartPeople" class="comparison-chart"><small>PEOPLE UNDER THE 100-YEAR STORM · millions</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="People flooded by the design storm per scenario"><line x1="0" x2="100" :y1="coastChartPeople.zero" :y2="coastChartPeople.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in coastChartPeople.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/><line :x1="coastChartPeople.x" :x2="coastChartPeople.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{coastChartPeople.hi.toFixed(2)}} M</small></div><div v-if="coastChartRegions" class="comparison-chart"><small>LAND LOST PER REGION · % (scenario in focus)</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Land lost per region"><line x1="0" x2="100" :y1="coastChartRegions.zero" :y2="coastChartRegions.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in coastChartRegions.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/><line :x1="coastChartRegions.x" :x2="coastChartRegions.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{coastChartRegions.hi.toFixed(0)}} %</small></div><div v-if="coastChartRivers" class="comparison-chart"><small>RIVERS · storm push-back (solid) and salt intrusion (dashed) · km</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="River push-back and salt intrusion"><line x1="0" x2="100" :y1="coastChartRivers.zero" :y2="coastChartRivers.zero" stroke="#4a5a66" stroke-width=".3"/><polyline v-for="l in coastChartRivers.lines" :key="l.name" :points="l.points" fill="none" :stroke="l.color" :stroke-width="l.width" :stroke-dasharray="l.dash"/><line :x1="coastChartRivers.x" :x2="coastChartRivers.x" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{coastChartRivers.hi.toFixed(0)}} km</small></div></div><p><span v-for="(name,k) in data.coast.block.region_names" :key="name" :style="{color:['#ff6b6b','#ffd166','#7fd1ff','#9fe870','#b388ff'][k%5],marginRight:'1em'}">■ {{name}}</span></p></section>
<section class="comparison outlet-report"><div class="section-label">RIVERS · THE SEA PUSHES BACK · {{coastSummary.year}}</div><p>How far up each channel the daily high water (tidal reach) and the 100-year storm (push-back) stand above the river's own level, how far salt water intrudes, and the river banks the storm floods — today → now.</p><table class="event-table"><thead><tr><th>River</th><th>Tidal reach</th><th>Storm push-back</th><th>Salt intrusion</th><th>Banks flooded</th></tr></thead><tbody><tr v-for="r in coastSummary.rivers" :key="r.name"><td>{{r.name}}</td><td>{{r.tidal_today_km.toFixed(0)}} → {{r.tidal_reach_km.toFixed(0)}} km</td><td>{{r.surge_today_km.toFixed(0)}} → {{r.surge_reach_km.toFixed(0)}} km</td><td>{{r.salt_today_km.toFixed(1)}} → {{r.salt_intrusion_km.toFixed(1)}} km</td><td>{{r.flooded_bank_km2.toFixed(0)}} km²</td></tr></tbody></table><p v-if="coastSummary.islands.length">Islands (land lost): <span v-for="isl in coastSummary.islands" :key="isl.id" style="margin-right:1em">{{isl.region>=0?data.coast.block.region_names[isl.region]:'island'}} #{{isl.id}} ({{isl.land_km2.toFixed(0)}} km², up to {{isl.highest_m.toFixed(1)}} m): <strong v-if="isl.lost_fraction>=.5">{{(isl.lost_fraction*100).toFixed(0)}} %</strong><template v-else>{{(isl.lost_fraction*100).toFixed(0)}} %</template></span></p></section></template><section v-if="hydrograph.length" class="comparison outlet-report"><div class="section-label">HYDROGRAPH · {{chosen&&data.hydrology?.network.channel[chosen.i]?`SELECTED REACH (cell ${hydrographCell})`:`PEAK REACH (cell ${hydrographCell})`}}</div><p>Discharge at the reach over the run with the rainfall rate on the same cell. Click a river cell to move the gauge.</p><div class="outlet-charts"><div class="comparison-chart"><small>DISCHARGE · m³/s</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Discharge hydrograph"><polyline :points="hydrographPath('discharge_m3_s',hydrographMax)" fill="none" stroke="#75bbfa" stroke-width=".7"/><line :x1="hydrographTimePosition" :x2="hydrographTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{hydrographMax.toFixed(1)}} m³/s · 0–{{(visibleFrames.at(-1).forecast.elapsed_seconds/3600).toFixed(0)}} h</small></div><div class="comparison-chart"><small>RAINFALL · mm/h</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Rainfall hyetograph"><polyline :points="hydrographPath('rain_mm_h',hydrographRainMax)" fill="none" stroke="#9fd0ff" stroke-width=".7"/><line :x1="hydrographTimePosition" :x2="hydrographTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{hydrographRainMax.toFixed(1)}} mm/h</small></div></div><p v-if="visibleLandslides.length">Landslides so far: <template v-for="(e,i) in visibleLandslides.slice(-6)" :key="i">{{i?' · ':''}}cell {{e.cell}} → {{e.deposited_in}} ({{(e.volume_m3/1000).toFixed(1)}} ×10³ m³, {{(e.runout_length_m/1000).toFixed(1)}} km)</template>{{visibleLandslides.length>6?` · … ${visibleLandslides.length} in total`:''}}</p></section><div v-if="stormMetrics" class="metrics extra-metrics"><div><small>TROPICAL CYCLONES</small><strong>{{stormMetrics.storm_count}} <em>({{stormMetrics.storms_over_land}} over land)</em></strong></div><div><small>STRONGEST</small><strong class="date">{{stormMetrics.strongest_wind_m_s.toFixed(1)}} m/s · {{categoryInfo(stormMetrics.strongest_category).label}}<template v-if="stormMetrics.lowest_pressure_hpa"> · {{stormMetrics.lowest_pressure_hpa.toFixed(0)}} hPa</template></strong></div><div><small>LANDFALLS / MERGERS / GENESES SO FAR</small><strong class="date">{{stormMetrics.landfalls_so_far}} / {{stormMetrics.mergers_so_far}} / {{stormMetrics.geneses_so_far}}</strong></div><div><small>MAX SURGE · MAX STORM WIND</small><strong class="date">{{stormMetrics.max_surge_m.toFixed(1)}} m · {{stormMetrics.max_storm_wind_m_s.toFixed(0)}} m/s</strong></div><div><small>POTENTIAL INTENSITY · GENESIS INDEX</small><strong class="date">{{stormMetrics.max_potential_intensity_m_s.toFixed(0)}} m/s · {{stormMetrics.max_genesis_index.toFixed(1)}}</strong></div><div v-if="severeMetrics"><small>TORNADO PARAMETER · POSSIBLE CELLS · CAP BROKEN</small><strong class="date">{{severeMetrics.max_stp.toFixed(2)}} · {{severeMetrics.tornado_possible_cells}} · {{severeMetrics.cap_broken_cells}} / {{severeMetrics.capped_cells}}</strong></div></div><div v-else-if="severeMetrics" class="metrics extra-metrics"><div><small>MAX TORNADO PARAMETER</small><strong>{{severeMetrics.max_stp.toFixed(2)}}</strong></div><div><small>TORNADO POSSIBLE CELLS</small><strong>{{severeMetrics.tornado_possible_cells}}</strong></div><div><small>CAP BROKEN / CAPPED CELLS</small><strong class="date">{{severeMetrics.cap_broken_cells}} / {{severeMetrics.capped_cells}}</strong></div><div><small>MAX HELICITY · LOW-LEVEL SHEAR</small><strong class="date">{{severeMetrics.max_helicity.toFixed(0)}} m²/s² · {{severeMetrics.max_low_level_shear.toFixed(1)}} m/s</strong></div><div><small>MAX HEATED CAPE</small><strong>{{severeMetrics.max_cape.toFixed(0)}} <em>J/kg</em></strong></div></div><div v-if="precipitationMetrics" class="metrics extra-metrics"><div><small>PRECIPITATING CELLS</small><strong>{{precipitationMetrics.precipitating_pct.toFixed(0)}} <em>%</em></strong></div><div><small>RATE MEAN (WET) / MAX</small><strong class="date">{{precipitationMetrics.mean_rate_mm_h.toFixed(1)}} / {{precipitationMetrics.max_rate_mm_h.toFixed(1)}} mm/h</strong></div><div><small>INTERVAL ACCUMULATION MEAN / MAX</small><strong class="date">{{precipitationMetrics.mean_accumulation_mm.toFixed(1)}} / {{precipitationMetrics.max_accumulation_mm.toFixed(1)}} mm</strong></div><div><small>THUNDERSTORM / HAIL / SNOW CELLS</small><strong class="date">{{precipitationMetrics.lightning_cells}} / {{precipitationMetrics.hail_cells}} / {{precipitationMetrics.snow_cells}}<template v-if="precipitationMetrics.max_hail_cm>0"> · hail ≤ {{precipitationMetrics.max_hail_cm.toFixed(1)}} cm</template></strong></div><div><small>HAZARDS</small><strong class="date">{{Object.entries(precipitationMetrics.hazard_counts).map(([h,n])=>`${HAZARD_LABELS[h]} ${n}`).join(' · ')||'none'}}</strong></div><div><small>PRECIPITABLE WATER (MEAN)</small><strong>{{precipitationMetrics.mean_precipitable_water_mm.toFixed(1)}} <em>mm</em></strong></div></div><div v-if="cloudMetrics" class="metrics extra-metrics"><div><small>CLOUD COVER / CLOUDY CELLS</small><strong class="date">{{cloudMetrics.cloud_cover_pct.toFixed(0)}} % / {{cloudMetrics.cloudy_pct.toFixed(0)}} %</strong></div><div><small>THUNDERSTORM CELLS</small><strong>{{cloudMetrics.thunderstorm_cells}}</strong></div><div><small>FRONTS COLD / WARM / STATIONARY</small><strong class="date">{{cloudMetrics.cold_front_cells}} / {{cloudMetrics.warm_front_cells}} / {{cloudMetrics.stationary_front_cells}} <em>cells</em></strong></div><div><small>DOMINANT CLOUD</small><strong class="date">{{cloudTypeInfo(cloudMetrics.dominant_type).label}}</strong></div><div><small>MEAN BASE / TOP</small><strong class="date">{{cloudMetrics.mean_base_m===null?'—':cloudMetrics.mean_base_m.toFixed(0)+' / '+cloudMetrics.mean_top_m.toFixed(0)+' m'}}</strong></div><div><small>FRONTAL / CONVECTIVE CELLS · HEATING</small><strong class="date">{{cloudMetrics.frontal_cells}} / {{cloudMetrics.convective_cells}} · {{cloudMetrics.surface_heating_k.toFixed(1)}} K</strong></div></div><div class="metrics"><div><small>GRID RESOLUTION</small><strong>{{grid.nx}} × {{grid.ny}} <em>cells</em></strong></div><div><small>CELL SPACING</small><strong v-if="data.dust||data.solarWind">{{(360/grid.nx).toFixed(0)}}° × {{(180/grid.ny).toFixed(0)}}° <em>≈ {{(grid.spacing_east_m/1e3).toFixed(0)}} km at the equator</em></strong><strong v-else>{{grid.spacing_east_m}} × {{grid.spacing_north_m}} <em>m</em></strong></div><div><small>VALID TIME</small><strong class="date">{{time}}</strong></div></div></article><template v-if="immersive">
   <DataStream :class="{'hud-collapsed':!hudStream}" :inert="hudStream?null:true" :paused="!hudStream" :state="streamState" :values="values" :range="range" :colors="streamColors" :color-for="streamColorFor" :channels="streamChannels" :overlays="streamOverlays" :telemetry="telemetry" :log="streamLog" :playing="playing" :loading="loading" @close="hudStream=false" @select="i=>{selected=i;inspecting=true}"/>
   <button v-if="!hudControls" class="hud-tab hud-tab-left" title="Show the control panel ( [ )" @click="hudControls=true"><kbd>[</kbd> CONTROLS</button>
   <button v-if="!hudStream" class="hud-tab hud-tab-right" title="Show the data stream ( ] )" @click="hudStream=true">STREAM <kbd>]</kbd></button>
   <div class="hud-frame" aria-hidden="true"><i></i><i></i><i></i><i></i></div>
   <transition name="hud-fade"><div v-if="hudToast" class="hud-toast" role="status">{{hudToast}}</div></transition>
   <p v-if="error" class="hud-error" role="alert">{{error}} <button class="hud-icon" aria-label="Dismiss" @click="error=''">✕</button></p>
   <button v-if="!hudHelp" class="hud-keys-hint" title="Keyboard shortcuts (?)" @click="hudHelp=true"><kbd>?</kbd> KEYS</button>
   <transition name="hud-fade"><div v-if="hudHelp" class="hud-help" role="dialog" aria-label="Keyboard shortcuts" @click.self="hudHelp=false"><div class="hud-help-card hud-panel"><div class="hud-panel-head"><span class="stream-title">KEYBOARD</span><button class="hud-icon" aria-label="Close" @click="hudHelp=false">✕</button></div><dl><template v-for="s in SHORTCUTS" :key="s.label"><dt><kbd v-for="k in s.keys" :key="k">{{k}}</kbd></dt><dd>{{s.label}}<small v-if="s.scope==='always'">also on the page</small></dd></template></dl></div></div></transition>
  </template></main>
  <p v-if="error" class="error" role="alert">{{error}}</p><footer><span>TerraFlux Physics / Research viewer v0.1</span><span>{{snapshots.length>1?'Snapshot playback':'Single snapshot'}} · {{data.terrain?'Visual terrain':'Flat surface'}} · No live solver</span></footer>
 </div>
</template>
