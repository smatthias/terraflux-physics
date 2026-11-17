const copy = value => JSON.parse(JSON.stringify(value));
export function createHistory(room) { return {past:[],present:copy(room),future:[]}; }
export function commitHistory(state,room) {
  if(JSON.stringify(state.present)===JSON.stringify(room)) return state;
  return {past:[...state.past,copy(state.present)].slice(-100),present:copy(room),future:[]};
}
export function undoHistory(state) {
  if(!state.past.length) return state;
  return {past:state.past.slice(0,-1),present:copy(state.past.at(-1)),future:[copy(state.present),...state.future]};
}
export function redoHistory(state) {
  if(!state.future.length) return state;
  return {past:[...state.past,copy(state.present)],present:copy(state.future[0]),future:state.future.slice(1)};
}
