// Stock settings are per cabinet; dimensions and edge trim are millimetres.
export function validateStock(item) {
  const stockLengthMm=item.stockLengthMm??2440,stockWidthMm=item.stockWidthMm??1220,stockTrimMm=item.stockTrimMm??0,stockAllowRotation=item.stockAllowRotation??true,stockKerfMm=item.stockKerfMm??3;
  if(![stockLengthMm,stockWidthMm].every(v=>typeof v==='number'&&Number.isFinite(v)&&v>=100&&v<=10000) || typeof stockTrimMm!=='number'||!Number.isFinite(stockTrimMm)||stockTrimMm<0||stockTrimMm>100 || 2*stockTrimMm>=Math.min(stockLengthMm,stockWidthMm) || typeof stockAllowRotation!=='boolean' || typeof stockKerfMm!=='number'||!Number.isFinite(stockKerfMm)||stockKerfMm<0||stockKerfMm>20) throw Error('Stock dimensions must be 100–10000 mm, with valid edge trim, kerf (0–20 mm) and rotation');
  return {stockLengthMm,stockWidthMm,stockTrimMm,stockAllowRotation,stockKerfMm};
}
export function panelStockFit(length,width,settings) {
  const stock=validateStock(settings),usableLength=(stock.stockLengthMm-2*stock.stockTrimMm)/1000,usableWidth=(stock.stockWidthMm-2*stock.stockTrimMm)/1000;
  const fits=(a,b)=>a<=usableLength+1e-9&&b<=usableWidth+1e-9;
  return fits(length,width)?'direct':stock.stockAllowRotation&&fits(width,length)?'rotated':'oversize';
}
