// Panels with their machining really cut out, for the designer's 3D view.
//
// Plain CSG would cut every feature from the whole panel, and each cut's infinite
// planes would slice the large faces again and again (64 holes took a minute).
// Instead, features are grouped into regions (overlapping footprints, plus a
// 1 mm margin), the rest of the panel is split into plain box cells around them,
// and CSG runs only inside each region with its own features. Faces between
// cells are internal and dropped, so the result shows the panel's outer faces
// and the cut surfaces. Work happens in the sheet frame (x = u, y = v, z = n).
import {subtract,boxSolid,cylinderSolid} from './csg.js';
import {featureSheetBox,drillSheetAxis} from '../shared/furniture-design-geometry.mjs';

const MARGIN=1,EPS=1e-6;
const SHEET_AXIS={u:'x',v:'y',n:'z'};
// Tool body of a feature, a little longer than its cut where it opens to a
// surface so the CSG never leaves a paper-thin skin.
function toolSolid(part,feature,tag) {
  const box=featureSheetBox(part,feature),limits={u:[0,part.length],v:[0,part.width],n:[0,part.thickness]};
  const ext=(axis,[a,b])=>[a<=limits[axis][0]+EPS?a-1:a,b>=limits[axis][1]-EPS?b+1:b];
  const u=ext('u',box.u),v=ext('v',box.v),n=ext('n',box.n);
  if(feature.type!=='drill') return boxSolid({x:u[0],y:v[0],z:n[0]},{x:u[1],y:v[1],z:n[1]},tag);
  const axis=drillSheetAxis(feature),range={u,v,n}[axis],centre={x:(u[0]+u[1])/2,y:(v[0]+v[1])/2,z:(n[0]+n[1])/2};
  return cylinderSolid(centre,SHEET_AXIS[axis],range[0],range[1],feature.diameter/2,feature.diameter>20?40:24,tag);
}
// Groups features whose footprints (with margin) overlap; returns {rect, features}.
function regionsOf(part,features) {
  let regions=features.map(feature=>{
    const box=featureSheetBox(part,feature);
    return {rect:[Math.max(0,box.u[0]-MARGIN),Math.min(part.length,box.u[1]+MARGIN),Math.max(0,box.v[0]-MARGIN),Math.min(part.width,box.v[1]+MARGIN)],features:[feature]};
  });
  const overlaps=(a,b)=>a[0]<b[1]-EPS&&b[0]<a[1]-EPS&&a[2]<b[3]-EPS&&b[2]<a[3]-EPS;
  for(let merged=true;merged;) {
    merged=false;
    for(let i=0;i<regions.length&&!merged;i++) for(let j=i+1;j<regions.length&&!merged;j++) if(overlaps(regions[i].rect,regions[j].rect)) {
      const a=regions[i],b=regions[j];
      a.rect=[Math.min(a.rect[0],b.rect[0]),Math.max(a.rect[1],b.rect[1]),Math.min(a.rect[2],b.rect[2]),Math.max(a.rect[3],b.rect[3])];
      a.features.push(...b.features);regions.splice(j,1);merged=true;
    }
  }
  return regions;
}
const uniqueSorted=values=>[...new Set(values.map(v=>Math.round(v*1e6)/1e6))].sort((a,b)=>a-b);

// Polygons of the cut panel in the sheet frame. Each polygon's `shared` is
// 'panel' or the index of the feature that cut it.
export function cutPanelPolygons(part,features) {
  const L=part.length,W=part.width,T=part.thickness,polygons=[];
  const regions=regionsOf(part,features),index=new Map(features.map((f,i)=>[f,i]));
  for(const region of regions) {
    const [u0,u1,v0,v1]=region.rect;
    let solid=boxSolid({x:u0,y:v0,z:0},{x:u1,y:v1,z:T},'panel');
    for(const feature of region.features) solid=subtract(solid,toolSolid(part,feature,index.get(feature)));
    polygons.push(...solid);
  }
  // Plain cells: slabs along u at every region boundary, split along v by the
  // regions crossing each slab.
  const us=uniqueSorted([0,L,...regions.flatMap(r=>[r.rect[0],r.rect[1]])]);
  for(let i=0;i<us.length-1;i++) {
    const a=us[i],b=us[i+1],mid=(a+b)/2,crossing=regions.filter(r=>r.rect[0]<mid&&r.rect[1]>mid);
    const vs=uniqueSorted([0,W,...crossing.flatMap(r=>[r.rect[2],r.rect[3]])]);
    for(let j=0;j<vs.length-1;j++) {
      const c=vs[j],d=vs[j+1],vm=(c+d)/2;
      if(crossing.some(r=>r.rect[2]<vm&&r.rect[3]>vm)) continue;
      polygons.push(...boxSolid({x:a,y:c,z:0},{x:b,y:d,z:T},'panel'));
    }
  }
  // Keep cut surfaces and faces on the panel's outside; drop faces between cells.
  const outside=p=>{const n=p.plane.normal,v=p.vertices[0];
    return (Math.abs(Math.abs(n.x)-1)<EPS&&(Math.abs(v.x)<EPS||Math.abs(v.x-L)<EPS))||(Math.abs(Math.abs(n.y)-1)<EPS&&(Math.abs(v.y)<EPS||Math.abs(v.y-W)<EPS))||(Math.abs(Math.abs(n.z)-1)<EPS&&(Math.abs(v.z)<EPS||Math.abs(v.z-T)<EPS));};
  return polygons.filter(p=>p.shared!=='panel'||outside(p));
}

