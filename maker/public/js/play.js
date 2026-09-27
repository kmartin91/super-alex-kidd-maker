// Play mode: the level runs in the page (WebAssembly engine), from the screen
// in view, saved or not. The big button in the corner switches back and forth.

import { $ } from './dom.js';
import { state, SCREEN_PX_W } from './state.js';
import { testPatch } from './backend.js';
import { Player } from './player.js';
import { normalizeSurprises } from './level-panel.js';
import { toast } from './toast.js';
import { setTip } from './tooltip.js';
import { beginChallenge, watchChallenge, endChallenge, stopChallenge, seconds } from './challenge.js';
import { levelHash, countClear } from './online.js';
import { persistDoc } from './storage.js';
import { mainModel } from './bonus-zone.js';
import { t, tError } from './i18n.js';

// Engine states -> status texts (translated when shown).
const STATUS = { starting: 'démarrage…', playing: 'en jeu', stopped: 'arrêté', cleared: 'niveau terminé !' };
let player = null;

function setPlaying(on) {
  state.playing = on;
  document.body.classList.toggle('playing', on);
  $('playView').hidden = !on;
  $('playLabel').textContent = on ? t('Éditer') : t('Jouer');
  $('playIconPlay').hidden = on;
  $('playIconEdit').hidden = !on;
  if (on) setTip($('playBtn'), t('Éditer'), t('Arrête la partie et reviens à l\'édition (Échap)'));
  else setTip($('playBtn'), t('Jouer'), t('Teste ton niveau tout de suite, à partir de l\'écran affiché (touche Espace)'));
}

// startColumn: null = level start.
export async function playLevel(startColumn = null) {
  if (!player) {
    player = new Player($('game'), (st) => {
      $('playStatus').textContent = STATUS[st] ? t(STATUS[st]) : st;
      if (st === 'playing') watchChallenge(player, failed);
      // A level of the Maker ends with itself: back to editing.
      if (st === 'cleared') {
        const { counts, time } = endChallenge();
        // Cleared from the start, conditions kept: the level can be shared.
        if (counts) {
          state.doc.cleared = { hash: levelHash(mainModel(), state.doc.base), time };
          if (!state.dirty) persistDoc().catch(() => {});
          if (state.doc.online) countClear(state.doc.online, time);
        }
        toast(counts ? t('Bravo, niveau réussi en {time} s !', { time: seconds(time) }) : t('Bravo, niveau terminé !'));
        setTimeout(backToEdit, 400);
      }
    });
  }
  normalizeSurprises();
  setPlaying(true);
  beginChallenge(startColumn === null);
  $('playStatus').textContent = t('préparation du niveau…');
  $('playFrom').textContent = startColumn ? t('depuis l\'écran {n}', { n: startColumn + 1 }) : t('depuis le début');
  const error = (err) => { $('playStatus').textContent = t('erreur : {message}', { message: tError(err.message) }); };
  try {
    const patch = testPatch(state.doc.base, mainModel(), startColumn);
    player.play(patch, state.level).catch(error);
  } catch (err) {
    error(err);
  }
}

// Screen to start from: the one at the left edge of the view (simple
// horizontal levels only), so the level start as long as it is in view.
function startColumn() {
  if (state.model.kind !== 'horizontal' || state.model.inZone) return null; // the zone: from the level start
  const wrap = $('mapWrap');
  const left = Math.max(0, wrap.scrollLeft - $('map').offsetLeft) / state.zoom;
  const col = Math.min(state.model.columns - 1, Math.round(left / SCREEN_PX_W));
  return col > 0 ? col : null;
}

// For automated tests (maker/tests/).
window.makerPlayer = () => player;

// playHooks.done: called once when the game stops (the menu goes back to itself).
export const playHooks = { done: null };

// A clear condition broke (challenge.js): the try is over.
async function failed(reason) {
  if (player) await player.stop();
  $('playStatus').textContent = t('raté');
  toast(t('{reason} · « Depuis le début » pour réessayer', { reason }));
}

export async function backToEdit() {
  stopChallenge();
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
