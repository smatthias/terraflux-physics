export function jobSummary(record){
  const revision=record.revisions.at(-1),f=revision.fulfilment;
  return {id:record.id,name:record.name,updatedAt:record.updatedAt,revision:revision.number,status:revision.status,
    workflowState:revision.manifest.validation.status==='blocked'?'blocked':f?.status??revision.status,
    workshop:f?.workshop??'',dueDate:f?.dueDate??'',summary:revision.manifest.summary,
    blockingIssues:revision.manifest.validation.issues.filter(i=>i.severity==='blocking').length};
}
export function filterJobs(jobs,{query='',status='all',sort='updated'}={}){
  const words=query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  const result=jobs.filter(job=>(status==='all'||job.workflowState===status)&&words.every(word=>[job.name,job.id,job.workshop].join(' ').toLowerCase().includes(word)));
  return result.sort((a,b)=>{
    let order=0;
    if(sort==='name')order=a.name.toLowerCase().localeCompare(b.name.toLowerCase());
    else if(sort==='due')order=(a.dueDate||'9999-99-99').localeCompare(b.dueDate||'9999-99-99');
    else order=b.updatedAt.localeCompare(a.updatedAt);
    return order||a.id.localeCompare(b.id);
  });
}
export function jobRollup(jobs){
  return jobs.reduce((result,job)=>{result[job.workflowState]=(result[job.workflowState]??0)+1;return result;},{total:jobs.length});
}
