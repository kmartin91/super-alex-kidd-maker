// Web Worker running the captures (engine/tools.js, built from
// engine/src/platform/tools_web.c): each one runs the game for hundreds of
// frames, which would freeze the page on the main thread.
//
// Messages: { id, cmd: 'rom', rom } | { id, cmd: 'video', level } | { id, cmd: 'icon', type, level }
// Replies:  { id, result } or { id, error }

importScripts('../engine/tools.js');

const ready = self.AlexKiddTools({ locateFile: (f) => '../engine/' + f });

function copyIn(tools, bytes) {
  const p = tools._malloc(bytes.length);
  tools.HEAPU8.set(bytes, p);
  return p;
}

const commands = {
  rom(tools, { rom }) {
    const p = copyIn(tools, rom);
    tools._web_tools_set_rom(p, rom.length);
    tools._free(p);
    return true;
  },
  video(tools, { level }) {
    const p = tools._web_level_video(level);
    if (!p) return null;
    return { cram: Array.from(tools.HEAPU8.subarray(p, p + 32)), vram: tools.HEAPU8.slice(p + 32, p + 32 + 0x4000) };
  },
  icon(tools, { type, level }) {
    const p = tools._web_entity_icon(type, level);
    if (!p) return null;
    const [w, h, dx, dy] = tools.HEAP32.subarray(p >> 2, (p >> 2) + 4);
    return { w, h, dx, dy, rgba: tools.HEAPU8.slice(p + 16, p + 16 + w * h * 4) };
  },
};

self.onmessage = async (ev) => {
  const { id, cmd } = ev.data;
  try {
    const tools = await ready;
    self.postMessage({ id, result: commands[cmd](tools, ev.data) });
  } catch (err) {
    self.postMessage({ id, error: String(err && err.message || err) });
  }
};
