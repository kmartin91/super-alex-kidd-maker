// Mouse on the map, Mario Maker style:
//   click / drag            place the item in hand (blocks paint while dragging)
//   press on an entity      pick it up and move it; a simple click selects it
//   drag Alex ("Départ")    move where the level starts
//   selection tool (S)      a zone to copy / cut / clear, pasted with Ctrl+V (clipboard.js)
//   right click / drag      erase (entities first, then blocks)
//   Shift + drag            fill a rectangle with the block in hand
//   Alt + click             take the block under the mouse in hand
// Parts can also be dragged from the shelf and dropped on the map.

import { $ } from './dom.js';
import { state, BLOCK } from './state.js';
import { locate, eventLevelPos } from './level.js';
import { selectPart, isBlockPart } from './brush.js';
import { applyBrush, fillBlocks } from './parts.js';
import { entityAt, moveEntityTo, addEntity, placeGoal, placeBoss, confirmRemove, removeEntity, startAt, moveStartTo } from './entities.js';
import { pushUndo, dropUndo } from './history.js';
import { render, renderGhost, snapEntity } from './render.js';
import { pasteAt } from './clipboard.js';

const blockOf = (p) => ({ x: Math.floor(p.x / BLOCK), y: Math.floor(p.y / BLOCK) });

// Places a part at a level pixel. Returns true if the map changed.
function place(part, p) {
  const loc = locate(p.x, p.y);
  if (!loc || loc.screen === null) return false;
  if (isBlockPart(part)) {
    const b = blockOf(p);
    return applyBrush(b.x, b.y, part);
  }
  const snapped = snapEntity(p);
  const at = locate(snapped.x, snapped.y);
  const spot = at && at.list !== null ? at : loc;
  if (part.kind === 'goal') { placeGoal(spot); return true; }
  if (part.kind === 'door') { addEntity(0x4C, spot, part.data); return true; }
  if (part.kind === 'boss') { placeBoss(spot, part); return true; }
  return addEntity(part.type, spot);
}

// Erases what is under a level pixel: an entity, else the block.
function eraseAt(p) {
  const hit = entityAt(p.x, p.y);
  if (hit && hit.special) {
    // Bosses and the level end are only removed once confirmed (in a dialog,
    // so not as part of the gesture).
    confirmRemove(hit).then((yes) => { if (yes) { pushUndo(); removeEntity(hit); render(); } });
    return false;
  }
  if (hit) {
    removeEntity(hit);
    return true;
  }
  const b = blockOf(p);
  return applyBrush(b.x, b.y, { kind: 'eraser' });
}

function pick(p) {
  const loc = locate(p.x, p.y);
  if (!loc || loc.screen === null) return;
  const id = state.model.screens[loc.screen].blocks[loc.block];
  selectPart(state.family.has(id) ? { kind: 'terrain', index: state.family.get(id) } : { kind: 'block', id });
}

