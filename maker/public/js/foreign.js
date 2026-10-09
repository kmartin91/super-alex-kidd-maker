// Blocks from other settings: the lake's water in the village, the castle's
// spikes on the mountain... A block of level `home` placed in the level gets
// a free number of the level's own block table (model.foreignBlocks:
// [{ id, home, metatile }]). The level tools copy it there and the engine
// draws its tiles with its setting's graphics and colours; what it does
// (water, deadly, solid) comes with it (rom/leveledit.js foreignBlockPatches,
// engine/src/rt/maker.h). In the editor its picture comes from its setting.

import { state } from './state.js';
import { exportLevelOf } from './backend.js';
import { levelVideo } from './capture.js';
import { blockImages } from './graphics.js';
import { hazardParts } from './hazards.js';
import { classifyWord } from './rom/levels.js';
import { t, tName } from './i18n.js';

// On the map at once: the ROM has room for 37 copies, and their tiles must
// fit in the engine's virtual tiles (4 per block at most).
export const MAX_FOREIGN_BLOCKS = 24;

// Blocks whose meaning comes from their tile number (ladders, doors, money,
// the castles' floors) stay home; boxes are in every setting already.
const STAY_HOME = new Set(['ladder', 'shop_door', 'money', 'shop_item', 'ladder_top', 'enter_down', 'puzzle_floor',
  'ghost_floor', 'invalid', 'question_box', 'star_box', 'skull_box', 'box']);

// ------------------------------------------------------------ the settings
const homes = new Map(); // level -> { model, canvases (null until its graphics are in), loading }

function home(n) {
  if (!homes.has(n)) homes.set(n, { model: exportLevelOf(n), canvases: null, loading: null });
  return homes.get(n);
}

// Block pictures of level `n`, or null while its graphics load (`then` is
// called once they are in).
export function homeCanvases(n, then = null) {
  const h = home(n);
  if (h.canvases) return h.canvases;
  if (!h.loading) {
    h.loading = levelVideo(n).then((video) => { h.canvases = blockImages(video, h.model.metatiles); });
  }
  if (then) h.loading.then(then).catch(() => {});
  return null;
}

const movable = (n, m) => {
  const words = home(n).model.metatiles[m];
  const kinds = words.map(classifyWord);
  return kinds.some((k) => k !== 'empty') && !kinds.some((k) => STAY_HOME.has(k));
};

// ------------------------------------------------------------ numbers
// Numbers of the level's block table it can't give away: those its setting
// draws with (its level's screens and parts), those on the map, those given.
function taken(model, theme) {
  const ref = home(theme).model;
  const ids = new Set([0, ref.parts.eraser]);
  for (const s of ref.screens) for (const b of s.blocks) ids.add(b);
  for (const tr of ref.parts.terrains) { ids.add(tr.fill); tr.tiles.forEach((b) => ids.add(b)); (tr.members || []).forEach((b) => ids.add(b)); }
  for (const b of ref.parts.blocks) ids.add(b.metatile);
  for (const st of ref.parts.stamps) st.cells.flat().forEach((b) => { if (b >= 0) ids.add(b); });
  for (const hz of hazardParts(theme)) hz.cells.flat().forEach((b) => ids.add(b));
  for (const s of model.screens) for (const b of s.blocks) ids.add(b);
  for (const f of model.foreignBlocks || []) ids.add(f.id);
  return ids;
}

// The number block `m` of level `home` has in `model` (of setting `theme`),
// given now if needed. Its own setting's blocks are themselves.
export function foreignId(model, theme, from, m) {
  if (from === theme) return m;
  const list = model.foreignBlocks || (model.foreignBlocks = []);
  const found = list.find((f) => f.home === from && f.metatile === m);
  if (found) return found.id;
  const busy = taken(model, theme);
  let id = 255;
  while (id > 0 && busy.has(id)) id--;
  if (id <= 0) throw new Error(t('Plus de numéro libre pour un bloc d\'un autre décor dans ce niveau.'));
  list.push({ id, home: from, metatile: m });
  model.metatileClasses[id] = home(from).model.metatileClasses[m];
  return id;
}

// Blocks from other settings on the map now.
export function foreignOnMap(model = state.model) {
  const ids = new Set((model.foreignBlocks || []).map((f) => f.id));
  const on = new Set();
  for (const s of model.screens) for (const b of s.blocks) if (ids.has(b)) on.add(b);
  return on;
}

