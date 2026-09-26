// Bridge between Super Alex Kidd Maker and the game: a port of
// maker/tools/leveledit.py that runs in the browser (and in Node).
//
//   listLevels(rom)                 levels [{level, name, canExtend}]
//   exportLevel(rom, n)             editor model of level n
//   buildMod(rom, edited, start)    compile {level: editor model} into a mod patch
//                                   (start {level, column}: test build beginning at that screen)
//   iconTypes(rom)                  "type:level" pairs for engine/build/entityicons
//   retheme(rom, model, theme)      convert an editor model to level `theme`'s graphics
//
// Editor model (one level):
//   level, name, kind, columns, rows
//   grid[row][col] = {screen: s, entities: e} or null   screen s, entity index e
//   screens[s] = {blocks: [192 metatile ids]}           shared by every cell using s
//   entities[e] = [{type, x, y, data}]                  normal entities of index e
//   specials[e] = [records]                             special records, kept as they are
//   metatileTable, metatiles[256] = [TL, TR, BL, BR words], metatileClasses[256]
//   entityTypes = [{id, name}]
//
// Building a mod:
//   * every edited level gets its own ROM bank (8 + level - 1, in a 512 KB ROM): its layout
//     tables and screens are re-encoded there and its descriptor points to them, so screens
//     may grow freely. The sub-area pointers hard-coded in the loader at $1735 are patched
//     when level 3 or 17 moves (level 3's sub-area reads level 4's layout);
//   * entity data must stay in bank 2 (hard-coded): all per-level tables and streams are
//     repacked into $B527-$BFFF (identical streams shared), so levels may gain screens;
//   * the output only contains bytes that differ from the user's ROM, in the AKMOD1 format
//     read by the engine (engine/src/rt/mod.h).
//
// The ROM is only read. Its decoded model is cached and never mutated: every
// function that changes a model works on a deep copy.

import * as levels from './levels.js';
import * as parts from './parts.js';

const { truthy, pyEq, pyInt, pyItem } = levels;

export const MOD_ROM_SIZE = 0x80000; // 512 KB: banks 8.. hold relocated levels
export const FIRST_LEVEL_BANK = 8;
export const ENTITY_REGION = [0xB527, 0xC000]; // bank 2: per-level entity tables, streams, free space
export const SUB_AREA_PATCHES = {
  3: { operands: [0x177E, 0x1781], layout_level: 4 }, // ld hl,$8AD6 / ld de,$8AD6
  17: { operands: [0x1790, 0x1793], layout_level: 17 }, // ld hl,$BC53 / ld de,$BC53
};

const clone = (v) => structuredClone(v);
const byNumber = (a, b) => a - b;

// Decoding takes a few milliseconds; the model is kept for the last ROM seen.
let cache = { id: null, model: null };
function decoded(rom) {
  const id = `${rom.length}:${levels.crc32(rom)}`;
  if (cache.id !== id) cache = { id, model: levels.load(rom) };
  return cache.model;
}

export const FRENCH_NAMES = {
  0x10: 'Pilier à pointes A', 0x11: 'Pilier à pointes B', 0x12: 'Pilier à pointes C',
  0x13: 'Pilier à pointes D', 0x15: 'Plafond à pointes', 0x16: 'Sol qui s\'effondre',
  0x17: 'Sol qui s\'effondre (au poing)', 0x20: 'Chauve-souris', 0x23: 'Homme-poisson',
  0x25: 'Ours des Blakwoods', 0x2A: 'Singe', 0x2C: 'Plante carnivore', 0x2D: 'Oiseau monstre (gauche)',
  0x2E: 'Poisson tueur (gauche)', 0x2F: 'Grenouille monstre', 0x30: 'Petit poisson (gauche)',
  0x31: 'Hippocampe (gauche)', 0x33: 'Oiseau monstre (droite)', 0x3D: 'Flamme circulaire',
  0x3E: 'Scorpion / flamme (gauche)', 0x40: 'Nuage d\'orage', 0x42: 'Poisson sauteur',
  0x45: 'Saint Nurari', 0x46: 'Taureau de Namui', 0x50: 'Vieux du village', 0x51: 'Prisonnier',
  0x52: 'Objet spécial', 0x53: 'Roi de Nibana', 0x54: 'Marcheur', 0x55: 'Sauteur',
  0x57: 'Flamme fixe', 0x63: 'Cible (coup de poing)',
};

