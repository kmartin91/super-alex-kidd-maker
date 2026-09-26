// The level being edited (state.doc = { id, name, base }) and my levels,
// kept in this browser (backend.js).

import { $ } from './dom.js';
import { state, setDirty, SCREEN_PX_H } from './state.js';
import { openMyLevel, saveMyLevel, deleteMyLevel, defaultStart } from './backend.js';
import { buildBlockCanvases } from './graphics.js';
import { buildFamilies } from './parts.js';
import { renderPalette } from './palette.js';
import { renderLevelPanel, normalizeSurprises } from './level-panel.js';
import { clearHistory } from './history.js';
import { render } from './render.js';
import { toast } from './toast.js';
import { setPref } from './prefs.js';
import { rateDifficulty } from './difficulty.js';
import { openModal, closeModal, h, ask, tell, askText } from './modal.js';

// Largest zoom that shows a whole screen height.
function fitZoom() {
  return Math.max(1, Math.min(4, Math.floor(($('mapWrap').clientHeight - 16) / SCREEN_PX_H)));
}

// Shows a level model and the video state its graphics come from.
export function showModel(model, video) {
  if (!model.start) model.start = defaultStart(state.doc.base);
  state.model = model;
  state.video = video;
  clearHistory();
  state.selected = null;
  buildBlockCanvases();
  buildFamilies();
  state.part = model.parts.terrains.length ? { kind: 'terrain', index: 0 } : { kind: 'eraser' };
  state.category = 'terrain';
  state.zoom = fitZoom();
  renderPalette();
  renderLevelPanel();
  render();
  $('mapWrap').scrollTo(0, 0);
}

// Starts editing a level that is not saved yet (a new one, or a copy of a
// level of the game).
export function editNew({ name, base, model, video }) {
  state.doc = { id: null, name, base };
  state.level = base;
  showModel(model, video);
  setDirty(true);
  $('status').textContent = 'pas encore enregistré';
}

export async function openLevel(id) {
  $('status').textContent = 'chargement…';
  const { doc, video } = await openMyLevel(id);
  state.doc = { id: doc.id, name: doc.name, base: doc.base };
  state.level = doc.base;
  setPref('lastLevel', doc.id);
  showModel(doc.model, video);
  setDirty(false);
  $('status').textContent = 'enregistré';
}

// Asks the level's name (first save). Resolves to the name, or null if cancelled.
function askName() {
  return new Promise((resolve) => {
    let done = false;
    const finish = (name) => { if (!done) { done = true; resolve(name); } };
    const input = h('input.name-input', { id: 'levelNameInput', value: state.doc.name, maxLength: 40,
      placeholder: 'Mon super niveau', onkeydown: (ev) => { if (ev.key === 'Enter') ok(); } });
    const ok = () => {
      const name = input.value.trim();
      if (!name) { input.focus(); return; }
      finish(name);
      closeModal();
    };
    openModal('Nom du niveau', h('div.name-sheet', {},
      h('p.hint', { textContent: 'Donne un nom à ton niveau : c\'est lui qu\'on verra dans « Jouer un niveau ».' }),
      input,
      h('div.name-actions', {}, h('button.key', { id: 'nameOk', textContent: 'Enregistrer', onclick: ok }))),
    { closed: () => finish(null) });
    input.focus();
    input.select();
  });
}

export async function save() {
  if (!state.doc.id) {
    const name = await askName();
    if (!name) return;
    state.doc.name = name;
    renderLevelPanel();
  }
  normalizeSurprises();
  $('status').textContent = 'enregistrement…';
  try {
    const saved = await saveMyLevel({ ...state.doc, model: state.model, difficulty: rateDifficulty(state.model).stars });
    state.doc = { id: saved.id, name: saved.name, base: saved.base };
    setPref('lastLevel', saved.id);
    setDirty(false);
    toast('Niveau enregistré');
  } catch (err) {
    $('status').textContent = 'erreur : ' + err.message;
    tell('Enregistrement impossible : ' + err.message);
    throw err;
  }
}

export async function rename() {
  const name = await askText('Renommer le niveau', state.doc.name, { ok: 'Renommer' });
  if (!name || !name.trim()) return;
  state.doc.name = name.trim();
  renderLevelPanel();
  if (state.doc.id) await save(); else setDirty(true);
}

// Deletes the current level; returns true when done.
export async function deleteCurrent() {
  if (!(await ask(`Supprimer « ${state.doc.name || 'Sans nom'} » ? Il ne pourra pas être récupéré.`,
    { title: 'Supprimer', ok: 'Supprimer', danger: true }))) return false;
  if (state.doc.id) await deleteMyLevel(state.doc.id);
  setDirty(false);
  toast('Niveau supprimé');
  return true;
}
