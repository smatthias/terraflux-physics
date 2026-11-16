<script setup>
// The VFR view's report, below the scene: the GAFOR category table, the map's share of each category, the picked
// point (visibility, main cloud base, category and the VFR airspace clear of cloud with the ground in sight) and its
// category over the run, and - with an official GAFOR area file loaded - the areas' categories in every period.
import { computed } from 'vue'
import { GAFOR_CATEGORIES, GAFOR, UNKNOWN_GAFOR, VFR } from './gafor.js'

const props = defineProps({
  shares: { type: Object, default: null },
  picked: { type: Object, default: null },           // vfrAt(...)
  pickedSeries: { type: Array, default: () => [] },  // category key per frame
  frameLabels: { type: Array, default: () => [] },
  frameIndex: { type: Number, default: 0 },
  areaTable: { type: Array, default: null },          // [{ area, cellCount, frames: [areaCategory | null] }]
  areaSource: { type: String, default: '' },
  time: { type: String, default: '' },
  cloudModelled: { type: Boolean, default: true },
})
const emit = defineEmits(['seek'])
const cat = key => GAFOR[key] ?? UNKNOWN_GAFOR
const km = m => (m === null || m === undefined ? '—' : m >= 10000 ? '≥ 10 km' : m >= 1000 ? `${(m / 1000).toFixed(1)} km` : `${Math.round(m / 50) * 50} m`)
const ft = v => (v === null || v === undefined ? 'unknown' : v === Infinity ? 'none (no BKN/OVC layer)' : `${(Math.round(v / 100) * 100).toLocaleString('en-US')} ft`)
const visText = c => (c.key === 'C' ? '≥ 10 km' : c.key === 'O' ? '≥ 8 km' : c.key === 'D' ? '5 000 m ≤ vis < 8 000 m' : c.key === 'M' ? '1 500 m ≤ vis < 5 000 m' : '< 1 500 m')
const baseText = c => (c.key === 'C' ? '≥ 5 000 ft' : c.key === 'O' ? '≥ 2 000 ft' : c.key === 'D' ? '1 000 ft ≤ hs < 2 000 ft' : c.key === 'M' ? '500 ft ≤ hs < 1 000 ft' : '< 500 ft')
const shareBars = computed(() => props.shares ? [...GAFOR_CATEGORIES, UNKNOWN_GAFOR].map(c => ({ c, pct: props.shares[c.key] ?? 0 })).filter(b => b.pct > 0) : [])
const coveredAreas = computed(() => (props.areaTable ?? []).filter(r => r.cellCount > 0))
const outsideAreas = computed(() => (props.areaTable ?? []).filter(r => r.cellCount === 0))
</script>

