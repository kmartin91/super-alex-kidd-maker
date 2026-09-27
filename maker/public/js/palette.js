// Top bar: the parts shelf, by category (terrain, blocks, decorations,
// enemies, every raw block). Click a part to take it in hand, or drag it
// onto the map.

import { $, copyCanvas } from './dom.js';
import { state } from './state.js';
import { icon } from './icons.js';
import { partCanvas } from './parts.js';
import { selectPart, samePart } from './brush.js';
import { blockClass, classLabel } from './blocks.js';
import { GOAL_TYPE, BOSSES, entityColor, HIDDEN_TYPES, SETTING_ONLY } from './entities.js';
import { setTip } from './tooltip.js';
import { t, tName } from './i18n.js';
import { hazardParts } from './hazards.js';

const SLOT = 44; // largest picture size in a slot, in CSS pixels

function inTheme(type) {
  return (type.levels || []).includes(state.model.theme || state.level);
}

// Items of a category: [{ part, name, canvas, off }] (names in the page's
// language; " · " separates the title from the explanation).
function items(category) {
  const p = state.model.parts;
  switch (category) {
    case 'terrain':
      return p.terrains.map((_, i) => ({ part: { kind: 'terrain', index: i }, name: t('Sol {n} · se raccorde tout seul', { n: i + 1 }) }));
    case 'blocks':
      // The setting's deadly blocks first (spikes, lava...), then the others.
      return hazardParts(state.model.theme || state.level).map((hz) => ({ part: hz, name: t('{name} · mortel', { name: t(hz.name) }) }))
        .concat(p.blocks.map((b) => ({ part: { kind: 'block', id: b.metatile }, name: tName(b.name) })));
    case 'decor':
      return p.stamps.map((st, i) => ({ part: { kind: 'stamp', index: i }, name: t('Décor {w}×{h}', { w: st.w, h: st.h }) }));
    case 'enemies': {
      const list = [{ part: { kind: 'goal' }, name: t('Arrivée : la boule de riz (une par niveau)') }];
      // Doors of the bonus zone (bonus-zone.js): into it from the level, back from it.
      if (state.model.inZone) list.push({ part: { kind: 'door', data: 1 }, name: t('Porte de retour · Alex revient dans le niveau, là où il était entré') });
      else if (state.model.zone) list.push({ part: { kind: 'door', data: 0 }, name: t('Porte vers la zone bonus · Alex y entre en la touchant') });
      for (const b of BOSSES) {
        list.push({ part: { kind: 'boss', type: b.type, data: b.data }, name: t('{name} · boss : {what}', { name: b.name, what: b.what }) });
      }
      // Enemies of every setting work anywhere (engine/src/rt/maker.h); the
      // setting's own come first.
      // Traps drawn with the castles' tiles only in their setting; a few
      // objects that only work at one place of the game are left out.
      const types = state.model.entityTypes.filter((type) => !HIDDEN_TYPES.includes(type.id) && (!SETTING_ONLY.includes(type.id) || inTheme(type)))
        .sort((a, b) => inTheme(b) - inTheme(a));
      for (const type of types) list.push({ part: { kind: 'entity', type: type.id }, name: tName(type.name) });
      return list;
    }
    default:
      return state.blockCanvases.map((_, i) => ({ part: { kind: 'block', id: i },
        name: t('Bloc {i} · {kind}', { i, kind: classLabel(blockClass(i)) || t('vide') }) }));
  }
}

const isEntity = (part) => ['entity', 'goal', 'boss', 'door'].includes(part.kind);

function picture(part) {
  if (part.kind === 'goal' || part.kind === 'entity' || part.kind === 'boss' || part.kind === 'door') {
    const type = part.kind === 'goal' ? GOAL_TYPE : part.kind === 'door' ? 0x4C : part.type;
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
  if (state.part.kind === 'eraser') return t('Gomme · efface les blocs et les ennemis');
  if (state.part.kind === 'select') return t('Sélection · entoure une zone : Ctrl+C copie, Ctrl+X coupe, Suppr efface, Ctrl+V colle');
  return '';
}

export function renderPalette() {
  const tabs = $('categories');
  tabs.replaceChildren();
  for (const c of CATEGORIES) {
    const b = document.createElement('button');
    b.className = 'tab' + (c.id === state.category ? ' active' : '');
    b.dataset.category = c.id;
    setTip(b, t(c.label), t(c.tip));
    b.append(icon(c.icon, 2), Object.assign(document.createElement('span'), { textContent: t(c.label) }));
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
    setTip(slot, title, more || (isEntity(it.part) ? t('Clique puis pose-le sur la carte, ou glisse-le')
      : t('Clique puis peins sur la carte, ou glisse-le')));
    slot.appendChild(fit(picture(it.part), state.category === 'all' ? 32 : SLOT));
    slot.addEventListener('click', () => selectPart(it.part));
    slot.addEventListener('mouseenter', () => showName(it.name));
    slot.addEventListener('mouseleave', () => showName(currentName()));
    slot.addEventListener('dragstart', (ev) => ev.dataTransfer.setData('text/x-part', JSON.stringify(it.part)));
    shelf.appendChild(slot);
  }
  showName(currentName());
  $('eraser').classList.toggle('active', state.part.kind === 'eraser');
  $('selectBtn').classList.toggle('active', state.part.kind === 'select');
}
