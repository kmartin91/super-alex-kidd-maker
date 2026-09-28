// In-page game player: runs the C engine compiled to WebAssembly
// (engine/src/platform/main_web.c, built with `make -C engine web`).
// Keyboard and game controllers (js/gamepad.js).

import { romBytes } from './backend.js';
import { readPads } from './gamepad.js';
import { pref } from './prefs.js';

const JOY_UP = 0x01, JOY_DOWN = 0x02, JOY_LEFT = 0x04, JOY_RIGHT = 0x08, JOY_BTN1 = 0x10, JOY_BTN2 = 0x20;
const W = 256, H = 192, RATE = 44100;

// Frames a second: 60 as in Japan and the USA, or 50 as on a European
// Master System (Paramètres), where the game and its music run a sixth slower.
export const refreshRate = () => (pref('hz50', false) ? 50 : 60);
const HIDDEN_LEFT = 8;

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error('cannot load ' + src));
    document.head.appendChild(s);
  });
}

// One engine for the page, loaded once with the ROM: the player and the
// menu's music (js/menu-music.js) share it.
let enginePromise = null;
let activePlayer = null;
export function loadEngine() {
  if (!enginePromise) {
    enginePromise = (async () => {
      await loadScript('engine/alexkidd.js');
      const engine = await window.AlexKiddEngine({
        locateFile: (f) => 'engine/' + f,
        present: (pix, snd, n) => activePlayer && activePlayer.present(pix, snd, n),
        waitFrame: () => (activePlayer ? activePlayer.waitFrame() : Promise.resolve()),
        onStatus: (s) => activePlayer && activePlayer.onStatus && activePlayer.onStatus(s),
        print: (t) => console.log('[engine]', t),
        printErr: (t) => console.warn('[engine]', t),
      });
      const rom = romBytes();
      const p = engine._malloc(rom.length);
      engine.HEAPU8.set(rom, p);
      engine._web_set_rom(p, rom.length);
      engine._free(p);
      return engine;
    })();
    enginePromise.catch(() => { enginePromise = null; });
  }
  return enginePromise;
}

