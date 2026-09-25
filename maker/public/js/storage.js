// Loading, saving and reverting levels through the server.

import { $ } from './dom.js';
import { state, setDirty, SCREEN_PX_H } from './state.js';
import { api } from './api.js';
import { buildBlockCanvases } from './graphics.js';
import { buildFamilies } from './parts.js';
import { renderPalette } from './palette.js';
import { renderLevelPanel, normalizeSurprises } from './level-panel.js';
import { clearHistory } from './history.js';
import { render } from './render.js';
import { toast } from './toast.js';

// Largest zoom that shows a whole screen height.
function fitZoom() {
  return Math.max(1, Math.min(4, Math.floor(($('mapWrap').clientHeight - 16) / SCREEN_PX_H)));
}

// Shows a level model and the video state its graphics come from.
export function showModel(model, video) {
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

export async function loadLevel(n) {
  $('status').textContent = 'chargement…';
  const { model, video, modified } = await api(`/api/level/${n}`);
  state.level = n;
  window.history.replaceState(null, '', `?level=${n}`);
  showModel(model, video);
  setDirty(false);
  $('status').textContent = modified ? 'niveau modifié' : 'niveau d\'origine';
}

export async function save() {
  normalizeSurprises();
  $('status').textContent = 'enregistrement…';
  try {
    await api(`/api/level/${state.level}`, { method: 'PUT', body: JSON.stringify(state.model) });
    setDirty(false);
    toast('Niveau enregistré');
  } catch (err) {
    $('status').textContent = 'erreur : ' + err.message;
    alert('Enregistrement impossible : ' + err.message);
    throw err;
  }
}

export async function revert() {
  if (!confirm('Revenir au niveau d\'origine ? Tes modifications de ce niveau seront supprimées du mod.')) return;
  await api(`/api/level/${state.level}`, { method: 'DELETE' });
  await loadLevel(state.level);
  toast('Niveau d\'origine rétabli');
}