// Entity types that the original levels place as normal entities (safe to place),
// with the levels using them.
export function entityTypeList(model) {
  const used = new Map();
  for (const level of model.levels) {
    for (const stream of level.entities.screens) {
      for (const r of stream.records) {
        if (r.kind !== 'entity') continue;
        if (!used.has(r.type)) used.set(r.type, new Set());
        used.get(r.type).add(level.number);
      }
    }
  }
  const out = [];
  for (const t of [...used.keys()].sort(byNumber)) {
    const name = levels.entityTypeName(t);
    let base = !name.startsWith('$') ? name.split('[')[0].replaceAll('_', ' ').trim() : `Objet ${levels.h8(t)}`;
    base = t in FRENCH_NAMES ? FRENCH_NAMES[t] : base.slice(0, 1).toUpperCase() + base.slice(1);
    out.push({ id: t, name: base, levels: [...used.get(t)].sort(byNumber) });
  }
  return out;
}

export const SPECIAL_NAMES = { 0x44: 'Boule de riz (fin du niveau)', 0x1D: 'Boss', 0x1E: 'Boss', 0x1F: 'Boss',
  0x4B: 'Changeur de décor', 0x4C: 'Entrée de zone bonus' };

export function specialTypeName(t) {
  if (t in SPECIAL_NAMES) return SPECIAL_NAMES[t];
  const name = levels.entityTypeName(t);
  return !name.startsWith('$') ? name.split('[')[0].replaceAll('_', ' ') : `Objet ${levels.h8(t)}`;
}

export const SURPRISE_ITEMS = [[0x4D, 'Vie supplémentaire (1up)'], [0x4E, 'Bracelet de puissance'], [0x4F, 'Fantôme (piège)']];
// Per-level tables that make up a level's look and sound (the rest is gameplay).
export const THEME_TABLES = ['palette_ptr', 'palette', 'main_tileset_ptr', 'tileset_loader', 'sprite_tiles_loader',
  'tile_updater', 'palette_updater'];

// retheme() in leveledit.py: converts an editor model (changed in place) to the
// graphics of level `theme`: terrain stays terrain (re-tiled), boxes stay boxes,
// water stays water, the rest becomes the new theme's background.
export function rethemeModel(model, ed, theme) {
  const target = pyItem(model.levels, theme - 1);
  const tp = parts.learnParts(model, target);
  const tclasses = parts.classesOf(model, target);
  const old = ed.parts;
  const family = new Map();
  old.terrains.forEach((t, i) => t.members.forEach((m) => family.set(m, i)));
  const kinds = new Map(old.blocks.map((b) => [b.metatile, b.kind]));
  const newBlock = new Map(tp.blocks.map((b) => [b.kind, b.metatile]));
  const oclasses = ed.metatileClasses;
  const nterr = tp.terrains.length;

  const convert = (m) => {
    if (family.has(m) && nterr) return tp.terrains[Math.min(family.get(m), nterr - 1)].fill;
    const k = kinds.get(m) || oclasses[m];
    if (newBlock.has(k)) return newBlock.get(k);
    if (oclasses[m].includes('solid') && nterr) return tp.terrains[0].fill;
    return tp.eraser;
  };

  for (const scr of ed.screens) scr.blocks = scr.blocks.map(convert);
  // Re-tile every terrain cell of the map with the new theme's tables (in place,
  // so later cells see the cells already re-tiled, as in Python).
  const tfam = new Map();
  tp.terrains.forEach((t, i) => t.members.forEach((m) => tfam.set(m, i)));
  const rows = ed.rows, cols = ed.columns;

  const at = (gx, gy) => {
    if (gx < 0 || gy < 0 || gx >= cols * 16 || gy >= rows * 12) return null;
    const cell = ed.grid[Math.floor(gy / 12)][Math.floor(gx / 16)];
    return cell == null ? null : ed.screens[cell.screen].blocks[(gy % 12) * 16 + (gx % 16)];
  };

  for (let gy = 0; gy < rows * 12; gy++) {
    for (let gx = 0; gx < cols * 16; gx++) {
      const m = at(gx, gy);
      if (m === null || !tfam.has(m)) continue;
      const f = tfam.get(m);
      const same = (dx, dy) => {
        const n = at(gx + dx, gy + dy);
        return n === null || tfam.get(n) === f;
      };
      const mask = (same(0, -1) ? 1 : 0) | (same(1, 0) ? 2 : 0) | (same(0, 1) ? 4 : 0) | (same(-1, 0) ? 8 : 0);
      const cell = ed.grid[Math.floor(gy / 12)][Math.floor(gx / 16)];
      ed.screens[cell.screen].blocks[(gy % 12) * 16 + (gx % 16)] = tp.terrains[f].tiles[mask];
    }
  }
  const table = target.descriptor.metatile_table;
  ed.theme = theme;
  ed.metatileTable = table;
  ed.metatiles = model.metatile_tables[table].entries.map((e) => e.words.slice());
  ed.metatileClasses = tclasses;
  ed.parts = tp;
  return ed;
}

