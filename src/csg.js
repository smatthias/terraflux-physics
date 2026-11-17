// Constructive solid geometry with BSP trees (after Evan Wallace's csg.js, MIT).
// Solids are lists of convex polygons; each polygon carries a `shared` tag so
// surfaces made by a cut remember which tool body (feature) produced them.
// Used to cut machining features out of panels for the designer's 3D view.
const EPSILON=1e-5;
const sub=(a,b)=>({x:a.x-b.x,y:a.y-b.y,z:a.z-b.z});
const dot=(a,b)=>a.x*b.x+a.y*b.y+a.z*b.z;
const cross=(a,b)=>({x:a.y*b.z-a.z*b.y,y:a.z*b.x-a.x*b.z,z:a.x*b.y-a.y*b.x});
const lerp=(a,b,t)=>({x:a.x+(b.x-a.x)*t,y:a.y+(b.y-a.y)*t,z:a.z+(b.z-a.z)*t});
const unit=a=>{const n=Math.hypot(a.x,a.y,a.z);return {x:a.x/n,y:a.y/n,z:a.z/n};};

class Plane {
  constructor(normal,w) {this.normal=normal;this.w=w;}
  static fromPoints(a,b,c) {const n=unit(cross(sub(b,a),sub(c,a)));return new Plane(n,dot(n,a));}
  clone() {return new Plane({...this.normal},this.w);}
  flip() {this.normal={x:-this.normal.x,y:-this.normal.y,z:-this.normal.z};this.w=-this.w;}
  // Sorts a polygon into the lists, splitting it when it spans the plane.
  splitPolygon(polygon,coplanarFront,coplanarBack,front,back) {
    const COPLANAR=0,FRONT=1,BACK=2,SPANNING=3;let type=0;const types=[];
    for(const v of polygon.vertices) {const t=dot(this.normal,v)-this.w,kind=t<-EPSILON?BACK:t>EPSILON?FRONT:COPLANAR;type|=kind;types.push(kind);}
    if(type===COPLANAR) (dot(this.normal,polygon.plane.normal)>0?coplanarFront:coplanarBack).push(polygon);
    else if(type===FRONT) front.push(polygon);
    else if(type===BACK) back.push(polygon);
    else {
      const f=[],b=[],n=polygon.vertices.length;
      for(let i=0;i<n;i++) {
        const j=(i+1)%n,ti=types[i],tj=types[j],vi=polygon.vertices[i],vj=polygon.vertices[j];
        if(ti!==BACK) f.push(vi);
        if(ti!==FRONT) b.push(vi);
        if((ti|tj)===SPANNING) {const t=(this.w-dot(this.normal,vi))/dot(this.normal,sub(vj,vi)),v=lerp(vi,vj,t);f.push(v);b.push(v);}
      }
      if(f.length>=3) front.push(new Polygon(f,polygon.shared,polygon.plane));
      if(b.length>=3) back.push(new Polygon(b,polygon.shared,polygon.plane));
    }
  }
}
export class Polygon {
  constructor(vertices,shared=null,plane) {this.vertices=vertices;this.shared=shared;this.plane=plane?plane.clone():Plane.fromPoints(vertices[0],vertices[1],vertices[2]);}
  clone() {return new Polygon(this.vertices.map(v=>({...v})),this.shared,this.plane);}
  flip() {this.vertices.reverse();this.plane.flip();}
}
class Node {
  constructor(polygons) {this.plane=null;this.front=null;this.back=null;this.polygons=[];if(polygons) this.build(polygons);}
  invert() {
    for(const p of this.polygons) p.flip();
    this.plane?.flip();this.front?.invert();this.back?.invert();
    [this.front,this.back]=[this.back,this.front];
  }
  clipPolygons(polygons) {
    if(!this.plane) return polygons.slice();
    let front=[],back=[];
    for(const p of polygons) this.plane.splitPolygon(p,front,back,front,back);
    if(this.front) front=this.front.clipPolygons(front);
    back=this.back?this.back.clipPolygons(back):[];
    return front.concat(back);
  }
  clipTo(bsp) {this.polygons=bsp.clipPolygons(this.polygons);this.front?.clipTo(bsp);this.back?.clipTo(bsp);}
  allPolygons() {return this.polygons.concat(this.front?.allPolygons()??[],this.back?.allPolygons()??[]);}
  build(polygons) {
    if(!polygons.length) return;
    if(!this.plane) this.plane=polygons[0].plane.clone();
    const front=[],back=[];
    for(const p of polygons) this.plane.splitPolygon(p,this.polygons,this.polygons,front,back);
    if(front.length) (this.front??=new Node()).build(front);
    if(back.length) (this.back??=new Node()).build(back);
  }
}
// a − b: the polygons of solid a with solid b removed.
export function subtract(a,b) {
  const A=new Node(a.map(p=>p.clone())),B=new Node(b.map(p=>p.clone()));
  A.invert();A.clipTo(B);B.clipTo(A);B.invert();B.clipTo(A);B.invert();A.build(B.allPolygons());A.invert();
  return A.allPolygons();
}

// ---- Primitive solids (outward-facing polygons).
export function boxSolid(min,max,shared=null) {
  const c=(x,y,z)=>({x:x?max.x:min.x,y:y?max.y:min.y,z:z?max.z:min.z});
  return [[0,4,6,2],[1,3,7,5],[0,1,5,4],[2,6,7,3],[0,2,3,1],[4,5,7,6]].map(face=>new Polygon(face.map(i=>c(i&1,i&2,i&4)),shared));
}
// Cylinder along world axis `axis` from `start` to `end` (coordinates on that axis).
export function cylinderSolid(centre,axis,start,end,radius,segments,shared=null) {
  const [a,b]={x:['y','z'],y:['z','x'],z:['x','y']}[axis];
  const at=(angle,along)=>{const p={x:centre.x,y:centre.y,z:centre.z};p[a]+=Math.cos(angle)*radius;p[b]+=Math.sin(angle)*radius;p[axis]=along;return p;};
  const ring=along=>Array.from({length:segments},(_,i)=>at(i/segments*Math.PI*2,along));
  const bottom=ring(start),top=ring(end),polygons=[];
  for(let i=0;i<segments;i++) {const j=(i+1)%segments;polygons.push(new Polygon([bottom[i],bottom[j],top[j],top[i]],shared));}
  polygons.push(new Polygon([...top],shared),new Polygon([...bottom].reverse(),shared));
  return polygons;
}
// Signed volume of a closed solid (divergence theorem); used to check cuts.
export function solidVolume(polygons) {
  let volume=0;
  for(const {vertices:v} of polygons) for(let i=1;i<v.length-1;i++) volume+=dot(v[0],cross(v[i],v[i+1]))/6;
  return volume;
}
