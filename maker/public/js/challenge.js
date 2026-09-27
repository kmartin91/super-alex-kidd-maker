// Clear conditions of a level, Mario Maker style (model.clear = { time:
// seconds or 0, noDeath }): checked while the level is played from its
// start. The time counts while Alex is playing (not during text boxes,
// pauses or the level intro) and is shown in the play bar.

import { $ } from './dom.js';
import { state } from './state.js';
import { openModal, closeModal, h } from './modal.js';
import { pushUndo } from './history.js';
import { render } from './render.js';
import { t, lang } from './i18n.js';

export const TIME_CHOICES = [0, 30, 60, 90, 120, 180, 300];
const LIFE_LOST = 6, JANKEN = 9, GAMEPLAY = 0x0A;

export const clearOf = (model = state.model) => model.clear || { time: 0, noDeath: false };

const duration = (s) => (s % 60 ? (s >= 60 ? `${Math.floor(s / 60)} min ${s % 60} s` : `${s} s`) : `${s / 60} min`);
// Seconds with one decimal, a comma in French.
export const seconds = (ms) => {
  const s = (ms / 1000).toFixed(1);
  return lang === 'fr' ? s.replace('.', ',') : s;
};

export function clearText(c = clearOf()) {
  const parts = [];
  if (c.time) parts.push(duration(c.time));
  if (c.noDeath) parts.push(t('sans mourir'));
  return parts.length ? parts.join(' · ') : t('aucun');
}

// ------------------------------------------------------------ while playing
let run = null;

// A new try; `counts`: played from the start (only then does it count).
export function beginChallenge(counts) {
  stopChallenge();
  run = { counts, elapsed: 0, last: 0, frame: 0, failed: null, done: false };
  $('playTimer').textContent = '';
}

// The game is running: watches time and deaths. `fail(reason)` stops it.
export function watchChallenge(player, fail) {
  if (!run) return;
  const c = clearOf();
  run.last = performance.now();
  const tick = (now) => {
    if (!run || run.done) return;
    const r = player.ram();
    const st = r ? r[0x1F] & 0x0F : 0;
    if (st === GAMEPLAY || st === JANKEN) run.elapsed += now - run.last;
    run.last = now;
    if (c.noDeath && st === LIFE_LOST) run.failed = t('Alex est mort : ce niveau se fait sans mourir');
    if (c.time && run.elapsed >= c.time * 1000) run.failed = t('Temps écoulé !');
    $('playTimer').textContent = c.time
      ? `⏱ ${Math.max(0, Math.ceil(c.time - run.elapsed / 1000))} s`
      : `⏱ ${seconds(run.elapsed)} s`;
    if (run.failed) { run.done = true; fail(run.failed); return; }
    run.frame = requestAnimationFrame(tick);
  };
  run.frame = requestAnimationFrame(tick);
}

// The level end was reached: { counts, time } (time in ms).
export function endChallenge() {
  if (!run) return { counts: false, time: 0 };
  run.done = true;
  cancelAnimationFrame(run.frame);
  const result = { counts: run.counts && !run.failed, time: Math.round(run.elapsed) };
  return result;
}

export function stopChallenge() {
  if (run) { run.done = true; cancelAnimationFrame(run.frame); }
  run = null;
}

// ------------------------------------------------------------ editor side
export function openChallengeSheet() {
  let c = { ...clearOf() };
  const times = h('div.cards.small', {});
  const death = h('button.menu-row', {});
  const draw = () => {
    times.replaceChildren(...TIME_CHOICES.map((time) => h('button.card' + (time === c.time ? '.active' : ''), {
      onclick: () => { c.time = time; draw(); },
    }, h('span.card-name', { textContent: time ? duration(time) : t('Pas de limite') }))));
    death.replaceChildren(h('span.menu-row-label', { textContent: t('Sans mourir') }), h('b', { textContent: c.noDeath ? t('oui') : t('non') }));
  };
  death.addEventListener('click', () => { c.noDeath = !c.noDeath; draw(); });
  draw();
  openModal(t('Défi du niveau'), h('div.challenge', {},
    h('p.hint', { textContent: t('Une condition pour réussir le niveau. Elle compte quand on joue depuis le début.') }),
    h('h3.sheet-sub', { textContent: t('Temps limite') }), times,
    h('h3.sheet-sub', { textContent: t('Vies') }), death,
    h('div.dialog-actions', {},
      h('button.key.plain', { textContent: t('Annuler'), onclick: () => closeModal() }),
      h('button.key.go', { textContent: 'OK', onclick: () => {
        pushUndo();
        if (c.time || c.noDeath) state.model.clear = c; else delete state.model.clear;
        closeModal();
        render();
      } }))));
}
