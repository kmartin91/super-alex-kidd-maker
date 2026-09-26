// Play mode: the level runs in the page (WebAssembly engine), from the screen
// in view, saved or not. The big button in the corner switches back and forth.

import { $ } from './dom.js';
import { state, SCREEN_PX_W } from './state.js';
import { testPatch } from './backend.js';
import { Player } from './player.js';
import { normalizeSurprises } from './level-panel.js';
import { toast } from './toast.js';
import { setTip } from './tooltip.js';

const STATUS = { starting: 'démarrage…', playing: 'en jeu', stopped: 'arrêté', cleared: 'niveau terminé !' };
let player = null;

function setPlaying(on) {
  state.playing = on;
  document.body.classList.toggle('playing', on);
  $('playView').hidden = !on;
  $('playLabel').textContent = on ? 'Éditer' : 'Jouer';
  $('playIconPlay').hidden = on;
  $('playIconEdit').hidden = !on;
  if (on) setTip($('playBtn'), 'Éditer', 'Arrête la partie et reviens à l\'édition (Échap)');
  else setTip($('playBtn'), 'Jouer', 'Teste ton niveau tout de suite, à partir de l\'écran affiché (touche Espace)');
}

// startColumn: null = level start.
export async function playLevel(startColumn = null) {
  if (!player) {
    player = new Player($('game'), (st) => {
      $('playStatus').textContent = STATUS[st] || st;
      // A level of the Maker ends with itself: back to editing.
      if (st === 'cleared') { toast('Bravo, niveau terminé !'); setTimeout(backToEdit, 400); }
    });
  }
  normalizeSurprises();
  setPlaying(true);
  $('playStatus').textContent = 'préparation du niveau…';
  $('playFrom').textContent = startColumn ? `depuis l'écran ${startColumn + 1}` : 'depuis le début';
  try {
    const patch = testPatch(state.doc.base, state.model, startColumn);
    player.play(patch, state.level).catch((err) => { $('playStatus').textContent = 'erreur : ' + err.message; });
  } catch (err) {
    $('playStatus').textContent = 'erreur : ' + err.message;
  }
}

// Screen to start from: the one at the left edge of the view (simple
// horizontal levels only), so the level start as long as it is in view.
function startColumn() {
  if (state.model.kind !== 'horizontal') return null;
  const wrap = $('mapWrap');
  const left = Math.max(0, wrap.scrollLeft - $('map').offsetLeft) / state.zoom;
  const col = Math.min(state.model.columns - 1, Math.round(left / SCREEN_PX_W));
  return col > 0 ? col : null;
}

// For automated tests (maker/tests/).
window.makerPlayer = () => player;

// playHooks.done: called once when the game stops (the menu goes back to itself).
export const playHooks = { done: null };

export async function backToEdit() {
  if (player) await player.stop();
  setPlaying(false);
  const done = playHooks.done;
  playHooks.done = null;
  if (done) done();
}

export function togglePlay() {
  return state.playing ? backToEdit() : playLevel(startColumn());
}

export function download(blob, name) {
  const a = Object.assign(document.createElement('a'), { href: URL.createObjectURL(blob), download: name });
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

export function bindPlay() {
  $('playBtn').addEventListener('click', togglePlay);
  $('playStart').addEventListener('click', () => playLevel(null));
}