// Horizontal levels whose row holds exactly the playable screens, one entity list each.
// Levels whose shape can change: horizontal levels whose row holds exactly the
// playable screens (one entity list each), and vertical levels made of one
// column of screens then a row at the bottom (level 1).
export function extendable(level) {
  if (level.kind === 'vertical') return verticalShape(level) !== null;
  if (level.kind !== 'horizontal') return false;
  const d = level.descriptor;
  const row = level.layout.rows[d.start_screen_y].screens;
  return row.length === d.width + 1 && level.entities.screens.length === d.width + 1;
}

// [column, bottom row] of a level made of one column of screens going down,
// then a row at the bottom starting under it, with one entity list per screen
// (column first); null for other shapes.
export function verticalShape(level) {
  const lay = level.layout, d = level.descriptor;
  if (lay.cols_is_rows || lay.cols.length !== 1 || lay.rows.length !== 2 || d.start_screen_y !== 1) return null;
  const column = lay.cols[0].screens, bottom = lay.rows[0].screens, top = lay.rows[1].screens;
  if (!pyEq(top, column.slice(0, 1)) || !pyEq(bottom.slice(0, 1), column.slice(-1)) ||
      d.height !== column.length - 1 || d.width !== bottom.length - 1) return null;
  if (level.entities.screens.length !== column.length + bottom.length - 1) return null;
  return [column, bottom];
}

// New column/bottom row of a vertical level from the editor grid: the first
// cell of every row is the column, the last row carries on to the right.
function applyVerticalShape(level, grid) {
  const column = [];
  grid.forEach((row, r) => {
    const cells = row.filter(truthy);
    if (!row.length || !truthy(row[0]) || (r < grid.length - 1 && cells.length !== 1)) {
      throw new Error(`level ${level.number}: a vertical level is one column of screens, then a row at the bottom`);
    }
    column.push(row[0].screen);
  });
  const bottom = grid[grid.length - 1].filter(truthy).map((c) => c.screen);
  if (column.length < 2) throw new Error('a level needs at least two screens');
  const lay = level.layout, d = level.descriptor;
  lay.cols[0].screens = column;
  lay.rows[1].screens = column.slice(0, 1);
  lay.rows[0].screens = bottom;
  d.height = column.length - 1;
  d.width = bottom.length - 1;
}

// Where Alex appears: a grid cell of the editor and a pixel of its screen (the
// top left of his 16x24 box).
export function levelStart(level) {
  const d = level.descriptor, t = level.tables;
  const col = level.kind === 'vertical' ? 0 : d.start_screen_x - 1;
  const row = level.kind === 'castle' ? d.start_screen_y : 0;
  return { col, row, x: t.start_x, y: t.start_y };
}

