// The workbench tool chest: the hand tools of a traditional hand-tool workshop,
// grouped as a woodworker keeps them. The list follows the usual essential kit
// taught for hand-tool woodworking (measuring and marking, saws, planes,
// chisels and boring tools, shaping and finishing, workholding and sharpening).
//
// Each tool either drives one of the workbench actions with settings of its own
// (a dovetail saw is a saw with a 0.6 mm kerf, a jointer plane planes edges, a
// mortise gauge marks two lines) or is listed for reference because the design
// model cannot show what it does (curves, angles, sharpening).
import {round} from './furniture-design.mjs';
import {cutFrame} from './cutting-bench.mjs';

export const CHEST_GROUPS=Object.freeze(['Measuring and marking','Saws','Planes','Chisels and boring','Shaping and finishing','Workholding and sharpening']);
const tool=(id,group,name,use,action,preset={},note='')=>Object.freeze({id,group:CHEST_GROUPS[group],name,use,action,preset:Object.freeze(preset),note});
export const TOOL_CHEST=Object.freeze([
  tool('rule',0,'Folding rule and tape','Measure lengths and distances','tape'),
  tool('try-square',0,'Try square','Mark and check lines square to an edge','mark',{mode:'square'}),
  tool('combination-square',0,'Combination square','Mark a line a set distance from an end or edge','mark',{mode:'gauge'}),
  tool('marking-gauge',0,'Marking gauge','Scribe a line parallel to an edge, along the grain','mark',{mode:'gauge',direction:'along'}),
  tool('cutting-gauge',0,'Cutting gauge','Knife a line parallel to an end, across the grain','mark',{mode:'gauge',direction:'across'}),
  tool('mortise-gauge',0,'Mortise gauge','Scribe the two sides of a mortise or tenon at once','mark',{mode:'mortise'}),
  tool('marking-knife',0,'Marking knife','Knife a crisp line through the cross','mark',{mode:'square'}),
  tool('dividers',0,'Dividers','Step off equal spacings','mark',{mode:'divide'}),
  tool('awl',0,'Awl','Mark a point to drill at','mark',{mode:'point'}),
  tool('sliding-bevel',0,'Sliding bevel','Set out and copy angles',null,{},'Angles are not part of the design model yet; pieces stay square.'),
  tool('winding-sticks',0,'Winding sticks and straightedge','Check a board for twist and flatness',null,{},'Pieces in the design are always flat and true.'),
  tool('panel-saw-crosscut',1,'Crosscut panel saw','Cut boards to length, across the grain','saw',{along:'length',kerf:1.6}),
  tool('panel-saw-rip',1,'Rip panel saw','Cut boards to width, along the grain','saw',{along:'width',kerf:1.6}),
  tool('carcass-saw',1,'Carcass saw','Fine crosscuts, shoulders and housings','saw',{along:'length',kerf:.9}),
  tool('tenon-saw',1,'Tenon saw','Rip tenon cheeks and fine rip cuts','saw',{along:'width',kerf:.9}),
  tool('dovetail-saw',1,'Dovetail saw','The finest rip cuts, for dovetails','saw',{along:'length',kerf:.6},'Dovetails themselves are not modelled; it cuts to size with a 0.6 mm kerf.'),
  tool('coping-saw',1,'Coping saw','Curves and dovetail waste',null,{},'Curved cuts are not part of the design model yet.'),
  tool('jack-plane',2,'Jack plane (No. 5)','Rough stock to size','plane',{side:'up',amount:1}),
  tool('smoothing-plane',2,'Smoothing plane (No. 4)','Final surface before finishing','plane',{side:'up',amount:.1}),
  tool('jointer-plane',2,'Jointer plane (No. 7)','Straighten and square long edges','plane',{side:'edge',amount:.5}),
  tool('block-plane',2,'Block plane','Trim end grain and small parts','plane',{side:'end',amount:.5}),
  tool('rabbet-plane',2,'Rabbet plane (moving fillister)','Cut rebates along an edge','rebate'),
  tool('plough-plane',2,'Plough plane','Cut grooves along the grain, for panels and drawer bottoms','plough',{widths:[3,4,5,6,8,10,12]}),
  tool('router-plane',2,'Router plane','Bring housings and grooves to an exact depth','router'),
  tool('shoulder-plane',2,'Shoulder plane','Trim the walls of rebates, tenons and housings','shoulder'),
  tool('card-scraper',2,'Card scraper','Take the finest shaving off a surface','plane',{side:'up',amount:.05}),
  tool('bench-chisels',3,'Bench chisels','Chop and pare joints, notches and recesses','chisel',{widths:[6,10,12,16,20,25],depth:6}),
  tool('mortise-chisel',3,'Mortise chisel','Chop deep mortises','chisel',{widths:[6,8,10,12],depth:25}),
  tool('paring-chisel',3,'Paring chisel','Pare to a line with hand pressure','chisel',{widths:[12,20,25],depth:3}),
  tool('brace-and-bits',3,'Brace and auger bits','Bore larger holes','bore',{bits:[6,8,10,12,16,19,22,25],depth:12}),
  tool('hand-drill',3,'Hand drill (eggbeater)','Drill small holes','bore',{bits:[2,3,4,5,6,8],depth:10}),
  tool('gimlet',3,'Gimlet','Start screw holes','bore',{bits:[2,3,4],depth:8}),
  tool('mallet',3,'Mallet','Drive chisels',null,{},'Used with the chisels; nothing to set.'),
  tool('spokeshave',4,'Spokeshave','Shape curved edges',null,{},'Curved edges are not part of the design model yet.'),
  tool('rasps-files',4,'Rasps and files','Shape curves and round-overs',null,{},'Curves and round-overs are not part of the design model yet.'),
  tool('brush',4,'Finishing brush','Stain, oil or paint','brush'),
  tool('shooting-board',5,'Shooting board','Square ends with a plane on its side','plane',{side:'end',amount:.2}),
  tool('bench-hook',5,'Bench hook','Hold work for crosscutting',null,{},'Workholding; the bench holds the piece in the model.'),
  tool('holdfast-vise',5,'Vise and holdfasts','Hold work on and at the bench',null,{},'Workholding; the bench holds the piece in the model.'),
  tool('clamps',5,'Clamps','Hold glue-ups and work',null,{},'Gluing pieces together is not part of the design model yet.'),
  tool('sharpening',5,'Sharpening stones and strop','Keep edges sharp',null,{},'Tools on the bench are always sharp.'),
]);
export const chestTool=id=>TOOL_CHEST.find(t=>t.id===id)??null;

