export function edgeBanding(item){
 const value=item.edgeBanding;
 if(value===undefined)return {thicknessMm:0,fronts:false,shelves:false};
 if(!value||typeof value!=='object'||Array.isArray(value)||typeof value.thicknessMm!=='number'||!Number.isFinite(value.thicknessMm)||value.thicknessMm<0||value.thicknessMm>5||typeof value.fronts!=='boolean'||typeof value.shelves!=='boolean')throw Error('Edge banding needs thickness 0–5 mm and front/shelf selections');
 return {thicknessMm:value.thicknessMm,fronts:value.fronts,shelves:value.shelves};
}
export function panelEdgeBanding(item,row){
 const settings=edgeBanding(item),front=settings.fronts&&row.part.startsWith('front'),shelf=settings.shelves&&row.part.startsWith('shelf');
 const lengthEdges=front?2:0,widthEdges=front?2:shelf?1:0;
 return {finishedLength:row.length+row.joineryLengthMm/1000,finishedWidth:row.width,edgeThicknessMm:settings.thicknessMm,edgeLengthEdges:lengthEdges,edgeWidthEdges:widthEdges,coreLength:row.length+row.joineryLengthMm/1000-lengthEdges*settings.thicknessMm/1000,coreWidth:row.width-widthEdges*settings.thicknessMm/1000,bandLengthMetres:settings.thicknessMm>0?row.quantity*(lengthEdges*row.width+widthEdges*row.length):0};
}
export function edgeBandingNote(item){const e=edgeBanding(item);return e.thicknessMm>0&&(e.fronts||e.shelves)?`Edge banding ${e.thicknessMm} mm: ${[e.fronts?'four edges on fronts':'',e.shelves?'shelf front edge':''].filter(Boolean).join('; ')}. Core-cut sizes subtract band thickness; blanks add machining allowances. Other panels unbanded.`:'No edge-banding deduction.';}