export function cmdList(model) {
  return model.levels.map((l) => ({ level: l.number, name: l.name, canExtend: extendable(l) }));
}

// export_level() in leveledit.py. Its specials are the model's own records.
export function exportModel(model, lv) {
  const level = pyItem(model.levels, lv - 1);
  const cells = level.map;
  const columns = Math.max(...cells.map((c) => c.x)) + 1;
  const rows = Math.max(...cells.map((c) => c.y)) + 1;
  const grid = Array.from({ length: rows }, () => new Array(columns).fill(null));
  for (const c of cells) grid[c.y][c.x] = { screen: c.screen, entities: c.entity_index };
  const entities = [], specials = [];
  for (const stream of level.entities.screens) {
    entities.push(stream.records.filter((r) => r.kind === 'entity').map((r) => ({ type: r.type, x: r.x, y: r.y, data: r.data })));
    specials.push(stream.records.filter((r) => r.kind !== 'entity' && r.kind !== 'end'));
  }
  const table = level.descriptor.metatile_table;
  const entries = model.metatile_tables[table].entries;
  const specialTypes = [...new Set(specials.flat().filter((r) => 'type' in r).map((r) => r.type))].sort(byNumber);
  const notes = [];
  const used = new Map();
  for (const c of cells) {
    if (!used.has(c.screen)) used.set(c.screen, []);
    used.get(c.screen).push([c.x, c.y]);
  }
  const shared = [...used].filter(([, where]) => where.length > 1).map(([s]) => s);
  if (shared.length) notes.push(`Écrans partagés (modifier l'un modifie les autres) : ${shared.sort(byNumber).join(', ')}`);
  if ('sub_area' in level) notes.push('Ce niveau a une zone bonus qui n\'est pas encore éditable.');
  return {
    level: lv,
    name: level.name,
    kind: level.kind,
    columns,
    rows,
    grid,
    screens: level.screens.map((s) => ({ blocks: s.metatiles.slice() })),
    entities,
    specials,
    metatileTable: table,
    metatiles: entries.map((e) => e.words.slice()),
    metatileClasses: entries.map((e) => ('class' in e ? e.class : '')),
    entityTypes: entityTypeList(model),
    specialTypes: specialTypes.map((t) => ({ id: t, name: specialTypeName(t) })),
    canExtend: extendable(level),
    start: levelStart(level),
    parts: parts.learnParts(model, level),
    theme: lv,
    themeMusic: true,
    surprises: null,
    surpriseItems: SURPRISE_ITEMS.map(([t, n]) => ({ id: t, name: n })),
    levelNames: Object.fromEntries(model.levels.map((l) => [String(l.number), l.name])),
    notes,
  };
}

