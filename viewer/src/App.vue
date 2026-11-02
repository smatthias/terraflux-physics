<script setup>
import { ref, computed, watch, onMounted, onBeforeUnmount } from 'vue'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { demo, validateSequence } from './data.js'
const adjustedTemperature=ref(false)
const temperatureValues=d=>adjustedTemperature.value&&d.terrainTemperature?d.terrainTemperature.values:d.forecast.fields.air_temperature.values
const loading=ref(false), terrainEnabled=ref(true), exaggeration=ref(2)
const snapshots=ref(validateSequence(demo())), frameIndex=ref(0), playing=ref(false)
const data=computed(()=>snapshots.value[frameIndex.value])
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
function loadFrames(input){const parsed=validateSequence(input);stop();snapshots.value=parsed;frameIndex.value=0;selected.value=null}
watch(data,()=>{if(!data.value.terrainTemperature)adjustedTemperature.value=false;if((layer.value==='rainfall'&&!data.value.rain)||(layer.value==='terrain'&&!data.value.terrain))layer.value='temperature'})
const forecast=computed(()=>data.value.forecast), grid=computed(()=>forecast.value.grid)
const values=computed(()=>layer.value==='temperature'?temperatureValues(data.value).map(v=>v-273.15):layer.value==='wind'?forecast.value.fields.wind_east.values.map((u,i)=>Math.hypot(u,forecast.value.fields.wind_north.values[i])):layer.value==='terrain'?data.value.terrain?.values??[]:data.value.rain?.values ?? [])
const range=computed(()=> { const a=snapshots.value.flatMap(d=>layer.value==='temperature'?temperatureValues(d).map(v=>v-273.15):layer.value==='wind'?d.forecast.fields.wind_east.values.map((u,i)=>Math.hypot(u,d.forecast.fields.wind_north.values[i])):layer.value==='terrain'?d.terrain?.values??[]:d.rain?.values??[]).filter(v=>v!==null); return a.length?a.reduce(([lo,hi],v)=>[Math.min(lo,v),Math.max(hi,v)],[Infinity,-Infinity]):[0,0] })
const unit=computed(()=>layer.value==='temperature'?'°C':layer.value==='wind'?'m/s':layer.value==='terrain'?'m':'mm')
const time=computed(()=>new Date(Date.parse(forecast.value.start_time_utc)+forecast.value.elapsed_seconds*1000).toISOString().replace('T',' ').replace('.000Z',' UTC'))
const chosen=computed(()=>selected.value===null?null:{i:selected.value,x:selected.value%grid.value.nx,y:Math.floor(selected.value/grid.value.nx)})
function disposeGroup(){ if(!group)return; group.traverse(o=>{o.geometry?.dispose(); if(o.material) o.material.dispose()});scene.remove(group) }
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
  const color=value===null?new THREE.Color('#283b49'):new THREE.Color().setHSL(layer.value==='rainfall'?.59-norm*.13:.62-norm*.59,.70,.46+norm*.08)
  const mesh=new THREE.Mesh(geometry.clone(),new THREE.MeshStandardMaterial({color,roughness:.6,metalness:.12}))
  const h=surface(i)??(value===null?.10:.20+norm*2)
  mesh.scale.y=h;mesh.position.set(((i%g.nx)+.5-g.nx/2)*dx,h/2,-(Math.floor(i/g.nx)+.5-g.ny/2)*dz); mesh.userData.index=i; group.add(mesh);cells.push(mesh)
 }
 geometry.dispose()
 if(vectors.value){ const stride=Math.max(1,Math.ceil(Math.sqrt(g.nx*g.ny/90)))
 for(let y=0;y<g.ny;y+=stride)for(let x=0;x<g.nx;x+=stride){const i=y*g.nx+x,u=forecast.value.fields.wind_east.values[i],v=forecast.value.fields.wind_north.values[i],speed=Math.hypot(u,v);if(speed<.01)continue; const arrow=new THREE.ArrowHelper(new THREE.Vector3(u,0,-v).normalize(),new THREE.Vector3((x+.5-g.nx/2)*dx,(surface(i)??2.4)+1,-(y+.5-g.ny/2)*dz),Math.min(2.5,.3+speed*.18),0xb7e8de,.3,.15);group.add(arrow)} }
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
watch([data,layer,vectors,terrainEnabled,exaggeration,adjustedTemperature],draw)
onMounted(()=>{try{scene=new THREE.Scene();scene.background=new THREE.Color('#09151d');scene.fog=new THREE.Fog('#09151d',65,110);camera=new THREE.PerspectiveCamera(42,1,.1,200);renderer=new THREE.WebGLRenderer({antialias:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));host.value.appendChild(renderer.domElement);scene.add(new THREE.AmbientLight(0xd6efff,2));const sun=new THREE.DirectionalLight(0xffffff,3);sun.position.set(12,30,20);scene.add(sun);const floor=new THREE.GridHelper(60,30,0x28404b,0x142b36);floor.position.y=-.05;scene.add(floor);controls=new OrbitControls(camera,renderer.domElement);controls.enableDamping=true;controls.maxPolarAngle=Math.PI/2.05;controls.minDistance=8;controls.maxDistance=85;reset();renderer.domElement.addEventListener('click',pick);observer=new ResizeObserver(()=>{const {width,height}=host.value.getBoundingClientRect();renderer.setSize(width,height);camera.aspect=width/height;camera.updateProjectionMatrix()});observer.observe(host.value);draw();const tick=()=>{frame=requestAnimationFrame(tick);controls.update();renderer.render(scene,camera)};tick()}catch(err){fallback.value=true;error.value='WebGL unavailable in this browser. Showing an interactive 2D field instead.'}})
onBeforeUnmount(()=>{stop();cancelAnimationFrame(frame);observer?.disconnect();controls?.dispose();disposeGroup();scene?.traverse(o=>{o.geometry?.dispose();o.material?.dispose()});renderer?.dispose()})
</script>
<template>
 <div class="shell">
  <header><a class="brand" href="#"><span class="mark">T</span><span>TERRAFLUX<small>PHYSICS OBSERVATORY</small></span></a><span class="status"><i></i> LOCAL WORKSPACE</span><label class="upload">↥ Open JSON<input type="file" accept=".json,application/json" @change="upload"></label></header>
  <main><aside><div class="eyebrow">EXPERIMENT / 001</div><h1>Weather,<br>in perspective.</h1><p class="intro">Explore the fields behind the physics. Inspect temperature, wind and accumulated rainfall on a shared grid.</p>
   <section><div class="section-label">01 / FIELD LAYERS</div><button v-for="[key,title,description] in [['temperature','Air temperature','Dry transport · °C'],['wind','Wind speed','Prescribed field · m/s'],['rainfall','Rainfall accumulation','Observation-derived · mm'],['terrain','Terrain elevation','Surface geometry · m']]" :key="key" class="layer" :class="{active:layer===key}" :disabled="(key==='rainfall'&&!data.rain)||(key==='terrain'&&!data.terrain)" @click="layer=key"><span>{{title}}<small>{{description}}</small></span><span>{{layer===key?'●':'○'}}</span></button><label class="toggle"><input type="checkbox" v-model="vectors"> Wind direction vectors</label><template v-if="data.terrain"><label class="toggle"><input type="checkbox" v-model="terrainEnabled"> Place fields on terrain</label><label class="toggle">Height scale {{exaggeration}}× <input aria-label="Terrain vertical exaggeration" type="range" min="1" max="5" step="0.5" v-model="exaggeration"></label><label v-if="data.terrainTemperature" class="toggle"><input type="checkbox" v-model="adjustedTemperature"> Terrain-adjusted temperature</label><p v-if="data.terrainTemperature" class="hint">Lapse rate {{data.terrainTemperature.lapse_rate_k_per_m*1000}} K/km · reference {{data.terrainTemperature.reference_elevation_m}} m<br>Diagnostic adjustment; no feedback into transport.</p><p class="hint">{{data.terrain.source_id}} · {{data.terrain.vertical_reference}}<br>Geometry is a display layer; lapse-rate adjustment is optional.</p></template></section>
   <section><div class="section-label">02 / DATA SOURCE</div><strong class="source">{{source}}</strong><p class="hint">{{source==='Synthetic demonstration'?'Illustrative fields only. These values are not engine output or live observations.':source==='Engine output · synthetic inputs'?'C++ dry transport + radar rain estimation + accumulation + gridding. Synthetic inputs; not live weather.':'Loaded in this browser only. No upload service or database.'}}</p><button class="example-button" :disabled="loading" @click="loadEngineExample">{{loading?'Loading…':'→ Load engine example'}}</button><button class="text-button" @click="useDemo">↻ Load synthetic demo</button></section>
   <section class="inspection"><div class="section-label">03 / CELL INSPECTOR</div><template v-if="chosen"><h2>{{values[chosen.i]===null?'No data':values[chosen.i]?.toFixed(2)}} <small>{{unit}}</small></h2><p>Column {{chosen.x}} · Row {{chosen.y}} · Index {{chosen.i}}</p><p>East {{(grid.origin_east_m+(chosen.x+.5)*grid.spacing_east_m).toFixed(0)}} m · North {{(grid.origin_north_m+(chosen.y+.5)*grid.spacing_north_m).toFixed(0)}} m</p><p v-if="data.terrainTemperature && layer==='temperature'">Reference temperature {{(forecast.fields.air_temperature.values[chosen.i]-273.15).toFixed(2)}} °C<br>Terrain correction {{(data.terrainTemperature.values[chosen.i]-forecast.fields.air_temperature.values[chosen.i]).toFixed(2)}} K</p><p v-if="data.terrain">Elevation {{data.terrain.values[chosen.i].toFixed(2)}} m · {{data.terrain.vertical_reference}}</p><p v-if="layer==='rainfall'">Quality: {{data.rain.quality[chosen.i]}}</p></template><p v-else class="hint">Select a cell in the scene to inspect its value and local coordinates.</p></section>
  </aside><article><div class="scene-head"><div><span class="eyebrow">FIELD EXPLORER</span><h2>{{layer==='temperature'?(adjustedTemperature?'Terrain-adjusted temperature':'Air temperature'):layer==='wind'?'Wind speed':layer==='terrain'?'Terrain elevation':'Rainfall accumulation'}}</h2></div><button class="reset" @click="reset">↺ Reset view</button></div><div class="viewport" ref="host" aria-label="Interactive 3D weather grid"><div v-if="fallback" class="fallback-grid" :style="{gridTemplateColumns:`repeat(${grid.nx},1fr)`}"><button v-for="(value,i) in values" :key="i" :aria-label="`Cell ${i}: ${value===null?'no data':value.toFixed(2)} ${unit}`" :style="{gridRow:grid.ny-Math.floor(i/grid.nx),gridColumn:i%grid.nx+1,background:value===null?'#283b49':`hsl(${layer==='rainfall'?212-((value-range[0])/(range[1]-range[0]||1))*47:223-((value-range[0])/(range[1]-range[0]||1))*212} 70% 50%)`}" @click="selected=i"></button></div><div class="scene-note">{{fallback?'2D FALLBACK / NORTH UP':'NORTH −Z / EAST +X'}}<br><span>{{fallback?'Color encodes field value · select a cell':data.terrain&&(terrainEnabled||layer==='terrain')?'Height: relative terrain · color: selected field':'Height encodes field value · flat reference surface'}}</span></div><div class="legend"><span>{{range[0].toFixed(1)}} {{unit}}</span><div :style="layer==='rainfall'?{background:'linear-gradient(90deg,#2675cc,#26ccb2)'}:{}"></div><span>{{range[1].toFixed(1)}} {{unit}}</span></div><div class="gestures">{{fallback?'SELECT A CELL TO INSPECT':'DRAG TO ORBIT · SCROLL TO ZOOM · CLICK TO INSPECT'}}</div></div><div v-if="snapshots.length>1" class="timeline"><div class="playback"><button @click="seek(frameIndex-1)" :disabled="frameIndex===0" aria-label="Previous frame">‹</button><button @click="togglePlayback">{{playing?'Pause':'Play'}}</button><button @click="seek(frameIndex+1)" :disabled="frameIndex===snapshots.length-1" aria-label="Next frame">›</button><span>Frame {{frameIndex+1}} / {{snapshots.length}} · +{{forecast.elapsed_seconds/60}} min</span></div><input aria-label="Timeline frame" type="range" min="0" :max="snapshots.length-1" :value="frameIndex" @input="seek($event.target.value)"><p>Discrete solver snapshots · fixed color scale · rainfall accumulates from the start · unavailable at time zero</p></div><p v-if="layer==='rainfall' && data.rain" class="rain-interval">Accumulation interval: {{data.rain.period_start_utc.replace('T',' ').replace('Z',' UTC')}} → {{data.rain.valid_time_utc.replace('T',' ').replace('Z',' UTC')}}</p><div class="metrics"><div><small>GRID RESOLUTION</small><strong>{{grid.nx}} × {{grid.ny}} <em>cells</em></strong></div><div><small>CELL SPACING</small><strong>{{grid.spacing_east_m}} × {{grid.spacing_north_m}} <em>m</em></strong></div><div><small>VALID TIME</small><strong class="date">{{time}}</strong></div></div></article></main>
  <p v-if="error" class="error" role="alert">{{error}}</p><footer><span>TerraFlux Physics / Research viewer v0.1</span><span>{{snapshots.length>1?'Snapshot playback':'Single snapshot'}} · {{data.terrain?'Visual terrain':'Flat surface'}} · No live solver</span></footer>
 </div>
</template>
