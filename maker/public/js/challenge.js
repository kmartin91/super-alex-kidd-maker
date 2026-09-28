// A level's time limit (model.clear = { time: seconds, 0 for none }), and the
// try's time and lives lost, shown in the play bar. The time counts while
// Alex is playing (not during text boxes, pauses or the level intro). There
// is no checkpoint: a lost life starts the level over (the engine does it)
// with the time back at 0; the lives lost are counted for the whole run and
// go with the result (the level's record online).

import { $ } from './dom.js';
import { state } from './state.js';
import { openModal, closeModal, h } from './modal.js';
import { pushUndo } from './history.js';
import { render } from './render.js';
import { t, lang } from './i18n.js';
import { refreshRate } from './player.js';

export const TIME_CHOICES = [0, 30, 60, 90, 120, 180, 300];
const LIFE_LOST = 6, JANKEN = 9, GAMEPLAY = 0x0A;

// (Older levels may also say noDeath: every death restarts the level now.)
export const clearOf = (model = state.model) => ({ time: (model.clear && model.clear.time) || 0 });

// "1 vie perdue", "2 vies perdues".
export const livesLost = (n) => (n === 1 ? t('1 vie perdue') : t('{n} vies perdues', { n }));

const duration = (s) => (s % 60 ? (s >= 60 ? `${Math.floor(s / 60)} min ${s % 60} s` : `${s} s`) : `${s / 60} min`);
// Seconds with one decimal, a comma in French.
export const seconds = (ms) => {
  const s = (ms / 1000).toFixed(1);
  return lang === 'fr' ? s.replace('.', ',') : s;
};

export function clearText(c = clearOf()) {
  const parts = [];
  return c.time ? duration(c.time) : t('aucun');
}

// ------------------------------------------------------------ while playing
let run = null;

// A new try; `counts`: played from the start (only then does it count).
// again: the same run goes on (the time ran out): the lives lost are kept.
export function beginChallenge(counts, again = false) {
  const deaths = again && run ? run.deaths : 0;
  stopChallenge();
  run = { counts, elapsed: 0, last: 0, frame: 0, failed: null, done: false, deaths, state: 0 };
  $('playTimer').textContent = '';
}

// The game is running: watches time and deaths. `fail(reason)` when the time
// runs out (the level then starts over).
export function watchChallenge(player, fail) {
  if (!run) return;
  const c = clearOf();
  run.last = performance.now();
  // Game time: at 50 Hz the game runs a sixth slower, its seconds are longer
  // (time limits and records stay the same at 50 and 60 Hz).
  const speed = refreshRate() / 60;
  const tick = (now) => {
    if (!run || run.done) return;
    const r = player.ram();
    const st = r ? r[0x1F] & 0x0F : 0;
    // A life lost: the engine starts the level over, the time with it.
    if (st === LIFE_LOST && run.state !== LIFE_LOST) { run.deaths++; run.elapsed = 0; }
    run.state = st;
    if (st === GAMEPLAY || st === JANKEN) run.elapsed += (now - run.last) * speed;
    run.last = now;
    if (c.time && run.elapsed >= c.time * 1000) { run.failed = t('Temps écoulé !'); run.deaths++; }
    $('playTimer').textContent = (c.time
      ? `⏱ ${Math.max(0, Math.ceil(c.time - run.elapsed / 1000))} s`
      : `⏱ ${seconds(run.elapsed)} s`) + (run.deaths ? ` · ${livesLost(run.deaths)}` : '');
    if (run.failed) { run.done = true; fail(run.failed); return; }
    run.frame = requestAnimationFrame(tick);
  };
  run.frame = requestAnimationFrame(tick);
}

// The level end was reached: { counts, time, deaths } (time in ms).
export function endChallenge() {
  if (!run) return { counts: false, time: 0, deaths: 0 };
  run.done = true;
  cancelAnimationFrame(run.frame);
  return { counts: run.counts && !run.failed, time: Math.round(run.elapsed), deaths: run.deaths };
}

export function stopChallenge() {
  if (run) { run.done = true; cancelAnimationFrame(run.frame); }
  run = null;
}

// ------------------------------------------------------------ editor side
export function openChallengeSheet() {
  const c = { ...clearOf() };
  const times = h('div.cards.small', {});
  const draw = () => {
    times.replaceChildren(...TIME_CHOICES.map((time) => h('button.card' + (time === c.time ? '.active' : ''), {
      onclick: () => { c.time = time; draw(); },
    }, h('span.card-name', { textContent: time ? duration(time) : t('Pas de limite') }))));
  };
  draw();
  openModal(t('Défi du niveau'), h('div.challenge', {},
    h('p.hint', { textContent: t('Une condition pour réussir le niveau. Elle compte quand on joue depuis le début.') }),
    h('h3.sheet-sub', { textContent: t('Temps limite') }), times,
    h('div.dialog-actions', {},
      h('button.key.plain', { textContent: t('Annuler'), onclick: () => closeModal() }),
      h('button.key.go', { textContent: 'OK', onclick: () => {
        pushUndo();
        if (c.time) state.model.clear = c; else delete state.model.clear;
        closeModal();
        render();
      } }))));
}
