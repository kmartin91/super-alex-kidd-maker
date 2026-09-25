// Enemies, items and special records (bosses, fixed objects, level end).
//
// Entities live in per-screen lists (state.model.entities[list]) with
// coordinates relative to their screen. A selection is {list, index, special?}.

import { state } from './state.js';
import { api } from './api.js';
import { decodeBase64 } from './graphics.js';
import { locate, listOrigin } from './level.js';
import { pushUndo } from './history.js';
import { render } from './render.js';

// The level end (rice ball): one per level, kept in a $84 special record.
export const GOAL_TYPE = 0x44;

export function hex2(n) {
  return n.toString(16).toUpperCase().padStart(2, '0');
}

export function entityName(type) {
  const t = state.model.entityTypes.find((e) => e.id === type);
  return t ? t.name : `Objet $${hex2(type)}`;
}

export function specialName(type) {
  const t = (state.model.specialTypes || []).find((e) => e.id === type);
  return t ? t.name : entityName(type);
}

// Fallback colour for entities without an icon.
export function entityColor(type) {
  return `hsl(${(type * 47) % 360} 75% 55%)`;
}

// Hand-drawn rice ball for the level end, when the game gives no picture of it.
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

function riceBall() {
  const c = document.createElement('canvas');
  c.width = RICE_BALL[0].length;
  c.height = RICE_BALL.length;
  const ctx = c.getContext('2d');
  const colors = { '#': '#19121c', w: '#ffffff', n: '#1f3a2a' };
  RICE_BALL.forEach((row, y) => [...row].forEach((ch, x) => {
    if (!colors[ch]) return;
    ctx.fillStyle = colors[ch];
    ctx.fillRect(x, y, 1, 1);
  }));
  return { canvas: c, dx: -8, dy: -12 };
}

// Loads the sprites drawn for each entity type (state.icons).
export async function loadIcons() {
  try {
    const raw = await api('/api/entity-icons');
    for (const [type, ic] of Object.entries(raw)) {
      const c = document.createElement('canvas');
      c.width = ic.w;
      c.height = ic.h;
      const img = new ImageData(new Uint8ClampedArray(decodeBase64(ic.rgba).buffer), ic.w, ic.h);
      c.getContext('2d').putImageData(img, 0, 0);
      state.icons[Number(type)] = { canvas: c, dx: ic.dx, dy: ic.dy };
    }
  } catch (err) {
    console.warn('entity icons unavailable:', err.message);
  }
  if (!state.icons[GOAL_TYPE]) state.icons[GOAL_TYPE] = riceBall();
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

export function addEntity(type, loc) {
  const e = { type, x: loc.lx, y: loc.ly, data: 0 };
  clampEntity(e);
  state.model.entities[loc.list].push(e);
  state.selected = { list: loc.list, index: state.model.entities[loc.list].length - 1 };
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

// Special records (bosses, level end) are only removed after a confirmation.
export function confirmRemove(sel) {
  return !sel.special || confirm('Supprimer cet objet spécial (boss, boule de riz…) ? Le niveau risque de ne plus pouvoir se terminer.');
}

export function removeEntity(sel) {
  (sel.special ? state.model.specials : state.model.entities)[sel.list].splice(sel.index, 1);
  state.selected = null;
}

export function deleteSelected() {
  const sel = state.selected;
  if (!sel || !confirmRemove(sel)) return;
  pushUndo();
  removeEntity(sel);
  render();
}
