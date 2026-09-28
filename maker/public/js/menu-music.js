// The music of the menu and its title screen: the song of level 1, played by
// the game's own sound engine from the player's ROM (web_music_* in
// engine/src/platform/main_web.c), a frame at a time like in the game.
// Off with the button in the menu's corner (kept in the preferences).

import { loadEngine, refreshRate } from './player.js';
import { pref, setPref } from './prefs.js';

// AHEAD: seconds of sound queued; more when the window is out of sight
// (its timers may be slowed down).
const RATE = 44100, AHEAD = 0.25, AHEAD_HIDDEN = 1.5, VOLUME = 0.8, LEVEL = 1;
let ctx = null, out = null, timer = 0, playing = false, session = 0;

export const menuMusicOn = () => pref('menuMusic', true);

export function setMenuMusic(on) {
  setPref('menuMusic', on);
  if (on) startMenuMusic(); else stopMenuMusic();
}

// Browsers keep sound off until a key, a click (or a pad button) is pressed.
export function wakeMenuMusic() {
  if (ctx && ctx.state === 'suspended') ctx.resume().catch(() => {});
}
window.addEventListener('pointerdown', wakeMenuMusic, true);
window.addEventListener('keydown', wakeMenuMusic, true);

export async function startMenuMusic() {
  if (playing || !menuMusicOn()) return;
  playing = true;
  const run = ++session;
  let engine;
  try { engine = await loadEngine(); } catch { playing = false; return; } // no ROM yet
  if (run !== session) return; // stopped (and maybe started again) meanwhile
  const FRAME = engine._web_set_refresh(refreshRate()); // samples per frame (50 or 60 Hz)
  if (!engine._web_music_start(LEVEL)) { playing = false; return; }
  if (!ctx) ctx = new AudioContext({ sampleRate: RATE });
  wakeMenuMusic();
  // A gain per start: what a stop left scheduled fades out on its own.
  const gain = ctx.createGain();
  gain.connect(ctx.destination);
  gain.gain.setValueAtTime(0, ctx.currentTime);
  gain.gain.linearRampToValueAtTime(VOLUME, ctx.currentTime + 0.8);
  out = gain;
  let time = 0;
  const fill = () => {
    if (run !== session || ctx.state !== 'running') return;
    const now = ctx.currentTime;
    if (time < now) time = now + 0.05;
    const ahead = document.hidden ? AHEAD_HIDDEN : AHEAD;
    while (time < now + ahead) {
      const p = engine._web_music_frame();
      if (!p) { stopMenuMusic(); return; } // a game started
      const pcm = engine.HEAP16.subarray(p >> 1, (p >> 1) + FRAME);
      const buf = ctx.createBuffer(1, FRAME, RATE);
      const ch = buf.getChannelData(0);
      for (let i = 0; i < FRAME; i++) ch[i] = pcm[i] / 32768;
      const node = ctx.createBufferSource();
      node.buffer = buf;
      node.connect(gain);
      node.start(time);
      time += FRAME / RATE;
    }
  };
  timer = setInterval(fill, 40);
  fill();
}

export function stopMenuMusic() {
  playing = false;
  session++;
  clearInterval(timer);
  if (ctx && out) {
    const now = ctx.currentTime;
    out.gain.cancelScheduledValues(now);
    out.gain.setValueAtTime(out.gain.value, now);
    out.gain.linearRampToValueAtTime(0, now + 0.15);
    out = null;
  }
}
