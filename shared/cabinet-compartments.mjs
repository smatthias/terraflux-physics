export function cabinetCompartments(item){
 const columns=item.columns===undefined?1:item.columns,t=item.thickness,innerWidth=item.width-2*t,innerHeight=item.height-2*t;
 if(!Number.isInteger(columns)||columns<1||columns>8)throw Error('Cabinet columns must be an integer from 1 to 8');
 const counts=item.columnShelves===undefined?Array(columns).fill(item.shelves):item.columnShelves;
 if(!Array.isArray(counts)||counts.length!==columns||!Array.from(counts).every(count=>Number.isInteger(count)&&count>=0&&count<=10))throw Error('Column shelf counts must match columns and contain integers from 0 to 10');
 if(counts.some(count=>count>0&&innerHeight/(count+1)<=t))throw Error('Column shelves do not fit the cabinet height');
 const weights=item.columnWeights===undefined?Array(columns).fill(1):item.columnWeights;
 if(!Array.isArray(weights)||weights.length!==columns||!Array.from(weights).every(v=>typeof v==='number'&&Number.isFinite(v)&&v>=.01&&v<=1000))throw Error('Column width weights must match columns and be numbers from 0.01 to 1000');
 const fronts=item.columnFronts;
 if(fronts!==undefined&&(!Array.isArray(fronts)||fronts.length!==columns||!Array.from(fronts).every(v=>['open','single','double'].includes(v))))throw Error('Column fronts must match columns and use open, single or double');
 const available=innerWidth-(columns-1)*t,total=weights.reduce((n,v)=>n+v,0),widths=weights.map(v=>available*v/total);
 if(widths.some(width=>!Number.isFinite(width)||width<.05-1e-10))throw Error('Cabinet compartments need at least 0.05 m clear width');
 let left=-innerWidth/2;
 const bays=widths.map((width,n)=>{const bay={x:left+width/2,width,shelves:counts[n],front:fronts?.[n]};left+=width+t;return bay;});
 return {columns,bayWidth:available/columns,innerHeight,bays,dividers:bays.slice(0,-1).map(bay=>bay.x+bay.width/2+t/2)};
}