// ------------------------------------------------------------------------- build
// Copies the editor's screens, layout changes and entities into the decoded level.
export function applyEdits(level, ed) {
  const canExtend = extendable(level); // before the layout changes below
  const nOld = level.screens.length;
  if (ed.screens.length < nOld) {
    throw new Error(`level ${level.number}: screens cannot be removed from the list (${ed.screens.length} < ${nOld})`);
  }
  ed.screens.forEach((es, i) => {
    const blocks = es.blocks.map((b) => pyInt(b) & 0xFF);
    if (blocks.length !== levels.SCREEN_CELLS) throw new Error(`a screen must have ${levels.SCREEN_CELLS} blocks`);
    if (i >= nOld) {
      level.screens.push({ ptr: null, metatiles: blocks });
    } else if (!pyEq(blocks, level.screens[i].metatiles)) {
      level.screens[i].metatiles = blocks;
      delete level.screens[i].rle_tokens;
    }
  });

  // Layout: only extendable (simple horizontal) levels may change their screen row.
  const d = level.descriptor;
  let newRow = null;
  if (level.kind === 'horizontal' && truthy(ed.grid)) newRow = ed.grid[0].filter(truthy).map((c) => c.screen);
  const oldRow = level.layout.rows[d.start_screen_y].screens;
  if (level.kind === 'vertical' && canExtend && truthy(ed.grid)) applyVerticalShape(level, ed.grid);
  if (level.kind === 'horizontal' && newRow !== null && !pyEq(newRow, oldRow.slice(0, d.width + 1))) {
    if (!canExtend) throw new Error(`level ${level.number}: its screen layout cannot be changed`);
    if (newRow.length < 2) throw new Error('a level needs at least two screens');
    const rowPtr = level.layout.rows[d.start_screen_y].ptr;
    for (const tab of [...level.layout.rows, ...level.layout.cols]) {
      if (tab.ptr === rowPtr) tab.screens = newRow.slice();
    }
    d.width = newRow.length - 1;
  }

  const streams = level.entities.screens;
  const ents = ed.entities;
  const specials = truthy(ed.specials) ? ed.specials : new Array(ents.length).fill(null);
  if (ents.length < streams.length && !canExtend) throw new Error(`level ${level.number}: wrong number of entity lists`);
  while (streams.length < ents.length) streams.push({ ptr: null, records: [] });
  streams.length = ents.length;
  const n = Math.min(streams.length, ents.length, specials.length); // zip()
  for (let i = 0; i < n; i++) {
    const stream = streams[i];
    let spec = specials[i];
    if (spec == null) spec = stream.records.filter((r) => r.kind !== 'entity' && r.kind !== 'end');
    // text / moves: the Maker's janken opponent set-up (written apart, backend.js).
    const recs = spec.map(({ text, moves, ...r }) => r);
    for (const r of recs) {
      for (const k of ['type', 'x', 'y', 'data']) if (k in r) r[k] = pyInt(r[k]) & 0xFF;
    }
    for (const e of ents[i]) {
      recs.push({ kind: 'entity', type: pyInt(e.type) & 0xFF, y: pyInt(e.y) & 0xFF,
        x: pyInt(e.x) & 0xFF, data: pyInt(e.data) & 0xFF });
    }
    stream.records = recs;
  }

  // Alex's start: a pixel of the start screen, which simple horizontal levels
  // can move to any of their screens.
  if (truthy(ed.start)) {
    level.tables.start_x = pyInt(ed.start.x) & 0xFF;
    level.tables.start_y = pyInt(ed.start.y) & 0xFF;
    const col = pyInt(ed.start.col);
    if (level.kind === 'horizontal' && canExtend && col >= 0 && col <= d.width) d.start_screen_x = col + 1;
  }
}

// Assigns fresh addresses in `bank` to the level's layout tables and screens.
// `extra` (optional) is another level whose layout must also be present in the bank
// (level 3's sub-area reads level 4's layout); returns its new top-table address.
export function relocateLayout(level, bank, extra = null) {
  let cursor = 0x8000;

  const alloc = (n) => {
    const a = cursor;
    cursor += n;
    if (cursor > 0xC000) throw new Error(`level ${level.number} does not fit in one bank`);
    return a;
  };

  const place = (lvl) => {
    const lay = lvl.layout, scr = lvl.screens;
    const rowsPtr = alloc(2 * lay.rows.length);
    const colsPtr = lay.cols_is_rows ? rowsPtr : alloc(2 * lay.cols.length);
    const newPtr = new Map();
    for (const tab of [...lay.rows, ...(lay.cols_is_rows ? [] : lay.cols)]) {
      if (!newPtr.has(tab.ptr)) newPtr.set(tab.ptr, alloc(2 * tab.screens.length));
    }
    for (const tab of [...lay.rows, ...lay.cols]) {
      if (newPtr.has(tab.ptr)) tab.ptr = newPtr.get(tab.ptr);
    }
    for (const s of scr) s.ptr = alloc(levels.encodeScreen(s).length);
    return [rowsPtr, colsPtr];
  };

  const [rowsPtr, colsPtr] = place(level);
  const d = level.descriptor;
  d.bank = bank;
  d.bank_byte = 0x80 | bank;
  d.rows_ptr = rowsPtr;
  d.cols_ptr = colsPtr;
  let extraTop = null;
  if (extra !== null) [extraTop] = place(extra);
  return extraTop;
}

