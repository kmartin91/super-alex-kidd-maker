// Right column (undo, eraser, view, save, menu) and keyboard shortcuts.

import { $ } from './dom.js';
import { state } from './state.js';
import { undo, redo } from './history.js';
import { render } from './render.js';
import { selectPart } from './brush.js';
import { deleteSelected } from './entities.js';
import { save, rename, deleteCurrent, openLevel } from './storage.js';
import { togglePlay, backToEdit, download } from './play.js';
import { levelFile, importLevelFile } from './backend.js';
import { rateDifficulty } from './difficulty.js';
import { openLevelSheet } from './new-level.js';
import { askRom } from './rom-setup.js';
import { toast } from './toast.js';
import { openModal, closeModal, h, tell } from './modal.js';
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
    item('saveMenu', 'save', 'Enregistrer', 'dans Mes niveaux (Ctrl+S)', save),
    item('renameMenu', 'pencil', 'Renommer ce niveau', state.doc.name, rename),
    item('exportLevelMenu', 'window', 'Exporter ce niveau', 'un fichier à garder ou à partager', saveLevelFile),
    item('importLevelMenu', 'plus', 'Importer un niveau', 'depuis un fichier exporté', openLevelFileDialog),
    item('deleteMenu', 'trash', 'Supprimer ce niveau', 'définitivement', async () => { if (await deleteCurrent()) openLevelSheet(); }),
    item('romMenu', 'box', 'Changer de ROM', 'si tu as une autre copie du jeu', () => askRom({ first: false })),
    item('helpMenu', 'help', 'Commandes', 'souris et clavier', openHelp)));
}

async function saveLevelFile() {
  const data = levelFile({ ...state.doc, model: state.model, difficulty: rateDifficulty(state.model).stars });
  const file = state.doc.name.replace(/[^\p{L}\p{N} _-]+/gu, '').trim() || 'niveau';
  download(new Blob([JSON.stringify(data)], { type: 'application/json' }), `${file}.json`);
}

function openLevelFileDialog() {
  const input = Object.assign(document.createElement('input'), { type: 'file' });
  input.addEventListener('change', async () => {
    try {
      const docs = await importLevelFile(JSON.parse(await input.files[0].text()));
      toast(docs.length > 1 ? `${docs.length} niveaux importés` : `« ${docs[0].name} » importé`);
      await openLevel(docs[0].id);
    } catch (err) {
      tell('Import impossible : ' + err.message);
    }
  });
  input.click();
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
