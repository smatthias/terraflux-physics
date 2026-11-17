<script setup>
defineProps({layout:{type:Object,required:true},scope:{type:String,default:'Selected cabinet'}});
</script>
<template>
  <section class="sheet-layout" aria-label="Panel nesting layout">
    <p data-testid="nesting-summary">{{ layout.sheets.length }} sheets · {{ (layout.utilisation*100).toFixed(1) }}% stock utilisation · {{ layout.unplaced.length }} unplaced</p>
    <p class="hint">{{ scope }} only. Coordinates start at the sheet’s top-left. Kerf is the gap between blanks; edge trim bounds the usable area. Workshop review required before cutting.</p>
    <figure v-for="sheet in layout.sheets" :key="sheet.number">
      <figcaption>Sheet {{ sheet.number }} · {{ layout.stock.stockLengthMm }} × {{ layout.stock.stockWidthMm }} mm</figcaption>
      <svg :viewBox="`0 0 ${layout.stock.stockLengthMm} ${layout.stock.stockWidthMm}`" role="img" :aria-label="`Sheet ${sheet.number} panel layout`">
        <rect width="100%" height="100%" fill="#eee8db"/>
        <rect :x="layout.stock.stockTrimMm" :y="layout.stock.stockTrimMm" :width="layout.stock.stockLengthMm-2*layout.stock.stockTrimMm" :height="layout.stock.stockWidthMm-2*layout.stock.stockTrimMm" fill="none" stroke="#555" stroke-width="2" stroke-dasharray="12 8"/>
        <g v-for="panel in sheet.panels" :key="panel.id">
          <rect :x="panel.x" :y="panel.y" :width="panel.width" :height="panel.height" fill="#b7cb9d" stroke="#344b30" stroke-width="2"><title>{{ panel.shortId || panel.id }} · {{ panel.width.toFixed(1) }} × {{ panel.height.toFixed(1) }} mm · {{ panel.rotated ? 'rotated' : 'direct' }}</title></rect>
          <text :x="panel.x+10" :y="panel.y+35" font-size="30" fill="#20321e">{{ panel.shortId || panel.id }}</text>
        </g>
      </svg>
    </figure>
    <p v-if="layout.unplaced.length" class="hint">Oversize: {{ layout.unplaced.map(p=>p.id).join(', ') }}</p>
  </section>
</template>
