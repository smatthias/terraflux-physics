<script setup>
// Machining plan, CAM settings (per design), tools (per machining layer) and exports.
import {computed} from 'vue';
import {CAM_DEFAULTS,TOOL_DEFAULTS,POSTS} from '../shared/design-cam.mjs';
const props=defineProps({plan:{type:Object,required:true},cam:{type:Object,default:null},design:{type:Object,required:true}});
const emit=defineEmits(['export','select','dxf','gcode','gcode-all','update-cam','update-tool','update-stock']);
const stock=computed(()=>({stockLengthMm:2440,stockWidthMm:1220,stockTrimMm:0,stockKerfMm:3,stockAllowRotation:true,...props.design.stock}));
const settings=computed(()=>({...CAM_DEFAULTS,...props.design.cam}));
const machining=computed(()=>props.design.layers.filter(l=>l.operation!=='none'));
const TOOL_FIELDS=[['diameter','Ø mm',.1],['feed','Feed',10],['plunge','Plunge',10],['rpm','rpm',500],['stepdown','Step mm',.5]];
// Empty fields clear the setting, falling back to the default for the operation.
const value=event=>event.target.value===''?null:Number(event.target.value);
const issues=computed(()=>[...(props.plan.issues??[]),...(props.cam?.issues??[]).filter(i=>i.code==='TOOL_TOO_LARGE'||i.code==='TABS')]);
const set=(key,v)=>emit('update-cam',{[key]:v});
const ready=computed(()=>props.plan.ready&&!!props.cam?.ready);
</script>
<template>
  <section class="design-cnc" aria-label="CNC plan">
    <p class="eyebrow">CNC PLAN · CAM</p>
    <p v-if="plan.error" class="hint">{{ plan.error }}</p>
    <template v-else>
      <p data-testid="cnc-status" role="status" :class="ready?'cnc-ready':'cnc-blocked'">{{ ready ? 'Ready for CAM' : 'Resolve errors before CAM' }} · {{ plan.parts.length }} sheet parts · {{ plan.parts.reduce((n,p)=>n+p.operations.length,0) }} operations</p>
      <table class="cnc-layers"><thead><tr><th>#</th><th>Layer</th><th>Operation</th><th>Ops</th><th>Max depth</th></tr></thead>
        <tbody><tr v-for="layer in plan.layers" :key="layer.id"><td>{{ layer.order }}</td><td>{{ layer.name }}</td><td>{{ layer.operation }}</td><td>{{ layer.operations }}</td><td>{{ layer.maxDepthMm }} mm</td></tr></tbody></table>
      <ul v-if="issues.length" class="cnc-issues" data-testid="cnc-issues">
        <li v-for="(issue,n) in issues" :key="n" :class="issue.severity"><button @click="emit('select',issue)">{{ issue.severity }} · {{ issue.message }}</button></li>
      </ul>
      <details class="cam-settings">
        <summary>CAM settings and tools</summary>
        <div class="mm-grid">
          <label>Post-processor<select :value="settings.post" aria-label="Post-processor" @change="set('post',$event.target.value)"><option v-for="(post,id) in POSTS" :key="id" :value="id">{{ post.label }}</option></select></label>
          <label>Units<select :value="settings.units" aria-label="Program units" @change="set('units',$event.target.value)"><option value="mm">mm (G21)</option><option value="inch">inch (G20)</option></select></label>
          <label>Arcs<select :value="settings.arcs" aria-label="Arcs" @change="set('arcs',$event.target.value)"><option value="native">Arc moves</option><option value="lines">Short lines</option></select></label>
          <label>Spindle wait s<input type="number" min="0" max="60" step="0.5" :value="settings.spinUpS" aria-label="Spindle start wait" @change="set('spinUpS',value($event))"></label>
          <label class="cam-check"><input type="checkbox" :checked="settings.lineNumbers" aria-label="Line numbers" @change="set('lineNumbers',$event.target.checked)">Line numbers</label>
          <label v-if="settings.post==='mach'" class="cam-check"><input type="checkbox" :checked="settings.dwellMs" aria-label="Mach dwell in milliseconds" @change="set('dwellMs',$event.target.checked)">G4 in ms</label>
          <label v-if="settings.post==='fanuc'">Program O<input type="number" min="1" max="9999" step="1" :value="settings.programNumber" aria-label="Program number" @change="set('programNumber',value($event))"></label>
          <label>Tabs per part<input type="number" min="0" max="16" step="1" :value="settings.tabs" aria-label="Tabs per part" @change="set('tabs',value($event))"></label>
          <label>Tab width mm<input type="number" min="1" max="50" step="1" :value="settings.tabWidth" aria-label="Tab width" @change="set('tabWidth',value($event))"></label>
          <label>Tab height mm<input type="number" min="0.2" max="20" step="0.5" :value="settings.tabHeight" aria-label="Tab height" @change="set('tabHeight',value($event))"></label>
          <label>Cutting<select :value="settings.direction" aria-label="Cutting direction" @change="emit('update-cam',{direction:$event.target.value})"><option value="climb">Climb</option><option value="conventional">Conventional</option></select></label>
          <label>Safe Z mm<input type="number" min="1" max="200" step="1" :value="settings.safeZ" @change="emit('update-cam',{safeZ:value($event)})"></label>
          <label>Through allowance mm<input type="number" min="0" max="5" step="0.1" :value="settings.throughMm" @change="emit('update-cam',{throughMm:value($event)})"></label>
          <label>Stepover %<input type="number" min="10" max="90" step="5" :value="settings.stepoverPct" @change="emit('update-cam',{stepoverPct:value($event)})"></label>
        </div>
        <table class="cam-tools">
          <thead><tr><th>Layer tool</th><th v-for="[key,label] in TOOL_FIELDS" :key="key">{{ label }}</th></tr></thead>
          <tbody><tr v-for="layer in machining" :key="layer.id"><td>T{{ machining.indexOf(layer)+1 }} {{ layer.name }}</td>
            <td v-for="[key,label,step] in TOOL_FIELDS" :key="key"><input type="number" :step="step" :aria-label="layer.name+' tool '+key" :placeholder="String(TOOL_DEFAULTS[layer.operation][key])" :value="layer.tool?.[key]??''" @change="emit('update-tool',layer.id,{[key]:value($event)})"></td></tr></tbody>
        </table>
        <p class="hint">Empty tool fields use the defaults shown. Feeds are mm/min. Drill layers peck holes the size of the tool and bore larger ones helically.</p>
        <p class="eyebrow">SHEET STOCK · NESTING AND QUOTES</p>
        <div class="mm-grid">
          <label v-for="[key,label] in [['stockLengthMm','Sheet length mm'],['stockWidthMm','Sheet width mm'],['stockTrimMm','Edge trim mm'],['stockKerfMm','Kerf mm']]" :key="key">{{ label }}<input type="number" min="0" max="10000" step="1" :value="stock[key]" :aria-label="'Design '+label" @change="emit('update-stock',{[key]:value($event)})"></label>
          <label class="cam-check"><input type="checkbox" :checked="stock.stockAllowRotation" aria-label="Design panels may rotate on the sheet" @change="emit('update-stock',{stockAllowRotation:$event.target.checked})">Rotate panels</label>
        </div>
        <p class="hint">When the design is placed in the room, its panels are nested on these sheets with cabinet panels of the same material, thickness and stock, and its bars, rods and tubes are listed as a profile cut list. Both feed workshop jobs and quotes.</p>
      </details>
      <div class="design-row">
        <button @click="emit('dxf')">Export DXF</button>
        <button :disabled="!ready||!cam?.programs.length" @click="emit('gcode-all')">Export NC program (all panels)</button>
        <button @click="emit('export')">Export CNC plan JSON</button>
      </div>
      <ul v-if="cam?.programs.length" class="cam-programs" aria-label="Panel programs">
        <li v-for="program in cam.programs" :key="program.partId+program.face"><span>{{ program.name }} · face {{ program.face }}</span>
          <button :disabled="issues.some(i=>i.severity==='error'&&i.partId===program.partId)" :aria-label="'NC program for '+program.name+' face '+program.face" @click="emit('gcode',program.partId,program.face)">{{ POSTS[settings.post].extension }}</button></li>
      </ul>
      <p class="hint">NC programs are for 3-axis routers and machining centres: origin at the sheet corner on the top surface, Z negative into the material, face B turned over about its length. Through outlines keep holding tabs (0 tabs turns them off). No lead-ins or ramping; inside corners keep the tool radius. Posts follow each control's documented dialect but are not tested on your machine: check every program in a simulator and with a dry run before cutting. DXF layers are named layer_OPERATION_Zdepth for CAM software.</p>
    </template>
  </section>
</template>
