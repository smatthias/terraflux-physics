<script setup>
import { ref, computed, watch, onMounted, onBeforeUnmount } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { demo, validateSequence } from './data.js'
import { regionalWeatherDemo, summarizeFrame } from './weatherSystems.js'
import { validateComparison, waterComparison, comparisonCsv, dischargeSeries } from './comparison.js'
import { defaultCoupledForm, formToRequest, formFromRequest, checkCoupledRequest, requestFromCoupledAtmosphere, CORIOLIS_MODES, DENSITY_MODES, SCALAR_BOUNDARIES, GRADIENT_BOUNDARIES, CLOUD_CONFIG_KEYS } from './coupledWind.js'
import { cloudFormationDemo, summarizeClouds, CLOUD_TYPES, cloudTypeIndex, cloudTypeInfo, FRONT_COLORS } from './cloudFormation.js'
import { isobars, rateColor, summarizePrecipitation, precipitationTypeInfo, PRECIPITATION_TYPES, HAZARD_LABELS } from './weatherMap.js'
import { PRECIP_CONFIG_KEYS } from './coupledWind.js'
// Weather map view: a flat composition of switchable layers over the loaded frames.
const view=ref('field')
const mapLayers=ref({base:'temperature',isobars:true,isobarIntervalHpa:2,fronts:true,clouds:true,volumes:false,precipitation:true,symbols:true,hazards:true,wind:false})
const precipitationMetrics=computed(()=>data.value.precipitation?summarizePrecipitation(data.value):null)
const isobarLines=computed(()=>{try{return data.value.coupled&&view.value==='map'&&mapLayers.value.isobars?isobars(data.value.coupled.pressure,grid.value,mapLayers.value.isobarIntervalHpa*100):[]}catch{return []}})
const temperatureRange=computed(()=>{const a=snapshots.value.flatMap(d=>d.forecast.fields.air_temperature.values);return [Math.min(...a),Math.max(...a)]})
const pressureRange=computed(()=>data.value.coupled?[Math.min(...data.value.coupled.pressure),Math.max(...data.value.coupled.pressure)]:[0,1])
// Composite map colour of one cell as a THREE.Color.
function mapCellColor(d,i){
 const L=mapLayers.value,color=new THREE.Color('#16242d')
 if(L.base==='temperature'){const [lo,hi]=temperatureRange.value,n=(d.forecast.fields.air_temperature.values[i]-lo)/((hi-lo)||1);color.setHSL((223-n*212)/360,.55,.30+n*.12)}
 else if(L.base==='pressure'&&d.coupled){const [lo,hi]=pressureRange.value,n=(d.coupled.pressure[i]-lo)/((hi-lo)||1);color.setHSL((260-n*120)/360,.35,.26+n*.18)}
 if(L.clouds&&d.clouds&&d.clouds.type[i]!=='none')color.lerp(new THREE.Color(cloudTypeInfo(d.clouds.type[i]).color),.35+.5*d.clouds.fraction[i])
 if(L.precipitation&&d.precipitation&&d.precipitation.rate[i]>0)color.lerp(new THREE.Color(rateColor(d.precipitation.rate[i])),.85)
 return color
}
const mapCellStyles=computed(()=>view.value==='map'?Array.from({length:grid.value.nx*grid.value.ny},(_,i)=>mapCellColor(data.value,i).getStyle()):[])
function glyphSprite(text,color,size=1.1){const canvas=document.createElement('canvas');canvas.width=96;canvas.height=96;const ctx=canvas.getContext('2d');ctx.font='bold 64px sans-serif';ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillStyle=color;ctx.fillText(text,48,52);const sprite=new THREE.Sprite(new THREE.SpriteMaterial({map:new THREE.CanvasTexture(canvas),depthTest:false,transparent:true}));sprite.scale.set(size,size,1);return sprite}
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
let renderer, scene, camera, controls, observer, frame, group, cells=[], playback
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
watch(data,()=>{if(!data.value.terrainTemperature)adjustedTemperature.value=false;if((layer.value==='rainfall'&&!data.value.rain)||(layer.value==='terrain'&&!data.value.terrain)||(['slope','aspect'].includes(layer.value)&&!data.value.terrainDerivatives)||(layer.value==='water'&&!data.value.surfaceWater)||(layer.value==='soil'&&!data.value.surfaceWater?.soil_values)||(layer.value==='cloud'&&!data.value.atmosphere)||(['pressure','humidity'].includes(layer.value)&&!data.value.atmosphere&&!data.value.coupled)||(['density','moisture'].includes(layer.value)&&!data.value.coupled)||(['cape','cloudbase','cloudtop'].includes(layer.value)&&!data.value.coupled?.columns)||(layer.value==='clouds'&&!data.value.clouds)||(['precip','accum'].includes(layer.value)&&!data.value.precipitation))layer.value='temperature'})
const forecast=computed(()=>data.value.forecast), grid=computed(()=>forecast.value.grid)
function layerValues(d){
 if(layer.value==='cloud')return d.atmosphere?.cloud.map(v=>v*100)??[]
 if(layer.value==='pressure')return d.coupled?d.coupled.pressure.map(v=>v/100):d.atmosphere?.pressure??[]
 if(layer.value==='humidity')return d.coupled?.humidity??d.atmosphere?.humidity??[]
 if(layer.value==='density')return d.coupled?.density??[]
 if(layer.value==='moisture')return d.coupled?.specificHumidity.map(v=>v*1000)??[]
 if(layer.value==='clouds')return d.clouds?.type.map(cloudTypeIndex)??[]
 if(layer.value==='precip')return d.precipitation?.rate??[]
 if(layer.value==='accum')return d.precipitation?.accumulation??[]
 if(layer.value==='cape')return d.coupled?.columns?.cape??[]
 if(layer.value==='cloudbase')return d.coupled?.columns?.lcl??[]
 if(layer.value==='cloudtop')return d.coupled?.columns?.el??[]
 if(layer.value==='temperature')return temperatureValues(d).map(v=>v-273.15)
 if(layer.value==='wind')return d.forecast.fields.wind_east.values.map((u,i)=>Math.hypot(u,d.forecast.fields.wind_north.values[i]))
 if(layer.value==='soil')return d.surfaceWater?.soil_values?.map(v=>v===null?null:v*1000)??[]
 if(layer.value==='water')return d.surfaceWater?.values.map(v=>v===null?null:v*1000)??[]
 if(layer.value==='terrain')return d.terrain?.values??[]
 if(layer.value==='slope')return d.terrainDerivatives?.slope_deg??[]
 if(layer.value==='aspect')return d.terrainDerivatives?.aspect_deg??[]
 return d.rain?.values??[]
}
const values=computed(()=>layerValues(data.value))
const range=computed(()=>{
 if(layer.value==='cloud'||(layer.value==='humidity'&&!data.value.coupled))return [0,100]
 if(layer.value==='aspect')return [0,360]
 if(layer.value==='clouds')return [0,CLOUD_TYPES.length-1]
 const a=[...snapshots.value,...(comparisonFrames.value??[])].flatMap(layerValues).filter(v=>v!==null)
 return a.length?a.reduce(([lo,hi],v)=>[Math.min(lo,v),Math.max(hi,v)],[Infinity,-Infinity]):[0,0]
})
function hue(value){
 if(layer.value==='aspect')return value
 const norm=(value-range.value[0])/(range.value[1]-range.value[0]||1)
 if(layer.value==='cloud')return 210
 return ['rainfall','water','soil'].includes(layer.value)?212-norm*47:223-norm*212
}
const unit=computed(()=>['cloud','humidity'].includes(layer.value)?'%':layer.value==='pressure'?'hPa':layer.value==='density'?'kg/m³':layer.value==='moisture'?'g/kg':layer.value==='cape'?'J/kg':['cloudbase','cloudtop'].includes(layer.value)?'m':layer.value==='clouds'?'':layer.value==='precip'?'mm/h':layer.value==='accum'?'mm':layer.value==='temperature'?'°C':layer.value==='wind'?'m/s':layer.value==='terrain'?'m':['slope','aspect'].includes(layer.value)?'°':'mm')
const time=computed(()=>new Date(Date.parse(forecast.value.start_time_utc)+forecast.value.elapsed_seconds*1000).toISOString().replace('T',' ').replace('.000Z',' UTC'))
const rainfallPhase=computed(()=>{
 if(frameIndex.value===0)return 'Initial state'
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
function disposeGroup(){ if(!group)return; group.traverse(o=>{o.geometry?.dispose(); if(o.material){o.material.map?.dispose();o.material.dispose()}});scene.remove(group) }
function draw(){
 if(!renderer || fallback.value)return
 disposeGroup(); group=new THREE.Group();scene.add(group);cells=[]
 const g=grid.value, scale=32/Math.max(g.nx*g.spacing_east_m,g.ny*g.spacing_north_m), dx=g.spacing_east_m*scale, dz=g.spacing_north_m*scale
  const elevations=data.value.terrain?.values
 const terrainBase=elevations?elevations.reduce((lo,v)=>Math.min(lo,v),Infinity):0
 const surface=i=>(elevations&&(terrainEnabled.value||layer.value==='terrain')) ? 0.15+(elevations[i]-terrainBase)*scale*Number(exaggeration.value) : null
 const geometry=new THREE.BoxGeometry(dx*.94,1,dz*.94)
 for(let i=0;i<g.nx*g.ny;i++){
  const value=values.value[i]??null, norm=value===null?0:(value-range.value[0])/(range.value[1]-range.value[0]||1)
  const color=view.value==='map'?mapCellColor(data.value,i):value===null?new THREE.Color('#283b49'):layer.value==='clouds'?new THREE.Color(CLOUD_TYPES[value].color):new THREE.Color().setHSL(hue(value)/360,layer.value==='cloud'?.15:.70,layer.value==='cloud'?.18+norm*.72:layer.value==='aspect'?.50:.46+norm*.08)
  const mesh=new THREE.Mesh(geometry.clone(),new THREE.MeshStandardMaterial({color,roughness:.6,metalness:.12}))
  const h=view.value==='map'?.2:surface(i)??(value===null?.10:layer.value==='clouds'?.15+data.value.clouds.fraction[i]*.6:.20+norm*2)
  mesh.scale.y=h;mesh.position.set(((i%g.nx)+.5-g.nx/2)*dx,h/2,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz); mesh.userData.index=i; group.add(mesh);cells.push(mesh)
 }
 geometry.dispose()
 if(view.value==='field'?vectors.value:mapLayers.value.wind){ const stride=Math.max(1,Math.ceil(Math.sqrt(g.nx*g.ny/90)))
 for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x,u=forecast.value.fields.wind_east.values[i],v=forecast.value.fields.wind_north.values[i],speed=Math.hypot(u,v);if(speed<.01)continue; const arrow=new THREE.ArrowHelper(new THREE.Vector3(u,0,-v).normalize(),new THREE.Vector3((x+.5-g.nx/2)*dx,(surface(i)??2.4)+1,-(y+.5-g.ny/2)*dz),Math.min(2.5,.3+speed*.18),0xb7e8de,.3,.15);group.add(arrow)} }
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
  if(L.hazards&&data.value.precipitation){const p=data.value.precipitation
   for(let i=0;i<g.nx*g.ny;i++){if(!p.lightning[i])continue;const sprite=glyphSprite(p.hail[i]>0?'⚡◆':'⚡','#ffd166',1.2);sprite.position.set(((i%g.nx)+.5-g.nx/2)*dx,1.15,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(sprite)}}
 }
 if(data.value.clouds){
  const c=data.value.clouds,heightScale=6/Math.max(1,c.config.max_cloud_top_m)
  if(view.value==='field'?cloudVolumes.value:mapLayers.value.volumes)for(let i=0;i<g.nx*g.ny;i++){
   if(c.type[i]==='none')continue
   const base=c.base[i]*heightScale,depth=Math.max(.05,(c.top[i]-c.base[i])*heightScale),info=cloudTypeInfo(c.type[i]),ground=(surface(i)??.2)+.3
   const volume=new THREE.Mesh(new THREE.BoxGeometry(dx*(info.level==='deep'?.7:.88),depth,dz*(info.level==='deep'?.7:.88)),new THREE.MeshStandardMaterial({color:new THREE.Color(info.color),transparent:true,opacity:info.level==='deep'?.8:.45,roughness:1,depthWrite:false}))
   volume.position.set(((i%g.nx)+.5-g.nx/2)*dx,ground+base+depth/2,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(volume)
   if(c.thunderstorm[i]){const bolt=new THREE.Mesh(new THREE.SphereGeometry(.22,8,6),new THREE.MeshBasicMaterial({color:0xffd166}));bolt.position.set(((i%g.nx)+.5-g.nx/2)*dx,ground+base+depth+.3,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(bolt)}
  }
  if(view.value==='field'?frontOverlay.value:mapLayers.value.fronts)for(let i=0;i<g.nx*g.ny;i++){
   if(c.frontType[i]==='none')continue
   const plate=new THREE.Mesh(new THREE.BoxGeometry(dx*.96,.08,dz*.96),new THREE.MeshBasicMaterial({color:new THREE.Color(FRONT_COLORS[c.frontType[i]]),transparent:true,opacity:.85}))
   const top=cells[i]?cells[i].scale.y:.2
   plate.position.set(((i%g.nx)+.5-g.nx/2)*dx,top+.06,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz);group.add(plate)
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
   const points=[]
   for(let y=0;y<g.ny;y+=2)for(let x=0;x<g.nx;x+=2){const i=y*g.nx+x;if(atmosphere.cloud[i]>.35)points.push({x,y,c:atmosphere.cloud[i]})}
   if(points.length){
    const cloudMesh=new THREE.InstancedMesh(new THREE.SphereGeometry(1,10,7),new THREE.MeshStandardMaterial({color:0xe5f0fa,transparent:true,opacity:.42,roughness:1,depthWrite:false}),points.length)
    const dummy=new THREE.Object3D()
    points.forEach((p,i)=>{dummy.position.set((p.x+.5-g.nx/2)*dx,5.5+p.c,-(p.y+.5-g.ny/2)*dz);dummy.scale.set(dx*(1+p.c),.25+p.c*.25,dz*(1+p.c));dummy.updateMatrix();cloudMesh.setMatrixAt(i,dummy.matrix)})
    group.add(cloudMesh)
   }
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

}
function reset(){if(!controls)return;camera.position.set(26,27,31);controls.target.set(0,0,0);controls.update()}
function pick(e){const rect=renderer.domElement.getBoundingClientRect(),ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2((e.clientX-rect.left)/rect.width*2-1,-(e.clientY-rect.top)/rect.height*2+1),camera);const hit=ray.intersectObjects(cells)[0];selected.value=hit?hit.object.userData.index:null}
async function upload(e){error.value=''; const file=e.target.files[0]; if(!file)return;try{if(file.size>20*1024*1024)throw Error('Choose a JSON file smaller than 20 MB.');loadFrames(JSON.parse(await file.text()));source.value=file.name}catch(err){error.value=err.message}finally{e.target.value=''}}
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
watch([data,layer,vectors,terrainEnabled,exaggeration,adjustedTemperature,cloudsEnabled,systemsEnabled,cloudVolumes,frontOverlay,view,mapLayers],draw,{deep:true})
onMounted(()=>{try{scene=new THREE.Scene();scene.background=new THREE.Color('#09151d');scene.fog=new THREE.Fog('#09151d',65,110);camera=new THREE.PerspectiveCamera(42,1,.1,200);renderer=new THREE.WebGLRenderer({antialias:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));host.value.appendChild(renderer.domElement);scene.add(new THREE.AmbientLight(0xd6efff,2));const sun=new THREE.DirectionalLight(0xffffff,3);sun.position.set(12,30,20);scene.add(sun);const floor=new THREE.GridHelper(60,30,0x28404b,0x142b36);floor.position.y=-.05;scene.add(floor);controls=new OrbitControls(camera,renderer.domElement);controls.enableDamping=true;controls.maxPolarAngle=Math.PI/2.05;controls.minDistance=8;controls.maxDistance=85;reset();renderer.domElement.addEventListener('click',pick);observer=new ResizeObserver(()=>{const {width,height}=host.value.getBoundingClientRect();renderer.setSize(width,height);camera.aspect=width/height;camera.updateProjectionMatrix()});observer.observe(host.value);draw();const tick=()=>{frame=requestAnimationFrame(tick);controls.update();renderer.render(scene,camera)};tick()}catch(err){fallback.value=true;error.value='WebGL unavailable in this browser. Showing an interactive 2D field instead.'}})
onBeforeUnmount(()=>{stop();cancelAnimationFrame(frame);observer?.disconnect();controls?.dispose();disposeGroup();scene?.traverse(o=>{o.geometry?.dispose();o.material?.dispose()});renderer?.dispose()})
</script>
<template>
 <div class="shell">
  <header><a class="brand" href="#"><span class="mark">T</span><span>TERRAFLUX<small>PHYSICS OBSERVATORY</small></span></a><span class="status"><i></i> LOCAL WORKSPACE</span><label class="upload">↥ Open JSON<input type="file" accept=".json,application/json" @change="upload"></label></header>
  <main><aside><div class="eyebrow">EXPERIMENT / 001</div><h1>Weather,<br>in perspective.</h1><p class="intro">Explore the fields behind the physics. Inspect temperature, wind and accumulated rainfall on a shared grid.</p>
   <section><div class="section-label">01 / FIELD LAYERS</div><button v-for="[key,title,description] in [['temperature','Air temperature','Dry transport · °C'],['wind','Wind speed','Prescribed field · m/s'],['rainfall','Rainfall accumulation','Observation-derived · mm'],['terrain','Terrain elevation','Surface geometry · m'],['slope','Terrain slope','Inclination · degrees'],['aspect','Downhill aspect','Bearing from north · degrees'],['water','Surface water depth','Closed-domain routing · mm'],['soil','Soil water storage','Infiltrated water · mm'],['cloud','Cloud cover','Synthetic fraction · %'],['pressure','Sea-level pressure','Synthetic regional field · hPa'],['humidity','Relative humidity','Synthetic regional field · %'],['density','Air density','Coupled moist layer · kg/m³'],['moisture','Specific humidity','Coupled moist layer · g/kg'],['cape','Convective energy (CAPE)','Surface-parcel column · J/kg'],['cloudbase','Cloud base (LCL)','Surface-parcel column · m'],['cloudtop','Equilibrium level','Surface-parcel column · m'],['clouds','Cloud formation','Fronts + convection · type'],['precip','Precipitation rate','Cloud outcomes · mm/h'],['accum','Interval accumulation','Cloud outcomes · mm']]" :key="key" class="layer" :class="{active:layer===key}" :disabled="(['precip','accum'].includes(key)&&!data.precipitation)||(key==='clouds'&&!data.clouds)||(['cape','cloudbase','cloudtop'].includes(key)&&!data.coupled?.columns)||(key==='rainfall'&&!data.rain)||(key==='terrain'&&!data.terrain)||(['slope','aspect'].includes(key)&&!data.terrainDerivatives)||(key==='water'&&!data.surfaceWater)||(key==='soil'&&!data.surfaceWater?.soil_values)||(key==='cloud'&&!data.atmosphere)||(['pressure','humidity'].includes(key)&&!data.atmosphere&&!data.coupled)||(['density','moisture'].includes(key)&&!data.coupled)" @click="layer=key"><span>{{title}}<small>{{data.atmosphere&&key==='temperature'?'Synthetic regional field · °C':data.coupled&&key==='temperature'?'Coupled moist layer · °C':data.coupled&&key==='wind'?'Pressure · Coriolis · friction · m/s':data.coupled&&key==='pressure'?'Prescribed static field · hPa':data.coupled&&key==='humidity'?'Diagnosed from T, q, p · %':description}}</small></span><span>{{layer===key?'●':'○'}}</span></button><template v-if="data.atmosphere"><label class="toggle"><input type="checkbox" v-model="cloudsEnabled"> Cloud overlay</label><label class="toggle"><input type="checkbox" v-model="systemsEnabled"> Weather-system markers</label><p class="hint">Synthetic pressure, clouds and front. Cloud height is schematic; these fields do not drive the local physics.</p></template><p v-if="data.coupled" class="hint">Coupled engine run: wind from the static pressure field, Coriolis and friction; density diagnosed from temperature and humidity feeds back into the wind; temperature and humidity are carried by the wind with configurable diffusion.<br>Diffusion T {{data.coupled.request.diffusion.temperature_m2_s}} · q {{data.coupled.request.diffusion.humidity_m2_s}} · momentum {{data.coupled.request.diffusion.momentum_m2_s}} m²/s · friction {{data.coupled.request.atmosphere.surface_friction_coefficient_s}} 1/s · Coriolis {{data.coupled.request.atmosphere.coriolis_mode}} · density {{data.coupled.request.density_mode}}<template v-if="data.coupled.columns"><br>Vertical columns: hydrostatic layers, convective adjustment and a lifted surface parcel give cloud base, equilibrium level and CAPE per cell. Diagnostic only; no surface fluxes resupply heat or moisture.</template></p><template v-if="data.clouds"><label class="toggle"><input type="checkbox" v-model="cloudVolumes"> Cloud volumes (base → top)</label><label class="toggle"><input type="checkbox" v-model="frontOverlay"> Front overlay (blue cold · red warm · purple stationary)</label><p class="hint">{{data.clouds.synthetic?'Synthetic mock in the documented cloud-formation format; illustration only.':'Engine diagnosis: fronts from the temperature gradient, advection and convergence; convection from the heated surface parcel; combined where frontal lifting releases it.'}}<br>Heating now {{data.clouds.heating.toFixed(1)}} K · lifting over {{(data.clouds.config.lifting_time_scale_s/3600).toFixed(0)}} h · warm slope 1:{{(1/data.clouds.config.warm_front_slope).toFixed(0)}} · cold slope 1:{{(1/data.clouds.config.cold_front_slope).toFixed(0)}}</p></template><label class="toggle"><input type="checkbox" v-model="vectors"> Wind direction vectors</label><template v-if="data.terrain"><label class="toggle"><input type="checkbox" v-model="terrainEnabled"> Place fields on terrain</label><label class="toggle">Height scale {{exaggeration}}× <input aria-label="Terrain vertical exaggeration" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label><label v-if="data.terrainTemperature" class="toggle"><input type="checkbox" v-model="adjustedTemperature"> Terrain-adjusted temperature</label><p v-if="data.terrainTemperature" class="hint">Lapse rate {{data.terrainTemperature.lapse_rate_k_per_m*1000}} K/km · reference {{data.terrainTemperature.reference_elevation_m}} m<br>Diagnostic adjustment; no feedback into transport.</p><p class="hint">{{data.terrain.source_id}} · {{data.terrain.vertical_reference}}<br>Geometry is a display layer; lapse-rate adjustment is optional.</p></template></section>
   <section v-if="view==='map'" class="map-layers"><div class="section-label">01b / WEATHER MAP LAYERS</div><p class="hint">Switch layers on and off to compose the map. Engine or labelled mock output only; no rendering of external map tiles yet.</p>
    <label class="toggle">Base field <select v-model="mapLayers.base"><option value="temperature">Air temperature</option><option value="pressure">Surface pressure</option><option value="none">None (dark)</option></select></label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.isobars"> Isobars every <select v-model.number="mapLayers.isobarIntervalHpa"><option :value="1">1</option><option :value="2">2</option><option :value="4">4</option></select> hPa</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.fronts" :disabled="!data.clouds"> Fronts (cold blue · warm red · stationary purple)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.clouds" :disabled="!data.clouds"> Cloud cover by genus</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.volumes" :disabled="!data.clouds"> Cloud volumes (3D base → top)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.precipitation" :disabled="!data.precipitation"> Precipitation rate shading</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.symbols" :disabled="!data.precipitation"> Precipitation symbols (● rain ▽ showers ✱ snow ▲ sleet ’ drizzle)</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.hazards" :disabled="!data.precipitation"> Thunderstorm ⚡ and hail ◆ markers</label>
    <label class="toggle"><input type="checkbox" v-model="mapLayers.wind"> Wind vectors</label>
    <p v-if="!data.clouds" class="hint">Load the cloud formation example or mock for fronts, clouds and precipitation layers.</p>
   </section>
   <section><div class="section-label">02 / DATA SOURCE</div><strong class="source">{{source}}</strong><p class="hint">{{source==='Synthetic demonstration'?'Illustrative fields only. These values are not engine output or live observations.':source==='Regional weather · synthetic'?'Analytic 1,200 × 900 km weather illustration. Low/high pressure, a moving front and cloud bands; not solver output.':source==='Engine output · synthetic inputs'?'C++ dry transport + radar rain estimation + accumulation + gridding. Synthetic inputs; not live weather.':source==='Engine output · coupled wind'?'C++ coupled wind / moist-layer model on synthetic Gaussian pressure systems. Pressure is prescribed; not live weather or a forecast.':source==='Engine output · cloud formation'?'C++ coupled model with column and cloud-formation diagnostics: cold/warm fronts, frontal and convective cloud, afternoon thunderstorms. Synthetic inputs; rule-of-thumb cloud types.':source==='Cloud formation · synthetic mock'?'Hand-built mock in the documented cloud-formation format (moving warm and cold fronts, diurnal convection). Not solver output.':'Loaded in this browser only. No upload service or database.'}}</p><button class="example-button" :disabled="loading" @click="loadEngineExample">{{loading?'Loading…':'→ Load engine example'}}</button><button class="example-button" :disabled="loading" @click="loadOutletExample">↘ Load outlet scenario</button><button class="example-button" :disabled="loading" @click="loadComparisonDemo">⇄ Compare hydrology scenarios</button><label v-if="data.surfaceWater" class="compare-upload">Open comparison JSON<input type="file" accept=".json,application/json" @change="uploadComparison"></label><button class="example-button" :disabled="loading" @click="loadCoupledExample">⇶ Load coupled wind example</button><button class="example-button" :disabled="loading" @click="loadCloudExample">⛅ Load cloud formation example</button><button class="example-button" @click="useCloudMock">⛈ Cloud formation mock (synthetic)</button><button class="example-button" @click="useRegionalDemo">☁ Regional weather demo</button><button class="text-button" @click="useDemo">↻ Load synthetic demo</button></section>
   <section class="inspection"><div class="section-label">03 / CELL INSPECTOR</div><template v-if="chosen"><h2>{{values[chosen.i]===null?(layer==='aspect'?'Undefined (flat)':'No data'):values[chosen.i]?.toFixed(2)}} <small>{{unit}}</small></h2><p>Column {{chosen.x}} · Row {{chosen.y}} · Index {{chosen.i}}</p><p>East {{(grid.origin_east_m+(chosen.x+.5)*grid.spacing_east_m).toFixed(0)}} m · North {{(grid.origin_north_m+(chosen.y+.5)*grid.spacing_north_m).toFixed(0)}} m</p><p v-if="data.atmosphere">Cloud {{(data.atmosphere.cloud[chosen.i]*100).toFixed(1)}}% · Pressure {{data.atmosphere.pressure[chosen.i].toFixed(1)}} hPa<br>Relative humidity {{data.atmosphere.humidity[chosen.i].toFixed(1)}}%</p><p v-if="data.coupled">Pressure {{(data.coupled.pressure[chosen.i]/100).toFixed(1)}} hPa · Density {{data.coupled.density[chosen.i].toFixed(4)}} kg/m³<br>Specific humidity {{(data.coupled.specificHumidity[chosen.i]*1000).toFixed(2)}} g/kg · Relative humidity {{data.coupled.humidity[chosen.i].toFixed(1)}}%<br>Coriolis f {{data.coupled.coriolis[chosen.i].toExponential(3)}} 1/s</p><p v-if="data.clouds">Cloud: <strong>{{cloudTypeInfo(data.clouds.type[chosen.i]).label}}</strong><template v-if="data.clouds.type[chosen.i]!=='none'"> · base {{data.clouds.base[chosen.i].toFixed(0)}} m · top {{data.clouds.top[chosen.i].toFixed(0)}} m · cover {{(data.clouds.fraction[chosen.i]*100).toFixed(0)}}%<br>Mechanism {{data.clouds.mechanism[chosen.i]}} · trigger {{data.clouds.trigger[chosen.i]}}{{data.clouds.thunderstorm[chosen.i]?' · thunderstorm':''}}</template><br>Front {{data.clouds.frontType[chosen.i]}} · |∇T| {{(data.clouds.gradient[chosen.i]*1e5).toFixed(2)}} K/100 km · advection {{(data.clouds.advection[chosen.i]*3600).toFixed(2)}} K/h · divergence {{data.clouds.divergence[chosen.i].toExponential(2)}} 1/s<br>{{data.clouds.distance[chosen.i]===null?'No front in the domain':`Nearest front ${(data.clouds.distance[chosen.i]/1000).toFixed(0)} km · ${data.clouds.side[chosen.i]===1?'warm side':data.clouds.side[chosen.i]===-1?'cold side':'on the front'}`}} · lifting {{data.clouds.lifting[chosen.i].toFixed(0)}} m · heated CAPE {{data.clouds.cape[chosen.i].toFixed(0)}} / CIN {{data.clouds.cin[chosen.i].toFixed(0)}} J/kg</p><p v-if="data.precipitation">Precipitation: <strong>{{precipitationTypeInfo(data.precipitation.type[chosen.i]).label}}</strong><template v-if="data.precipitation.type[chosen.i]!=='none'"> · {{data.precipitation.intensity[chosen.i]}} {{data.precipitation.character[chosen.i]}} · {{data.precipitation.rate[chosen.i].toFixed(2)}} mm/h for {{(data.precipitation.duration[chosen.i]*100).toFixed(0)}}% of the interval → {{data.precipitation.accumulation[chosen.i].toFixed(1)}} mm · phase {{data.precipitation.phase[chosen.i]}}</template><template v-if="data.precipitation.virga[chosen.i]"> · virga (evaporates aloft)</template><br>{{data.precipitation.hazards[chosen.i].length?data.precipitation.hazards[chosen.i].map(h=>HAZARD_LABELS[h]).join(' · '):'no hazards'}}<template v-if="data.precipitation.hail[chosen.i]>0"> · hail up to {{data.precipitation.hail[chosen.i].toFixed(1)}} cm</template><br>Wet-bulb {{(data.precipitation.wetBulb[chosen.i]-273.15).toFixed(1)}} °C · freezing level {{data.precipitation.freezingLevel[chosen.i]===null?'none':data.precipitation.freezingLevel[chosen.i].toFixed(0)+' m'}} · precipitable water {{data.precipitation.precipitableWater[chosen.i].toFixed(1)}} mm<template v-if="data.precipitation.baseTemperature[chosen.i]!==null"> · cloud {{(data.precipitation.baseTemperature[chosen.i]-273.15).toFixed(0)}} °C at base, {{(data.precipitation.topTemperature[chosen.i]-273.15).toFixed(0)}} °C at top · lifting {{data.precipitation.updraft[chosen.i].toFixed(2)}} m/s</template></p><p v-if="data.coupled?.columns">Column ({{data.coupled.columns.config.layer_count}} × {{data.coupled.columns.config.layer_thickness_m}} m): LCL {{data.coupled.columns.lcl[chosen.i]===null?'none':data.coupled.columns.lcl[chosen.i].toFixed(0)+' m'}} · LFC {{data.coupled.columns.lfc[chosen.i]===null?'none':data.coupled.columns.lfc[chosen.i].toFixed(0)+' m'}} · EL {{data.coupled.columns.el[chosen.i]===null?'none':data.coupled.columns.el[chosen.i].toFixed(0)+' m'}}<br>CAPE {{data.coupled.columns.cape[chosen.i].toFixed(0)}} J/kg · CIN {{data.coupled.columns.cin[chosen.i].toFixed(0)}} J/kg · potential {{data.coupled.columns.potential[chosen.i]}} · condensed {{data.coupled.columns.condensed[chosen.i].toFixed(3)}} kg/m²</p><p v-if="data.terrainTemperature && layer==='temperature'">Reference temperature {{(forecast.fields.air_temperature.values[chosen.i]-273.15).toFixed(2)}} °C<br>Terrain correction {{(data.terrainTemperature.values[chosen.i]-forecast.fields.air_temperature.values[chosen.i]).toFixed(2)}} K</p><p v-if="data.terrainDerivatives">Slope {{data.terrainDerivatives.slope_deg[chosen.i].toFixed(2)}}° · Aspect {{data.terrainDerivatives.aspect_deg[chosen.i]===null?'flat / undefined':data.terrainDerivatives.aspect_deg[chosen.i].toFixed(1)+'°'}}</p><p v-if="data.terrain">Elevation {{data.terrain.values[chosen.i].toFixed(2)}} m · {{data.terrain.vertical_reference}}</p><p v-if="layer==='rainfall'">Quality: {{data.rain.quality[chosen.i]}}</p></template><p v-else class="hint">Select a cell in the scene to inspect its value and local coordinates.</p></section>
   <section class="request"><div class="section-label">04 / COUPLED WIND REQUEST</div><p class="hint">Edit the time steps, grid, atmosphere, diffusion and pressure systems, download the request and run <code>terraflux-run-coupled-wind</code>. No physics runs in the browser; checks mirror the solver bounds.</p>
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
    <label v-if="coupledForm.cloud_enabled" class="toggle"><input type="checkbox" v-model="coupledForm.precip_enabled"> Precipitation outcomes (rain, snow, sleet, hail, thunderstorms)</label>
    <div v-if="coupledForm.cloud_enabled&&coupledForm.precip_enabled" class="request-form">
     <label v-for="[key,label] in PRECIP_CONFIG_KEYS" :key="key">{{label}}<input type="number" step="any" v-model.number="coupledForm.precip[key]"></label>
    </div>
    <ul class="report"><li v-for="e in coupledReport.errors" :key="e" class="report-error">✕ {{e}}</li><li v-for="w in coupledReport.warnings" :key="w" class="report-warn">△ {{w}}</li><li v-if="!coupledReport.errors.length" class="report-ok">✓ Request passes the configuration checks</li></ul>
    <p v-if="!coupledReport.errors.length" class="hint">f {{coupledReport.estimates.coriolis_parameter_s?.toExponential(3)}} 1/s · inertial period {{fmt(coupledReport.estimates.inertial_period_h,1)}} h · friction time {{fmt(coupledReport.estimates.friction_time_h,1)}} h · cross-isobar angle {{fmt(coupledReport.estimates.cross_isobar_angle_deg,1)}}°<br>Δt(|f|+k) {{fmt(coupledReport.estimates.coriolis_friction_number,3)}} · momentum diffusion number {{fmt(coupledReport.estimates.momentum_diffusion_number,3)}} · scalar stability estimate at spin-up peak {{fmt(coupledReport.estimates.scalar_stability_estimate,2)}}<br>Balanced wind ≈ {{fmt(coupledReport.estimates.balanced_speed_estimate_m_s,1)}} m/s · peak ≈ {{fmt(coupledReport.estimates.peak_speed_estimate_m_s,1)}} m/s · upwind numerical diffusion ≈ {{fmt(coupledReport.estimates.numerical_diffusivity_estimate_m2_s,0)}} m²/s<template v-if="coupledReport.estimates.column_top_m"><br>Column top {{fmt(coupledReport.estimates.column_top_m/1000,1)}} km</template></p>
    <div class="comparison-switch"><button :disabled="coupledReport.errors.length>0" @click="downloadCoupledRequest">⇩ Download request JSON</button><button v-if="data.coupled" @click="useLoadedCoupledSettings">Use loaded run's settings</button><button @click="resetCoupledForm">Reset to example</button></div>
    <p v-if="coupledNote" class="hint">{{coupledNote}}</p>
   </section>
  </aside><article><div class="scene-head"><div><span class="eyebrow">FIELD EXPLORER</span><h2>{{layer==='temperature'?(adjustedTemperature?'Terrain-adjusted temperature':'Air temperature'):layer==='wind'?'Wind speed':layer==='terrain'?'Terrain elevation':layer==='slope'?'Terrain slope':layer==='aspect'?'Downhill aspect':layer==='water'?'Surface water depth':layer==='soil'?'Soil water storage':layer==='cloud'?'Cloud cover':layer==='pressure'?'Sea-level pressure':layer==='humidity'?'Relative humidity':layer==='density'?'Air density':layer==='moisture'?'Specific humidity':layer==='cape'?'Convective available potential energy':layer==='cloudbase'?'Cloud base (lifting condensation level)':layer==='cloudtop'?'Equilibrium level':layer==='clouds'?'Cloud formation · fronts and convection':layer==='precip'?'Precipitation rate at the ground':layer==='accum'?'Precipitation accumulated over the snapshot interval':'Rainfall accumulation'}}</h2></div><div class="view-switch"><button :class="{selected:view==='field'}" @click="view='field'">Field explorer</button><button :class="{selected:view==='map'}" :disabled="!data.coupled" @click="view='map'">Weather map</button><button class="reset" @click="reset">↺ Reset view</button></div></div><div class="viewport" ref="host" aria-label="Interactive 3D weather grid"><div v-if="fallback" class="fallback-grid" :style="{gridTemplateColumns:`repeat(${grid.nx},1fr)`}"><button v-for="(value,i) in values" :key="i" :aria-label="`Cell ${i}: ${value===null?'no data':value.toFixed(2)} ${unit}`" :style="{gridRow:grid.ny-Math.floor(i/grid.nx),gridColumn:i%grid.nx+1,background:view==='map'?mapCellStyles[i]:value===null?'#283b49':layer==='clouds'?CLOUD_TYPES[value].color:`hsl(${hue(value)} ${layer==='cloud'?15:70}% ${layer==='cloud'?18+value*.72:50}%)`}" @click="selected=i"></button></div><svg v-if="fallback && data.atmosphere" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Synthetic cloud and weather-system overlay"><template v-if="cloudsEnabled"><ellipse v-for="(c,i) in data.atmosphere.cloud" :key="'cloud'+i" v-show="i%3===0 && c>.35" :cx="((i%grid.nx)+.5)/grid.nx*100" :cy="(1-(Math.floor(i/grid.nx)+.5)/grid.ny)*100" :rx="2.1" :ry="1.7" fill="#e9f5ff" :opacity="c*.32"/></template><template v-if="systemsEnabled"><template v-for="(s,i) in data.atmosphere.systems" :key="i"><polyline v-if="s.kind==='cold_front'" :points="s.points.map(([u,v])=>`${u*100},${(1-v)*100}`).join(' ')" fill="none" stroke="#85ccff" stroke-width=".7" stroke-dasharray="2 1"/><text v-else :x="s.u*100" :y="(1-s.v)*100" :fill="s.kind==='low'?'#ffb39c':'#c5e9ff'" font-size="7" font-weight="bold" text-anchor="middle">{{s.kind==='low'?'L':'H'}}</text></template></template></svg><svg v-if="fallback && view==='map'" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Weather map overlays"><template v-for="level in isobarLines" :key="level.level_pa"><polyline v-for="(line,li) in level.lines" :key="li" :points="line.map(([u,v])=>`${u*100},${(1-v)*100}`).join(' ')" fill="none" :stroke="Math.round(level.level_hpa)%(mapLayers.isobarIntervalHpa*2)===0?'#f3f7f9':'#a9bcc6'" :stroke-width="Math.round(level.level_hpa)%(mapLayers.isobarIntervalHpa*2)===0?.5:.3"/><text v-if="Math.round(level.level_hpa)%(mapLayers.isobarIntervalHpa*2)===0&&level.lines[0]" :x="level.lines[0][0][0]*100" :y="(1-level.lines[0][0][1])*100" font-size="2.6" fill="#f3f7f9">{{Math.round(level.level_hpa)}}</text></template><template v-if="mapLayers.fronts&&data.clouds"><template v-for="(t,i) in data.clouds.frontType" :key="'f'+i"><rect v-if="t!=='none'" :x="(i%grid.nx)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+1)/grid.ny)*100" :width="100/grid.nx" :height="100/grid.ny" :fill="FRONT_COLORS[t]" opacity=".5"/></template></template><template v-if="mapLayers.symbols&&data.precipitation"><template v-for="(t,i) in data.precipitation.type" :key="'p'+i"><text v-if="t!=='none'&&(i%grid.nx)%symbolStride===0&&Math.floor(i/grid.nx)%symbolStride===0" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.3)/grid.ny)*100" font-size="2.8" text-anchor="middle" :fill="data.precipitation.phase[i]==='solid'?'#ffffff':'#bfe0ff'">{{precipitationTypeInfo(t).symbol}}</text></template></template><template v-if="mapLayers.hazards&&data.precipitation"><template v-for="(b,i) in data.precipitation.lightning" :key="'h'+i"><text v-if="b" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.7)/grid.ny)*100" font-size="3" text-anchor="middle" fill="#ffd166">{{data.precipitation.hail[i]>0?'⚡◆':'⚡'}}</text></template></template></svg><svg v-if="fallback && view==='field' && data.clouds && frontOverlay" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Front overlay"><template v-for="(t,i) in data.clouds.frontType" :key="i"><rect v-if="t!=='none'" :x="(i%grid.nx)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+1)/grid.ny)*100" :width="100/grid.nx" :height="100/grid.ny" :fill="FRONT_COLORS[t]" opacity=".55"/></template><template v-for="(s,i) in data.clouds.thunderstorm" :key="'s'+i"><text v-if="s" :x="((i%grid.nx)+.5)/grid.nx*100" :y="(1-(Math.floor(i/grid.nx)+.3)/grid.ny)*100" font-size="3" text-anchor="middle" fill="#ffd166">⚡</text></template></svg><svg v-if="fallback && data.surfaceWater?.outlets?.length" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Configured surface outlet locations"><circle v-for="(o,i) in data.surfaceWater.outlets" :key="i" :cx="(o.x+.5)/grid.nx*100" :cy="(1-(o.y+.5)/grid.ny)*100" r="1.6" fill="#ffa15c" stroke="#fff2e6" stroke-width=".4"/></svg><div class="scene-note">{{fallback?'2D FALLBACK / NORTH UP':'NORTH −Z / EAST +X'}}<br><span>{{fallback?'Color encodes field value · select a cell':data.terrain&&(terrainEnabled||layer==='terrain')?'Height: relative terrain · color: selected field':'Height encodes field value · flat reference surface'}}</span></div><div v-if="view==='map'" class="cloud-key map-key"><span v-if="mapLayers.base!=='none'"><i :style="{background:mapLayers.base==='temperature'?'linear-gradient(90deg,hsl(223 55% 30%),hsl(11 55% 42%))':'linear-gradient(90deg,hsl(260 35% 26%),hsl(140 35% 44%))'}"></i>{{mapLayers.base==='temperature'?`Temperature ${(temperatureRange[0]-273.15).toFixed(0)}–${(temperatureRange[1]-273.15).toFixed(0)} °C`:`Pressure ${(pressureRange[0]/100).toFixed(0)}–${(pressureRange[1]/100).toFixed(0)} hPa`}}</span><span v-if="mapLayers.isobars">━ isobars {{mapLayers.isobarIntervalHpa}} hPa (bright = {{mapLayers.isobarIntervalHpa*2}} hPa)</span><template v-if="mapLayers.precipitation&&data.precipitation"><span v-for="r in [0.5,3,15,40]" :key="r"><i :style="{background:rateColor(r)}"></i>{{r}} mm/h</span></template><template v-if="mapLayers.clouds&&data.clouds"><span v-for="t in CLOUD_TYPES.slice(1)" :key="t.key"><i :style="{background:t.color}"></i>{{t.abbreviation}}</span></template><span v-if="mapLayers.fronts&&data.clouds"><i style="background:#4da3ff"></i>cold <i style="background:#ff6b6b"></i>warm <i style="background:#c792ea"></i>stationary</span></div><div v-else-if="layer==='clouds'" class="cloud-key"><span v-for="t in CLOUD_TYPES" :key="t.key"><i :style="{background:t.color}"></i>{{t.abbreviation}} {{t.label}}</span></div><div v-else class="legend"><span>{{range[0].toFixed(1)}} {{unit}}</span><div :style="layer==='aspect'?{background:'linear-gradient(90deg,red,yellow,lime,cyan,blue,magenta,red)'}:['rainfall','water','soil'].includes(layer)?{background:'linear-gradient(90deg,#2675cc,#26ccb2)'}:{}"></div><span>{{range[1].toFixed(1)}} {{unit}}</span></div><div v-if="layer==='aspect'" class="aspect-key">0° N · 90° E · 180° S · 270° W · gray = flat</div><div class="gestures">{{fallback?'SELECT A CELL TO INSPECT':'DRAG TO ORBIT · SCROLL TO ZOOM · CLICK TO INSPECT'}}</div></div><div v-if="snapshots.length>1" class="timeline"><div class="playback"><button @click="seek(frameIndex-1)" :disabled="frameIndex===0" aria-label="Previous frame">‹</button><button @click="togglePlayback">{{playing?'Pause':'Play'}}</button><button @click="seek(frameIndex+1)" :disabled="frameIndex===snapshots.length-1" aria-label="Next frame">›</button><span>Frame {{frameIndex+1}} / {{snapshots.length}} · +{{forecast.elapsed_seconds/60}} min · {{rainfallPhase}}</span></div><input aria-label="Timeline frame" type="range" min="0" :max="snapshots.length-1" :value="frameIndex" @input="seek($event.target.value)"><p>{{data.precipitation?`${data.precipitation.synthetic?'Synthetic mock':'Engine'} outcomes · ${data.precipitation.precipitating} precipitating cells · ${data.precipitation.lightningCells} thunderstorms · ${data.precipitation.hailCells} hail · ${data.precipitation.snowCells} snow · max ${data.precipitation.maxRate.toFixed(1)} mm/h`:data.clouds?`${data.clouds.synthetic?'Synthetic mock':'Engine'} cloud snapshots · heating ${data.clouds.heating.toFixed(1)} K · ${data.clouds.cloudy} cloudy cells · ${data.clouds.storms} thunderstorm cells · ${data.clouds.fronts} frontal cells`:data.coupled?`Coupled engine snapshots · fixed color scale · pressure static · scalar stability number ${data.coupled.stability.toFixed(2)} · supersaturated cells ${data.coupled.supersaturated}`:data.atmosphere?'Synthetic regional snapshots · fixed color scale · no coupling to local engine physics':'Discrete solver snapshots · fixed color scale · rainfall accumulates from the start · unavailable at time zero'}}</p></div><p v-if="layer==='rainfall' && data.rain" class="rain-interval">Accumulation interval: {{data.rain.period_start_utc.replace('T',' ').replace('Z',' UTC')}} → {{data.rain.valid_time_utc.replace('T',' ').replace('Z',' UTC')}}</p><p v-if="['water','soil'].includes(layer) && data.surfaceWater" class="rain-interval">Surface {{data.surfaceWater.stored_volume_m3.toFixed(2)}} m³ · Soil {{data.surfaceWater.soil_volume_m3.toFixed(2)}} m³ · Rain input {{data.surfaceWater.cumulative_rainfall_volume_m3.toFixed(2)}} m³ · Evaporated {{data.surfaceWater.evaporated_volume_m3.toFixed(2)}} m³ · Drained {{data.surfaceWater.drained_volume_m3.toFixed(2)}} m³ · Discharged {{data.surfaceWater.discharged_volume_m3.toFixed(2)}} m³ · Balance error {{data.surfaceWater.balance_error_m3.toExponential(2)}} m³<br>{{data.surfaceWater.boundary==='configured_outlets'?'Configured surface outlets':'Closed boundaries'}} · unknown rainfall cells are impermeable masks · prototype routing<br>Step {{data.surfaceWater.parameters.timestep_seconds}} s · Infiltration {{(data.surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h · Evaporation {{(data.surfaceWater.parameters.evaporation_rate_m_s*3600000).toFixed(2)}} mm/h · Drainage {{(data.surfaceWater.parameters.drainage_rate_m_s*3600000).toFixed(2)}} mm/h<span v-if="data.surfaceWater.soil_capacity_m!==null"> · soil capacity {{data.surfaceWater.soil_capacity_m*1000}} mm</span></p><section v-if="comparisonFrames" class="comparison"><div class="section-label">SCENARIO COMPARISON · SAME FORCING / TIME</div><div class="comparison-switch"><button @click="exportComparison">Export all frames (CSV)</button><button :class="{selected:comparisonView==='baseline'}" @click="comparisonView='baseline'">Show baseline</button><button :class="{selected:comparisonView==='variant'}" @click="comparisonView='variant'">Show {{comparisonName}}</button></div><p>Viewing {{comparisonView==='baseline'?'baseline':comparisonName}} · Δ = variant − baseline · volumes in m³</p><table><thead><tr><th>Metric</th><th>Baseline</th><th>{{comparisonName}}</th><th>Δ</th></tr></thead><tbody><tr v-for="row in comparisonRows" :key="row.label"><th>{{row.label}}</th><td>{{row.baseline.toFixed(2)}}</td><td>{{row.variant.toFixed(2)}}</td><td>{{row.delta>=0?'+':''}}{{row.delta.toFixed(2)}}</td></tr></tbody></table><div class="comparison-chart"><small>SURFACE STORAGE OVER TIME · full timeline, shared scale<br>Green = baseline · Blue = variant · dashed line = selected time</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Surface water volume comparison"><polyline :points="chartPath(snapshots)" fill="none" stroke="#b1d9a4" stroke-width=".6"/><polyline :points="chartPath(comparisonFrames)" fill="none" stroke="#75bbfa" stroke-width=".6"/><line :x1="frameIndex/(snapshots.length-1)*100" :x2="frameIndex/(snapshots.length-1)*100" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0 → {{snapshots.at(-1).forecast.elapsed_seconds/60}} minutes · 0–{{chartMaximum.toFixed(0)}} m³</small></div><p>Baseline infiltration {{(snapshots[0].surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h · Variant {{(comparisonFrames[0].surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h</p></section><section v-if="outletSeries.length" class="comparison outlet-report"><div class="section-label">SURFACE OUTLET DISCHARGE</div><p>Orange markers identify configured boundary outlets. Discharged water leaves the modeled domain and remains in the mass budget.</p><div class="outlet-charts"><div class="comparison-chart"><small>CUMULATIVE DISCHARGE · m³</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Cumulative outlet discharge"><polyline :points="outletPath('cumulative_m3',outletVolumeMax)" fill="none" stroke="#ffb77a" stroke-width=".7"/><line :x1="outletTimePosition" :x2="outletTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{outletVolumeMax.toFixed(1)}} m³ · 0–{{visibleFrames.at(-1).forecast.elapsed_seconds/60}} min</small></div><div class="comparison-chart"><small>INTERVAL-MEAN DISCHARGE · m³/s</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Interval mean outlet discharge"><polyline :points="outletPath('mean_rate_m3_s',outletRateMax)" fill="none" stroke="#75bbfa" stroke-width=".7"/><line :x1="outletTimePosition" :x2="outletTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{outletRateMax.toFixed(2)}} m³/s · averages between snapshots</small></div></div><p>Selected time: discharged {{outletSeries[frameIndex].cumulative_m3.toFixed(2)}} m³ · interval mean {{outletSeries[frameIndex].mean_rate_m3_s===null?'no preceding interval':outletSeries[frameIndex].mean_rate_m3_s.toFixed(3)+' m³/s'}}</p></section><div class="metrics extra-metrics"><div><small>DOMAIN AREA</small><strong>{{metrics.area_km2.toLocaleString()}} <em>km²</em></strong></div><div><small>TEMPERATURE MIN / MEAN / MAX</small><strong class="date">{{metrics.temperature_min_c.toFixed(1)}} / {{metrics.temperature_mean_c.toFixed(1)}} / {{metrics.temperature_max_c.toFixed(1)}} °C</strong></div><div><small>WIND MEAN / MAX</small><strong class="date">{{metrics.wind_mean_m_s.toFixed(1)}} / {{metrics.wind_max_m_s.toFixed(1)}} m/s</strong></div><template v-if="data.atmosphere"><div><small>MEAN CLOUD FRACTION</small><strong>{{metrics.cloud_pct.toFixed(1)}} <em>%</em></strong></div><div><small>PRESSURE MIN / MAX</small><strong class="date">{{metrics.pressure_min_hpa.toFixed(1)}} / {{metrics.pressure_max_hpa.toFixed(1)}} hPa</strong></div><div><small>MEAN RELATIVE HUMIDITY</small><strong>{{metrics.humidity_mean_pct.toFixed(1)}} <em>%</em></strong></div></template><template v-if="data.rain"><div><small>RAINFALL COVERAGE</small><strong>{{metrics.rain_coverage_pct.toFixed(1)}} <em>%</em></strong></div><div><small>MEAN RAIN · COVERED CELLS</small><strong>{{metrics.rain_mean_mm?.toFixed(2)??'Unknown'}} <em>mm</em></strong></div></template><template v-if="data.surfaceWater"><div><small>WET AREA · DEPTH &gt; 0.1 MM</small><strong>{{metrics.wet_area_km2.toFixed(2)}} <em>km²</em></strong></div><div><small>MAX SURFACE WATER</small><strong>{{metrics.water_max_mm?.toFixed(2)}} <em>mm</em></strong></div><div><small>MEAN SOIL CAPACITY USED</small><strong>{{metrics.soil_fill_pct?.toFixed(1)??'Unknown'}} <em>%</em></strong></div></template></div><div v-if="precipitationMetrics" class="metrics extra-metrics"><div><small>PRECIPITATING CELLS</small><strong>{{precipitationMetrics.precipitating_pct.toFixed(0)}} <em>%</em></strong></div><div><small>RATE MEAN (WET) / MAX</small><strong class="date">{{precipitationMetrics.mean_rate_mm_h.toFixed(1)}} / {{precipitationMetrics.max_rate_mm_h.toFixed(1)}} mm/h</strong></div><div><small>INTERVAL ACCUMULATION MEAN / MAX</small><strong class="date">{{precipitationMetrics.mean_accumulation_mm.toFixed(1)}} / {{precipitationMetrics.max_accumulation_mm.toFixed(1)}} mm</strong></div><div><small>THUNDERSTORM / HAIL / SNOW CELLS</small><strong class="date">{{precipitationMetrics.lightning_cells}} / {{precipitationMetrics.hail_cells}} / {{precipitationMetrics.snow_cells}}<template v-if="precipitationMetrics.max_hail_cm>0"> · hail ≤ {{precipitationMetrics.max_hail_cm.toFixed(1)}} cm</template></strong></div><div><small>HAZARDS</small><strong class="date">{{Object.entries(precipitationMetrics.hazard_counts).map(([h,n])=>`${HAZARD_LABELS[h]} ${n}`).join(' · ')||'none'}}</strong></div><div><small>PRECIPITABLE WATER (MEAN)</small><strong>{{precipitationMetrics.mean_precipitable_water_mm.toFixed(1)}} <em>mm</em></strong></div></div><div v-if="cloudMetrics" class="metrics extra-metrics"><div><small>CLOUD COVER / CLOUDY CELLS</small><strong class="date">{{cloudMetrics.cloud_cover_pct.toFixed(0)}} % / {{cloudMetrics.cloudy_pct.toFixed(0)}} %</strong></div><div><small>THUNDERSTORM CELLS</small><strong>{{cloudMetrics.thunderstorm_cells}}</strong></div><div><small>FRONTS COLD / WARM / STATIONARY</small><strong class="date">{{cloudMetrics.cold_front_cells}} / {{cloudMetrics.warm_front_cells}} / {{cloudMetrics.stationary_front_cells}} <em>cells</em></strong></div><div><small>DOMINANT CLOUD</small><strong class="date">{{cloudTypeInfo(cloudMetrics.dominant_type).label}}</strong></div><div><small>MEAN BASE / TOP</small><strong class="date">{{cloudMetrics.mean_base_m===null?'—':cloudMetrics.mean_base_m.toFixed(0)+' / '+cloudMetrics.mean_top_m.toFixed(0)+' m'}}</strong></div><div><small>FRONTAL / CONVECTIVE CELLS · HEATING</small><strong class="date">{{cloudMetrics.frontal_cells}} / {{cloudMetrics.convective_cells}} · {{cloudMetrics.surface_heating_k.toFixed(1)}} K</strong></div></div><div class="metrics"><div><small>GRID RESOLUTION</small><strong>{{grid.nx}} × {{grid.ny}} <em>cells</em></strong></div><div><small>CELL SPACING</small><strong>{{grid.spacing_east_m}} × {{grid.spacing_north_m}} <em>m</em></strong></div><div><small>VALID TIME</small><strong class="date">{{time}}</strong></div></div></article></main>
  <p v-if="error" class="error" role="alert">{{error}}</p><footer><span>TerraFlux Physics / Research viewer v0.1</span><span>{{snapshots.length>1?'Snapshot playback':'Single snapshot'}} · {{data.terrain?'Visual terrain':'Flat surface'}} · No live solver</span></footer>
 </div>
</template>
