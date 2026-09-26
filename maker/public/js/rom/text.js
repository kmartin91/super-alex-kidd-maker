// Text boxes of the game (engine/src/game/states/textbox.c): decoding a
// message into its lines, and encoding new lines in the same layout.
//
// A message (bank 7, pointed to by the bank 0 table at $7F49) is a list of
// segments "count, flags, characters": the characters are typed one by one
// while a cursor moves as the flags say (bit 5 right, bits 6+5 left, bit 7
// down, bits 7+6 up); bit 7 of the count repeats one character. The game's
// boxes draw their frame first, then the lines in a zigzag, one blank row
// between two lines. Characters are the name-table tiles ASCII + $90.

const TEXT_POINTERS = 0x7F49;
const TEXT_BANK = 7;
const OPPONENT_SETTINGS = 0x764A; // 16 bytes per janken opponent, the message index at +14
const BLANK = 0xB0;
const RIGHT = 0x20, LEFT = 0x60, DOWN = 0x80, UP = 0xC0;

// What the font can show (the rest is dropped or simplified).
export const TEXT_CHARS = ' !"\',-.?0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ';
export const LINE_WIDTH = 26;

const romOffset = (addr) => TEXT_BANK * 0x4000 + addr - 0x8000;

function messageAddress(rom, index) {
  const t = TEXT_POINTERS + 2 * (index - 1);
  return rom[t] | (rom[t + 1] << 8);
}

// Message index of janken opponent `data` (0/1 Janken, 2/3 Gooseka, 4/5
// Chokkinna, 6/7 Parplin).
export function opponentMessage(rom, data) {
  return rom[OPPONENT_SETTINGS + 16 * (data >> 1) + 14];
}

// Walks a message as the game prints it: {grid: Map "x,y" -> tile, size}.
function typeMessage(rom, index) {
  let o = romOffset(messageAddress(rom, index));
  const start = o;
  const load = (q) => { o = q + 2; return [rom[q], rom[q + 1]]; };
  let [count, flags] = load(o);
  let x = 0, y = 0, print = false;
  const grid = new Map();
  for (let guard = 0; guard < 4000; guard++) {
    // The game prints at a frame's start what the previous frame moved to.
    if (print) {
      grid.set(`${x},${y}`, rom[o]);
      if (!(count & 0x80)) o++;
    }
    print = true;
    if (count & 0x80) {
      count = (((count & 0x7F) - 1) & 0x7F) | 0x80;
      if ((count & 0x7F) === 0) [count, flags] = load(o + 1);
    } else {
      count = (count - 1) & 0xFF;
      if (count === 0) [count, flags] = load(o);
    }
    const move = flags & 0xE0;
    if (!move) return { grid, size: o - start };
    const step = move & 0x40 ? -1 : 1;
    if (move & 0x80) y += step; else x += step;
  }
  throw new Error(`message ${index}: never ends`);
}

// Lines of text of a message (trailing spaces removed) and its size in bytes.
export function decodeMessage(rom, index) {
  const { grid, size } = typeMessage(rom, index);
  const ys = [...grid.keys()].map((k) => Number(k.split(',')[1]));
  const xs = [...grid.keys()].map((k) => Number(k.split(',')[0]));
  const lines = [];
  // Text rows: every other row inside the frame.
  for (let y = Math.min(...ys) + 1; y < Math.max(...ys); y += 2) {
    let line = '';
    for (let x = Math.min(...xs) + 1; x < Math.max(...xs); x++) {
      const t = grid.get(`${x},${y}`);
      line += t >= 0xB0 && t <= 0xEA ? String.fromCharCode(t - 0x90) : ' ';
    }
    lines.push(line.trimEnd());
  }
  return { lines, size };
}

// Upper case, no accents, only what the font has.
export function cleanText(text) {
  return text.normalize('NFD').replace(/[̀-ͯ]/g, '').toUpperCase()
    .replace(/[«»“”]/g, '"').replace(/[’‘`]/g, '\'').replace(/[…]/g, '...').replace(/[—–_]/g, '-')
    .replace(/\s*[:;]/g, ',').replace(/ +([!?])/g, '$1').replace(/[^\n !"',\-.?0-9A-Z]/g, '');
}

// Words wrapped into lines of at most `width` characters.
export function wrapText(text, width = LINE_WIDTH) {
  const lines = [];
  for (const para of cleanText(text).split('\n')) {
    let line = '';
    for (let word of para.split(/ +/).filter(Boolean)) {
      while (word.length > width) {
        if (line) { lines.push(line); line = ''; }
        lines.push(word.slice(0, width));
        word = word.slice(width);
      }
      if (!line) line = word;
      else if (line.length + 1 + word.length <= width) line += ' ' + word;
      else { lines.push(line); line = word; }
    }
    lines.push(line);
  }
  while (lines.length > 1 && !lines[lines.length - 1]) lines.pop();
  return lines;
}

// Bytes of a message showing `lines`, laid out like the game's boxes.
export function encodeMessage(lines) {
  const w = Math.max(3, ...lines.map((l) => l.length));
  const n = lines.length;
  const tiles = (s) => [...s.padEnd(w)].map((c) => c.charCodeAt(0) + 0x90);
  const out = [];
  const repeat = (count, flags) => out.push(0x80 | count, flags, BLANK);
  // The frame: along the top, down the right, back along the bottom, up the left.
  repeat(w + 3, RIGHT);
  repeat(2 * n, DOWN);
  repeat(w + 1, LEFT);
  repeat(2 * n - 1, UP);
  // The lines in a zigzag: the blank row in between is typed going back left.
  lines.forEach((line, i) => {
    const t = tiles(line);
    if (i === 0) {
      out.push(w, RIGHT, ...t);
    } else {
      out.push(1, DOWN, BLANK);
      repeat(w - 1, LEFT);
      out.push(1, DOWN, t[0], w - 1, RIGHT, ...t.slice(1));
    }
  });
  out.push(0, 0);
  return Uint8Array.from(out);
}

// Most lines of `width` that fit in `size` bytes.
export function maxLines(size, width = LINE_WIDTH) {
  return Math.floor((size - 6) / (width + 10));
}

// [ROM offset, bytes] rewriting message `index` with `lines`, in place.
export function messagePatch(rom, index, lines) {
  const { size } = decodeMessage(rom, index);
  const bytes = encodeMessage(lines);
  if (bytes.length > size) throw new Error('le texte est trop long pour cette boîte');
  return [romOffset(messageAddress(rom, index)), bytes];
}