// Patches for the row/column tables and screens of a level whose layout sits in `bank`.
function layoutPatches(lay, scr, bank, rowsPtr, colsPtr) {
  const off = (a) => bank * 0x4000 + (a - 0x8000);
  const out = [[off(rowsPtr), levels.packWords(lay.rows.map((r) => r.ptr))]];
  if (!lay.cols_is_rows) out.push([off(colsPtr), levels.packWords(lay.cols.map((c) => c.ptr))]);
  const seen = new Set();
  for (const tab of [...lay.rows, ...(lay.cols_is_rows ? [] : lay.cols)]) {
    if (seen.has(tab.ptr)) continue;
    seen.add(tab.ptr);
    out.push([off(tab.ptr), levels.packWords(tab.screens.map((i) => pyItem(scr, i).ptr))]);
  }
  for (const s of scr) out.push([off(s.ptr), levels.encodeScreen(s)]);
  return out;
}

// Patches for a level's descriptor fields, layout tables and screens only.
export function encodeLayout(level) {
  const d = level.descriptor;
  return [[d.rom_offset, levels.pack('<BHHBBBBBH', d.bank_byte, d.rows_ptr, d.cols_ptr, d.start_screen_x,
    d.start_screen_y, d.width, d.height, d.scroll_flags, d.metatile_table_ptr)],
  ...layoutPatches(level.layout, level.screens, d.bank, d.rows_ptr, d.cols_ptr)];
}

// Like encodeLayout for a copy of `level` placed in `bank` (no descriptor).
export function encodeLayoutCopy(level, bank, rowsPtr, colsPtr) {
  return layoutPatches(level.layout, level.screens, bank, rowsPtr, colsPtr);
}

export function countQuestionBoxes(model, level) {
  const classes = parts.classesOf(model, level);
  let n = 0;
  for (const c of level.map) {
    for (const m of level.screens[c.screen].metatiles) if (classes[m] === 'question_box') n++;
  }
  return n;
}

// Gives each level with custom surprises its own run of the question-box item
// table, in slots no other level reads (a level reads one entry per box broken,
// starting at its index). `edited` is a Map level -> editor model.
export function assignSurprises(model, edited) {
  const items = model.globals.question_box_items;
  const wanted = new Map();
  for (const [lv, ed] of edited) {
    if (truthy(ed.surprises)) wanted.set(lv, ed.surprises.map((x) => pyInt(x) & 0xFF));
  }
  if (!wanted.size) return;
  const used = new Array(items.length).fill(false);
  for (const level of model.levels) {
    if (wanted.has(level.number)) continue;
    const start = level.tables.question_box_index;
    const boxes = countQuestionBoxes(model, level);
    for (let k = 0; k < boxes; k++) if (start + k < used.length) used[start + k] = true;
  }
  for (const lv of [...wanted.keys()].sort(byNumber)) {
    const seq = wanted.get(lv);
    const n = seq.length;
    let pos = null;
    for (let i = 0; i < items.length - n + 1; i++) {
      if (!used.slice(i, i + n).some(Boolean)) {
        pos = i;
        break;
      }
    }
    if (pos === null) throw new Error(`pas assez de place pour ${n} surprises dans le niveau ${lv}`);
    items.splice(pos, n, ...seq);
    for (let k = 0; k < n; k++) used[pos + k] = true;
    pyItem(model.levels, lv - 1).tables.question_box_index = pos;
  }
}

