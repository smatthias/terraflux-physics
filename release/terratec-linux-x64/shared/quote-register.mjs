export function quoteRegister(records){
  return records.flatMap(job=>job.revisions.flatMap(r=>(r.quotes??[]).map(q=>({jobId:job.id,jobName:job.name,revision:r.number,quoteId:q.id,createdAt:q.createdAt,customer:q.details?.customer??'',customerId:q.details?.customerId??'',reference:q.details?.reference??'',currency:q.costing.pricing.currency,totalCents:q.costing.totalCents,status:q.lifecycle?.status??'draft',sourceSha256:q.costing.sourceSha256}))))
    .sort((a,b)=>b.createdAt.localeCompare(a.createdAt)||a.jobId.localeCompare(b.jobId)||a.quoteId.localeCompare(b.quoteId));
}
export function filterQuotes(rows,query='',status='all'){
  const words=query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  return rows.filter(q=>(status==='all'||q.status===status)&&words.every(w=>[q.jobName,q.customer,q.reference,q.quoteId,q.jobId].join(' ').toLowerCase().includes(w)));
}
