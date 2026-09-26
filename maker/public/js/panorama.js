// The level scrolling behind the game menu: a level of the game on foot,
// drawn from the ROM like in the editor, with its enemies once their
// pictures are known.

import { state, SCREEN_W, BLOCK, SCREEN_PX_W, SCREEN_PX_H } from './state.js';
import { templateLevel } from './backend.js';
import { blockImages } from './graphics.js';

const LEVELS = [2, 4, 6, 7, 8, 10, 12, 14, 15]; // horizontal levels of the game
const SPEED = 24; // level pixels per second

let level = null;

async function strip() {
  if (!level) {
    const n = LEVELS[Math.floor(Math.random() * LEVELS.length)];
    const { model, video } = await templateLevel(n);
    level = { model, blocks: blockImages(video, model.metatiles) };
  }
  const { model, blocks } = level;
  const row = model.grid[0].filter(Boolean);
  const c = document.createElement('canvas');
  c.width = row.length * SCREEN_PX_W;
  c.height = SCREEN_PX_H;
  const ctx = c.getContext('2d');
  row.forEach((cell, i) => {
    const b = model.screens[cell.screen].blocks;
    for (let k = 0; k < b.length; k++) {
      ctx.drawImage(blocks[b[k]], i * SCREEN_PX_W + (k % SCREEN_W) * BLOCK, Math.floor(k / SCREEN_W) * BLOCK);
    }
    for (const e of model.entities[cell.entities] || []) {
      const icon = state.icons[e.type];
      if (icon) ctx.drawImage(icon.canvas, i * SCREEN_PX_W + e.x + icon.dx, e.y + icon.dy);
    }
  });
  return c;
}

// Scrolls a level in `canvas` (drawn at the console's resolution, scaled up
// by CSS) while it is in the page. Returns redraw(), to call once the enemy
// pictures are loaded.
export function startPanorama(canvas) {
  let image = null, x = 0, last = performance.now();
  const ctx = canvas.getContext('2d');
  const frame = (now) => {
    if (!canvas.isConnected) return;
    const dt = Math.min(0.1, (now - last) / 1000);
    last = now;
    if (image && canvas.offsetParent) {
      const w = Math.ceil(SCREEN_PX_H * canvas.clientWidth / Math.max(1, canvas.clientHeight));
      if (canvas.width !== w || canvas.height !== SCREEN_PX_H) { canvas.width = w; canvas.height = SCREEN_PX_H; }
      x = (x + SPEED * dt) % image.width;
      const sx = Math.floor(x);
      for (let dx = -sx; dx < w; dx += image.width) ctx.drawImage(image, dx, 0);
      canvas.classList.add('ready');
    }
    requestAnimationFrame(frame);
  };
  const redraw = () => strip().then((c) => { image = c; }).catch((err) => console.warn('menu background:', err.message));
  redraw();
  requestAnimationFrame(frame);
  return redraw;
}