// Numbers for a stamp of `from`'s blocks, or null when the level has no room
// left for more of them.
export function foreignCells(part) {
  const theme = state.model.theme || state.level;
  const before = foreignOnMap();
  const cells = part.cells.map((row) => row.map((m) => (m >= 0 ? foreignId(state.model, theme, part.home, m) : -1)));
  const after = new Set(before);
  for (const b of cells.flat()) if (b >= 0 && state.model.foreignBlocks.some((f) => f.id === b)) after.add(b);
  return after.size > MAX_FOREIGN_BLOCKS ? null : cells;
}

// The level's block pictures (graphics.js): those from other settings come
// from their setting; `redraw` once one of them has loaded.
export function applyForeignImages(canvases, redraw) {
  for (const f of state.model.foreignBlocks || []) {
    state.model.metatileClasses[f.id] = home(f.home).model.metatileClasses[f.metatile];
    const hc = homeCanvases(f.home, redraw);
    if (hc) canvases[f.id] = hc[f.metatile];
  }
}

// ------------------------------------------------------------ theme change
// Where the map has blocks from other settings: [screen, cell, home, metatile].
export function takeForeign(model) {
  const byId = new Map((model.foreignBlocks || []).map((f) => [f.id, f]));
  const out = [];
  model.screens.forEach((s, si) => s.blocks.forEach((b, ci) => {
    const f = byId.get(b);
    if (f) out.push([si, ci, f.home, f.metatile]);
  }));
  return out;
}

// Puts them back in the level converted to setting `theme` (retheme turned
// them into background): new numbers, or its own blocks for that setting.
export function restoreForeign(model, theme, places) {
  model.foreignBlocks = [];
  for (const [si, ci, from, m] of places) {
    if (model.screens[si]) model.screens[si].blocks[ci] = foreignId(model, theme, from, m);
  }
  if (!model.foreignBlocks.length) delete model.foreignBlocks;
}

// ------------------------------------------------------------ the shelf
// The most common water surface over water in `n`'s screens: [top, body].
function waterPair(n) {
  const m = home(n).model;
  const water = (b) => (m.metatileClasses[b] || '').includes('water') && m.metatiles[b].every((w) => classifyWord(w) === 'water');
  const count = new Map();
  for (const s of m.screens) {
    for (let x = 0; x < 16; x++) {
      for (let y = 1; y < 11; y++) {
        const top = s.blocks[y * 16 + x], above = s.blocks[(y - 1) * 16 + x], body = s.blocks[(y + 1) * 16 + x];
        if (water(top) && !water(above) && water(body)) count.set(`${top}:${body}`, (count.get(`${top}:${body}`) || 0) + 1);
      }
    }
  }
  const best = [...count].sort((a, b) => b[1] - a[1])[0];
  return best ? best[0].split(':').map(Number) : null;
}

// What setting `n` offers other levels: [{ part, name }] (parts are stamps of
// its own block numbers, with `home`).
export function foreignShelf(n) {
  const m = home(n).model;
  const setting = m.levelNames[n];
  const out = [];
  const add = (name, cells) => {
    if (!cells.flat().every((b) => b < 0 || movable(n, b))) return;
    const key = JSON.stringify(cells);
    if (out.some((it) => JSON.stringify(it.part.cells) === key)) return;
    out.push({ part: { kind: 'stamp', home: n, w: cells[0].length, h: cells.length, cells }, name: `${name} · ${setting}` });
  };
  const water = waterPair(n);
  if (water) add(t('Eau'), [[water[0]], [water[1]]]);
  for (const hz of hazardParts(n)) add(t('{name} · mortel', { name: t(hz.name) }), hz.cells);
  if (m.parts.terrains.length) {
    const T = m.parts.terrains[0].tiles;
    add(t('Sol'), [[T[2 | 4 | 8]], [T[15]]]);
  }
  for (const b of m.parts.blocks) if (!(water && b.kind === 'water')) add(tName(b.name), [[b.metatile]]);
  for (const st of m.parts.stamps.slice(0, 8)) add(t('Décor {w}×{h}', { w: st.w, h: st.h }), st.cells);
  return out;
}

// The settings with something to offer, but the level's own.
export function foreignSettings() {
  const theme = state.model.theme || state.level;
  return state.levels.map((l) => l.level).filter((n) => n !== theme && foreignShelf(n).length);
}