// Mesh data in the piece's own centred frame: triangle positions and normals
// grouped by tag (group 0 is the panel, group i+1 is feature i). Sheet axes map
// to the panel orientation; odd axis orders mirror, so their winding is reversed.
const ORDER={horizontal:['x','z','y'],front:['x','y','z'],side:['z','y','x']};
const MIRRORED={horizontal:true,front:false,side:true};
const cache=new Map();
export function panelMeshData(part,features) {
  const key=JSON.stringify([part.orientation,part.length,part.width,part.thickness,features.map(f=>[f.type,f.face,f.u,f.v,f.t,f.depth,f.diameter,f.length,f.width])]);
  let data=cache.get(key);
  if(data) {cache.delete(key);cache.set(key,data);return data;}
  const [au,av,an]=ORDER[part.orientation],half={u:part.length/2,v:part.width/2,n:part.thickness/2};
  const map=p=>{const out={};out[au]=p.x-half.u;out[av]=p.y-half.v;out[an]=p.z-half.n;return [out.x,out.y,out.z];};
  const mapNormal=n=>{const out={};out[au]=n.x;out[av]=n.y;out[an]=n.z;return [out.x,out.y,out.z];};
  const byGroup=Array.from({length:features.length+1},()=>({positions:[],normals:[]}));
  for(const polygon of cutPanelPolygons(part,features)) {
    const group=byGroup[polygon.shared==='panel'?0:polygon.shared+1],normal=mapNormal(polygon.plane.normal);
    const vertices=MIRRORED[part.orientation]?[...polygon.vertices].reverse():polygon.vertices;
    for(let i=1;i<vertices.length-1;i++) for(const v of [vertices[0],vertices[i],vertices[i+1]]) {group.positions.push(...map(v));group.normals.push(...normal);}
  }
  let start=0;
  data={positions:new Float32Array(byGroup.flatMap(g=>g.positions)),normals:new Float32Array(byGroup.flatMap(g=>g.normals)),
    groups:byGroup.map((g,i)=>{const count=g.positions.length/3,range={start,count,materialIndex:i};start+=count;return range;}).filter(g=>g.count)};
  cache.set(key,data);
  if(cache.size>300) cache.delete(cache.keys().next().value);
  return data;
}
// Outlines of a feature at its opening and at its floor (sheet frame → local).
export function featureOutline(part,feature) {
  const [au,av,an]=ORDER[part.orientation],half={u:part.length/2,v:part.width/2,n:part.thickness/2};
  const map=(u,v,n)=>{const out={};out[au]=u-half.u;out[av]=v-half.v;out[an]=n-half.n;return [out.x,out.y,out.z];};
  const box=featureSheetBox(part,feature),loops=[];
  if(feature.type==='drill') {
    const axis=drillSheetAxis(feature),range={u:box.u,v:box.v,n:box.n}[axis],c={u:(box.u[0]+box.u[1])/2,v:(box.v[0]+box.v[1])/2,n:(box.n[0]+box.n[1])/2},r=feature.diameter/2;
    const [a,b]={n:['u','v'],u:['v','n'],v:['n','u']}[axis];
    for(const at of range) loops.push(Array.from({length:32},(_,i)=>{const p={...c,[axis]:at};p[a]+=Math.cos(i/32*Math.PI*2)*r;p[b]+=Math.sin(i/32*Math.PI*2)*r;return map(p.u,p.v,p.n);}));
  } else for(const n of box.n) loops.push([[box.u[0],box.v[0]],[box.u[1],box.v[0]],[box.u[1],box.v[1]],[box.u[0],box.v[1]]].map(([u,v])=>map(u,v,n)));
  return loops.flatMap(loop=>loop.flatMap((p,i)=>[...p,...loop[(i+1)%loop.length]]));
}
