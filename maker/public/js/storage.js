// The level being edited (state.doc = { id, name, base }) and my levels,
// kept in this browser (backend.js).

import { $ } from './dom.js';
import { state, setDirty, SCREEN_PX_H } from './state.js';
import { openMyLevel, saveMyLevel, deleteMyLevel, defaultStart, saveDraft, clearDraft } from './backend.js';
import { buildBlockCanvases } from './graphics.js';
import { buildFamilies } from './parts.js';
import { renderPalette } from './palette.js';
import { renderLevelPanel, normalizeSurprises } from './level-panel.js';
import { clearHistory } from './history.js';
import { render, levelThumbnail } from './render.js';
import { maybeStartTutorial } from './tutorial.js';
import { mainModel } from './bonus-zone.js';
import { toast } from './toast.js';
import { setPref } from './prefs.js';
import { rateDifficulty } from './difficulty.js';
import { openModal, closeModal, h, ask, tell, askText } from './modal.js';
import { t, tError } from './i18n.js';

// Largest zoom that shows a whole screen height.
function fitZoom() {
  return Math.max(1, Math.min(4, Math.floor(($('mapWrap').clientHeight - 16) / SCREEN_PX_H)));
}

// Shows a level model and the video state its graphics come from.
export function showModel(model, video) {
  delete model.inZone; // levels open on their main area
  document.body.classList.remove('in-zone');
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
  maybeStartTutorial();
}

// Starts editing a level that is not saved yet (a new one, or a copy of a
// level of the game).
export function editNew({ name, base, model, video }) {
  state.doc = { id: null, name, base };
  state.level = base;
  showModel(model, video);
  setDirty(true);
  $('status').textContent = t('pas encore enregistré');
}

export async function openLevel(id) {
  $('status').textContent = t('chargement…');
  const { doc, video } = await openMyLevel(id);
  state.doc = { id: doc.id, name: doc.name, base: doc.base, cleared: doc.cleared, published: doc.published };
  state.level = doc.base;
  setPref('lastLevel', doc.id);
  showModel(doc.model, video);
  setDirty(false);
  $('status').textContent = t('enregistré');
}

// Asks the level's name (first save). Resolves to the name, or null if cancelled.
function askName() {
  return new Promise((resolve) => {
    let done = false;
    const finish = (name) => { if (!done) { done = true; resolve(name); } };
    const input = h('input.name-input', { id: 'levelNameInput', value: state.doc.name, maxLength: 40,
      placeholder: t('Mon super niveau'), onkeydown: (ev) => { if (ev.key === 'Enter') ok(); } });
    const ok = () => {
      const name = input.value.trim();
      if (!name) { input.focus(); return; }
      finish(name);
      closeModal();
    };
    openModal(t('Nom du niveau'), h('div.name-sheet', {},
      h('p.hint', { textContent: t('Donne un nom à ton niveau : c\'est lui qu\'on verra dans « Jouer un niveau ».') }),
      input,
      h('div.name-actions', {}, h('button.key', { id: 'nameOk', textContent: t('Enregistrer'), onclick: ok }))),
    { closed: () => finish(null) });
    input.focus();
    input.select();
  });
}

// Writes the level into My levels (with its picture).
async function store() {
  normalizeSurprises();
  let thumb = null;
  try { thumb = levelThumbnail(); } catch { /* no picture */ }
  const version = state.version;
  const model = mainModel();
  const saved = await saveMyLevel({ ...state.doc, model, difficulty: rateDifficulty(model).stars, thumb });
  state.doc = { ...state.doc, id: saved.id };
  setPref('lastLevel', saved.id);
  if (state.version === version) setDirty(false); // else changed meanwhile: saved again later
}

// Saves what is known about the level (cleared, published) without asking.
export async function persistDoc() {
  if (state.doc.id) await store();
}

// Saves now what the autosave would save a little later.
export async function flushAutosave() {
  clearTimeout(autosaveTimer);
  if (state.dirty) await autosave();
}

export async function save() {
  if (!state.doc.id) {
    const name = await askName();
    if (!name) return;
    state.doc.name = name;
    renderLevelPanel();
  }
  $('status').textContent = t('enregistrement…');
  try {
    await store();
    clearDraft();
    toast(t('Niveau enregistré'));
  } catch (err) {
    $('status').textContent = t('erreur : {message}', { message: tError(err.message) });
    tell(t('Enregistrement impossible : {message}', { message: tError(err.message) }));
    throw err;
  }
}

// Autosave, a few seconds after the last change: a saved level is saved
// again; a level never saved yet is kept as a draft (Mes niveaux offers it
// back).
let autosaveTimer = 0;
state.onChange = () => {
  clearTimeout(autosaveTimer);
  autosaveTimer = setTimeout(autosave, 3000);
};

async function autosave() {
  if (!state.dirty || !state.model) return;
  if (state.gesture || !$('modal').hidden) { state.onChange(); return; } // later, not mid-action
  try {
    if (state.doc.id) {
      await store();
      $('status').textContent = t('enregistré automatiquement');
    } else {
      await saveDraft({ ...state.doc, model: mainModel() });
      $('status').textContent = t('brouillon gardé');
    }
  } catch (err) {
    // An invalid level (it can't be built): kept as it is until fixed.
    $('status').textContent = t('pas enregistré : {message}', { message: tError(err.message) });
  }
}

export async function rename() {
  const name = await askText(t('Renommer le niveau'), state.doc.name, { ok: t('Renommer') });
  if (!name || !name.trim()) return;
  state.doc.name = name.trim();
  renderLevelPanel();
  if (state.doc.id) await save(); else setDirty(true);
}

// Deletes the current level; returns true when done.
export async function deleteCurrent() {
  if (!(await ask(t('Supprimer « {name} » ? Il ne pourra pas être récupéré.', { name: state.doc.name || t('Sans nom') }),
    { title: t('Supprimer'), ok: t('Supprimer'), danger: true }))) return false;
  if (state.doc.id) await deleteMyLevel(state.doc.id);
  setDirty(false);
  toast(t('Niveau supprimé'));
  return true;
}
