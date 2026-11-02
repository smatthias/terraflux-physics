<script setup>
import { ref, computed, watch, onMounted, onBeforeUnmount } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { demo, validateSequence } from './data.js'
import { regionalWeatherDemo, summarizeFrame } from './weatherSystems.js'
import { validateComparison, waterComparison, comparisonCsv, dischargeSeries } from './comparison.js'
const cloudsEnabled=ref(true), systemsEnabled=ref(true)
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
watch(data,()=>{if(!data.value.terrainTemperature)adjustedTemperature.value=false;if((layer.value==='rainfall'&&!data.value.rain)||(layer.value==='terrain'&&!data.value.terrain)||(['slope','aspect'].includes(layer.value)&&!data.value.terrainDerivatives)||(layer.value==='water'&&!data.value.surfaceWater)||(layer.value==='soil'&&!data.value.surfaceWater?.soil_values)||(['cloud','pressure','humidity'].includes(layer.value)&&!data.value.atmosphere))layer.value='temperature'})
const forecast=computed(()=>data.value.forecast), grid=computed(()=>forecast.value.grid)
function layerValues(d){
 if(layer.value==='cloud')return d.atmosphere?.cloud.map(v=>v*100)??[]
 if(layer.value==='pressure')return d.atmosphere?.pressure??[]
 if(layer.value==='humidity')return d.atmosphere?.humidity??[]
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
 if(['cloud','humidity'].includes(layer.value))return [0,100]
 if(layer.value==='aspect')return [0,360]
 const a=[...snapshots.value,...(comparisonFrames.value??[])].flatMap(layerValues).filter(v=>v!==null)
 return a.length?a.reduce(([lo,hi],v)=>[Math.min(lo,v),Math.max(hi,v)],[Infinity,-Infinity]):[0,0]
})
function hue(value){
 if(layer.value==='aspect')return value
 const norm=(value-range.value[0])/(range.value[1]-range.value[0]||1)
 if(layer.value==='cloud')return 210
 return ['rainfall','water','soil'].includes(layer.value)?212-norm*47:223-norm*212
}
const unit=computed(()=>['cloud','humidity'].includes(layer.value)?'%':layer.value==='pressure'?'hPa':layer.value==='temperature'?'°C':layer.value==='wind'?'m/s':layer.value==='terrain'?'m':['slope','aspect'].includes(layer.value)?'°':'mm')
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
  const color=value===null?new THREE.Color('#283b49'):new THREE.Color().setHSL(hue(value)/360,layer.value==='cloud'?.15:.70,layer.value==='cloud'?.18+norm*.72:layer.value==='aspect'?.50:.46+norm*.08)
  const mesh=new THREE.Mesh(geometry.clone(),new THREE.MeshStandardMaterial({color,roughness:.6,metalness:.12}))
  const h=surface(i)??(value===null?.10:.20+norm*2)
  mesh.scale.y=h;mesh.position.set(((i%g.nx)+.5-g.nx/2)*dx,h/2,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz); mesh.userData.index=i; group.add(mesh);cells.push(mesh)
 }
 geometry.dispose()
 if(vectors.value){ const stride=Math.max(1,Math.ceil(Math.sqrt(g.nx*g.ny/90)))
 for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x,u=forecast.value.fields.wind_east.values[i],v=forecast.value.fields.wind_north.values[i],speed=Math.hypot(u,v);if(speed<.01)continue; const arrow=new THREE.ArrowHelper(new THREE.Vector3(u,0,-v).normalize(),new THREE.Vector3((x+.5-g.nx/2)*dx,(surface(i)??2.4)+1,-(y+.5-g.ny/2)*dz),Math.min(2.5,.3+speed*.18),0xb7e8de,.3,.15);group.add(arrow)} }
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
watch([data,layer,vectors,terrainEnabled,exaggeration,adjustedTemperature,cloudsEnabled,systemsEnabled],draw)
onMounted(()=>{try{scene=new THREE.Scene();scene.background=new THREE.Color('#09151d');scene.fog=new THREE.Fog('#09151d',65,110);camera=new THREE.PerspectiveCamera(42,1,.1,200);renderer=new THREE.WebGLRenderer({antialias:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));host.value.appendChild(renderer.domElement);scene.add(new THREE.AmbientLight(0xd6efff,2));const sun=new THREE.DirectionalLight(0xffffff,3);sun.position.set(12,30,20);scene.add(sun);const floor=new THREE.GridHelper(60,30,0x28404b,0x142b36);floor.position.y=-.05;scene.add(floor);controls=new OrbitControls(camera,renderer.domElement);controls.enableDamping=true;controls.maxPolarAngle=Math.PI/2.05;controls.minDistance=8;controls.maxDistance=85;reset();renderer.domElement.addEventListener('click',pick);observer=new ResizeObserver(()=>{const {width,height}=host.value.getBoundingClientRect();renderer.setSize(width,height);camera.aspect=width/height;camera.updateProjectionMatrix()});observer.observe(host.value);draw();const tick=()=>{frame=requestAnimationFrame(tick);controls.update();renderer.render(scene,camera)};tick()}catch(err){fallback.value=true;error.value='WebGL unavailable in this browser. Showing an interactive 2D field instead.'}})
onBeforeUnmount(()=>{stop();cancelAnimationFrame(frame);observer?.disconnect();controls?.dispose();disposeGroup();scene?.traverse(o=>{o.geometry?.dispose();o.material?.dispose()});renderer?.dispose()})
</script>
<template>
 <div class="shell">
  <header><a class="brand" href="#"><span class="mark">T</span><span>TERRAFLUX<small>PHYSICS OBSERVATORY</small></span></a><span class="status"><i></i> LOCAL WORKSPACE</span><label class="upload">↥ Open JSON<input type="file" accept=".json,application/json" @change="upload"></label></header>
  <main><aside><div class="eyebrow">EXPERIMENT / 001</div><h1>Weather,<br>in perspective.</h1><p class="intro">Explore the fields behind the physics. Inspect temperature, wind and accumulated rainfall on a shared grid.</p>
   <section><div class="section-label">01 / FIELD LAYERS</div><button v-for="[key,title,description] in [['temperature','Air temperature','Dry transport · °C'],['wind','Wind speed','Prescribed field · m/s'],['rainfall','Rainfall accumulation','Observation-derived · mm'],['terrain','Terrain elevation','Surface geometry · m'],['slope','Terrain slope','Inclination · degrees'],['aspect','Downhill aspect','Bearing from north · degrees'],['water','Surface water depth','Closed-domain routing · mm'],['soil','Soil water storage','Infiltrated water · mm'],['cloud','Cloud cover','Synthetic fraction · %'],['pressure','Sea-level pressure','Synthetic regional field · hPa'],['humidity','Relative humidity','Synthetic regional field · %']]" :key="key" class="layer" :class="{active:layer===key}" :disabled="(key==='rainfall'&&!data.rain)||(key==='terrain'&&!data.terrain)||(['slope','aspect'].includes(key)&&!data.terrainDerivatives)||(key==='water'&&!data.surfaceWater)||(key==='soil'&&!data.surfaceWater?.soil_values)||(['cloud','pressure','humidity'].includes(key)&&!data.atmosphere)" @click="layer=key"><span>{{title}}<small>{{data.atmosphere&&key==='temperature'?'Synthetic regional field · °C':description}}</small></span><span>{{layer===key?'●':'○'}}</span></button><template v-if="data.atmosphere"><label class="toggle"><input type="checkbox" v-model="cloudsEnabled"> Cloud overlay</label><label class="toggle"><input type="checkbox" v-model="systemsEnabled"> Weather-system markers</label><p class="hint">Synthetic pressure, clouds and front. Cloud height is schematic; these fields do not drive the local physics.</p></template><label class="toggle"><input type="checkbox" v-model="vectors"> Wind direction vectors</label><template v-if="data.terrain"><label class="toggle"><input type="checkbox" v-model="terrainEnabled"> Place fields on terrain</label><label class="toggle">Height scale {{exaggeration}}× <input aria-label="Terrain vertical exaggeration" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label><label v-if="data.terrainTemperature" class="toggle"><input type="checkbox" v-model="adjustedTemperature"> Terrain-adjusted temperature</label><p v-if="data.terrainTemperature" class="hint">Lapse rate {{data.terrainTemperature.lapse_rate_k_per_m*1000}} K/km · reference {{data.terrainTemperature.reference_elevation_m}} m<br>Diagnostic adjustment; no feedback into transport.</p><p class="hint">{{data.terrain.source_id}} · {{data.terrain.vertical_reference}}<br>Geometry is a display layer; lapse-rate adjustment is optional.</p></template></section>
   <section><div class="section-label">02 / DATA SOURCE</div><strong class="source">{{source}}</strong><p class="hint">{{source==='Synthetic demonstration'?'Illustrative fields only. These values are not engine output or live observations.':source==='Regional weather · synthetic'?'Analytic 1,200 × 900 km weather illustration. Low/high pressure, a moving front and cloud bands; not solver output.':source==='Engine output · synthetic inputs'?'C++ dry transport + radar rain estimation + accumulation + gridding. Synthetic inputs; not live weather.':'Loaded in this browser only. No upload service or database.'}}</p><button class="example-button" :disabled="loading" @click="loadEngineExample">{{loading?'Loading…':'→ Load engine example'}}</button><button class="example-button" :disabled="loading" @click="loadOutletExample">↘ Load outlet scenario</button><button class="example-button" :disabled="loading" @click="loadComparisonDemo">⇄ Compare hydrology scenarios</button><label v-if="data.surfaceWater" class="compare-upload">Open comparison JSON<input type="file" accept=".json,application/json" @change="uploadComparison"></label><button class="example-button" @click="useRegionalDemo">☁ Regional weather demo</button><button class="text-button" @click="useDemo">↻ Load synthetic demo</button></section>
   <section class="inspection"><div class="section-label">03 / CELL INSPECTOR</div><template v-if="chosen"><h2>{{values[chosen.i]===null?(layer==='aspect'?'Undefined (flat)':'No data'):values[chosen.i]?.toFixed(2)}} <small>{{unit}}</small></h2><p>Column {{chosen.x}} · Row {{chosen.y}} · Index {{chosen.i}}</p><p>East {{(grid.origin_east_m+(chosen.x+.5)*grid.spacing_east_m).toFixed(0)}} m · North {{(grid.origin_north_m+(chosen.y+.5)*grid.spacing_north_m).toFixed(0)}} m</p><p v-if="data.atmosphere">Cloud {{(data.atmosphere.cloud[chosen.i]*100).toFixed(1)}}% · Pressure {{data.atmosphere.pressure[chosen.i].toFixed(1)}} hPa<br>Relative humidity {{data.atmosphere.humidity[chosen.i].toFixed(1)}}%</p><p v-if="data.terrainTemperature && layer==='temperature'">Reference temperature {{(forecast.fields.air_temperature.values[chosen.i]-273.15).toFixed(2)}} °C<br>Terrain correction {{(data.terrainTemperature.values[chosen.i]-forecast.fields.air_temperature.values[chosen.i]).toFixed(2)}} K</p><p v-if="data.terrainDerivatives">Slope {{data.terrainDerivatives.slope_deg[chosen.i].toFixed(2)}}° · Aspect {{data.terrainDerivatives.aspect_deg[chosen.i]===null?'flat / undefined':data.terrainDerivatives.aspect_deg[chosen.i].toFixed(1)+'°'}}</p><p v-if="data.terrain">Elevation {{data.terrain.values[chosen.i].toFixed(2)}} m · {{data.terrain.vertical_reference}}</p><p v-if="layer==='rainfall'">Quality: {{data.rain.quality[chosen.i]}}</p></template><p v-else class="hint">Select a cell in the scene to inspect its value and local coordinates.</p></section>
  </aside><article><div class="scene-head"><div><span class="eyebrow">FIELD EXPLORER</span><h2>{{layer==='temperature'?(adjustedTemperature?'Terrain-adjusted temperature':'Air temperature'):layer==='wind'?'Wind speed':layer==='terrain'?'Terrain elevation':layer==='slope'?'Terrain slope':layer==='aspect'?'Downhill aspect':layer==='water'?'Surface water depth':layer==='soil'?'Soil water storage':layer==='cloud'?'Cloud cover':layer==='pressure'?'Sea-level pressure':layer==='humidity'?'Relative humidity':'Rainfall accumulation'}}</h2></div><button class="reset" @click="reset">↺ Reset view</button></div><div class="viewport" ref="host" aria-label="Interactive 3D weather grid"><div v-if="fallback" class="fallback-grid" :style="{gridTemplateColumns:`repeat(${grid.nx},1fr)`}"><button v-for="(value,i) in values" :key="i" :aria-label="`Cell ${i}: ${value===null?'no data':value.toFixed(2)} ${unit}`" :style="{gridRow:grid.ny-Math.floor(i/grid.nx),gridColumn:i%grid.nx+1,background:value===null?'#283b49':`hsl(${hue(value)} ${layer==='cloud'?15:70}% ${layer==='cloud'?18+value*.72:50}%)`}" @click="selected=i"></button></div><svg v-if="fallback && data.atmosphere" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Synthetic cloud and weather-system overlay"><template v-if="cloudsEnabled"><ellipse v-for="(c,i) in data.atmosphere.cloud" :key="'cloud'+i" v-show="i%3===0 && c>.35" :cx="((i%grid.nx)+.5)/grid.nx*100" :cy="(1-(Math.floor(i/grid.nx)+.5)/grid.ny)*100" :rx="2.1" :ry="1.7" fill="#e9f5ff" :opacity="c*.32"/></template><template v-if="systemsEnabled"><template v-for="(s,i) in data.atmosphere.systems" :key="i"><polyline v-if="s.kind==='cold_front'" :points="s.points.map(([u,v])=>`${u*100},${(1-v)*100}`).join(' ')" fill="none" stroke="#85ccff" stroke-width=".7" stroke-dasharray="2 1"/><text v-else :x="s.u*100" :y="(1-s.v)*100" :fill="s.kind==='low'?'#ffb39c':'#c5e9ff'" font-size="7" font-weight="bold" text-anchor="middle">{{s.kind==='low'?'L':'H'}}</text></template></template></svg><svg v-if="fallback && data.surfaceWater?.outlets?.length" class="systems-map" viewBox="0 0 100 100" preserveAspectRatio="none" aria-label="Configured surface outlet locations"><circle v-for="(o,i) in data.surfaceWater.outlets" :key="i" :cx="(o.x+.5)/grid.nx*100" :cy="(1-(o.y+.5)/grid.ny)*100" r="1.6" fill="#ffa15c" stroke="#fff2e6" stroke-width=".4"/></svg><div class="scene-note">{{fallback?'2D FALLBACK / NORTH UP':'NORTH −Z / EAST +X'}}<br><span>{{fallback?'Color encodes field value · select a cell':data.terrain&&(terrainEnabled||layer==='terrain')?'Height: relative terrain · color: selected field':'Height encodes field value · flat reference surface'}}</span></div><div class="legend"><span>{{range[0].toFixed(1)}} {{unit}}</span><div :style="layer==='aspect'?{background:'linear-gradient(90deg,red,yellow,lime,cyan,blue,magenta,red)'}:['rainfall','water','soil'].includes(layer)?{background:'linear-gradient(90deg,#2675cc,#26ccb2)'}:{}"></div><span>{{range[1].toFixed(1)}} {{unit}}</span></div><div v-if="layer==='aspect'" class="aspect-key">0° N · 90° E · 180° S · 270° W · gray = flat</div><div class="gestures">{{fallback?'SELECT A CELL TO INSPECT':'DRAG TO ORBIT · SCROLL TO ZOOM · CLICK TO INSPECT'}}</div></div><div v-if="snapshots.length>1" class="timeline"><div class="playback"><button @click="seek(frameIndex-1)" :disabled="frameIndex===0" aria-label="Previous frame">‹</button><button @click="togglePlayback">{{playing?'Pause':'Play'}}</button><button @click="seek(frameIndex+1)" :disabled="frameIndex===snapshots.length-1" aria-label="Next frame">›</button><span>Frame {{frameIndex+1}} / {{snapshots.length}} · +{{forecast.elapsed_seconds/60}} min · {{rainfallPhase}}</span></div><input aria-label="Timeline frame" type="range" min="0" :max="snapshots.length-1" :value="frameIndex" @input="seek($event.target.value)"><p>{{data.atmosphere?'Synthetic regional snapshots · fixed color scale · no coupling to local engine physics':'Discrete solver snapshots · fixed color scale · rainfall accumulates from the start · unavailable at time zero'}}</p></div><p v-if="layer==='rainfall' && data.rain" class="rain-interval">Accumulation interval: {{data.rain.period_start_utc.replace('T',' ').replace('Z',' UTC')}} → {{data.rain.valid_time_utc.replace('T',' ').replace('Z',' UTC')}}</p><p v-if="['water','soil'].includes(layer) && data.surfaceWater" class="rain-interval">Surface {{data.surfaceWater.stored_volume_m3.toFixed(2)}} m³ · Soil {{data.surfaceWater.soil_volume_m3.toFixed(2)}} m³ · Rain input {{data.surfaceWater.cumulative_rainfall_volume_m3.toFixed(2)}} m³ · Evaporated {{data.surfaceWater.evaporated_volume_m3.toFixed(2)}} m³ · Drained {{data.surfaceWater.drained_volume_m3.toFixed(2)}} m³ · Discharged {{data.surfaceWater.discharged_volume_m3.toFixed(2)}} m³ · Balance error {{data.surfaceWater.balance_error_m3.toExponential(2)}} m³<br>{{data.surfaceWater.boundary==='configured_outlets'?'Configured surface outlets':'Closed boundaries'}} · unknown rainfall cells are impermeable masks · prototype routing<br>Step {{data.surfaceWater.parameters.timestep_seconds}} s · Infiltration {{(data.surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h · Evaporation {{(data.surfaceWater.parameters.evaporation_rate_m_s*3600000).toFixed(2)}} mm/h · Drainage {{(data.surfaceWater.parameters.drainage_rate_m_s*3600000).toFixed(2)}} mm/h<span v-if="data.surfaceWater.soil_capacity_m!==null"> · soil capacity {{data.surfaceWater.soil_capacity_m*1000}} mm</span></p><section v-if="comparisonFrames" class="comparison"><div class="section-label">SCENARIO COMPARISON · SAME FORCING / TIME</div><div class="comparison-switch"><button @click="exportComparison">Export all frames (CSV)</button><button :class="{selected:comparisonView==='baseline'}" @click="comparisonView='baseline'">Show baseline</button><button :class="{selected:comparisonView==='variant'}" @click="comparisonView='variant'">Show {{comparisonName}}</button></div><p>Viewing {{comparisonView==='baseline'?'baseline':comparisonName}} · Δ = variant − baseline · volumes in m³</p><table><thead><tr><th>Metric</th><th>Baseline</th><th>{{comparisonName}}</th><th>Δ</th></tr></thead><tbody><tr v-for="row in comparisonRows" :key="row.label"><th>{{row.label}}</th><td>{{row.baseline.toFixed(2)}}</td><td>{{row.variant.toFixed(2)}}</td><td>{{row.delta>=0?'+':''}}{{row.delta.toFixed(2)}}</td></tr></tbody></table><div class="comparison-chart"><small>SURFACE STORAGE OVER TIME · full timeline, shared scale<br>Green = baseline · Blue = variant · dashed line = selected time</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Surface water volume comparison"><polyline :points="chartPath(snapshots)" fill="none" stroke="#b1d9a4" stroke-width=".6"/><polyline :points="chartPath(comparisonFrames)" fill="none" stroke="#75bbfa" stroke-width=".6"/><line :x1="frameIndex/(snapshots.length-1)*100" :x2="frameIndex/(snapshots.length-1)*100" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0 → {{snapshots.at(-1).forecast.elapsed_seconds/60}} minutes · 0–{{chartMaximum.toFixed(0)}} m³</small></div><p>Baseline infiltration {{(snapshots[0].surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h · Variant {{(comparisonFrames[0].surfaceWater.parameters.infiltration_rate_m_s*3600000).toFixed(2)}} mm/h</p></section><section v-if="outletSeries.length" class="comparison outlet-report"><div class="section-label">SURFACE OUTLET DISCHARGE</div><p>Orange markers identify configured boundary outlets. Discharged water leaves the modeled domain and remains in the mass budget.</p><div class="outlet-charts"><div class="comparison-chart"><small>CUMULATIVE DISCHARGE · m³</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Cumulative outlet discharge"><polyline :points="outletPath('cumulative_m3',outletVolumeMax)" fill="none" stroke="#ffb77a" stroke-width=".7"/><line :x1="outletTimePosition" :x2="outletTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{outletVolumeMax.toFixed(1)}} m³ · 0–{{visibleFrames.at(-1).forecast.elapsed_seconds/60}} min</small></div><div class="comparison-chart"><small>INTERVAL-MEAN DISCHARGE · m³/s</small><svg viewBox="0 0 100 50" preserveAspectRatio="none" aria-label="Interval mean outlet discharge"><polyline :points="outletPath('mean_rate_m3_s',outletRateMax)" fill="none" stroke="#75bbfa" stroke-width=".7"/><line :x1="outletTimePosition" :x2="outletTimePosition" y1="0" y2="50" stroke="#93a5af" stroke-dasharray="1 1" stroke-width=".3"/></svg><small>0–{{outletRateMax.toFixed(2)}} m³/s · averages between snapshots</small></div></div><p>Selected time: discharged {{outletSeries[frameIndex].cumulative_m3.toFixed(2)}} m³ · interval mean {{outletSeries[frameIndex].mean_rate_m3_s===null?'no preceding interval':outletSeries[frameIndex].mean_rate_m3_s.toFixed(3)+' m³/s'}}</p></section><div class="metrics extra-metrics"><div><small>DOMAIN AREA</small><strong>{{metrics.area_km2.toLocaleString()}} <em>km²</em></strong></div><div><small>TEMPERATURE MIN / MEAN / MAX</small><strong class="date">{{metrics.temperature_min_c.toFixed(1)}} / {{metrics.temperature_mean_c.toFixed(1)}} / {{metrics.temperature_max_c.toFixed(1)}} °C</strong></div><div><small>WIND MEAN / MAX</small><strong class="date">{{metrics.wind_mean_m_s.toFixed(1)}} / {{metrics.wind_max_m_s.toFixed(1)}} m/s</strong></div><template v-if="data.atmosphere"><div><small>MEAN CLOUD FRACTION</small><strong>{{metrics.cloud_pct.toFixed(1)}} <em>%</em></strong></div><div><small>PRESSURE MIN / MAX</small><strong class="date">{{metrics.pressure_min_hpa.toFixed(1)}} / {{metrics.pressure_max_hpa.toFixed(1)}} hPa</strong></div><div><small>MEAN RELATIVE HUMIDITY</small><strong>{{metrics.humidity_mean_pct.toFixed(1)}} <em>%</em></strong></div></template><template v-if="data.rain"><div><small>RAINFALL COVERAGE</small><strong>{{metrics.rain_coverage_pct.toFixed(1)}} <em>%</em></strong></div><div><small>MEAN RAIN · COVERED CELLS</small><strong>{{metrics.rain_mean_mm?.toFixed(2)??'Unknown'}} <em>mm</em></strong></div></template><template v-if="data.surfaceWater"><div><small>WET AREA · DEPTH &gt; 0.1 MM</small><strong>{{metrics.wet_area_km2.toFixed(2)}} <em>km²</em></strong></div><div><small>MAX SURFACE WATER</small><strong>{{metrics.water_max_mm?.toFixed(2)}} <em>mm</em></strong></div><div><small>MEAN SOIL CAPACITY USED</small><strong>{{metrics.soil_fill_pct?.toFixed(1)??'Unknown'}} <em>%</em></strong></div></template></div><div class="metrics"><div><small>GRID RESOLUTION</small><strong>{{grid.nx}} × {{grid.ny}} <em>cells</em></strong></div><div><small>CELL SPACING</small><strong>{{grid.spacing_east_m}} × {{grid.spacing_north_m}} <em>m</em></strong></div><div><small>VALID TIME</small><strong class="date">{{time}}</strong></div></div></article></main>
  <p v-if="error" class="error" role="alert">{{error}}</p><footer><span>TerraFlux Physics / Research viewer v0.1</span><span>{{snapshots.length>1?'Snapshot playback':'Single snapshot'}} · {{data.terrain?'Visual terrain':'Flat surface'}} · No live solver</span></footer>
 </div>
</template>
