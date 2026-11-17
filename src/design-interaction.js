// Pointer state machine for the furniture designer canvas, independent of the
// Vue component so it can be driven in tests with a real camera and scene.
//
// Move tool: press on a piece and drag more than 4 px to move it on the view's
// drag plane (Shift in 3D moves vertically). Dragging a piece that is part of a
// multi-selection moves the whole selection, snapped by the grabbed piece.
// Selecting: a click selects one piece; Shift-click or Ctrl/⌘-click adds or
// removes it (Ctrl/⌘ never starts a drag; Shift only toggles when not dragged).
// Rotate tool: press on a ring around the selection and drag around it; the
// selection turns rigidly about its centre, about a world or local axis (local
// axes also in the orthographic views).
// While dragging, onPreview receives the edited design; on release onCommit
// receives it once, so each drag is one undo step. cancel() discards a drag.
import * as THREE from 'three';
import {pickDesign} from './design-scene.js';
import {movePlane,dragOffset,movedCorner,ringAngle,ringDelta,pickRing,pixelSize} from './design-gizmo.js';
import {rotatePartsAbout,moveParts} from '../shared/furniture-design-edit.mjs';

const round1=v=>Math.round(v*10)/10;
export function createDesignInteraction(ctx) {
  // ctx: element, camera(), group(), gizmo(), view(), tool(), design(), selectedIds(),
  //      axisMode() ('world' or 'local'), gridStep(), angleStep(),
  //      onPreview(design, info), onCommit(design, message), onSelect(hit, {toggle})
  let press=null,drag=null;
  function ndc(event) {const rect=ctx.element.getBoundingClientRect();return {x:(event.clientX-rect.left)/rect.width*2-1,y:-(event.clientY-rect.top)/rect.height*2+1};}
  function planeHit(event,plane) {
    const p=ndc(event),ray=new THREE.Raycaster();ray.setFromCamera(new THREE.Vector2(p.x,p.y),ctx.camera());
    return ray.ray.intersectPlane(plane,new THREE.Vector3());
  }
  const layerLocked=(design,layerId)=>design.layers.find(l=>l.id===layerId)?.locked;
  function begin(event,state) {
    event.preventDefault?.();event.stopImmediatePropagation?.();
    drag={...state,pointerId:event.pointerId,start:{x:event.clientX,y:event.clientY}};
    ctx.element.setPointerCapture?.(event.pointerId);
  }
  function pointerDown(event) {
    const group=ctx.group(),camera=ctx.camera();
    if(event.button!==0||!group||!camera) return;
    const ids=ctx.selectedIds(),gizmo=ctx.gizmo();
    if(ctx.tool()==='rotate'&&ids.length&&gizmo) {
      const p=ndc(event),centre={x:gizmo.position.x,y:gizmo.position.y,z:gizmo.position.z};
      const ring=pickRing(gizmo,camera,p.x,p.y,pixelSize(camera,centre,ctx.element.getBoundingClientRect().height)*7);
      if(ring) {
        const n=ring.basis.axis,plane=new THREE.Plane().setFromNormalAndCoplanarPoint(new THREE.Vector3(n.x,n.y,n.z),new THREE.Vector3(centre.x,centre.y,centre.z));
        const hit=planeHit(event,plane),about=ctx.axisMode()==='local'?n:ring.axis;
        if(hit) {begin(event,{kind:'rotate',axis:ring.axis,about,basis:ring.basis,ids,centre,plane,startAngle:ringAngle(hit,centre,ring.basis),moved:true});return;}
      }
    }
    const p=ndc(event),picked=pickDesign(group,camera,p.x,p.y,true),hit=picked&&{partId:picked.partId,featureId:picked.featureId};
    const toggle=!!(event.ctrlKey||event.metaKey);
    if(toggle) {
      event.preventDefault?.();event.stopImmediatePropagation?.();
      if(hit) ctx.onSelect(hit,{toggle:true});
      press=null;return;
    }
    press={x:event.clientX,y:event.clientY,hit,shift:!!event.shiftKey};
    if(ctx.tool()==='move'&&hit) {
      const design=ctx.design(),target=design.parts.find(q=>q.id===hit.partId);
      const group=ids.length>1&&ids.includes(hit.partId)?ids:[hit.partId];
      const parts=design.parts.filter(q=>group.includes(q.id));
      if(target&&!parts.some(q=>layerLocked(design,q.layer))) begin(event,{kind:'move',part:target,ids:group,grab:picked.point,moved:false});
    }
  }
  function pointerMove(event) {
    if(!drag||event.pointerId!==drag.pointerId) return;
    const design=ctx.design(),view=ctx.view();
    let next,info;
    if(drag.kind==='move') {
      if(!drag.moved) {
        if(Math.hypot(event.clientX-drag.start.x,event.clientY-drag.start.y)<=4) return;
        const vertical=view==='3D'&&!!event.shiftKey;
        // The plane passes through the grabbed point, so that point follows the pointer.
        drag.plane=movePlane(view,drag.grab,ctx.camera().getWorldDirection(new THREE.Vector3()),vertical);drag.vertical=vertical;
        // Measure from the press, not from where the threshold was crossed, so
        // the piece stays under the pointer.
        drag.origin=planeHit({clientX:drag.start.x,clientY:drag.start.y},drag.plane);if(!drag.origin) return;
        drag.moved=true;
        if(drag.ids.length===1) ctx.onSelect({partId:drag.part.id,featureId:null},{toggle:false});
      }
      const point=planeHit(event,drag.plane);if(!point) return;
      const corner=movedCorner(drag.part,dragOffset(view,drag.origin,point,drag.vertical),ctx.gridStep());
      const offset={x:corner.x-drag.part.x,y:corner.y-drag.part.y,z:corner.z-drag.part.z};
      try {next=moveParts(design,drag.ids,offset);} catch {return;}
      info=`Move ${['x','y','z'].filter(k=>offset[k]!==0).map(k=>k+' '+round1(offset[k])).join(' · ')||'0'} mm${drag.ids.length>1?' · '+drag.ids.length+' pieces':''}`;
    } else {
      const point=planeHit(event,drag.plane);if(!point) return;
      const delta=ringDelta(drag.startAngle,ringAngle(point,drag.centre,drag.basis),ctx.angleStep());
      try {next=delta===0?design:rotatePartsAbout(design,drag.ids,drag.about,delta);} catch {return;}
      info=`Rotate about ${typeof drag.about==='string'?'':'local '}${drag.axis} ${delta>0?'+':''}${round1(delta)}°${drag.ids.length>1?' · '+drag.ids.length+' pieces':''}`;
    }
    drag.result=next;ctx.onPreview(next,info);
  }
  function release() {
    if(drag) ctx.element.releasePointerCapture?.(drag.pointerId);
    const finished=drag;drag=null;ctx.onPreview(null,'');return finished;
  }
  function pointerUp(event) {
    if(drag&&event.pointerId===drag.pointerId) {
      const finished=release();
      if(finished.moved&&finished.result) {press=null;ctx.onCommit(finished.result,finished.kind==='move'?'Piece moved':'Piece rotated');return;}
      if(finished.kind==='rotate') {press=null;return;}
    }
    const start=press;press=null;
    if(!start||Math.hypot(event.clientX-start.x,event.clientY-start.y)>5) return;
    if(start.shift) {if(start.hit) ctx.onSelect(start.hit,{toggle:true});return;}
    ctx.onSelect(start.hit,{toggle:false});
  }
  function cancel() {if(!drag) return false;release();press=null;return true;}
  return {pointerDown,pointerMove,pointerUp,cancel,dragging:()=>!!drag};
}
