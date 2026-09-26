// Enemies, items and special records (bosses, fixed objects, level end).
//
// Entities live in per-screen lists (state.model.entities[list]) with
// coordinates relative to their screen. A selection is {list, index, special?}.

import { $ } from './dom.js';
import { state, SCREEN_PX_W, SCREEN_PX_H } from './state.js';
import { entityIcons } from './backend.js';
import { locate, listOrigin } from './level.js';
import { pushUndo } from './history.js';
import { alexImage } from './graphics.js';
import { ask } from './modal.js';
import { render } from './render.js';

// The level end (rice ball): one per level, kept in a $84 special record.
export const GOAL_TYPE = 0x44;

// Janken opponents (docs/notes/enemies2.md): a $84 special record too; `data`
// picks the opponent's settings, bit 0 = a fight follows a won match. Beating
// one ends the level, like the rice ball.
export const BOSSES = [
  { type: 0x1d, data: 2, name: 'Gooseka', what: 'pierre-feuille-ciseaux' },
  { type: 0x1d, data: 3, name: 'Gooseka', what: 'pierre-feuille-ciseaux, puis combat' },
  { type: 0x1e, data: 4, name: 'Chokkinna', what: 'pierre-feuille-ciseaux' },
  { type: 0x1e, data: 5, name: 'Chokkinna', what: 'pierre-feuille-ciseaux, puis combat' },
  { type: 0x1f, data: 6, name: 'Parplin', what: 'pierre-feuille-ciseaux' },
  { type: 0x1f, data: 7, name: 'Parplin', what: 'pierre-feuille-ciseaux, puis combat' },
  { type: 0x1c, data: 1, name: 'Janken le Grand', what: 'pierre-feuille-ciseaux, puis combat' },
];

export function hex2(n) {
  return n.toString(16).toUpperCase().padStart(2, '0');
}

export function entityName(type) {
  const t = state.model.entityTypes.find((e) => e.id === type);
  return t ? t.name : `Objet $${hex2(type)}`;
}

export function specialName(type) {
  if (type === GOAL_TYPE) return 'Boule de riz (fin du niveau)';
  const boss = BOSSES.find((b) => b.type === type);
  if (boss) return boss.name;
  const t = (state.model.specialTypes || []).find((e) => e.id === type);
  return t ? t.name : entityName(type);
}

// Fallback colour for entities without an icon.
export function entityColor(type) {
  return `hsl(${(type * 47) % 360} 75% 55%)`;
}

// Hand-drawn rice ball for the level end.
const RICE_BALL = [
  '......####......',
  '.....#wwww#.....',
  '....#wwwwww#....',
  '...#wwwwwwww#...',
  '..#wwwwwwwwww#..',
  '.#wwwwwwwwwwww#.',
  '#wwwwwwwwwwwwww#',
  '#wwwwnnnnnnwwww#',
  '#wwwwnnnnnnwwww#',
  '#wwwwnnnnnnwwww#',
  '.#wwwnnnnnnwww#.',
  '..############..',
];

// Traps drawn by the game with background blocks (no sprite to capture).
const SPIKES = [
  '..#...#...#...#.',
  '.#g#.#g#.#g#.#g#',
  '################',
  '#gggggggggggggg#',
  '#g##g##g##g##gg#',
  '#gggggggggggggg#',
  '#g##g##g##g##gg#',
  '#gggggggggggggg#',
  '################',
];
const CRUMBLE = [
  '################',
  '#bbbbbb#bbbbbbb#',
  '#bbbbb#.#bbbbbb#',
  '#bbbb#...#bbbbb#',
  '#######...######',
  '#bbb#.....#bbbb#',
  '#bbbb#...#bbbbb#',
  '################',
];
const CROWN = [
  '#......#......#.',
  '##....###....##.',
  '#y#..#yyy#..#y#.',
  '#yy##yyryy##yy#.',
  '#yyyyyyyyyyyyy#.',
  '###############.',
];
const TARGET = [
  '...#####...',
  '..#rrrrr#..',
  '.#rwwwwwr#.',
  '#rwrrrrrwr#',
  '#rwr###rwr#',
  '#rwr###rwr#',
  '#rwrrrrrwr#',
  '.#rwwwwwr#.',
  '..#rrrrr#..',
  '...#####...',
];

