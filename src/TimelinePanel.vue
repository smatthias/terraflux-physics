<script setup>
// Version history drawer: scrub or jump back in time, preview a version
// read-only, restore it (as a new version) or restore one element from it.
import {ref,computed,watch,onBeforeUnmount} from 'vue';
import {versionAtTime,elementHistory,ELEMENT_KINDS} from '../shared/timeline.mjs';
const props=defineProps({timeline:{type:Object,required:true},previewSeq:{type:Number,default:null},focus:{type:Object,default:null},busy:Boolean});
const emit=defineEmits(['preview','exit','restore','restore-element','close']);
const MIN=60000,JUMPS=[['5 min ago',5*MIN],['15 min ago',15*MIN],['1 h ago',60*MIN],['3 h ago',180*MIN]];
const now=ref(Date.now()),timer=setInterval(()=>{now.value=Date.now();},30000);
onBeforeUnmount(()=>clearInterval(timer));
const entries=computed(()=>props.timeline.entries),head=computed(()=>entries.value.at(-1));
const shownSeq=computed(()=>props.previewSeq??head.value.seq);
const index=computed(()=>entries.value.findIndex(e=>e.seq===shownSeq.value));
const shown=computed(()=>entries.value[index.value]);
// Every element the history has changed (including removed ones), newest name first.
const elements=computed(()=>{
  const found=new Map();
  for(const e of entries.value) for(const c of e.changes) found.set(c.key,c);
  return [...found.values()].sort((a,b)=>a.kind.localeCompare(b.kind)||a.name.localeCompare(b.name));
});
const elementKey=ref(''),limit=ref(200),goto=ref(''),note=ref('');
const element=computed(()=>elements.value.find(e=>e.key===elementKey.value)??null);
watch(elements,list=>{if(elementKey.value&&!list.some(e=>e.key===elementKey.value)) elementKey.value='';});
const rows=computed(()=>{
  const list=element.value?elementHistory(props.timeline,element.value.key):[...entries.value].reverse();
  return list.slice(0,limit.value);
});
const total=computed(()=>element.value?elementHistory(props.timeline,element.value.key).length:entries.value.length);
const ago=at=>{const m=Math.round((now.value-Date.parse(at))/MIN);return m<1?'just now':m<60?m+' min ago':m<1440?Math.round(m/60)+' h ago':Math.round(m/1440)+' d ago';};
const clock=at=>{const d=new Date(at);return (d.toDateString()===new Date(now.value).toDateString()?'':d.toLocaleDateString()+' ')+d.toLocaleTimeString();};
const kind=k=>ELEMENT_KINDS[k];
const changeText=e=>e.changes.slice(0,3).map(c=>`${kind(c.kind)} “${c.name}” ${c.action}`).join(' · ')+(e.changes.length>3?` · +${e.changes.length-3} more`:'');
function jump(time) {
  note.value='';
  if(!Number.isFinite(time)) return;
  const entry=versionAtTime(props.timeline,time);
  if(entry.seq===head.value.seq) note.value='No changes since then · this is the current version';
  else if(time<Date.parse(entries.value[0].at)) note.value='History starts at '+clock(entries.value[0].at)+' · showing the oldest version';
  emit('preview',entry.seq);
}
const scrub=value=>{note.value='';emit('preview',entries.value[Number(value)].seq);};
</script>