// ---- Which side a plane preset works on, for this piece and face.
export function planeSide(part,face,preset) {
  if(part.shape==='panel') return {up:face,edge:'v1',end:'u1'}[preset];
  if(part.shape==='bar') return {up:'height1',edge:'width1',end:'length1'}[preset];
  return 'length1';
}
// ---- Gauges and dividers: marks (see workbench.mjs markLine for their form).
export function gaugeMarks(part,face,{mode,direction='along',from='near',distance=0,width=0,parts=2}) {
  const f=cutFrame(part,face),length=direction==='across'?f.a.length:f.b.length,at=d=>round(from==='near'?d:length-d);
  const mark=value=>{if(!(value>=0&&value<=length)) throw Error(`The mark must be on the piece (0–${length} mm from the ${direction==='across'?'start':'edge'})`);return {direction,at:round(value)};};
  if(mode==='gauge') return [mark(at(distance))];
  if(mode==='mortise') {
    if(!(width>0)) throw Error('Set the mortise width on the gauge');
    return from==='near'?[mark(distance),mark(distance+width)]:[mark(length-distance-width),mark(length-distance)];
  }
  if(mode==='divide') {
    if(!Number.isInteger(parts)||parts<2||parts>50) throw Error('Divide into 2–50 equal parts');
    return Array.from({length:parts-1},(_,i)=>mark(length*(i+1)/parts));
  }
  throw Error('Unknown marking');
}
// ---- Plough plane: a groove along the grain (the length), `distance` from the
// near or far edge (the plane's fence), the iron wide.
export function ploughGroove(part,face,{distance,width,depth,from='near'}) {
  const f=cutFrame(part,face);
  if(f.profile) throw Error('The plough plane works on panels');
  if(!(depth>0&&depth<f.depthLimit)) throw Error(`Groove depth must be between 0 and ${f.depthLimit} mm`);
  const v=from==='near'?distance:f.b.length-distance-width;
  return {type:'groove',face,u:0,v:round(v),length:f.a.length,width,depth:round(depth)};
}
// ---- Housing (dado): across the whole face at `at` along the length.
export function housing(part,face,{at,width,depth}) {
  const f=cutFrame(part,face);
  if(f.profile) throw Error('Housings are cut in panels');
  if(!(depth>0&&depth<f.depthLimit)) throw Error(`Housing depth must be between 0 and ${f.depthLimit} mm`);
  if(!(width>0)) throw Error('Enter the housing width');
  return {type:'groove',face,u:round(at),v:0,length:round(width),width:f.b.length,depth:round(depth)};
}
// ---- Shoulder plane: widen a groove, rebate or pocket by `amount` toward its
// lower (u or v = 0) or higher side, across its run (a groove along the length
// gets wider across it).
export function widenFeature(feature,{toward='high',amount}) {
  if(!(amount>0)) throw Error('Enter how much to trim off the wall');
  const across=feature.length>=feature.width?{pos:'v',size:'width'}:{pos:'u',size:'length'};
  const next={[across.size]:round(feature[across.size]+amount)};
  if(toward==='low') next[across.pos]=round(feature[across.pos]-amount);
  return next;
}
