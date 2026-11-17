<script setup>
defineProps({review:{type:Object,required:true},selectedId:String});
const emit=defineEmits(['select','export']);
</script>
<template>
 <section aria-label="Project clearance review">
  <h3>Project clearance review</h3>
  <p v-if="review.error" role="status">{{ review.error }}</p>
  <template v-else>
   <button @click="emit('export','print')">Export printable clearance handoff</button><button @click="emit('export','json')">Export clearance JSON</button><button @click="emit('export','csv')">Export clearance CSV</button>
   <p data-testid="clearance-review-summary" role="status">{{ review.issues.length }} issues · {{ review.affectedCount }} of {{ review.cabinetCount }} cabinets affected</p>
   <p v-if="!review.cabinetCount" class="hint">Add a cabinet to review its clearances.</p>
   <p v-else-if="!review.issues.length" class="hint">No clearance issues under the current planning settings.</p>
   <ul v-else data-testid="clearance-review-issues">
    <li v-for="(issue,index) in review.issues" :key="index"><button :aria-pressed="selectedId===issue.cabinetId" @click="emit('select',issue.cabinetId)">{{ issue.cabinetName }} · {{ issue.message }}</button></li>
   </ul>
   <p class="hint">Reviews applied cabinet geometry using the clearance settings above. Select an issue to inspect its cabinet and overlay.</p>
  </template>
 </section>
</template>
