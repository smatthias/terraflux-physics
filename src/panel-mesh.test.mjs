import {test} from 'node:test';
import assert from 'node:assert/strict';
import {cutPanelPolygons,panelMeshData} from './panel-mesh.js';
import {solidVolume} from './csg.js';
const area=(n,r)=>n/2*r*r*Math.sin(2*Math.PI/n);
const panel=(orientation='horizontal')=>({shape:'panel',orientation,x:0,y:0,z:0,length:600,width:400,thickness:18});
const features=[
  {type:'drill',face:'A',u:50,v:50,diameter:5,depth:12},                 // blind hole, top
  {type:'drill',face:'B',u:100,v:50,diameter:8,depth:18},                // through hole from below
  {type:'pocket',face:'A',u:200,v:100,length:100,width:60,depth:6},       // pocket
  {type:'drill',face:'A',u:250,v:130,diameter:6,depth:18},               // through hole inside the pocket
  {type:'groove',face:'B',u:0,v:390,length:600,width:6,depth:8},          // full-length groove underneath
  {type:'drill',face:'u0',u:0,v:200,t:9,diameter:8,depth:30},            // edge hole
];
const expected=600*400*18-area(24,2.5)*12-area(24,4)*18-100*60*6-area(24,3)*12-600*6*8-area(24,4)*30;

test('the cut panel encloses exactly the remaining material',()=>{
  const polygons=cutPanelPolygons(panel(),features);
  assert.ok(Math.abs(solidVolume(polygons)-expected)<1e-3,`${solidVolume(polygons)} vs ${expected}`);
  assert.ok(Math.abs(solidVolume(cutPanelPolygons(panel(),[]))-600*400*18)<1e-6,'no features: the plain panel');
});

test('only outside faces and cut surfaces remain, each cut tagged with its feature',()=>{
  const polygons=cutPanelPolygons(panel(),features),onOutside=p=>{
    const v=p.vertices[0],n=p.plane.normal;
    return (Math.abs(n.x)>.999&&[0,600].some(x=>Math.abs(v.x-x)<1e-6))||(Math.abs(n.y)>.999&&[0,400].some(y=>Math.abs(v.y-y)<1e-6))||(Math.abs(n.z)>.999&&[0,18].some(z=>Math.abs(v.z-z)<1e-6));};
  assert.ok(polygons.filter(p=>p.shared==='panel').every(onOutside),'no faces left between cells');
  assert.deepEqual([...new Set(polygons.map(p=>p.shared))].filter(s=>s!=='panel').sort(),[0,1,2,3,4,5]);
});

test('mesh data is in the piece frame for every orientation, facing outward, and cached',()=>{
  for(const [orientation,size] of [['horizontal',[600,18,400]],['front',[600,400,18]],['side',[18,400,600]]]) {
    const data=panelMeshData(panel(orientation),features),p=data.positions,n=data.normals;
    const span=k=>{let lo=Infinity,hi=-Infinity;for(let i=k;i<p.length;i+=3){lo=Math.min(lo,p[i]);hi=Math.max(hi,p[i]);}return hi-lo;};
    assert.deepEqual([0,1,2].map(span).map(v=>Math.round(v*1e6)/1e6),size,orientation);
    // Triangle winding agrees with the stored normals (so lighting is not inverted).
    for(let i=0;i<p.length;i+=9) {
      const e1=[p[i+3]-p[i],p[i+4]-p[i+1],p[i+5]-p[i+2]],e2=[p[i+6]-p[i],p[i+7]-p[i+1],p[i+8]-p[i+2]];
      const c=[e1[1]*e2[2]-e1[2]*e2[1],e1[2]*e2[0]-e1[0]*e2[2],e1[0]*e2[1]-e1[1]*e2[0]];
      if(Math.hypot(...c)>1e-9) assert.ok(c[0]*n[i]+c[1]*n[i+1]+c[2]*n[i+2]>0,orientation+' winding');
    }
    assert.deepEqual(data.groups.map(g=>g.materialIndex),[0,1,2,3,4,5,6]);
    assert.equal(panelMeshData(panel(orientation),features),data,'same panel and features: cached');
  }
});

test('dense machining stays fast',()=>{
  const part={...panel(),length:2000,width:600},holes=Array.from({length:64},(_,i)=>({type:'drill',face:'A',u:40+i*30,v:100+(i%5)*90,diameter:5,depth:12}));
  const start=performance.now();cutPanelPolygons(part,holes);
  assert.ok(performance.now()-start<2000,'64 holes in under 2 s (a plain CSG took about a minute)');
});
