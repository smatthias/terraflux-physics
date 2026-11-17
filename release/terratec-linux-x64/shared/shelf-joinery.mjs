export function shelfJoinery(item){
 const value=item.shelfJoinery;
 if(value===undefined)return {type:'butt',engagementMm:0};
 if(!value||typeof value!=='object'||Array.isArray(value)||!['butt','dado'].includes(value.type)||typeof value.engagementMm!=='number'||!Number.isFinite(value.engagementMm))throw Error('Choose butt or dado shelf joinery with a numeric engagement');
 const depth=value.engagementMm;
 if(value.type==='butt'&&depth!==0)throw Error('Butt shelf joints require zero engagement');
 if(value.type==='dado'&&(depth<=0||depth>10||depth>=item.thickness*1000/2))throw Error('Dado engagement must be positive, at most 10 mm and less than half the panel thickness');
 return {type:value.type,engagementMm:depth};
}
export function shelfJoineryNote(item){const joint=shelfJoinery(item);return joint.type==='dado'?`Dado planning: ${joint.engagementMm} mm engagement per shelf end; +${2*joint.engagementMm} mm shelf blank length. Grooves and tooling require workshop definition; geometry shows clear spans.`:'Butt shelf joints: no engagement allowance.';}