// Lays out every level's entity pointer table and streams again in bank 2
// (tables first, then streams, identical streams shared); returns [patches, size].
export function repackEntityData(model) {
  const [lo, hi] = ENTITY_REGION;
  let cursor = lo;
  const out = [];
  for (const level of model.levels) {
    const ent = level.entities;
    ent.table_ptr = cursor;
    cursor += 2 * ent.screens.length;
  }
  const placed = new Map(); // stream bytes (as text) -> address
  for (const level of model.levels) {
    for (const stream of level.entities.screens) {
      const data = levels.encodeEntityStream(stream.records);
      const id = data.join(',');
      if (!placed.has(id)) {
        if (cursor + data.length > hi) throw new Error(`too many entities: bank 2 has room for ${hi - lo} bytes of entity data`);
        placed.set(id, cursor);
        out.push([cursor, data]); // bank 2: ROM offset = CPU address
        cursor += data.length;
      }
      stream.ptr = placed.get(id);
    }
  }
  for (const level of model.levels) {
    const ent = level.entities;
    out.push([ent.table_ptr, levels.packWords(ent.screens.map((s) => s.ptr))]);
  }
  out.push([levels.ENTITY_DESCRIPTOR_TABLE, levels.packWords(model.levels.map((l) => l.entities.table_ptr))]);
  return [out, cursor - lo];
}

// Blocks of the screens along a horizontal level's row, in order (the
// screen list itself may be ordered differently once screens are copied).
export function shownScreens(level) {
  const d = level.descriptor;
  const row = level.layout.rows[d.start_screen_y].screens.slice(0, d.width + 1);
  return row.map((i) => pyItem(level.screens, i).metatiles);
}

// build() in leveledit.py, minus the file IO: `editedIn` maps level numbers to
// editor models (the MODDIR/level_NN.json files), `start` = [level, column] for
// test builds only (the level begins at that screen; simple horizontal levels).
// Returns [patch bytes, info].
export function build(romBytes, editedIn, start = null) {
  const original = decoded(romBytes); // never mutated: stands for copy.deepcopy(model)
  const model = clone(original);
  const edited = new Map(); // insertion order = Python's dict order (files sorted by name)
  const entries = (editedIn instanceof Map ? [...editedIn] : Object.entries(editedIn))
    .map(([k, ed]) => [pyInt(k), ed]).sort((a, b) => a[0] - b[0]);
  for (const [lv, ed] of entries) {
    edited.set(lv, ed);
    applyEdits(pyItem(model.levels, lv - 1), ed);
  }
  // Themes: the level borrows another level's graphics tables (and music).
  for (const [lv, ed] of edited) {
    const theme = pyInt(truthy(ed.theme) ? ed.theme : lv);
    if (theme !== lv) {
      const level = pyItem(model.levels, lv - 1), src = pyItem(model.levels, theme - 1);
      for (const k of THEME_TABLES) level.tables[k] = clone(src.tables[k]);
      if (!('themeMusic' in ed) || truthy(ed.themeMusic)) level.tables.song = src.tables.song;
      level.descriptor.metatile_table_ptr = src.descriptor.metatile_table_ptr;
      level.descriptor.metatile_table = src.descriptor.metatile_table;
    }
  }
  assignSurprises(model, edited);

  if (start) {
    const [lv, col] = start;
    const level = pyItem(model.levels, lv - 1);
    if (!edited.has(lv)) edited.set(lv, exportModel(model, lv));
    if (level.kind === 'horizontal' && col >= 0 && col <= level.descriptor.width) {
      level.descriptor.start_screen_x = col + 1;
    }
  }

  const order = [...edited.keys()].sort(byNumber);
  const patches = [];
  for (const lv of order) patches.push(...levels.encodeLevelTables(lv, pyItem(model.levels, lv - 1).tables));
  if ([...edited.values()].some((e) => truthy(e.surprises))) {
    patches.push([levels.QUESTION_BOX_ITEMS, levels.toBytes(model.globals.question_box_items, 'bytes must be in range(0, 256)')]);
  }
  for (const lv of order) {
    const level = pyItem(model.levels, lv - 1);
    const bank = FIRST_LEVEL_BANK + lv - 1;
    const sub = SUB_AREA_PATCHES[lv];
    let extra = null;
    if (sub && sub.layout_level !== lv) extra = clone(pyItem(model.levels, sub.layout_level - 1));
    const extraTop = relocateLayout(level, bank, extra);
    patches.push(...encodeLayout(level));
    if (extra !== null) patches.push(...encodeLayoutCopy(extra, bank, extraTop, extraTop));
    if (sub) {
      const target = extra !== null ? extraTop : level.descriptor.rows_ptr;
      for (const operand of sub.operands) patches.push([operand, levels.pack('<H', target)]);
    }
  }

  let entityBytes = 0;
  if ([...edited.keys()].some((lv) => !pyEq(pyItem(model.levels, lv - 1).entities, pyItem(original.levels, lv - 1).entities))) {
    const [p, n] = repackEntityData(model);
    patches.push(...p);
    entityBytes = n;
  }

  const size = edited.size ? MOD_ROM_SIZE : romBytes.length;
  const ref = new Uint8Array(Math.max(size, romBytes.length)).fill(0xFF);
  ref.set(romBytes);
  const patched = levels.applyPatches(ref, patches);

  // Check: the patched ROM decodes to exactly the edited levels.
  const check = levels.load(patched);
  for (const lv of edited.keys()) {
    const got = pyItem(check.levels, lv - 1), want = pyItem(model.levels, lv - 1);
    if (!pyEq(got.screens.map((s) => s.metatiles), want.screens.map((s) => s.metatiles))
        && !pyEq(shownScreens(got), shownScreens(want))) {
      throw new Error(`level ${lv}: screens do not decode back identically`);
    }
    if (got.descriptor.width !== want.descriptor.width) throw new Error(`level ${lv}: layout width does not decode back`);
    const norm = (st) => st.entities.screens.map((s) => s.records.filter((r) => r.kind !== 'end'));
    if (!pyEq(norm(got), norm(want))) throw new Error(`level ${lv}: entities do not decode back identically`);
  }

  // Only the differences go into the mod file.
  const records = [];
  const differs8 = (j) => {
    for (let k = j; k < j + 8; k++) if (patched[k] !== ref[k]) return true;
    return false;
  };
  let i = 0;
  while (i < size) {
    if (patched[i] === ref[i]) {
      i += 1;
      continue;
    }
    let j = i;
    while (j < size && (patched[j] !== ref[j] || (j + 8 < size && differs8(j)))) j += 1;
    records.push([i, patched.slice(i, j)]);
    i = j;
  }
  const chunks = [new TextEncoder().encode('AKMOD1\0\0'), levels.pack('<I', size)];
  for (const [off, data] of records) chunks.push(levels.pack('<II', off, data.length), data);
  const total = records.reduce((n, [, d]) => n + d.length, 0);
  return [levels.concatBytes(chunks), { levels: order, records: records.length, bytes: total, entity_bytes: entityBytes }];
}

