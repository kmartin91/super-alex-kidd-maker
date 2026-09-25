// In-page game player: runs the C engine compiled to WebAssembly
// (engine/src/platform/main_web.c, built with `make -C port web`).

const JOY_UP = 0x01, JOY_DOWN = 0x02, JOY_LEFT = 0x04, JOY_RIGHT = 0x08, JOY_BTN1 = 0x10, JOY_BTN2 = 0x20;
const W = 256, H = 192, RATE = 44100, FRAME_MS = 1000 / 60;

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = resolve;
    s.onerror = () => reject(new Error('cannot load ' + src));
    document.head.appendChild(s);
  });
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
    this.next = 0;
    this.audio = null;
    this.audioTime = 0;
    this.onKey = this.onKey.bind(this);
  }

  async init() {
    if (this.engine) return;
    await loadScript('engine/alexkidd.js');
    this.engine = await window.AlexKiddEngine({
      locateFile: (f) => 'engine/' + f,
      present: (pix, snd, n) => this.present(pix, snd, n),
      waitFrame: () => this.waitFrame(),
      onStatus: (s) => this.onStatus && this.onStatus(s),
      print: (t) => console.log('[engine]', t),
      printErr: (t) => console.warn('[engine]', t),
    });
    const rom = new Uint8Array(await (await fetch('/api/rom')).arrayBuffer());
    const p = this.engine._malloc(rom.length);
    this.engine.HEAPU8.set(rom, p);
    this.engine._web_set_rom(p, rom.length);
    this.engine._free(p);
  }

  // Starts the level (optionally with a mod patch); resolves when the run ends.
  async play(patch, level) {
    await this.stop();
    await this.init();
    if (!this.audio) this.audio = new AudioContext({ sampleRate: RATE });
    if (this.audio.state === 'suspended') await this.audio.resume();
    this.audioTime = 0;
    this.keys.clear();
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

  async stop() {
    if (!this.running) return;
    this.engine._web_stop();
    try { await this.running; } catch (e) { /* ignore */ }
    this.running = null;
  }

  present(pixPtr, sndPtr, count) {
    const src = this.engine.HEAPU32.subarray(pixPtr >> 2, (pixPtr >> 2) + W * H);
    const d = this.image.data;
    for (let i = 0, j = 0; i < W * H; i++, j += 4) {
      const p = src[i];
      d[j] = (p >> 16) & 255; d[j + 1] = (p >> 8) & 255; d[j + 2] = p & 255; d[j + 3] = 255;
    }
    this.ctx.putImageData(this.image, 0, 0);
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

  // Resolves at the next 60 Hz tick (requestAnimationFrame-paced).
  waitFrame() {
    return new Promise((resolve) => {
      const tick = () => {
        const now = performance.now();
        if (now + 1 >= this.next) {
          this.next = Math.max(this.next + FRAME_MS, now - 3 * FRAME_MS);
          resolve();
        } else {
          requestAnimationFrame(tick);
        }
      };
      requestAnimationFrame(tick);
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
    this.engine._web_set_joy(bits);
  }
}
