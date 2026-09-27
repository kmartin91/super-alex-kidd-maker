// The intro: the credits on black (the title screen comes next, in the
// menu). Any key, click or gamepad button skips it. No console or publisher
// logo is shown: only the credit line of the original game.

import { h } from './modal.js';
import { t } from './i18n.js';

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Resolves after `ms`, or earlier on a key, a click or a gamepad button, or
// when `until` resolves.
function waitOrSkip(ms, until = null) {
  return new Promise((resolve) => {
    let done = false, frame = 0;
    const finish = () => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      cancelAnimationFrame(frame);
      window.removeEventListener('keydown', finish, true);
      window.removeEventListener('pointerdown', finish, true);
      resolve();
    };
    const pressed = () => [...(navigator.getGamepads ? navigator.getGamepads() : [])]
      .some((g) => g && g.buttons.some((b) => b.pressed));
    const held = pressed(); // a button still held from the previous screen does not count
    const poll = () => {
      if (!held && pressed()) finish(); else frame = requestAnimationFrame(poll);
    };
    const timer = setTimeout(finish, ms);
    window.addEventListener('keydown', finish, true);
    window.addEventListener('pointerdown', finish, true);
    frame = requestAnimationFrame(poll);
    if (until) until.then(finish);
  });
}

// The credits, on black.
async function creditsScreen(root) {
  const screen = h('div.intro-screen.credits-screen', {},
    h('div.credits', {},
      h('p.credits-game', {}, h('b', { textContent: 'Alex Kidd in Miracle World' }), h('span', { textContent: '© SEGA 1986' })),
      h('p', { textContent: t('Super Alex Kidd Maker est un jeu de fan, non officiel et gratuit, basé sur Alex Kidd in Miracle World ' +
        '(Master System). Le jeu original et ses personnages appartiennent à SEGA.') }),
      h('p.credits-dev', {}, h('span', { textContent: t('Développement') }), h('b', { textContent: 'Studio KMA' }))));
  root.appendChild(screen);
  await sleep(50);
  screen.classList.add('shown');
  await waitOrSkip(5000);
  screen.classList.remove('shown');
  await sleep(350);
  screen.remove();
}

export async function playIntro() {
  const root = h('div', { id: 'intro' });
  document.body.appendChild(root);
  await creditsScreen(root);
  root.remove();
}
