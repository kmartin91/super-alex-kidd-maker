// Level geometry: pixels, screens and blocks.
//
// Level pixels start at the top-left of the grid; each grid cell is a screen of
// 16x12 blocks of 16x16 pixels. Map-wide block coordinates: gx in
// [0, columns*16), gy in [0, rows*12).

import { $ } from './dom.js';
import { state, SCREEN_W, SCREEN_H, BLOCK, SCREEN_PX_W, SCREEN_PX_H } from './state.js';

export function levelSize() {
  return { w: state.model.columns * SCREEN_PX_W, h: state.model.rows * SCREEN_PX_H };
}

// Grid cell {row, col} at the centre of the visible part of the map.
export function viewCell() {
  const wrap = $('mapWrap');
  const x = (wrap.scrollLeft + wrap.clientWidth / 2) / state.zoom;
  const y = (wrap.scrollTop + wrap.clientHeight / 2) / state.zoom;
  const col = Math.min(state.model.columns - 1, Math.max(0, Math.floor(x / SCREEN_PX_W)));
  const row = Math.min(state.model.rows - 1, Math.max(0, Math.floor(y / SCREEN_PX_H)));
  return { row, col };
}

// Level pixel -> {cell, screen, entity list, col, row, local x/y, block} or null.
export function locate(px, py) {
  const col = Math.floor(px / SCREEN_PX_W), row = Math.floor(py / SCREEN_PX_H);
  if (row < 0 || row >= state.model.rows || col < 0 || col >= state.model.columns) return null;
  const cell = state.model.grid[row][col];
  const lx = px - col * SCREEN_PX_W, ly = py - row * SCREEN_PX_H;
  const bx = Math.floor(lx / BLOCK), by = Math.floor(ly / BLOCK);
  return {
    cell, screen: cell ? cell.screen : null, list: cell ? cell.entities : null,
    col, row, lx, ly, bx, by, block: by * SCREEN_W + bx,
  };
}

// Mouse event -> level pixel.
export function eventLevelPos(ev) {
  const r = $('map').getBoundingClientRect();
  return { x: (ev.clientX - r.left) / state.zoom, y: (ev.clientY - r.top) / state.zoom };
}

// Pixel origin of the cell whose entity list is `list`.
export function listOrigin(list) {
  for (let row = 0; row < state.model.rows; row++)
    for (let col = 0; col < state.model.columns; col++) {
      const c = state.model.grid[row][col];
      if (c && c.entities === list) return { x: col * SCREEN_PX_W, y: row * SCREEN_PX_H };
    }
  return null;
}

// Block id at map-wide block coordinates, or undefined outside the level.
export function blockAt(gx, gy) {
  const col = Math.floor(gx / SCREEN_W), row = Math.floor(gy / SCREEN_H);
  if (gx < 0 || gy < 0 || row >= state.model.rows || col >= state.model.columns) return undefined;
  const cell = state.model.grid[row][col];
  if (!cell) return undefined;
  return state.model.screens[cell.screen].blocks[(gy % SCREEN_H) * SCREEN_W + (gx % SCREEN_W)];
}

// Number of grid cells showing screen `s`.
export function cellUsage(s) {
  let n = 0;
  for (const row of state.model.grid) for (const c of row) if (c && c.screen === s) n++;
  return n;
}

// Sets a block; returns true if it changed. Where the level layout may change,
// a screen shown in several places is copied first, so that editing one place
// never changes another (elsewhere, all the copies change together).
export function putBlock(gx, gy, v) {
  const col = Math.floor(gx / SCREEN_W), row = Math.floor(gy / SCREEN_H);
  if (gx < 0 || gy < 0 || row >= state.model.rows || col >= state.model.columns) return false;
  const cell = state.model.grid[row][col];
  if (!cell) return false;
  const i = (gy % SCREEN_H) * SCREEN_W + (gx % SCREEN_W);
  if (state.model.screens[cell.screen].blocks[i] === v) return false;
  if (state.model.canExtend && cellUsage(cell.screen) > 1) {
    state.model.screens.push({ blocks: state.model.screens[cell.screen].blocks.slice() });
    cell.screen = state.model.screens.length - 1;
  }
  state.model.screens[cell.screen].blocks[i] = v;
  return true;
}

// Every block id of the screens reachable in the level.
export function* levelBlocks() {
  for (const row of state.model.grid)
    for (const c of row)
      if (c) yield* state.model.screens[c.screen].blocks;
}
