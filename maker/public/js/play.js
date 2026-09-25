// Play mode: the level runs in the page (WebAssembly engine), from the screen
// in view, saved or not. The big button in the corner switches back and forth.

import { $ } from './dom.js';
import { state } from './state.js';
import { api } from './api.js';
import { Player } from './player.js';
import { viewCell } from './level.js';
import { save } from './storage.js';
import { normalizeSurprises } from './level-panel.js';
import { toast } from './toast.js';

const STATUS = { starting: 'démarrage…', playing: 'en jeu', stopped: 'arrêté' };
let player = null;

function setPlaying(on) {
  state.playing = on;
  document.body.classList.toggle('playing', on);
  $('playView').hidden = !on;
  $('playLabel').textContent = on ? 'Éditer' : 'Jouer';
  $('playIconPlay').hidden = on;
  $('playIconEdit').hidden = !on;
}

// startColumn: null = level start.
export async function playLevel(startColumn = null) {
  if (!player) player = new Player($('game'), (st) => { $('playStatus').textContent = STATUS[st] || st; });
  normalizeSurprises();
  setPlaying(true);
  $('playStatus').textContent = 'préparation du niveau…';
  $('playFrom').textContent = startColumn ? `depuis l'écran ${startColumn + 1}` : 'depuis le début';
  try {
    const q = startColumn === null ? '' : `?start=${startColumn}`;
    const res = await fetch(`/api/testpatch/${state.level}${q}`, {
      method: 'POST', body: state.dirty ? JSON.stringify(state.model) : '',
    });
    if (!res.ok) throw new Error((await res.json()).error);
    const patch = new Uint8Array(await res.arrayBuffer());
    player.play(patch, state.level).catch((err) => { $('playStatus').textContent = 'erreur : ' + err.message; });
  } catch (err) {
    $('playStatus').textContent = 'erreur : ' + err.message;
  }
}

// Screen to start from: the one in view (simple horizontal levels only).
function startColumn() {
  if (state.model.kind !== 'horizontal') return null;
  const { col } = viewCell();
  return col > 0 ? col : null;
}

export async function backToEdit() {
  if (player) await player.stop();
  setPlaying(false);
}

export function togglePlay() {
  return state.playing ? backToEdit() : playLevel(startColumn());
}

// Saves, then opens the level in the native game window (engine/build/alexkidd).
export async function playNative() {
  if (state.dirty) await save();
  await api(`/api/play/${state.level}`, { method: 'POST' });
  toast('Le jeu s\'ouvre dans sa propre fenêtre');
}

export function bindPlay() {
  $('playBtn').addEventListener('click', togglePlay);
  $('playStart').addEventListener('click', () => playLevel(null));
}
