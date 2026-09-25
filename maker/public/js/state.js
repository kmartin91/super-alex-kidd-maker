// Shared editor state.
//
// Level model (GET /api/level/N, produced by maker/tools/leveledit.py export):
//   { level, name, kind, columns, rows,
//     grid[row][col] = { screen, entities } | null    screen id + entity list index
//     screens[s] = { blocks: [16*12 metatile ids] }   shared by every cell showing s
//     entities[e] = [{ type, x, y, data }]            enemies/items of entity list e
//     specials[e] = [records]                          bosses, fixed objects, level end
//     metatiles[256] = [TL, TR, BL, BR name-table words], metatileClasses[256],
//     parts = { eraser, terrains, blocks, stamps }     building parts (maker/tools/parts.py)
//     entityTypes = [{ id, name, levels }], notes = [text],
//     theme, themeMusic, surprises, surpriseItems, levelNames, canExtend }
// Video state (from the running game): { vram: base64, cram: [32] }.

import { $ } from './dom.js';

export const SCREEN_W = 16, SCREEN_H = 12, BLOCK = 16;
export const SCREEN_PX_W = SCREEN_W * BLOCK, SCREEN_PX_H = SCREEN_H * BLOCK;

export const state = {
  level: 1, model: null, video: null,
  tiles: null, blockCanvases: [], blockColors: [],
  zoom: 3, showGrid: true, showSolid: false,
  // Item in hand: { kind: 'terrain', index } | { kind: 'block', id } | { kind: 'stamp', index }
  //   | { kind: 'eraser' } | { kind: 'entity', type } | { kind: 'goal' }
  part: { kind: 'block', id: 0 },
  lastPart: null,           // item in hand before the eraser
  category: 'terrain',      // palette tab
  family: null,             // metatile id -> terrain index
  selected: null,           // {list, index, special} of the selected entity
  icons: {},                // entity type -> {canvas, dx, dy}
  undo: [], redo: [],
  dirty: false,
  gesture: null,            // current mouse gesture on the map (map-input.js)
  hover: null,              // mouse position on the map, in level pixels
  rect: null,               // rectangle being drawn: {x0, y0, x1, y1} in blocks
  playing: false,
};

export function setDirty(d) {
  state.dirty = d;
  $('save').classList.toggle('dirty', d);
  $('status').textContent = d ? 'modifications non enregistrées' : 'enregistré';
}