export class Player {
  constructor(canvas, onStatus) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.image = this.ctx.createImageData(W, H);
    this.onStatus = onStatus;
    this.engine = null;
    this.running = null;   // promise of the current run
    this.keys = new Set();
    this.keyBits = 0;
    this.padBits = 0;
    this.padStart = true;
    this.frameMs = 1000 / 60;
    this.next = 0;
    this.audio = null;
    this.audioTime = 0;
    this.onKey = this.onKey.bind(this);
  }

  async init() {
    activePlayer = this;
    if (!this.engine) this.engine = await loadEngine();
  }

  // Starts the level (optionally with a mod patch); resolves when the run ends.
  async play(patch, level) {
    await this.stop();
    await this.init();
    const hz = refreshRate();
    this.engine._web_set_refresh(hz);
    this.frameMs = 1000 / hz;
    if (!this.audio) this.audio = new AudioContext({ sampleRate: RATE });
    if (this.audio.state === 'suspended') await this.audio.resume();
    this.audioTime = 0;
    this.keys.clear();
    this.keyBits = 0;
    this.padBits = 0;
    this.padStart = true; // a Start still held from the menu does not pause
    this.engine._web_set_joy(0);
    window.addEventListener('keydown', this.onKey, true);
    window.addEventListener('keyup', this.onKey, true);
    let ptr = 0, len = 0;
    if (patch && patch.length) {
      len = patch.length;
      ptr = this.engine._malloc(len);
      this.engine.HEAPU8.set(patch, ptr);
    }
    this.next = performance.now();
    this.running = this.engine.ccall('web_play', 'number', ['number', 'number', 'number'], [ptr, len, level],
      { async: true }).finally(() => {
      if (ptr) this.engine._free(ptr);
      window.removeEventListener('keydown', this.onKey, true);
      window.removeEventListener('keyup', this.onKey, true);
    });
    return this.running;
  }

  // Copy of the game's RAM ($C000-$DFFF), for tests.
  ram() {
    const p = this.engine._web_ram();
    return this.engine.HEAPU8.slice(p, p + 0x3000); // + maker mode extra RAM (engine/src/rt/maker.h)
  }

  async stop() {
    if (!this.running) return;
    this.engine._web_stop();
    try { await this.running; } catch (e) { /* ignore */ }
    this.running = null;
  }

  present(pixPtr, sndPtr, count) {
    this.pollPad();
    const src = this.engine.HEAPU32.subarray(pixPtr >> 2, (pixPtr >> 2) + W * H);
    const d = this.image.data;
    for (let i = 0, j = 0; i < W * H; i++, j += 4) {
      const p = src[i];
      d[j] = (p >> 16) & 255; d[j + 1] = (p >> 8) & 255; d[j + 2] = p & 255; d[j + 3] = 255;
    }
    // The console masks the leftmost 8-pixel column with a plain colour: it
    // is left out of the picture (the canvas is 248 pixels wide).
    this.ctx.putImageData(this.image, -HIDDEN_LEFT, 0, HIDDEN_LEFT, 0, W - HIDDEN_LEFT, H);
    if (this.audio) {
      const pcm = this.engine.HEAP16.subarray(sndPtr >> 1, (sndPtr >> 1) + count);
      const buf = this.audio.createBuffer(1, count, RATE);
      const ch = buf.getChannelData(0);
      for (let i = 0; i < count; i++) ch[i] = pcm[i] / 32768;
      const now = this.audio.currentTime;
      if (this.audioTime < now || this.audioTime > now + 0.25) this.audioTime = now + 0.05;
      const node = this.audio.createBufferSource();
      node.buffer = buf;
      node.connect(this.audio.destination);
      node.start(this.audioTime);
      this.audioTime += count / RATE;
    }
  }

  // Resolves at the next frame tick (requestAnimationFrame-paced). A window
  // out of sight gets no animation frames: a timer keeps the game (and its
  // sound) going then.
  waitFrame() {
    return new Promise((resolve) => {
      const later = (f) => (document.hidden ? setTimeout(f, 4) : requestAnimationFrame(f));
      const tick = () => {
        const now = performance.now();
        if (now + 1 >= this.next) {
          this.next = Math.max(this.next + this.frameMs, now - 3 * this.frameMs);
          resolve();
        } else {
          later(tick);
        }
      };
      later(tick);
    });
  }

  onKey(ev) {
    const k = ev.key.length === 1 ? ev.key.toLowerCase() : ev.key;
    const map = {
      ArrowUp: JOY_UP, ArrowDown: JOY_DOWN, ArrowLeft: JOY_LEFT, ArrowRight: JOY_RIGHT,
      ' ': JOY_BTN1, x: JOY_BTN1, k: JOY_BTN1,       // jump
      z: JOY_BTN2, w: JOY_BTN2, j: JOY_BTN2,         // punch / use item
    };
    if (ev.type === 'keydown' && (k === 'Enter' || k === 'p') && !ev.repeat) {
      this.engine._web_press_pause();
      ev.preventDefault();
      return;
    }
    if (!(k in map)) return;
    ev.preventDefault();
    ev.stopPropagation();
    if (ev.type === 'keydown') this.keys.add(k); else this.keys.delete(k);
    let bits = 0;
    for (const key of this.keys) bits |= map[key];
    this.keyBits = bits;
    this.engine._web_set_joy(this.keyBits | this.padBits);
  }

  // Game controllers, once per frame: the directions and buttons add to the
  // keyboard's; Start pauses.
  pollPad() {
    const p = readPads();
    const bits = (p.up ? JOY_UP : 0) | (p.down ? JOY_DOWN : 0) | (p.left ? JOY_LEFT : 0) | (p.right ? JOY_RIGHT : 0)
      | (p.jump ? JOY_BTN1 : 0) | (p.punch ? JOY_BTN2 : 0);
    if (p.start && !this.padStart) this.engine._web_press_pause();
    this.padStart = p.start;
    if (bits !== this.padBits) {
      this.padBits = bits;
      this.engine._web_set_joy(this.keyBits | this.padBits);
    }
  }
}