// ------------------------------------------------------------------------- API
// Each call decodes (or reuses) the ROM model and returns fresh objects.

export function listLevels(rom) {
  return cmdList(decoded(rom));
}

export function exportLevel(rom, n) {
  return clone(exportModel(decoded(rom), pyInt(n)));
}

// "type:level" for every entity type placed by a level (the first level using it).
export function iconTypes(rom) {
  const spec = new Map();
  for (const level of decoded(rom).levels) {
    for (const stream of level.entities.screens) {
      for (const r of stream.records) {
        if ('type' in r && !spec.has(r.type)) spec.set(r.type, level.number);
      }
    }
  }
  return [...spec].sort((a, b) => a[0] - b[0]).map(([t, lv]) => `${t}:${lv}`);
}

// The editor model converted to level `theme`'s graphics (the argument is left untouched).
export function retheme(rom, model, theme) {
  return rethemeModel(decoded(rom), clone(model), pyInt(theme));
}

// Compiles edited levels ({level: editor model}) into an AKMOD1 patch. start is
// null or {level, column}. Throws Error with leveledit.py's messages when an
// edit cannot be built.
export function buildMod(rom, edited, start = null) {
  const at = !start ? null : Array.isArray(start) ? start : [start.level, start.column];
  const [patch, info] = build(rom, edited, at && at.map(pyInt));
  return { patch, info };
}
