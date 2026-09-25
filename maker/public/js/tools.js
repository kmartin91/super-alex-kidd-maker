// Right column (undo, eraser, view, save, menu) and keyboard shortcuts.

import { $ } from './dom.js';
import { state } from './state.js';
import { undo, redo } from './history.js';
import { render } from './render.js';
import { selectPart } from './brush.js';
import { deleteSelected } from './entities.js';
import { save, revert } from './storage.js';
import { togglePlay, backToEdit, playNative } from './play.js';
import { openModal, closeModal, h } from './modal.js';
import { icon } from './icons.js';

function setZoom(z) {
  const wrap = $('mapWrap');
  const cx = (wrap.scrollLeft + wrap.clientWidth / 2) / state.zoom, cy = (wrap.scrollTop + wrap.clientHeight / 2) / state.zoom;
  state.zoom = Math.max(1, Math.min(4, z));
  render();
  wrap.scrollLeft = cx * state.zoom - wrap.clientWidth / 2;
  wrap.scrollTop = cy * state.zoom - wrap.clientHeight / 2;
}

function toggleView(key, button) {
  state[key] = !state[key];
  $(button).classList.toggle('active', state[key]);
  render();
}

function toggleEraser() {
  selectPart(state.part.kind === 'eraser' ? state.lastPart || { kind: 'terrain', index: 0 } : { kind: 'eraser' });
}

function openMenu() {
  const item = (id, ic, title, text, action) => h('button.menu-item', { id, onclick: () => { closeModal(); action(); } },
    icon(ic, 3), h('span', {}, h('b', { textContent: title }), h('small', { textContent: text })));
  openModal('Menu', h('div.menu', {},
    item('saveMenu', 'save', 'Enregistrer', 'dans le mod (Ctrl+S)', save),
    item('playNative', 'window', 'Ouvrir dans la fenêtre du jeu', 'enregistre, puis lance le vrai jeu', playNative),
    item('revert', 'start', 'Revenir au niveau d\'origine', 'efface tes changements de ce niveau', revert),
    item('helpMenu', 'help', 'Commandes', 'souris et clavier', openHelp)));
}

function openHelp() {
  const rows = [
    ['Clic', 'poser l\'objet choisi en haut'],
    ['Glisser', 'peindre des blocs'],
    ['Clic sur un ennemi', 'le choisir ; glisser pour le déplacer'],
    ['Clic droit', 'effacer (glisser pour effacer plus)'],
    ['Maj + glisser', 'remplir un rectangle'],
    ['Alt + clic', 'prendre le bloc sous la souris'],
    ['Ctrl+Z / Ctrl+Y', 'annuler / rétablir'],
    ['Ctrl+S', 'enregistrer'],
    ['Espace ou F5', 'jouer / revenir à l\'édition'],
    ['G, C, + et −', 'grille, collisions, zoom'],
    ['En jeu', 'flèches, Espace ou X : sauter, Z ou W : coup de poing, Entrée : pause'],
  ];
  openModal('Commandes', h('dl.help', {}, ...rows.flatMap(([k, v]) => [h('dt', { textContent: k }), h('dd', { textContent: v })])));
}

function onKeyDown(ev) {
  if (state.playing) {
    if (ev.key === 'Escape' || ev.key === 'F5') { ev.preventDefault(); backToEdit(); }
    return;
  }
  if (ev.key === 'Escape') {
    if (!closeModal() && state.selected) { state.selected = null; render(); }
    return;
  }
  if (!$('modal').hidden || ev.target.tagName === 'INPUT' || ev.target.tagName === 'SELECT') return;
  const k = ev.key.toLowerCase();
  const mod = ev.ctrlKey || ev.metaKey;
  if (mod && k === 'z') { ev.preventDefault(); ev.shiftKey ? redo() : undo(); }
  else if (mod && k === 'y') { ev.preventDefault(); redo(); }
  else if (mod && k === 's') { ev.preventDefault(); save(); }
  else if (mod) return;
  else if (ev.key === 'F5' || ev.key === ' ') { ev.preventDefault(); togglePlay(); }
  else if (ev.key === 'Delete' || ev.key === 'Backspace') deleteSelected();
  else if (k === 'g') toggleView('showGrid', 'gridBtn');
  else if (k === 'c') toggleView('showSolid', 'solidBtn');
  else if (k === '+' || k === '=') setZoom(state.zoom + 1);
  else if (k === '-') setZoom(state.zoom - 1);
  else if (k === 'e') toggleEraser();
  else if (k === '?') openHelp();
}

export function bindTools() {
  $('undo').addEventListener('click', undo);
  $('redo').addEventListener('click', redo);
  $('eraser').addEventListener('click', toggleEraser);
  $('gridBtn').addEventListener('click', () => toggleView('showGrid', 'gridBtn'));
  $('solidBtn').addEventListener('click', () => toggleView('showSolid', 'solidBtn'));
  $('zoomIn').addEventListener('click', () => setZoom(state.zoom + 1));
  $('zoomOut').addEventListener('click', () => setZoom(state.zoom - 1));
  $('save').addEventListener('click', save);
  $('menuBtn').addEventListener('click', openMenu);
  $('helpBtn').addEventListener('click', openHelp);
  $('mapWrap').addEventListener('wheel', (ev) => {
    if (!ev.ctrlKey) return;
    ev.preventDefault();
    setZoom(state.zoom + (ev.deltaY < 0 ? 1 : -1));
  }, { passive: false });
  window.addEventListener('keydown', onKeyDown);
  window.addEventListener('beforeunload', (ev) => { if (state.dirty) ev.preventDefault(); });
}
