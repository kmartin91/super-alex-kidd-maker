// Captures from the running game (level graphics, enemy pictures), computed
// in a Web Worker and cached in this browser.

import { dbGet, dbSet } from './db.js';

let worker = null, nextId = 0;
const pending = new Map();

function call(cmd, args = {}) {
  if (!worker) {
    worker = new Worker(new URL('./capture-worker.js', import.meta.url));
    worker.onmessage = (ev) => {
      const { id, result, error } = ev.data;
      const p = pending.get(id);
      pending.delete(id);
      if (error) p.reject(new Error(error)); else p.resolve(result);
    };
  }
  const id = ++nextId;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    worker.postMessage({ id, cmd, ...args });
  });
}

let romKey = null;

// Must be called (again) whenever the ROM changes; `key` names its cache.
export async function useRom(rom, key) {
  romKey = key;
  await call('rom', { rom });
}

// { vram: Uint8Array(16 KB), cram: [32] } of a level once playable.
export async function levelVideo(level) {
  const key = `video:${romKey}:${level}`;
  let v = await dbGet(key);
  if (!v) {
    v = await call('video', { level });
    if (!v) throw new Error(`le niveau ${level} ne démarre pas`);
    await dbSet(key, v);
  }
  return v;
}

// { type: { w, h, dx, dy, rgba: Uint8Array } } for the "type:level" specs;
// progress(done, total) is called along the way.
export async function entityIcons(specs, progress = () => {}) {
  const key = `icons3:${romKey}`; // 3: traced captures (engine/tools/capture.c), janken opponents
  let icons = await dbGet(key);
  if (icons) return icons;
  icons = {};
  let done = 0;
  for (const spec of specs) {
    const [type, level] = spec.split(':').map(Number);
    const icon = await call('icon', { type, level });
    if (icon) icons[type] = icon;
    progress(++done, specs.length);
  }
  await dbSet(key, icons);
  return icons;
}