<template>
  <section class="comparison wx-report vfr-report" aria-label="VFR GAFOR report">
    <div class="wx-head">
      <div>
        <div class="section-label">VFR · GAFOR · SIMULATED WEATHER · {{ time }}</div>
        <h3>GAFOR categories <small>from the estimated visibility and the main cloud base (hs: base of the lowest layer covering more than 4/8, BKN/OVC, above ground; above the reference altitude for an area). The worse of the two decides.</small></h3>
      </div>
    </div>

    <table class="vfr-table">
      <thead><tr><th>Category</th><th>Visibility</th><th>Main cloud base (hs)</th><th>Meaning / VFR status</th><th>Map now</th></tr></thead>
      <tbody>
        <tr v-for="c in GAFOR_CATEGORIES" :key="c.key">
          <th><span class="vfr-badge" :style="{ background: c.colour }">{{ c.key }}</span> {{ c.name }}</th>
          <td>{{ visText(c) }}</td><td>{{ baseText(c) }}</td><td>{{ c.meaning }}</td>
          <td>{{ shares && shares[c.key] ? `${Math.round(shares[c.key])} %` : '—' }}</td>
        </tr>
      </tbody>
    </table>
    <div v-if="shareBars.length" class="wx-sharebar vfr-sharebar"><i v-for="b in shareBars" :key="b.c.key" :style="{ width: `${b.pct}%`, background: b.c.colour }" :title="`${b.c.name}: ${Math.round(b.pct)} %`"></i></div>
    <p v-if="!cloudModelled" class="wx-note">This run has no cloud model: the categories rest on visibility alone.</p>

    <div class="wx-grid">
      <div class="wx-card">
        <div class="wx-small-label">THE PICKED POINT</div>
        <template v-if="picked">
          <p class="vfr-point"><span class="vfr-badge big" :style="{ background: picked.category.colour }">{{ picked.category.key }}</span><span><b>{{ picked.category.name }}</b><small>{{ picked.category.meaning }}{{ picked.category.partial ? ` · ${picked.category.basis}` : '' }}</small></span></p>
          <dl class="wx-dl">
            <dt>Visibility</dt><dd>{{ km(picked.visibilityM) }} <small v-if="picked.category.byVisibility">→ {{ picked.category.byVisibility }} · estimated</small></dd>
            <dt>Cloud base hs</dt><dd>{{ ft(picked.hsFt) }} <small v-if="picked.category.byBase">above ground → {{ picked.category.byBase }}</small></dd>
            <dt>Ground</dt><dd>{{ ft(picked.elevationFt) }} AMSL</dd>
            <dt>VFR airspace</dt><dd v-if="picked.space.ok">{{ ft(picked.space.bottomFt) }} to {{ ft(picked.space.topFt) }} AMSL <small>room {{ ft(picked.space.room) }} · limited by {{ picked.space.limitedBy }}</small></dd><dd v-else class="warn">none: {{ picked.space.limitedBy }}</dd>
          </dl>
          <div v-if="pickedSeries.length > 1" class="wx-strip-block">
            <div class="wx-small-label">THIS POINT OVER THE RUN · CLICK TO JUMP</div>
            <div class="wx-strip"><button v-for="(k, j) in pickedSeries" :key="j" :class="{ now: j === frameIndex }" :style="{ background: cat(k).colour }" :title="`${frameLabels[j] ?? j}: ${cat(k).name}`" @click="emit('seek', j)">{{ pickedSeries.length <= 30 ? k : '' }}</button></div>
          </div>
        </template>
        <p v-else class="wx-note">Click the map to read a point: its visibility, main cloud base, GAFOR category and the VFR airspace above it.</p>
      </div>
      <div class="wx-card">
        <div class="wx-small-label">CLEAR OF CLOUD, GROUND IN SIGHT (SERA.5001, SERA.5005)</div>
        <ul class="vfr-rules">
          <li>Lowest height {{ VFR.minHeightFt }} ft above ground (1 000 ft over congested areas and 2 000 ft for night VFR are not modelled).</li>
          <li>Up to {{ VFR.lowBandAmslFt.toLocaleString('en-US') }} ft AMSL or {{ VFR.lowBandAglFt.toLocaleString('en-US') }} ft above ground (whichever is higher): clear of cloud with the surface in sight, 5 km visibility (1 500 m for slow aircraft).</li>
          <li>Above: {{ VFR.verticalFromCloudFt.toLocaleString('en-US') }} ft below cloud (and 1 500 m horizontally), 5 km visibility.</li>
          <li>No VFR above a broken or overcast deck: the ground is not in sight. The columns stop at FL100.</li>
        </ul>
        <p class="wx-note">Airspace classes and their national variations are not modelled: these are the class G low-level rules and the general minima.</p>
      </div>
    </div>

    <div class="vfr-areas">
      <div class="wx-small-label">GAFOR AREAS {{ areaTable ? `· ${areaSource}` : '' }}</div>
      <template v-if="areaTable">
        <div v-if="coveredAreas.length" class="vfr-area-scroll">
          <table class="vfr-area-table">
            <thead><tr><th>Area</th><th>Ref. alt.</th><th v-for="(l, j) in frameLabels" :key="j" :class="{ now: j === frameIndex }" @click="emit('seek', j)">{{ l }}</th></tr></thead>
            <tbody>
              <tr v-for="r in coveredAreas" :key="r.area.id">
                <th>{{ r.area.id }} <small>{{ r.area.name }}</small></th>
                <td>{{ r.area.referenceFt.toLocaleString('en-US') }} ft</td>
                <td v-for="(f, j) in r.frames" :key="j" :class="{ now: j === frameIndex }" :style="{ background: f?.covered ? cat(f.prevailing).colour : 'transparent' }" :title="f?.covered ? `prevailing ${cat(f.prevailing).name}, worst ${cat(f.worst).name} · lowest hs ${ft(f.minHsFt)} · lowest visibility ${km(f.minVisibilityM)} · ${f.cells} cells` : ''" @click="emit('seek', j)">
                  <b>{{ f?.prevailing ?? '' }}</b><small v-if="f && f.worst !== f.prevailing">{{ f.worst }}</small>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
        <p class="wx-note">Each cell: the prevailing category (most of the area) and, small, the worst where it differs; the cloud base is taken above the area's reference altitude.<template v-if="outsideAreas.length"> {{ outsideAreas.length }} area(s) lie outside this run's grid: {{ outsideAreas.map(r => r.area.id).join(', ') }}.</template></p>
      </template>
      <p v-else class="wx-note">No GAFOR areas loaded. Load the official area file (control panel, <i>Load GAFOR areas</i>): a GeoJSON FeatureCollection with one Polygon or MultiPolygon per area and the properties <code>id</code> (the area code), <code>name</code> and <code>reference_altitude_ft</code>. No areas are built into the viewer: they must match the official publication.</p>
    </div>
  </section>
</template>
