// Selecting a zone of the level (the selection tool, key S), then copying
// (Ctrl+C), cutting (Ctrl+X) or clearing it (Delete), and pasting it
// elsewhere (Ctrl+V, then a click where its top-left corner goes).
// A zone holds its blocks and what stands in it (enemies, bosses; not the
// level end, which stays unique).

import { state, BLOCK } from './state.js';
import { blockAt, putBlock, locate, listOrigin } from './level.js';
import { retile } from './parts.js';
import { GOAL_TYPE, clampEntity } from './entities.js';
import { pushUndo } from './history.js';
import { render } from './render.js';
import { toast } from './toast.js';
import { t } from './i18n.js';

let clipboard = null;

// Level-pixel rectangle of the selection (blocks x0..x1, y0..y1 inclusive).
const pixels = (r) => ({ x: r.x0 * BLOCK, y: r.y0 * BLOCK, w: (r.x1 - r.x0 + 1) * BLOCK, h: (r.y1 - r.y0 + 1) * BLOCK });

// Records inside the selection: [{ lists, list, index, rec, px, py }].
function recordsIn(r) {
  const box = pixels(r), found = [];
  for (const [lists, special] of [[state.model.entities, false], [state.model.specials, true]]) {
    lists.forEach((recs, list) => {
      const o = listOrigin(list);
      if (!o) return;
      recs.forEach((rec, index) => {
        if (rec.type === undefined || rec.type === GOAL_TYPE) return;
        const px = o.x + rec.x, py = o.y + rec.y;
        if (px >= box.x && px < box.x + box.w && py >= box.y && py < box.y + box.h) found.push({ lists, list, index, rec, px, py, special });
      });
    });
  }
  return found;
}

export function copySelection() {
  const r = state.selection;
  if (!r) return false;
  const w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
  const blocks = Array.from({ length: h }, (_, j) => Array.from({ length: w }, (_, i) => {
    const b = blockAt(r.x0 + i, r.y0 + j);
    return b === undefined ? -1 : b;
  }));
  const box = pixels(r);
  const records = recordsIn(r).map(({ rec, px, py, special }) => ({ rec: { ...rec }, dx: px - box.x, dy: py - box.y, special }));
  clipboard = { w, h, blocks, records };
  const size = t('{w} × {h} blocs', { w, h }), n = records.length;
  const what = !n ? size : n > 1 ? t('{size}, {n} objets', { size, n }) : t('{size}, {n} objet', { size, n });
  toast(t('Zone copiée ({what}) · Ctrl+V pour la coller', { what }));
  return true;
}

// Empties the selection: sky blocks, nothing standing in it.
export function clearSelection() {
  const r = state.selection;
  if (!r) return false;
  pushUndo();
  for (let y = r.y0; y <= r.y1; y++) for (let x = r.x0; x <= r.x1; x++) putBlock(x, y, state.model.parts.eraser);
  // Remove from the end of each list so that indexes stay right.
  const found = recordsIn(r).sort((a, b) => b.index - a.index);
  for (const f of found) f.lists[f.list].splice(f.index, 1);
  retile(r.x0 - 1, r.y0 - 1, r.x1 + 1, r.y1 + 1);
  state.selected = null;
  render();
  return true;
}

export function cutSelection() {
  if (copySelection()) clearSelection();
}

// Ctrl+V: the copied zone follows the mouse until a click drops it.
export function startPaste() {
  if (!clipboard) { toast(t('Rien à coller : sélectionne une zone et copie-la d\'abord (Ctrl+C)')); return; }
  state.paste = clipboard;
  state.selection = null;
  render();
}

// Drops the copied zone with its top-left corner at block (gx, gy).
export function pasteAt(gx, gy) {
  const c = state.paste;
  if (!c) return;
  pushUndo();
  for (let j = 0; j < c.h; j++)
    for (let i = 0; i < c.w; i++)
      if (c.blocks[j][i] >= 0) putBlock(gx + i, gy + j, c.blocks[j][i]);
  retile(gx - 1, gy - 1, gx + c.w, gy + c.h);
  for (const { rec, dx, dy, special } of c.records) {
    const loc = locate(gx * BLOCK + dx, gy * BLOCK + dy);
    if (!loc || loc.list === null) continue; // outside the level
    const copy = { ...rec, x: loc.lx, y: loc.ly };
    clampEntity(copy);
    (special ? state.model.specials : state.model.entities)[loc.list].push(copy);
  }
  state.paste = null;
  state.selection = { x0: gx, y0: gy, x1: gx + c.w - 1, y1: gy + c.h - 1 };
  render();
}

export const pasteSize = () => state.paste && { w: state.paste.w, h: state.paste.h };