<template>
  <section class="timeline-panel" aria-label="Version history">
    <div class="timeline-head">
      <h2>Version history</h2>
      <span>{{ entries.length }} versions since {{ clock(entries[0].at) }}</span>
      <button @click="emit('close')">Close history</button>
    </div>
    <div class="timeline-jumps" role="group" aria-label="Jump back in time">
      <button v-for="[label,ms] in JUMPS" :key="label" :disabled="busy" @click="jump(now - ms)">{{ label }}</button>
      <button :disabled="busy" @click="emit('preview', entries[0].seq)">Oldest version</button>
      <label>Go to time<input v-model="goto" type="datetime-local" step="1" aria-label="Time to go to"></label>
      <button :disabled="busy || !goto" @click="jump(new Date(goto).getTime())">Go to time</button>
    </div>
    <input class="timeline-scrubber" type="range" aria-label="Version scrubber" min="0" :max="entries.length - 1" :value="index" :aria-valuetext="shown.label + ', ' + clock(shown.at)" :disabled="busy || entries.length < 2" @input="scrub($event.target.value)">
    <p v-if="note" class="hint" role="status">{{ note }}</p>
    <div v-if="previewSeq !== null" class="timeline-preview" role="status">
      <span>Viewing version {{ index + 1 }} of {{ entries.length }} · {{ clock(shown.at) }} ({{ ago(shown.at) }}) · read-only</span>
      <button class="primary" :disabled="busy" @click="emit('restore', previewSeq)">Restore this version</button>
      <button v-if="element" :disabled="busy" @click="emit('restore-element', {seq: previewSeq, key: element.key, name: element.name})">Restore only {{ element.name }}</button>
      <button @click="emit('exit')">Back to now</button>
    </div>
    <div class="timeline-filter">
      <label>Show changes of<select v-model="elementKey" aria-label="Show changes of"><option value="">All elements</option><option v-for="e in elements" :key="e.key" :value="e.key">{{ kind(e.kind) }} · {{ e.name }}</option></select></label>
      <button v-if="focus && focus.key !== elementKey && elements.some(e => e.key === focus.key)" @click="elementKey = focus.key">Only {{ focus.name }}</button>
    </div>
    <ol class="timeline-list">
      <li v-for="row in rows" :key="row.seq" :class="{ shown: row.seq === shownSeq }">
        <button :aria-pressed="row.seq === shownSeq" :aria-label="'Version ' + row.seq + ' · ' + clock(row.at) + ' · ' + row.label" :disabled="busy" @click="emit('preview', row.seq)">
          <time :datetime="row.at">{{ clock(row.at) }}</time> {{ row.label }}<small>{{ ago(row.at) }}{{ row.seq === head.seq ? ' · current' : '' }}</small>
        </button>
        <p v-if="element">{{ element.name }} {{ row.action }} · its version {{ row.version }}</p>
        <p v-else-if="row.changes.length">{{ changeText(row) }}</p>
      </li>
    </ol>
    <button v-if="total > rows.length" @click="limit += 200">Show older versions</button>
  </section>
</template>

<style>
.timeline-panel{position:fixed;left:12px;right:12px;bottom:48px;z-index:1100;max-height:46vh;overflow:auto;padding:12px 16px;background:#f2f3ebf8;border:1px solid #b9c6b9;border-radius:10px;box-shadow:0 8px 30px #001c2440;font-size:12px}
.timeline-head,.timeline-jumps,.timeline-preview,.timeline-filter{display:flex;flex-wrap:wrap;align-items:center;gap:6px}
.timeline-head h2{font-size:15px;margin:0 8px 0 0}.timeline-head button{margin-left:auto}
.timeline-panel button{padding:6px 10px;font-size:12px}.timeline-panel label{margin:0;display:flex;align-items:center;gap:6px;text-transform:none}
.timeline-panel input,.timeline-panel select{width:auto;margin:0;padding:5px}
.timeline-jumps,.timeline-filter{margin:8px 0}.timeline-scrubber{width:100%!important}
.timeline-preview{margin:8px 0;padding:8px;border:1px solid #d8b25a;border-radius:6px;background:#fff5d9}
.timeline-list{list-style:none;margin:0;padding:0}.timeline-list li{border-bottom:1px solid #dde3d9}
.timeline-list li.shown{background:#e2ebde}.timeline-list button{border:0;width:100%;text-align:left}
.timeline-list time{font-weight:600;margin-right:6px}.timeline-list small{margin-left:8px;color:#60746a}
.timeline-list p{margin:0 10px 6px;color:#60746a;font-size:11px}
</style>