function onMouseDown(ev) {
  if (ev.button === 1) return;
  const p = eventLevelPos(ev);
  if (ev.altKey) { pick(p); return; }
  // A copied zone being placed (Ctrl+V): a click drops it, a right click cancels.
  if (state.paste) {
    if (ev.button === 2) { state.paste = null; render(); return; }
    const b = blockOf(p);
    pasteAt(b.x, b.y);
    return;
  }
  // The selection tool: a rectangle of blocks.
  if (state.part.kind === 'select' && ev.button === 0) {
    const b = blockOf(p);
    state.selection = null;
    state.gesture = { kind: 'select', a: b };
    state.rect = { x0: b.x, y0: b.y, x1: b.x, y1: b.y };
    renderGhost();
    return;
  }
  const erase = ev.button === 2 || state.part.kind === 'eraser';
  const hit = !erase && entityAt(p.x, p.y);
  pushUndo();
  if (ev.button === 0 && startAt(p.x, p.y)) {
    state.selected = null;
    state.gesture = { kind: 'start', from: p, moved: false };
  } else if (hit) {
    // Pick up an entity: moving it is a drag, not moving it is a selection.
    state.selected = hit;
    state.gesture = { kind: 'move', sel: hit, from: p, moved: false };
  } else if (erase) {
    state.selected = null;
    state.gesture = { kind: 'erase', last: blockOf(p), changed: eraseAt(p) };
  } else if (ev.shiftKey && isBlockPart(state.part) && state.part.kind !== 'stamp') {
    const b = blockOf(p);
    state.gesture = { kind: 'rect', a: b };
    state.rect = { x0: b.x, y0: b.y, x1: b.x, y1: b.y };
  } else {
    state.selected = null;
    const changed = place(state.part, p);
    // Blocks keep painting while the mouse moves; entities are placed one per click.
    if (isBlockPart(state.part)) state.gesture = { kind: 'paint', last: blockOf(p), changed };
    else if (!changed) dropUndo();
  }
  render();
}

function onMouseMove(ev) {
  const p = eventLevelPos(ev);
  state.hover = p;
  const g = state.gesture;
  if (!g) {
    $('map').style.cursor = state.paste ? 'copy' : startAt(p.x, p.y) || entityAt(p.x, p.y) ? 'grab' : 'crosshair';
    renderGhost();
    return;
  }
  if (g.kind === 'start') {
    if (!g.moved && Math.hypot(p.x - g.from.x, p.y - g.from.y) < 3) return;
    g.moved = true;
    $('map').style.cursor = 'grabbing';
    moveStartTo(p.x, p.y);
    render();
  } else if (g.kind === 'move') {
    if (!g.moved && Math.hypot(p.x - g.from.x, p.y - g.from.y) < 3) return;
    g.moved = true;
    $('map').style.cursor = 'grabbing';
    const s = snapEntity(p);
    g.sel = state.selected = moveEntityTo(g.sel, s.x, s.y);
    render();
  } else if (g.kind === 'rect' || g.kind === 'select') {
    const b = blockOf(p);
    state.rect = { x0: Math.min(g.a.x, b.x), y0: Math.min(g.a.y, b.y), x1: Math.max(g.a.x, b.x), y1: Math.max(g.a.y, b.y) };
    renderGhost();
  } else {
    const b = blockOf(p);
    if (g.last && g.last.x === b.x && g.last.y === b.y) return;
    g.last = b;
    if (g.kind === 'erase' ? eraseAt(p) : place(state.part, p)) { g.changed = true; render(); }
  }
}

function onMouseUp() {
  const g = state.gesture;
  if (!g) return;
  if (g.kind === 'rect') fillBlocks(state.rect);
  if (g.kind === 'select') {
    state.selection = state.rect;
    state.gesture = null;
    state.rect = null;
    render();
    return;
  }
  if (((g.kind === 'move' || g.kind === 'start') && !g.moved) || ((g.kind === 'paint' || g.kind === 'erase') && !g.changed)) dropUndo();
  state.gesture = null;
  state.rect = null;
  render();
}

function onDrop(ev) {
  ev.preventDefault();
  const data = ev.dataTransfer.getData('text/x-part');
  if (!data) return;
  const part = JSON.parse(data);
  pushUndo();
  if (!place(part, eventLevelPos(ev))) { dropUndo(); return; }
  selectPart(part);
  render();
}

export function bindMap() {
  const map = $('map');
  map.addEventListener('contextmenu', (ev) => ev.preventDefault());
  map.addEventListener('mousedown', onMouseDown);
  map.addEventListener('mousemove', onMouseMove);
  map.addEventListener('mouseleave', () => { state.hover = null; renderGhost(); });
  window.addEventListener('mouseup', onMouseUp);
  map.addEventListener('dragover', (ev) => ev.preventDefault());
  map.addEventListener('drop', onDrop);
}