function pixelIcon(rows, dx, dy) {
  const c = document.createElement('canvas');
  c.width = rows[0].length;
  c.height = rows.length;
  const ctx = c.getContext('2d');
  const colors = { '#': '#19121c', w: '#ffffff', n: '#1f3a2a', g: '#a7a7b3', b: '#b8743a', y: '#ffc933', r: '#e4412f' };
  rows.forEach((row, y) => [...row].forEach((ch, x) => {
    if (!colors[ch]) return;
    ctx.fillStyle = colors[ch];
    ctx.fillRect(x, y, 1, 1);
  }));
  return { canvas: c, dx, dy };
}

const riceBall = () => pixelIcon(RICE_BALL, -8, -12);
const DRAWN_ICONS = {
  0x10: () => pixelIcon(SPIKES, -8, -8), 0x11: () => pixelIcon(SPIKES, -8, -8),
  0x12: () => pixelIcon(SPIKES, -8, -8), 0x13: () => pixelIcon(SPIKES, -8, -8),
  0x15: () => pixelIcon(SPIKES, -8, -8),
  0x16: () => pixelIcon(CRUMBLE, -8, -8), 0x17: () => pixelIcon(CRUMBLE, -8, -8),
  0x53: () => pixelIcon(CROWN, -8, -12), 0x63: () => pixelIcon(TARGET, -6, -10),
};

// Loads the sprites drawn for each entity type (state.icons). The first time,
// they are captured from the game, which takes a little while.
export async function loadIcons() {
  const before = $('status').textContent;
  try {
    const raw = await entityIcons((done, total) => {
      $('status').textContent = `images des ennemis : ${done}/${total}`;
      if (done === total) $('status').textContent = before;
    });
    for (const [type, ic] of Object.entries(raw)) {
      const c = document.createElement('canvas');
      c.width = ic.w;
      c.height = ic.h;
      const img = new ImageData(new Uint8ClampedArray(ic.rgba.buffer, ic.rgba.byteOffset, ic.rgba.length), ic.w, ic.h);
      c.getContext('2d').putImageData(img, 0, 0);
      state.icons[Number(type)] = { canvas: c, dx: ic.dx, dy: ic.dy };
    }
  } catch (err) {
    console.warn('entity icons unavailable:', err.message);
  }
  // The captured rice ball is hard to read at this size: a drawn one is clearer.
  state.icons[GOAL_TYPE] = riceBall();
  for (const [type, make] of Object.entries(DRAWN_ICONS)) if (!state.icons[type]) state.icons[type] = make();
}

// Level-pixel rectangle covered by an entity drawn at cell origin `o`.
export function entityBox(e, o) {
  const icon = state.icons[e.type];
  if (icon) return { x: o.x + e.x + icon.dx, y: o.y + e.y + icon.dy, w: icon.canvas.width, h: icon.canvas.height };
  return { x: o.x + e.x - 6, y: o.y + e.y - 12, w: 12, h: 12 };
}

export function isSelected(list, index, special) {
  const s = state.selected;
  return !!s && !!s.special === special && s.list === list && s.index === index;
}

export function selectedRecord(sel = state.selected) {
  if (!sel) return null;
  return (sel.special ? state.model.specials : state.model.entities)[sel.list][sel.index] || null;
}

// Topmost entity or special record under a level pixel (specials first).
export function entityAt(px, py) {
  const hit = (lists, special) => {
    for (let list = 0; list < lists.length; list++) {
      const o = listOrigin(list);
      if (!o) continue;
      const recs = lists[list];
      for (let i = recs.length - 1; i >= 0; i--) {
        if (recs[i].type === undefined) continue;
        const b = entityBox(recs[i], o);
        if (px >= b.x && px < b.x + b.w && py >= b.y && py < b.y + b.h) return { list, index: i, special };
      }
    }
    return null;
  };
  return hit(state.model.specials, true) || hit(state.model.entities, false);
}

// Keeps an entity inside its screen.
export function clampEntity(e) {
  e.x = Math.max(0, Math.min(255, Math.round(e.x)));
  e.y = Math.max(0, Math.min(191, Math.round(e.y)));
}

