// Rotation and oriented-box maths for furniture design pieces (no three.js, so
// shared validation and the server can use it).
//
// A piece's optional rotation {x, y, z} is in degrees, applied about the piece's
// centre in three.js 'XYZ' order (R = Rx · Ry · Rz). Its position (x, y, z) stays
// the minimum corner of the unrotated piece, so the piece's local frame is the
// world frame before rotation. Unrotated pieces behave exactly as before.
const RAD=Math.PI/180;
export const IDENTITY=Object.freeze([[1,0,0],[0,1,0],[0,0,1]]);
const multiply=(a,b)=>a.map((row,i)=>[0,1,2].map(j=>row[0]*b[0][j]+row[1]*b[1][j]+row[2]*b[2][j]));
export const transpose=m=>[0,1,2].map(i=>[0,1,2].map(j=>m[j][i]));
const apply=(m,v)=>({x:m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z,y:m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z,z:m[2][0]*v.x+m[2][1]*v.y+m[2][2]*v.z});
function axisMatrix(axis,degrees) {
  const c=Math.cos(degrees*RAD),s=Math.sin(degrees*RAD);
  if(axis==='x') return [[1,0,0],[0,c,-s],[0,s,c]];
  if(axis==='y') return [[c,0,s],[0,1,0],[-s,0,c]];
  return [[c,-s,0],[s,c,0],[0,0,1]];
}
export function rotationMatrix(rotation) {
  if(!rotation) return IDENTITY;
  return multiply(multiply(axisMatrix('x',rotation.x),axisMatrix('y',rotation.y)),axisMatrix('z',rotation.z));
}
// Euler angles (XYZ) from a rotation matrix, matching three.js.
function eulerFromMatrix(m) {
  const y=Math.asin(Math.max(-1,Math.min(1,m[0][2])));
  const [x,z]=Math.abs(m[0][2])<.9999999?[Math.atan2(-m[1][2],m[2][2]),Math.atan2(-m[0][1],m[0][0])]:[Math.atan2(m[2][1],m[1][1]),0];
  return {x:x/RAD,y:y/RAD,z:z/RAD};
}
// Angles in (−180, 180], rounded to 1e-6°; a zero rotation is omitted (undefined).
export function normaliseRotation(rotation) {
  if(rotation===undefined||rotation===null) return undefined;
  const out={};
  for(const axis of ['x','y','z']) {
    const value=rotation[axis]??0;
    if(typeof value!=='number'||!Number.isFinite(value)) throw Error('Rotation angles must be finite degrees');
    let angle=((value%360)+360)%360;if(angle>180) angle-=360;
    angle=Number(angle.toFixed(6));out[axis]=angle===0||angle===-180?Math.abs(angle):angle;
  }
  return out.x||out.y||out.z?out:undefined;
}
// Rotation by `degrees` about a unit world direction (Rodrigues' formula).
export function vectorMatrix(axis,degrees) {
  const n=Math.hypot(axis.x,axis.y,axis.z);
  if(!(n>1e-12)) throw Error('Rotation axis must have a direction');
  const x=axis.x/n,y=axis.y/n,z=axis.z/n,c=Math.cos(degrees*RAD),s=Math.sin(degrees*RAD),t=1-c;
  return [[t*x*x+c,t*x*y-s*z,t*x*z+s*y],[t*x*y+s*z,t*y*y+c,t*y*z-s*x],[t*x*z-s*y,t*y*z+s*x,t*z*z+c]];
}
const WORLD={x:{x:1,y:0,z:0},y:{x:0,y:1,z:0},z:{x:0,y:0,z:1}};
// `axis` is 'x', 'y', 'z' (world) or a direction vector.
export const axisDirection=axis=>typeof axis==='string'?WORLD[axis]:axis;
export const turnMatrix=(axis,degrees)=>typeof axis==='string'?axisMatrix(axis,degrees):vectorMatrix(axis,degrees);
// Turns a piece about a world axis or direction through its centre.
export function rotateAboutWorldAxis(rotation,axis,degrees) {
  return normaliseRotation(eulerFromMatrix(multiply(turnMatrix(axis,degrees),rotationMatrix(rotation))));
}
// A piece's own x, y or z axis in world space (a column of its rotation).
export function localAxis(rotation,axis) {
  const m=rotationMatrix(rotation),j={x:0,y:1,z:2}[axis];
  return {x:m[0][j],y:m[1][j],z:m[2][j]};
}

// Local box {min, max} of the unrotated piece → world corners.
export function boxCorners(box,rotation,centre) {
  const m=rotationMatrix(rotation),corners=[];
  for(const x of [box.min.x,box.max.x]) for(const y of [box.min.y,box.max.y]) for(const z of [box.min.z,box.max.z]) {
    const r=apply(m,{x:x-centre.x,y:y-centre.y,z:z-centre.z});
    corners.push({x:centre.x+r.x,y:centre.y+r.y,z:centre.z+r.z});
  }
  return corners;
}
export function boundsOf(points) {
  const min={},max={};
  for(const axis of ['x','y','z']) {min[axis]=Math.min(...points.map(p=>p[axis]));max[axis]=Math.max(...points.map(p=>p[axis]));}
  return {min,max};
}
// Maps a world point into a piece's local frame (centre c, rotation matrix m).
export const toLocal=(m,c,p)=>{const r=apply(transpose(m),{x:p.x-c.x,y:p.y-c.y,z:p.z-c.z});return {x:c.x+r.x,y:c.y+r.y,z:c.z+r.z};};
// True when two frames differ only by quarter turns, so boxes stay axis-aligned.
export const squareTo=(a,b)=>multiply(transpose(a),b).every(row=>row.every(v=>Math.abs(v)<1e-9||Math.abs(Math.abs(v)-1)<1e-9));

// Separating-axis test for two boxes given as 8 corners each, with box axes.
// Returns the smallest penetration depth (≤ 0 when the boxes only touch or miss).
export function boxPenetration(cornersA,axesA,cornersB,axesB) {
  const cross=(u,v)=>({x:u.y*v.z-u.z*v.y,y:u.z*v.x-u.x*v.z,z:u.x*v.y-u.y*v.x});
  const axes=[...axesA,...axesB];
  for(const u of axesA) for(const v of axesB) {const c=cross(u,v),n=Math.hypot(c.x,c.y,c.z);if(n>1e-9) axes.push({x:c.x/n,y:c.y/n,z:c.z/n});}
  let depth=Infinity;
  for(const axis of axes) {
    const project=corners=>corners.map(p=>p.x*axis.x+p.y*axis.y+p.z*axis.z);
    const a=project(cornersA),b=project(cornersB);
    depth=Math.min(depth,Math.min(Math.max(...a),Math.max(...b))-Math.max(Math.min(...a),Math.min(...b)));
    if(depth<=0) return depth;
  }
  return depth;
}
export const matrixAxes=m=>[0,1,2].map(j=>({x:m[0][j],y:m[1][j],z:m[2][j]}));
