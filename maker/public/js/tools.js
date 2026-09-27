// Right column (undo, eraser, view, save, menu) and keyboard shortcuts.

import { $ } from './dom.js';
import { state } from './state.js';
import { undo, redo } from './history.js';
import { render } from './render.js';
import { selectPart } from './brush.js';
import { deleteSelected } from './entities.js';
import { copySelection, cutSelection, clearSelection, startPaste } from './clipboard.js';
import { startTutorial } from './tutorial.js';
import { openPublish } from './online-ui.js';
import { mainModel } from './bonus-zone.js';
import { save, rename, deleteCurrent, openLevel } from './storage.js';
import { togglePlay, backToEdit, download } from './play.js';
import { levelFile, importLevelFile } from './backend.js';
import { rateDifficulty } from './difficulty.js';
import { openLevelSheet } from './new-level.js';
import { askRom } from './rom-setup.js';
import { toast } from './toast.js';
import { openModal, closeModal, h, tell } from './modal.js';
import { icon } from './icons.js';
import { t, tError } from './i18n.js';

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

function toggleSelect() {
  selectPart(state.part.kind === 'select' ? state.lastPart || { kind: 'terrain', index: 0 } : { kind: 'select' });
  render();
}

function openMenu() {
  const item = (id, ic, title, text, action) => h('button.menu-item', { id, onclick: () => { closeModal(); action(); } },
    icon(ic, 3), h('span', {}, h('b', { textContent: title }), h('small', { textContent: text })));
  openModal(t('Menu'), h('div.menu', {},
    item('saveMenu', 'save', t('Enregistrer'), t('dans Mes niveaux (Ctrl+S)'), save),
    item('renameMenu', 'pencil', t('Renommer ce niveau'), state.doc.name, rename),
    item('publishMenu', 'globe', t('Publier en ligne'),
      state.doc.published ? t('en ligne : {code}', { code: state.doc.published.code }) : t('pour que tout le monde y joue'), openPublish),
    item('exportLevelMenu', 'window', t('Exporter ce niveau'), t('un fichier à garder ou à partager'), saveLevelFile),
    item('importLevelMenu', 'plus', t('Importer un niveau'), t('depuis un fichier exporté'), openLevelFileDialog),
    item('deleteMenu', 'trash', t('Supprimer ce niveau'), t('définitivement'), async () => { if (await deleteCurrent()) openLevelSheet(); }),
    item('romMenu', 'box', t('Changer de ROM'), t('si tu as une autre copie du jeu'), () => askRom({ first: false })),
    item('helpMenu', 'help', t('Commandes'), t('souris et clavier'), openHelp)));
}

async function saveLevelFile() {
  const model = mainModel();
  const data = levelFile({ ...state.doc, model, difficulty: rateDifficulty(model).stars });
  const file = state.doc.name.replace(/[^\p{L}\p{N} _-]+/gu, '').trim() || t('niveau');
  download(new Blob([JSON.stringify(data)], { type: 'application/json' }), `${file}.json`);
}

function openLevelFileDialog() {
  const input = Object.assign(document.createElement('input'), { type: 'file' });
  input.addEventListener('change', async () => {
    try {
      const docs = await importLevelFile(JSON.parse(await input.files[0].text()));
      toast(docs.length > 1 ? t('{n} niveaux importés', { n: docs.length }) : t('« {name} » importé', { name: docs[0].name }));
      await openLevel(docs[0].id);
    } catch (err) {
      tell(t('Import impossible : {message}', { message: tError(err.message) }));
    }
  });
  input.click();
}

function openHelp() {
  const rows = [
    [t('Clic'), t('poser l\'objet choisi en haut')],
    [t('Glisser'), t('peindre des blocs')],
    [t('Clic sur un ennemi'), t('le choisir ; glisser pour le déplacer')],
    [t('Clic droit'), t('effacer (glisser pour effacer plus)')],
    [t('Maj + glisser'), t('remplir un rectangle')],
    [t('Alt + clic'), t('prendre le bloc sous la souris')],
    [t('S puis glisser'), t('sélectionner une zone : Ctrl+C la copie, Ctrl+X la coupe, Suppr l\'efface')],
    ['Ctrl+V', t('coller la zone copiée : clic pour la poser, Échap pour annuler')],
    [t('Glisser Alex (Départ)'), t('choisir où le niveau commence')],
    ['Ctrl+Z / Ctrl+Y', t('annuler / rétablir')],
    ['Ctrl+S', t('enregistrer')],
    [t('Espace ou F5'), t('jouer / revenir à l\'édition')],
    [t('G, C, + et −'), t('grille, collisions, zoom')],
    [t('En jeu'), t('flèches, Espace ou X : sauter, Z ou W : coup de poing, Entrée : pause')],
  ];
  openModal(t('Commandes'), h('div', {},
    h('dl.help', {}, ...rows.flatMap(([k, v]) => [h('dt', { textContent: k }), h('dd', { textContent: v })])),
    h('div.dialog-actions', {},
      h('button.key.small.plain', { textContent: t('Revoir le tutoriel'), onclick: () => { closeModal(); startTutorial(); } }))));
}

function onKeyDown(ev) {
  if (state.playing) {
    if (ev.key === 'Escape' || ev.key === 'F5') { ev.preventDefault(); backToEdit(); }
    return;
  }
  if (ev.key === 'Escape') {
    if (closeModal()) return;
    if (state.paste || state.selection) { state.paste = null; state.selection = null; render(); return; }
    if (state.selected) { state.selected = null; render(); }
    return;
  }
  if (!$('modal').hidden || ev.target.tagName === 'INPUT' || ev.target.tagName === 'SELECT') return;
  const k = ev.key.toLowerCase();
  const mod = ev.ctrlKey || ev.metaKey;
  if (mod && k === 'z') { ev.preventDefault(); ev.shiftKey ? redo() : undo(); }
  else if (mod && k === 'y') { ev.preventDefault(); redo(); }
  else if (mod && k === 's') { ev.preventDefault(); save(); }
  else if (mod && k === 'c' && state.selection) { ev.preventDefault(); copySelection(); }
  else if (mod && k === 'x' && state.selection) { ev.preventDefault(); cutSelection(); }
  else if (mod && k === 'v') { ev.preventDefault(); startPaste(); }
  else if (mod) return;
  else if (ev.key === 'F5' || ev.key === ' ') { ev.preventDefault(); togglePlay(); }
  else if (ev.key === 'Delete' || ev.key === 'Backspace') { if (state.selection) clearSelection(); else deleteSelected(); }
  else if (k === 'g') toggleView('showGrid', 'gridBtn');
  else if (k === 'c') toggleView('showSolid', 'solidBtn');
  else if (k === '+' || k === '=') setZoom(state.zoom + 1);
  else if (k === '-') setZoom(state.zoom - 1);
  else if (k === 'e') toggleEraser();
  else if (k === 's') toggleSelect();
  else if (k === '?') openHelp();
}

export function bindTools() {
  $('undo').addEventListener('click', undo);
  $('redo').addEventListener('click', redo);
  $('eraser').addEventListener('click', toggleEraser);
  $('selectBtn').addEventListener('click', toggleSelect);
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