// Moves the selected record to a level pixel, possibly to another screen.
// Returns the new selection.
export function moveEntityTo(sel, px, py) {
  const loc = locate(px, py);
  if (!loc || loc.list === null) return sel;
  const lists = sel.special ? state.model.specials : state.model.entities;
  const e = lists[sel.list][sel.index];
  if (loc.list !== sel.list) {
    lists[sel.list].splice(sel.index, 1);
    lists[loc.list].push(e);
    sel = { list: loc.list, index: lists[loc.list].length - 1, special: sel.special };
  }
  e.x = loc.lx;
  e.y = loc.ly;
  clampEntity(e);
  return sel;
}

// No limit: the engine has extra entity slots for the Maker's levels
// (engine/src/rt/maker.h).
export function addEntity(type, loc) {
  const e = { type, x: loc.lx, y: loc.ly, data: 0 };
  clampEntity(e);
  const list = state.model.entities[loc.list];
  list.push(e);
  state.selected = { list: loc.list, index: list.length - 1 };
  return true;
}

// Adds a janken opponent at `loc`.
export function placeBoss(loc, part) {
  const rec = { kind: 'fixed_slot', code: 0x84, type: part.type, data: part.data, x: loc.lx, y: loc.ly };
  clampEntity(rec);
  state.model.specials[loc.list].push(rec);
  state.selected = { list: loc.list, index: state.model.specials[loc.list].length - 1, special: true };
}

// Puts the level end at `loc`, moving the existing one if there is one.
export function placeGoal(loc) {
  let found = null;
  state.model.specials.forEach((recs, list) => recs.forEach((r, i) => {
    if (r.type === GOAL_TYPE && !found) found = { list, index: i };
  }));
  const rec = found ? state.model.specials[found.list].splice(found.index, 1)[0]
    : { kind: 'fixed_slot', code: 0x84, type: GOAL_TYPE, data: 0 };
  rec.x = loc.lx;
  rec.y = loc.ly;
  clampEntity(rec);
  state.model.specials[loc.list].push(rec);
  state.selected = { list: loc.list, index: state.model.specials[loc.list].length - 1, special: true };
}

// Alex's start (model.start: a grid cell and the top left of his box in its
// screen). Simple horizontal levels can start on any screen; other shapes
// start on their first screen.
// Alex's picture, taken from the level's video memory (graphics.js).
let alex = { video: null, canvas: null };
export function startIcon() {
  if (alex.video !== state.video) alex = { video: state.video, canvas: alexImage(state.video) };
  return alex.canvas;
}

export function startBox() {
  const s = state.model.start;
  if (!s) return null;
  return { x: s.col * SCREEN_PX_W + s.x, y: s.row * SCREEN_PX_H + s.y, w: 16, h: 24 };
}

export function startAt(px, py) {
  const b = startBox();
  return !!b && px >= b.x && px < b.x + b.w && py >= b.y && py < b.y + b.h;
}

// Moves Alex's start so that his box is centred on a level pixel (half-block grid).
export function moveStartTo(px, py) {
  const s = state.model.start;
  const free = state.model.kind === 'horizontal' && state.model.canExtend;
  const col = free ? Math.max(0, Math.min(state.model.columns - 1, Math.floor(px / SCREEN_PX_W))) : s.col;
  const x = Math.round((px - col * SCREEN_PX_W - 8) / 8) * 8;
  const y = Math.round((py - s.row * SCREEN_PX_H - 12) / 8) * 8;
  s.col = col;
  s.x = Math.max(0, Math.min(SCREEN_PX_W - 16, x));
  s.y = Math.max(0, Math.min(SCREEN_PX_H - 24, y));
}

// Special records (bosses, level end) are only removed after a confirmation.
export async function confirmRemove(sel) {
  return !sel.special || ask('Supprimer cet objet spécial (boss, boule de riz…) ? Le niveau risque de ne plus pouvoir se terminer.',
    { title: 'Supprimer', ok: 'Supprimer', danger: true });
}

export function removeEntity(sel) {
  (sel.special ? state.model.specials : state.model.entities)[sel.list].splice(sel.index, 1);
  state.selected = null;
}

export async function deleteSelected() {
  const sel = state.selected;
  if (!sel || !(await confirmRemove(sel))) return;
  pushUndo();
  removeEntity(sel);
  render();
}
