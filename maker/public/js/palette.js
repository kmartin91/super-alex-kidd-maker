// Top bar: the parts shelf, by category (terrain, blocks, decorations,
// enemies, every raw block). Click a part to take it in hand, or drag it
// onto the map.

import { $, copyCanvas } from './dom.js';
import { state } from './state.js';
import { icon } from './icons.js';
import { partCanvas } from './parts.js';
import { selectPart, samePart } from './brush.js';
import { blockClass, classLabel } from './blocks.js';
import { GOAL_TYPE, BOSSES, entityColor } from './entities.js';
import { setTip } from './tooltip.js';

const SLOT = 44; // largest picture size in a slot, in CSS pixels

function inTheme(t) {
  return (t.levels || []).includes(state.model.theme || state.level);
}

// Items of a category: [{ part, name, canvas, off }]
function items(category) {
  const p = state.model.parts;
  switch (category) {
    case 'terrain':
      return p.terrains.map((t, i) => ({ part: { kind: 'terrain', index: i }, name: `Sol ${i + 1} · se raccorde tout seul` }));
    case 'blocks':
      return p.blocks.map((b) => ({ part: { kind: 'block', id: b.metatile }, name: b.name }));
    case 'decor':
      return p.stamps.map((st, i) => ({ part: { kind: 'stamp', index: i }, name: `Décor ${st.w}×${st.h}` }));
    case 'enemies': {
      const list = [{ part: { kind: 'goal' }, name: 'Arrivée : la boule de riz (une par niveau)' }];
      for (const b of BOSSES) list.push({ part: { kind: 'boss', type: b.type, data: b.data }, name: `${b.name} · boss : ${b.what}` });
      // Enemies of every setting work anywhere (engine/src/rt/maker.h); the
      // setting's own come first.
      const types = state.model.entityTypes.slice().sort((a, b) => inTheme(b) - inTheme(a));
      for (const t of types) list.push({ part: { kind: 'entity', type: t.id }, name: t.name });
      return list;
    }
    default:
      return state.blockCanvases.map((_, i) => ({ part: { kind: 'block', id: i }, name: `Bloc ${i} · ${classLabel(blockClass(i)) || 'vide'}` }));
  }
}

const isEntity = (part) => part.kind === 'entity' || part.kind === 'goal' || part.kind === 'boss';

function picture(part) {
  if (part.kind === 'goal' || part.kind === 'entity' || part.kind === 'boss') {
    const type = part.kind === 'goal' ? GOAL_TYPE : part.type;
    const ic = state.icons[type];
    if (ic) return copyCanvas(ic.canvas);
    const c = document.createElement('canvas');
    c.width = c.height = 12;
    const ctx = c.getContext('2d');
    ctx.fillStyle = entityColor(type);
    ctx.fillRect(0, 0, 12, 12);
    return c;
  }
  return partCanvas(part);
}

// Scales a pixel picture by a whole factor when it fits, to keep it crisp.
function fit(canvas, max) {
  const big = Math.max(canvas.width, canvas.height);
  const scale = big * 2 <= max ? Math.floor(max / big) : max / big;
  canvas.style.width = `${canvas.width * Math.min(scale, 3)}px`;
  canvas.style.height = `${canvas.height * Math.min(scale, 3)}px`;
  return canvas;
}

const CATEGORIES = [
  { id: 'terrain', label: 'Sol', icon: 'theme', tip: 'Le sol et les murs : les bords et les coins se raccordent tout seuls' },
  { id: 'blocks', label: 'Blocs', icon: 'box', tip: 'Boîtes ?, boîtes étoile, têtes de mort, argent, roches cassables, pièges…' },
  { id: 'decor', label: 'Décor', icon: 'tree', tip: 'Nuages, arbres, maisons… posés d\'un clic, sans effet sur le jeu' },
  { id: 'enemies', label: 'Ennemis', icon: 'enemy', tip: 'Les ennemis et la boule de riz qui termine le niveau' },
  { id: 'all', label: 'Tous les blocs', icon: 'grid', tip: 'Les 256 blocs bruts du niveau, pour les experts' },
];

function showName(text) {
  $('itemName').textContent = text || '';
}

function currentName() {
  const it = items(state.category).find((i) => samePart(i.part, state.part));
  if (it) return it.name;
  if (state.part.kind === 'eraser') return 'Gomme · efface les blocs et les ennemis';
  return '';
}

export function renderPalette() {
  const tabs = $('categories');
  tabs.replaceChildren();
  for (const c of CATEGORIES) {
    const b = document.createElement('button');
    b.className = 'tab' + (c.id === state.category ? ' active' : '');
    b.dataset.category = c.id;
    setTip(b, c.label, c.tip);
    b.append(icon(c.icon, 2), Object.assign(document.createElement('span'), { textContent: c.label }));
    b.addEventListener('click', () => { state.category = c.id; renderPalette(); });
    tabs.appendChild(b);
  }
  const shelf = $('items');
  shelf.replaceChildren();
  shelf.classList.toggle('dense', state.category === 'all');
  for (const it of items(state.category)) {
    const slot = document.createElement('button');
    slot.className = 'slot' + (samePart(it.part, state.part) ? ' active' : '') + (it.off ? ' off' : '');
    slot.draggable = true;
    const [title, more] = it.name.split(' · ');
    setTip(slot, title, more || (isEntity(it.part) ? 'Clique puis pose-le sur la carte, ou glisse-le' : 'Clique puis peins sur la carte, ou glisse-le'));
    slot.appendChild(fit(picture(it.part), state.category === 'all' ? 32 : SLOT));
    slot.addEventListener('click', () => selectPart(it.part));
    slot.addEventListener('mouseenter', () => showName(it.name));
    slot.addEventListener('mouseleave', () => showName(currentName()));
    slot.addEventListener('dragstart', (ev) => ev.dataTransfer.setData('text/x-part', JSON.stringify(it.part)));
    shelf.appendChild(slot);
  }
  showName(currentName());
  $('eraser').classList.toggle('active', state.part.kind === 'eraser');
}
