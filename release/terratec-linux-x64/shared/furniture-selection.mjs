// Multi-selection rules for furniture. `active` is the item shown in the
// inspector; it is always a member of `ids` unless the selection is empty.
export function toggleSelection({ids,active},id) {
  if(ids.includes(id)) {
    const rest=ids.filter(other=>other!==id);
    return {ids:rest,active:active===id?rest.at(-1)??null:active};
  }
  return {ids:[...ids,id],active:id};
}
export function selectAll({active},allIds) {
  return {ids:[...allIds],active:allIds.includes(active)?active:allIds[0]??null};
}
// Drops items that no longer exist (after undo, cut, delete or another edit).
export function pruneSelection({ids,active},presentIds) {
  const present=new Set(presentIds),kept=ids.filter(id=>present.has(id));
  return {ids:kept,active:present.has(active)?active:kept[0]??null};
}
