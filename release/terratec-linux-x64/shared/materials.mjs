// Material catalogue for furniture. Woods (solid wood and wood-based boards) are
// panel materials; metals are hardware materials for screws and handles.
//
// Densities are typical values in kg/m³ for planning weight estimates, not
// certified specifications: real boards and alloys vary by grade, moisture and
// supplier. Existing panel materials keep their IDs and labels, which appear in
// saved projects, job manifests and quotes.
const material=(id,label,category,form,color,densityKgM3)=>Object.freeze({id,label,category,form,color,densityKgM3});
export const MATERIALS=Object.freeze(Object.fromEntries([
  material('oak','Oak board','wood','board',0xb88c59,700),
  material('birch','Birch plywood','wood','plywood',0xdec49a,680),
  material('white','White board','wood','board',0xe8e8e2,650),
  material('beech','Beech solid wood','wood','solid',0xd9a877,720),
  material('walnut','Walnut solid wood','wood','solid',0x6b4a32,640),
  material('maple','Maple solid wood','wood','solid',0xe6cfa4,700),
  material('pine','Pine solid wood','wood','solid',0xe3c08a,520),
  material('steel','Zinc-plated steel','metal','steel',0x9aa1a6,7850),
  material('stainless','Stainless steel','metal','steel',0xc7ccd0,7900),
  material('brass','Brass','metal','alloy',0xb5953f,8500),
  material('aluminium','Aluminium','metal','alloy',0xd3d7da,2700),
].map(m=>[m.id,m])));

export const WOOD_IDS=Object.freeze(Object.values(MATERIALS).filter(m=>m.category==='wood').map(m=>m.id));
export const METAL_IDS=Object.freeze(Object.values(MATERIALS).filter(m=>m.category==='metal').map(m=>m.id));
export const DEFAULT_WOOD='oak',DEFAULT_METAL='steel';
export const isWood=id=>WOOD_IDS.includes(id);
export const isMetal=id=>METAL_IDS.includes(id);
export function materialInfo(id) {
  const info=MATERIALS[id];
  if(!info) throw Error('Unknown material');
  return info;
}
// Grouped for selects: wood-based boards, solid woods, metals.
export const MATERIAL_GROUPS=Object.freeze([
  {label:'Wood-based boards',ids:WOOD_IDS.filter(id=>MATERIALS[id].form!=='solid')},
  {label:'Solid wood',ids:WOOD_IDS.filter(id=>MATERIALS[id].form==='solid')},
]);
