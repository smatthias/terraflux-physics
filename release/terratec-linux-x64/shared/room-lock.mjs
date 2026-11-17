// Locking fixes the room shell so only the interior can be edited.
//
// The lock is part of the room state (`room.locked`), so it travels through
// undo/redo, saving, recovery, JSON exchange and duplication. Locking and
// unlocking are separate edits: undo steps back through a lock before it can
// reach any wall edit made earlier, so walls never change while locked.

// The shell: wall geometry, wall identities, room height and the doors/windows
// hosted in the walls. Width and depth derive from the outline. Everything else
// in the room (furniture) is interior and stays editable.
export const SHELL_KEYS=Object.freeze(['outline','walls','height','openings']);
export const LOCKED_MESSAGE='Walls are locked · unlock walls to edit them';

export const isLocked=room=>room?.locked===true;
const canonical=value=>Array.isArray(value)?value.map(canonical):value&&typeof value==='object'?
  Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])])):value;
const same=(a,b)=>JSON.stringify(canonical(a))===JSON.stringify(canonical(b));
const sameShell=(a,b)=>SHELL_KEYS.every(key=>same(a[key],b[key]));

export function lockRoom(room) {
  if(isLocked(room)) throw Error('Walls are already locked');
  return {...room,locked:true};
}
export function unlockRoom(room) {
  if(!isLocked(room)) throw Error('Walls are already unlocked');
  const {locked:_locked,...rest}=room;
  return rest;
}
export function assertUnlocked(room,action='edit the walls') {
  if(isLocked(room)) throw Error(`Cannot ${action}: ${LOCKED_MESSAGE.toLowerCase()}`);
}

// Validates a transition between two room states (both already normalised).
// Lock changes must be pure; while locked, the shell must stay identical.
export function checkRoomEdit(before,after) {
  if(isLocked(before)!==isLocked(after)) {
    const {locked:_a,...restBefore}=before,{locked:_b,...restAfter}=after;
    if(!same(restBefore,restAfter)) throw Error('Lock or unlock the walls as a separate step');
    return;
  }
  if(isLocked(before) && !sameShell(before,after)) throw Error(LOCKED_MESSAGE);
}
