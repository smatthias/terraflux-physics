export function nextQuoteLifecycle(quote,input,at=new Date().toISOString()){
  const existing=quote.lifecycle??{status:'draft',events:[]};
  if(!Number.isInteger(input.expectedEvent)||input.expectedEvent!==existing.events.length)throw Error('Quote status changed; reload before editing');
  const allowed={draft:['sent'],sent:['accepted','declined','withdrawn']};
  if(!allowed[existing.status]?.includes(input.status))throw Error('Invalid quote status transition');
  if(typeof input.actor!=='string'||!input.actor.trim()||input.actor.trim().length>80||typeof input.note!=='string'||!input.note.trim()||input.note.trim().length>1000)throw Error('Quote status needs an actor and notes');
  return {status:input.status,events:[...existing.events,{number:existing.events.length+1,status:input.status,at,actor:input.actor.trim(),note:input.note.trim()}]};
}
export function validateQuoteLifecycle(quote,updatedAt){
  if(!quote.lifecycle)return;
  if(!Array.isArray(quote.lifecycle.events)||!quote.lifecycle.events.length||quote.lifecycle.events.length>2)throw Error('Invalid quote lifecycle');
  let result={...quote,lifecycle:undefined},previous=Date.parse(quote.createdAt);
  for(const event of quote.lifecycle.events){
    const time=typeof event.at==='string'?Date.parse(event.at):NaN;
    if(!Number.isFinite(time)||time<previous||time>Date.parse(updatedAt)||event.number!==(result.lifecycle?.events.length??0)+1)throw Error('Invalid quote lifecycle history');
    result.lifecycle=nextQuoteLifecycle(result,{...event,expectedEvent:result.lifecycle?.events.length??0},event.at);previous=time;
  }
  if(result.lifecycle.status!==quote.lifecycle.status)throw Error('Invalid quote lifecycle status');
}
