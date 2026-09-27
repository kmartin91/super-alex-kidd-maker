// Building parts (learnt by maker/tools/parts.py): terrains that tile
// themselves, special blocks, decorations, and the eraser.

import { state } from './state.js';
import { blockAt, putBlock } from './level.js';
import { blocksCanvas } from './graphics.js';
import { brushBlock } from './brush.js';

// Maps each terrain block to its terrain index (state.family).
export function buildFamilies() {
  state.family = new Map();
  (state.model.parts.terrains || []).forEach((t, i) => t.members.forEach((m) => state.family.set(m, i)));
}

// Re-picks the terrain blocks of a rectangle from their neighbours (auto-tiling).
export function retile(x0, y0, x1, y1) {
  for (let y = y0; y <= y1; y++) {
    for (let x = x0; x <= x1; x++) {
      const m = blockAt(x, y);
      if (m === undefined || !state.family.has(m)) continue;
      const f = state.family.get(m);
      const same = (dx, dy) => {
        const n = blockAt(x + dx, y + dy);
        return n === undefined || state.family.get(n) === f; // outside the map continues the terrain
      };
      const mask = (same(0, -1) ? 1 : 0) | (same(1, 0) ? 2 : 0) | (same(0, 1) ? 4 : 0) | (same(-1, 0) ? 8 : 0);
      putBlock(x, y, state.model.parts.terrains[f].tiles[mask]);
    }
  }
}

// Applies a block part at block (gx, gy). Returns true if something changed.
export function applyBrush(gx, gy, part = state.part) {
  if (part.kind === 'stamp') {
    const st = part.cells ? part : state.model.parts.stamps[part.index]; // hazards.js: stamps with their own blocks
    let changed = false;
    for (let j = 0; j < st.h; j++)
      for (let i = 0; i < st.w; i++)
        if (st.cells[j][i] >= 0) changed = putBlock(gx + i, gy + j, st.cells[j][i]) || changed;
    retile(gx - 1, gy - 1, gx + st.w, gy + st.h);
    return changed;
  }
  const changed = putBlock(gx, gy, brushBlock(part));
  retile(gx - 1, gy - 1, gx + 1, gy + 1);
  return changed;
}

// Fills a rectangle of blocks (inclusive) with a single-block part.
export function fillBlocks(r, part = state.part) {
  const v = brushBlock(part);
  for (let y = r.y0; y <= r.y1; y++)
    for (let x = r.x0; x <= r.x1; x++) putBlock(x, y, v);
  retile(r.x0 - 1, r.y0 - 1, r.x1 + 1, r.y1 + 1);
}

// Picture of a part: a small hill for terrains, the blocks themselves otherwise.
export function partCanvas(part) {
  const p = state.model.parts;
  if (part.kind === 'terrain') {
    const T = p.terrains[part.index].tiles;
    return blocksCanvas([[T[2 | 4], T[2 | 4 | 8], T[4 | 8]], [T[1 | 2], T[1 | 2 | 8], T[1 | 8]]], 3, 2);
  }
  if (part.kind === 'stamp') {
    const st = part.cells ? part : p.stamps[part.index];
    return blocksCanvas(st.cells, st.w, st.h);
  }
  return blocksCanvas([[brushBlock(part)]], 1, 1);
}
