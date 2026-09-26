// The intro: a SEGA screen as on the old consoles (the title screen comes
// next, in the menu). Any key, click or gamepad button skips it.

import { h } from './modal.js';

// SEGA letters drawn by hand (not taken from the ROM), 10x13 each.
const SEGA = [
  ['..########', '.#########', '##########', '###.......', '###.......', '##########', '##########',
    '##########', '.......###', '.......###', '##########', '#########.', '########..'],
  ['..########', '.#########', '##########', '###.......', '###.......', '##########', '##########',
    '##########', '###.......', '###.......', '##########', '.#########', '..########'],
  ['..########', '.#########', '##########', '###.......', '###.......', '###..#####', '###..#####',
    '###..#####', '###....###', '###....###', '##########', '.#########', '..########'],
  ['..######..', '.########.', '##########', '###....###', '###....###', '##########', '##########',
    '##########', '###....###', '###....###', '###....###', '###....###', '###....###'],
];
const SEGA_W = 4 * 10 + 3 * 2 + 2, SEGA_H = 13 + 2;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Resolves after `ms`, or earlier on a key, a click or a gamepad button.
function waitOrSkip(ms) {
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
  });
}

// Letter pixels of the SEGA logo (1 = letter).
function segaPixels() {
  const px = new Uint8Array(SEGA_W * SEGA_H);
  SEGA.forEach((rows, n) => rows.forEach((row, y) => [...row].forEach((ch, x) => {
    if (ch === '#') px[(y + 1) * SEGA_W + 1 + n * 12 + x] = 1;
  })));
  return px;
}

// The SEGA screen: the logo lights up, a shine runs across it.
async function segaScreen(root) {
  const canvas = h('canvas.intro-sega', { width: SEGA_W, height: SEGA_H });
  const screen = h('div.intro-screen.sega', {}, canvas);
  root.appendChild(screen);
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(SEGA_W, SEGA_H);
  const px = segaPixels();
  const start = performance.now();
  let frame = 0;
  const draw = (now) => {
    const t = (now - start) / 1000;
    const shine = (t - 0.5) * 90 - 10; // x of the shine, crossing between 0.5 s and 1.3 s
    for (let i = 0; i < px.length; i++) {
      const x = i % SEGA_W, y = Math.floor(i / SEGA_W);
      let c = [0, 0, 0, 0];
      if (px[i]) {
        const band = y % 5 === 2; // the logo's light lines
        c = band ? [120, 170, 255, 255] : [23, 72, 214, 255];
        if (Math.abs(x - shine - (SEGA_H - y) * 0.6) < 2.5) c = [235, 242, 255, 255];
      }
      img.data.set(c, i * 4);
    }
    ctx.putImageData(img, 0, 0);
    frame = requestAnimationFrame(draw);
  };
  frame = requestAnimationFrame(draw);
  await sleep(50);
  screen.classList.add('shown');
  await waitOrSkip(2300);
  screen.classList.remove('shown');
  await sleep(350);
  cancelAnimationFrame(frame);
  screen.remove();
}

export async function playIntro() {
  const root = h('div', { id: 'intro' });
  document.body.appendChild(root);
  await segaScreen(root);
  root.remove();
}
