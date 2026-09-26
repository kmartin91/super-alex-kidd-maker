// Level data codec for Alex Kidd in Miracle World (Sega Master System, USA/Europe
// rev 0): a port of maker/tools/levels.py (decode and re-encode only; the VRAM
// emulation, rendering and CLI stay in Python). Every value is read from the
// user's ROM at run time; no game data lives here. The format is documented in
// docs/level-format.md.
//
//   const model = load(romBytes);      // Uint8Array -> JSON-serialisable object
//   const patches = encode(model);     // [[romOffset, Uint8Array], ...]
//
// Function names follow the Python ones (in camelCase) and the model keeps the
// Python keys (snake_case), so levels.py stays the reference.

export const ROM_CRC32 = 0x17A40E29; // USA/Europe rev 0

export const LEVEL_COUNT = 17;
export const SCREEN_W = 16, SCREEN_H = 12; // metatiles per screen
export const SCREEN_CELLS = SCREEN_W * SCREEN_H; // 192 bytes once decompressed

// ---------------------------------------------------------------------------
// Fixed ROM locations (CPU addresses; banks 0/1 are fixed at $0000-$7FFF, slot 2 = $8000)
// ---------------------------------------------------------------------------
export const LEVEL_DESCRIPTOR_TABLE = 0x66CF; // bank 1, 17 x dw, read by loadLevel ($65AA)
export const LEVEL_DESCRIPTOR_SIZE = 12;
export const ENTITY_DESCRIPTOR_TABLE = 0xB505; // bank 2 (slot 2), 17 x dw, read by initGameplayState
export const ENTITY_BANK = 2;
export const SCROLL_FLAGS_UPDATERS = 0x0D0A; // 17 x dw (code pointers)
export const PALETTE_UPDATERS = 0x0D2C; // 17 x dw (code pointers)
export const ENTITY_LOADERS = 0x0D4E; // 17 x dw (code pointers)
export const SHOP_DOORS = 0x0D70; // 17 x (db offset, dw RAM name-table address)
export const STARTING_POSITIONS = 0x0DA3; // 17 x (db x, db y)
export const LEVEL_SONGS = 0x0DC5; // 18 x db (last entry unused by v_level 1..17)
export const SPAWN_STATES = 0x0E1F; // 17 x db
export const QUESTION_BOX_INDEXES = 0x0E30; // 17 x db
export const TILESET_LOADERS = 0x0E7D; // 17 x dw (code pointers), jumped to by loadLevelTiles
export const LEVEL_PALETTES = 0x1112; // 17 x dw -> 32-byte palettes in bank 7
export const PALETTE_BANK = 7;
export const SPRITE_TILES_LOADERS = 0x1142; // 17 x dw (code pointers)
export const TILE_UPDATERS = 0x156D; // 17 x dw (code pointers)
export const MAIN_TILESET_POINTERS = 0x8480; // bank 3, 17 x dw -> compressed tiles in bank 3
export const CAMERA_HANDLERS = 0x3F33; // 17 x dw (code pointers), rst $20 from updateAlex $297A
export const VEHICLE_CRASH_TO_WATER = 0x3904; // 17 x db: 1 = a wrecked vehicle drops Alex into water
export const MAP_ARROW_POSITIONS = 0x1BA7; // 17 x (db x, db y), map screen arrow
export const SHOP_ITEM_TABLES = 0x1F89; // 17 x dw -> bank 6: 3 x (dw name-table dest, dw graphics)
export const SHOP_SOLD_TABLES = 0x1FAB; // 17 x dw -> bank 6: 3 x (db item, dw flag RAM, dw name-table)
export const QUESTION_BOX_ITEMS = 0x0DD7; // 72 x db entity types, indexed by v_questionMarkBoxIndex
export const FLOOR_PUZZLE_SEQUENCE = 0x3DE9; // level-17 floor symbol order, $FF-terminated
export const OCTOPUS_ARM_SETS = 0x70FB; // 2 x dw -> (dw pot address, 8 x (delay, y, x, follower))
export const DEMO_LEVELS = 0x0A7C; // 4 x db levels shown by the attract-mode demos
export const MAIN_TILESET_BANK = 3;
export const METATILE_BANK = 5; // bank 5 is hard-wired by the name-table builders
export const METATILE_TABLES = { A: 0x8000, B: 0x8200 }; // 256 x dw each (bank 5)
// Breakable-block lists copied to $D900 by initGameplayState: [address in bank 2, rooms per row].
export const CASTLE_BLOCK_TABLES = {
  11: [0x97DD, 5], // radactianCastleMetatileDeletes
  16: [0x9800, 7], // "craggLakeMetatileDeletes" (really Janken's castle)
};
// Sub-areas entered through entity $4C (STATE_BONUS_LEVEL, loader at $1735), which
// hard-codes the layout pointers; see levels.py for the details.
export const SUB_AREAS = {
  3: { loader: 0x1735, layout_ptr: 0x8AD6, layout_level: 4, graphics_level: 4,
    screens: [5, 6, 7], entity_index: [8, 9, 10], variants: null },
  17: { loader: 0x1735, layout_ptr: 0xBC53, layout_level: 17, graphics_level: 17,
    screens: [2, 3], entity_index: [8, 8], variants: ['$C07F == 0', '$C07F != 0'] },
};

export const LEVEL_NAMES = {
  1: 'Mt. Eternal (vertical descent)',
  2: 'Mt. Eternal part 2',
  3: 'Lake Fathom (underwater)',
  4: 'Island of St. Nurari',
  5: 'Lake Fathom part 2 (Peticopter)',
  6: 'Village of Namui',
  7: 'Mt. Kave',
  8: 'The Blakwoods',
  9: 'River (boat)',
  10: 'Bingoo Lowland',
  11: 'Radactian Castle',
  12: 'City of Radactian',
  13: 'Swamp (Peticopter, scrolls left)',
  14: 'Kingdom of Nibana part 1',
  15: 'Kingdom of Nibana part 2',
  16: 'Janken\'s Castle',
  17: 'Crag Lake (vertical)',
};

export const SCROLL_FLAG_NAMES = [[0x01, 'down'], [0x02, 'up'], [0x04, 'left'], [0x08, 'right'],
  [0x10, 'bit4'], [0x20, 'drop_to_row1_first_screen'],
  [0x40, 'drop_to_row1_screen_x_div_4'], [0x80, 'vertical/auto']];

export const SONG_NAMES = { 0x82: 'main theme', 0x83: 'underwater', 0x84: 'castle', 0x85: 'bike',
  0x88: 'peticopter' };
export const SPAWN_STATE_NAMES = { 0: 'on foot', 1: 'riding the boat', 9: 'flying the Peticopter' };

// Name-table word attribute byte (high byte). Bits 0-4 are VDP bits; 5-7 are game flags.
export const ATTR_TILE_HI = 0x01, ATTR_HFLIP = 0x02, ATTR_VFLIP = 0x04, ATTR_SPRITE_PALETTE = 0x08, ATTR_PRIORITY = 0x10;
export const ATTR_GAME_FLAGS = 0xE0;
export const GAME_FLAG_CLASSES = { // attribute bits 7-5 of one name-table word (see the spec)
  0x00: 'passable',
  0x20: 'water',
  0x40: 'collectable: money bag tile (tile < $90) / shop item selector (tile >= $90)',
  0x60: 'ladder (tile $3F) / shop door (tile >= $70) / deadly (any other tile)',
  0x80: 'solid',
  0xA0: 'solid floor trigger: tile $0D-$24 puzzle/ghost floor, $3F ladder top, > $3F enter-down hole',
  0xC0: 'solid, breakable rock',
  0xE0: 'solid, breakable box: tile 1-4 skull, 5-8 question, 9-12 star',
};

// ---------------------------------------------------------------------------
// Python semantics the tools rely on (shared with leveledit.js)
// ---------------------------------------------------------------------------
// Python truthiness of a JSON value: None, 0, '', [] and {} are false.
export function truthy(v) {
  if (v === null || v === undefined) return false;
  if (Array.isArray(v) || typeof v === 'string') return v.length > 0;
  if (typeof v === 'object') return Object.keys(v).length > 0;
  return Boolean(v);
}

// Python == on JSON-like data: bool == int, key order ignored.
export function pyEq(a, b) {
  if (a === b) return true;
  const numA = typeof a === 'number' || typeof a === 'boolean';
  const numB = typeof b === 'number' || typeof b === 'boolean';
  if (numA || numB) return numA && numB && Number(a) === Number(b);
  if (a === null || b === null || typeof a !== 'object' || typeof b !== 'object') return false;
  const listA = Array.isArray(a) || ArrayBuffer.isView(a), listB = Array.isArray(b) || ArrayBuffer.isView(b);
  if (listA !== listB) return false;
  if (listA) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (!pyEq(a[i], b[i])) return false;
    return true;
  }
  const keys = Object.keys(a);
  if (keys.length !== Object.keys(b).length) return false;
  for (const k of keys) if (!Object.prototype.hasOwnProperty.call(b, k) || !pyEq(a[k], b[k])) return false;
  return true;
}

function pyTypeName(v) {
  if (v === null || v === undefined) return 'NoneType';
  if (Array.isArray(v)) return 'list';
  if (typeof v === 'object') return 'dict';
  if (typeof v === 'number') return Number.isInteger(v) ? 'int' : 'float';
  return typeof v === 'boolean' ? 'bool' : 'str';
}

// Python int(): truncates floats, parses decimal strings.
export function pyInt(v) {
  if (typeof v === 'boolean') return v ? 1 : 0;
  if (typeof v === 'number') {
    if (Number.isNaN(v)) throw new Error('cannot convert float NaN to integer');
    if (!Number.isFinite(v)) throw new Error('cannot convert float infinity to integer');
    return Math.trunc(v);
  }
  if (typeof v === 'string') {
    if (/^\s*[+-]?\d+(_\d+)*\s*$/.test(v)) return parseInt(v.replace(/_/g, ''), 10);
    throw new Error(`invalid literal for int() with base 10: '${v}'`);
  }
  throw new Error(`int() argument must be a string, a bytes-like object or a real number, not '${pyTypeName(v)}'`);
}

// list[i] with Python's negative indexes and IndexError.
export function pyItem(list, i) {
  const j = i < 0 ? list.length + i : i;
  if (!Number.isInteger(j) || j < 0 || j >= list.length) throw new Error('list index out of range');
  return list[j];
}

const byNumber = (a, b) => a - b;

// bytes(values) / bytearray.extend(values): only integers 0..255.
export function toBytes(values, message = 'byte must be in range(0, 256)') {
  const out = new Uint8Array(values.length);
  for (let i = 0; i < values.length; i++) {
    let v = values[i];
    if (typeof v === 'boolean') v = +v;
    if (!Number.isInteger(v)) throw new Error(`'${pyTypeName(v)}' object cannot be interpreted as an integer`);
    if (v < 0 || v > 255) throw new Error(message);
    out[i] = v;
  }
  return out;
}

// struct.pack for the little-endian formats used here ('<' then B, H or I).
export function pack(fmt, ...values) {
  const out = [];
  let k = 0;
  for (const c of fmt.slice(1)) {
    let v = values[k++];
    if (typeof v === 'boolean') v = +v;
    if (!Number.isInteger(v)) throw new Error('required argument is not an integer');
    const size = c === 'B' ? 1 : c === 'H' ? 2 : 4;
    const max = c === 'B' ? 0xFF : c === 'H' ? 0xFFFF : 0xFFFFFFFF;
    if (v < 0 || v > max) throw new Error(`'${c}' format requires 0 <= number <= ${max}`);
    for (let b = 0; b < size; b++) out.push((v >>> (8 * b)) & 0xFF);
  }
  return Uint8Array.from(out);
}

// b"".join(struct.pack("<H", v) for v in values)
export function packWords(values) {
  const out = new Uint8Array(2 * values.length);
  values.forEach((v, i) => out.set(pack('<H', v), 2 * i));
  return out;
}

export function concatBytes(parts) {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let o = 0;
  for (const p of parts) {
    out.set(p, o);
    o += p.length;
  }
  return out;
}

export function bytesEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xEDB88320 : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

// zlib.crc32
export function crc32(bytes) {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

const hex = (v, width) => v.toString(16).toUpperCase().padStart(width, '0');

// ---------------------------------------------------------------------------
// ROM access
// ---------------------------------------------------------------------------
export class Rom {
  constructor(data) {
    this.data = data instanceof Uint8Array ? data : new Uint8Array(data);
  }

  // ROM file offset of CPU address `addr` (slot 2 addresses need `bank`).
  offset(addr, bank = null) {
    if (addr < 0x8000) return addr;
    if (bank === null || bank === undefined) throw new Error(`slot-2 address ${hex(addr, 4)} needs a bank`);
    return (bank & 0x3F) * 0x4000 + (addr - 0x8000);
  }

  // Reading past the end raises, as in Python (and keeps the parsers from looping forever).
  at(o) {
    if (o < 0 || o >= this.data.length) throw new Error('index out of range');
    return this.data[o];
  }

  byte(addr, bank = null) {
    return this.at(this.offset(addr, bank));
  }

  word(addr, bank = null) {
    const o = this.offset(addr, bank);
    return this.at(o) | (this.at(o + 1) << 8);
  }

  bytes(addr, n, bank = null) {
    const o = this.offset(addr, bank);
    return this.data.subarray(o, o + n);
  }
}

export const h16 = (v) => '$' + hex(v, 4);
export const h8 = (v) => '$' + hex(v, 2);

// ---------------------------------------------------------------------------
// Screen RLE ("run/literal" byte stream, decoded by $6BE8 into v_decompressedLevelLayoutData)
// ---------------------------------------------------------------------------
// Returns [values, encodedLength, tokens]; tokens: [['R', n] | ['L', n], ...].
export function rleDecode(data, start = 0) {
  const out = [];
  const tokens = [];
  let o = start;
  for (;;) {
    if (o >= data.length) throw new Error('index out of range');
    const b = data[o];
    if (b === 0) return [out, o + 1 - start, tokens];
    if (b & 0x80) {
      const n = b & 0x7F;
      for (const v of data.subarray(o + 1, o + 1 + n)) out.push(v);
      tokens.push(['L', n]);
      o += 1 + n;
    } else {
      if (o + 1 >= data.length) throw new Error('index out of range');
      for (let k = 0; k < b; k++) out.push(data[o + 1]);
      tokens.push(['R', b]);
      o += 2;
    }
  }
}

function runLength(d, i) {
  let j = i;
  while (j < d.length && d[j] === d[i] && j - i < 127) j++;
  return j - i;
}

// Canonical encoder (reproduces 166 of the 174 original screens byte for byte).
// Runs of >= 3 are always run tokens. A run of exactly 2 is a run token when no
// literal is pending, or when it is followed by another run of >= 2 or by the end
// of data; otherwise the two bytes join the pending literal. Literals are split at 127.
export function rleEncode(values) {
  const d = Array.from(values);
  const out = [];
  const lit = [];
  const flush = () => {
    while (lit.length) {
      const chunk = lit.splice(0, 127);
      out.push(0x80 | chunk.length, ...chunk);
    }
  };
  let i = 0;
  const n = d.length;
  while (i < n) {
    const r = runLength(d, i);
    if (r >= 3) {
      flush();
      out.push(r, d[i]);
      i += r;
    } else if (r === 2) {
      const nxt = i + 2 < n ? runLength(d, i + 2) : 0;
      if (!lit.length || nxt >= 2 || i + 2 >= n) {
        flush();
        out.push(2, d[i]);
      } else {
        lit.push(d[i], d[i + 1]);
      }
      i += 2;
    } else {
      lit.push(d[i]);
      i += 1;
    }
  }
  flush();
  out.push(0);
  return toBytes(out);
}

// Re-encodes following an explicit token list; null if it does not fit `values`.
export function rleEncodeTokens(values, tokens) {
  const d = Array.from(values);
  const out = [];
  let i = 0;
  for (const [kind, n] of tokens) {
    if (n <= 0 || n > 127 || i + n > d.length) return null;
    if (kind === 'R') {
      for (let k = i; k < i + n; k++) if (d[k] !== d[i]) return null;
      out.push(n, d[i]);
    } else {
      out.push(0x80 | n, ...d.slice(i, i + n));
    }
    i += n;
  }
  if (i !== d.length) return null;
  out.push(0);
  return toBytes(out);
}

// Bytes for one screen: explicit tokenisation if recorded and still valid, else canonical.
export function encodeScreen(screen) {
  const toks = screen.rle_tokens;
  if (truthy(toks)) {
    const b = rleEncodeTokens(screen.metatiles, toks.map((t) => [t[0], pyInt(t.slice(1))]));
    if (b !== null) return b;
  }
  return rleEncode(screen.metatiles);
}

// ---------------------------------------------------------------------------
// Metatiles
// ---------------------------------------------------------------------------
export function decodeWord(w) {
  const hi = w >> 8;
  return {
    word: w,
    tile: w & 0x1FF,
    hflip: Boolean(hi & ATTR_HFLIP),
    vflip: Boolean(hi & ATTR_VFLIP),
    sprite_palette: Boolean(hi & ATTR_SPRITE_PALETTE),
    priority: Boolean(hi & ATTR_PRIORITY),
    game_flags: hi & ATTR_GAME_FLAGS,
  };
}

export function loadMetatileTable(rom, name) {
  const base = METATILE_TABLES[name];
  const entries = [];
  for (let i = 0; i < 256; i++) {
    const ptr = rom.word(base + 2 * i, METATILE_BANK);
    const raw = rom.bytes(ptr, 8, METATILE_BANK);
    const words = [0, 1, 2, 3].map((k) => raw[2 * k] | (raw[2 * k + 1] << 8));
    entries.push({
      id: i,
      ptr,
      rom_offset: rom.offset(ptr, METATILE_BANK),
      words, // TL, TR, BL, BR
      decoded: words.map(decodeWord),
      game_flags: words.map((w) => (w >> 8) & ATTR_GAME_FLAGS),
      word_kinds: words.map(classifyWord),
      class: classifyMetatile(words),
    });
  }
  // Entries whose 8 bytes overlap another entry (pointer to a 1-byte stub): never used by a level.
  const starts = [...new Set(entries.map((e) => e.ptr))].sort(byNumber);
  for (const e of entries) e.overlaps_next_entry = starts.some((p) => e.ptr < p && p < e.ptr + 8);
  return { name, table_ptr: base, rom_offset: rom.offset(base, METATILE_BANK), entries };
}

// Behaviour of one name-table word, as decided by the collision/interaction code.
export function classifyWord(w) {
  const f = (w >> 8) & ATTR_GAME_FLAGS, t = w & 0xFF;
  if (f === 0x00) return (w & 0x1FF) === 0 ? 'empty' : 'background';
  if (f === 0x20) return 'water';
  if (f === 0x40) return t < 0x90 ? 'money' : 'shop_item';
  if (f === 0x60) return t === 0x3F ? 'ladder' : (t >= 0x70 ? 'shop_door' : 'deadly');
  if (f === 0x80) return 'solid';
  if (f === 0xA0) {
    if (t < 0x0D) return 'solid';
    if (t <= 0x24) return t >= 0x1D && t <= 0x20 ? 'ghost_floor' : 'puzzle_floor';
    if (t === 0x3F) return 'ladder_top';
    if (t > 0x3F) return 'enter_down';
    return 'invalid'; // $25-$3E overrun the floor jump table
  }
  if (f === 0xC0) return 'breakable';
  // $E0: box; the type comes from the top-left word's tile
  if (t >= 1 && t <= 4) return 'skull_box';
  if (t >= 5 && t <= 8) return 'question_box';
  if (t >= 9 && t <= 12) return 'star_box';
  return 'box';
}

export function classifyMetatile(words) {
  let kinds = words.map(classifyWord);
  if (kinds.every((k) => k === 'empty')) return 'empty';
  kinds = kinds.map((k) => (k === 'empty' ? 'background' : k));
  if (kinds[0].endsWith('_box')) return kinds[0]; // boxes are identified by their top-left word
  if (new Set(kinds).size === 1) return kinds[0];
  if (kinds[0] === kinds[1] && kinds[2] === kinds[3]) return `${kinds[0]}/${kinds[2]}`;
  return `mixed(${kinds.join(',')})`;
}

// ---------------------------------------------------------------------------
// Level descriptor, layout tables and screens (bank given by the descriptor, normally 6)
// ---------------------------------------------------------------------------
// Reads dw entries from `addr` until the next known structure start. Every value read
// is added to `known`, so the last table before the screens stops at the first screen.
function readPtrTable(rom, addr, bank, known) {
  const out = [];
  let pos = addr;
  for (;;) {
    if (out.length) {
      for (const k of known) if (k > addr && pos >= k) return out;
    }
    const v = rom.word(pos, bank);
    out.push(v);
    known.add(v);
    pos += 2;
  }
}

export function loadLevelLayout(rom, lv, desc) {
  const bank = desc.bank;
  const rowsPtr = desc.rows_ptr, colsPtr = desc.cols_ptr;
  // The level's tables are stored as: rows table, cols table (if different), row tables,
  // column tables, then the screens. A table ends where the next known structure starts.
  const known = new Set([rowsPtr, colsPtr]);
  const top = new Map();
  for (const t of [...new Set([rowsPtr, colsPtr])].sort(byNumber)) top.set(t, readPtrTable(rom, t, bank, known));
  const sub = new Map();
  for (const s of [...new Set([...top.values()].flat())].sort(byNumber)) sub.set(s, readPtrTable(rom, s, bank, known));
  const screenPtrs = [...new Set([...sub.values()].flat())].sort(byNumber);
  const screens = [];
  const indexOf = new Map();
  for (const p of screenPtrs) {
    const [vals, ln, toks] = rleDecode(rom.data, rom.offset(p, bank));
    if (vals.length !== SCREEN_CELLS) throw new Error(`level ${lv} screen ${hex(p, 4)} decodes to ${vals.length} bytes`);
    const scr = { ptr: p, rom_offset: rom.offset(p, bank), encoded_size: ln, metatiles: vals };
    if (!bytesEqual(rleEncode(vals), rom.data.subarray(scr.rom_offset, scr.rom_offset + ln))) {
      scr.rle_tokens = toks.map(([k, n]) => `${k}${n}`);
    }
    indexOf.set(p, screens.length);
    screens.push(scr);
  }
  const layout = {
    rows: top.get(rowsPtr).map((r) => ({ ptr: r, screens: sub.get(r).map((p) => indexOf.get(p)) })),
    cols: top.get(colsPtr).map((c) => ({ ptr: c, screens: sub.get(c).map((p) => indexOf.get(p)) })),
    cols_is_rows: rowsPtr === colsPtr,
  };
  return [layout, screens];
}

export function loadDescriptor(rom, lv) {
  const ptr = rom.word(LEVEL_DESCRIPTOR_TABLE + 2 * (lv - 1));
  const d = rom.bytes(ptr, LEVEL_DESCRIPTOR_SIZE);
  const mt = d[10] | (d[11] << 8);
  const tname = Object.keys(METATILE_TABLES).filter((k) => METATILE_TABLES[k] === mt);
  return {
    ptr,
    rom_offset: rom.offset(ptr),
    bank_byte: d[0],
    bank: d[0] & 0x3F,
    rows_ptr: d[1] | (d[2] << 8),
    cols_ptr: d[3] | (d[4] << 8),
    start_screen_x: d[5],
    start_screen_y: d[6],
    width: d[7],
    height: d[8],
    scroll_flags: d[9],
    scroll_flag_names: SCROLL_FLAG_NAMES.filter(([bit]) => d[9] & bit).map(([, n]) => n),
    metatile_table_ptr: mt,
    metatile_table: tname.length ? tname[0] : null,
  };
}

export function levelKind(lv, desc) {
  const f = desc.scroll_flags;
  if (lv === 1 || lv === 17) return 'vertical';
  if (lv === 13) return 'reverse';
  if (f & 0x80) return 'castle';
  if (f & 0x60) return 'drop';
  return 'horizontal';
}

// Physical placement of the reachable screens, with their entity-descriptor index.
// Returns [kind, cells], cells = [{x, y, screen, entity_index, part}]. The engine
// itself only knows the rows/cols tables; this reproduces how it walks them.
export function deriveMap(lv, desc, layout) {
  const kind = levelKind(lv, desc);
  const rows = layout.rows, cols = layout.cols;
  const w = desc.width, hgt = desc.height;
  const sx = desc.start_screen_x, sy = desc.start_screen_y;
  const cells = [];
  const at = (list, i) => pyItem(list, i);
  if (kind === 'horizontal') {
    for (let i = 0; i < w + 1; i++) {
      cells.push({ x: i, y: 0, screen: at(at(rows, sy).screens, i), entity_index: i, part: 'main' });
    }
  } else if (kind === 'reverse') {
    for (let i = 0; i < sx; i++) {
      cells.push({ x: i, y: 0, screen: at(at(rows, sy).screens, i), entity_index: i, part: 'main' });
    }
  } else if (kind === 'vertical') {
    const col = at(cols, sx - 1).screens;
    for (let v = 0; v < hgt + 1; v++) cells.push({ x: 0, y: v, screen: at(col, v), entity_index: v, part: 'vertical' });
    if (desc.scroll_flags & 0x80) { // level 1: continues to the right on row 0
      const row = at(rows, 0).screens;
      for (let i = 1; i < w + 1; i++) {
        cells.push({ x: i, y: hgt, screen: at(row, i), entity_index: hgt + i, part: 'horizontal' });
      }
    }
  } else if (kind === 'drop') {
    for (let i = 0; i < w + 1; i++) {
      cells.push({ x: i, y: 0, screen: at(at(rows, 0).screens, i), entity_index: i, part: 'upper' });
    }
    // bit 5: the drop lands on row 1 screen 0 (entity index 6) and updater $6574 sets
    // width 1; bit 6: row 1 screen x/4 (index $10 + x/4) and updater $6532 sets width 3.
    const [lowerW, firstIdx] = desc.scroll_flags & 0x20 ? [1, 6] : [3, 16];
    for (let i = 0; i < lowerW + 1; i++) {
      cells.push({ x: i, y: 1, screen: at(at(rows, 1).screens, i), entity_index: firstIdx + i, part: 'lower' });
    }
  } else if (kind === 'castle') {
    rows.forEach((row, r) => row.screens.forEach((s, c) => {
      cells.push({ x: c, y: r, screen: s, entity_index: r * w + c, part: 'room' });
    }));
  }
  return [kind, cells];
}

// ---------------------------------------------------------------------------
// Entity descriptors (bank 2)
// ---------------------------------------------------------------------------
// Parses one per-screen entity descriptor. Returns [records, length].
export function parseEntityStream(rom, ptr) {
  const recs = [];
  let pos = ptr;
  const four = (p) => Array.from(rom.bytes(p, 4, ENTITY_BANK));
  for (;;) {
    const c = rom.byte(pos, ENTITY_BANK);
    if (c === 0) {
      recs.push({ kind: 'end' });
      pos += 1;
      break;
    }
    if (c & 0x80) {
      if (c & 0x01) {
        const [t, y, x, dat] = four(pos + 1);
        recs.push({ kind: 'extra_slot', code: c, type: t, y, x, data: dat });
        pos += 5;
      } else if (c & 0x02) {
        recs.push({ kind: 'octopus_arms', code: c, set: rom.byte(pos + 1, ENTITY_BANK) });
        pos += 2;
      } else if (c & 0x04) {
        const [t, y, x, dat] = four(pos + 1);
        recs.push({ kind: 'fixed_slot', code: c, type: t, y, x, data: dat });
        pos += 5;
      } else {
        const n = rom.byte(pos + 1, ENTITY_BANK);
        const raw = Array.from(rom.bytes(pos + 2, n, ENTITY_BANK));
        const rec = { kind: 'ram_block', code: c, bytes: raw };
        if (n >= 1 && n === 1 + 3 * raw[0]) {
          rec.patches = Array.from({ length: raw[0] }, (_, k) => ({
            addr: raw[1 + 3 * k] | (raw[2 + 3 * k] << 8), value: raw[3 + 3 * k],
          }));
        }
        recs.push(rec);
        pos += 2 + n;
      }
      continue;
    }
    const n = c;
    for (let k = 0; k < n; k++) {
      const [t, y, x, dat] = four(pos + 1 + 4 * k);
      recs.push({ kind: 'entity', type: t, y, x, data: dat });
    }
    pos += 1 + 4 * n;
    break;
  }
  return [recs, pos - ptr];
}

const get = (obj, key, dflt) => (key in obj ? obj[key] : dflt);

export function encodeEntityStream(recs) {
  const out = [];
  const ents = recs.filter((r) => r.kind === 'entity');
  for (const r of recs) {
    const k = r.kind;
    if (k === 'extra_slot') {
      out.push(get(r, 'code', 0x81), r.type, r.y, r.x, r.data);
    } else if (k === 'fixed_slot') {
      out.push(get(r, 'code', 0x84), r.type, r.y, r.x, r.data);
    } else if (k === 'octopus_arms') {
      out.push(get(r, 'code', 0x82), r.set);
    } else if (k === 'ram_block') {
      out.push(get(r, 'code', 0x88), r.bytes.length, ...r.bytes);
    }
  }
  if (ents.length) {
    if (ents.length > 127) throw new Error('too many entities in one screen');
    out.push(ents.length);
    for (const r of ents) out.push(r.type, r.y, r.x, r.data);
  } else {
    out.push(0);
  }
  return toBytes(out);
}

export function loadEntities(rom, lv, nextTable) {
  const table = rom.word(ENTITY_DESCRIPTOR_TABLE + 2 * (lv - 1), ENTITY_BANK);
  const n = Math.floor((nextTable - table) / 2);
  const ptrs = Array.from({ length: Math.max(0, n) }, (_, i) => rom.word(table + 2 * i, ENTITY_BANK));
  const streams = ptrs.map((p) => {
    const [recs, ln] = parseEntityStream(rom, p);
    return { ptr: p, rom_offset: rom.offset(p, ENTITY_BANK), size: ln, records: recs };
  });
  return { table_ptr: table, rom_offset: rom.offset(table, ENTITY_BANK), screens: streams };
}

// Start of each level's per-screen pointer table, plus the end of the last one.
export function entityTableBounds(rom) {
  const starts = Array.from({ length: LEVEL_COUNT }, (_, i) => rom.word(ENTITY_DESCRIPTOR_TABLE + 2 * i, ENTITY_BANK));
  // The per-level tables are stored back to back; the last one ends where the first
  // descriptor stream begins (the lowest first entry of any table).
  const firstStream = Math.min(...starts.map((s) => rom.word(s, ENTITY_BANK)));
  const order = [...starts].sort(byNumber);
  const ends = new Map();
  order.forEach((s, i) => ends.set(s, i + 1 < order.length ? order[i + 1] : firstStream));
  return [starts, ends];
}

// ---------------------------------------------------------------------------
// Castle breakable-block tables (levels 11 and 16)
// ---------------------------------------------------------------------------
export function loadCastleBlocks(rom, lv) {
  const [addr, perRow] = CASTLE_BLOCK_TABLES[lv];
  const rows = [];
  let pos = addr;
  for (;;) {
    const row = [];
    for (let k = 0; k < perRow; k++) {
      const n = rom.byte(pos, 2);
      row.push(Array.from(rom.bytes(pos + 1, n, 2)));
      pos += 1 + n;
    }
    rows.push(row);
    if (rom.byte(pos, 2) === 0xFF) {
      pos += 1;
      break;
    }
  }
  return { ptr: addr, rom_offset: rom.offset(addr, 2), rooms_per_row: perRow, rows, size: pos - addr };
}

export function encodeCastleBlocks(cb) {
  const out = [];
  for (const row of cb.rows) {
    for (const cells of row) out.push(cells.length, ...cells);
  }
  out.push(0xFF);
  return toBytes(out);
}

// ---------------------------------------------------------------------------
// Per-level tables in banks 0/1/3/7
// ---------------------------------------------------------------------------
export function loadLevelTables(rom, lv) {
  const i = lv - 1;
  return {
    start_x: rom.byte(STARTING_POSITIONS + 2 * i),
    start_y: rom.byte(STARTING_POSITIONS + 2 * i + 1),
    song: rom.byte(LEVEL_SONGS + i),
    spawn_state: rom.byte(SPAWN_STATES + i),
    question_box_index: rom.byte(QUESTION_BOX_INDEXES + i),
    shop_door_offset: rom.byte(SHOP_DOORS + 3 * i),
    shop_door_nametable_addr: rom.word(SHOP_DOORS + 3 * i + 1),
    palette_ptr: rom.word(LEVEL_PALETTES + 2 * i),
    palette: Array.from(rom.bytes(rom.word(LEVEL_PALETTES + 2 * i), 32, PALETTE_BANK)),
    main_tileset_ptr: rom.word(MAIN_TILESET_POINTERS + 2 * i, MAIN_TILESET_BANK),
    tileset_loader: rom.word(TILESET_LOADERS + 2 * i),
    sprite_tiles_loader: rom.word(SPRITE_TILES_LOADERS + 2 * i),
    tile_updater: rom.word(TILE_UPDATERS + 2 * i),
    palette_updater: rom.word(PALETTE_UPDATERS + 2 * i),
    scroll_flags_updater: rom.word(SCROLL_FLAGS_UPDATERS + 2 * i),
    entity_loader: rom.word(ENTITY_LOADERS + 2 * i),
    camera_handler: rom.word(CAMERA_HANDLERS + 2 * i),
    vehicle_crash_to_water: rom.byte(VEHICLE_CRASH_TO_WATER + i),
    map_arrow_x: rom.byte(MAP_ARROW_POSITIONS + 2 * i),
    map_arrow_y: rom.byte(MAP_ARROW_POSITIONS + 2 * i + 1),
    shop_items_ptr: rom.word(SHOP_ITEM_TABLES + 2 * i),
    shop_sold_ptr: rom.word(SHOP_SOLD_TABLES + 2 * i),
  };
}

export function encodeLevelTables(lv, t) {
  const i = lv - 1;
  const bytes = (...v) => toBytes(v, 'bytes must be in range(0, 256)');
  return [
    [STARTING_POSITIONS + 2 * i, bytes(t.start_x, t.start_y)],
    [LEVEL_SONGS + i, bytes(t.song)],
    [SPAWN_STATES + i, bytes(t.spawn_state)],
    [QUESTION_BOX_INDEXES + i, bytes(t.question_box_index)],
    [SHOP_DOORS + 3 * i, pack('<BH', t.shop_door_offset, t.shop_door_nametable_addr)],
    [LEVEL_PALETTES + 2 * i, pack('<H', t.palette_ptr)],
    [0x4000 * MAIN_TILESET_BANK + MAIN_TILESET_POINTERS - 0x8000 + 2 * i, pack('<H', t.main_tileset_ptr)],
    [TILESET_LOADERS + 2 * i, pack('<H', t.tileset_loader)],
    [SPRITE_TILES_LOADERS + 2 * i, pack('<H', t.sprite_tiles_loader)],
    [TILE_UPDATERS + 2 * i, pack('<H', t.tile_updater)],
    [PALETTE_UPDATERS + 2 * i, pack('<H', t.palette_updater)],
    [SCROLL_FLAGS_UPDATERS + 2 * i, pack('<H', t.scroll_flags_updater)],
    [ENTITY_LOADERS + 2 * i, pack('<H', t.entity_loader)],
    [CAMERA_HANDLERS + 2 * i, pack('<H', t.camera_handler)],
    [VEHICLE_CRASH_TO_WATER + i, bytes(t.vehicle_crash_to_water)],
    [MAP_ARROW_POSITIONS + 2 * i, bytes(t.map_arrow_x, t.map_arrow_y)],
    [SHOP_ITEM_TABLES + 2 * i, pack('<H', t.shop_items_ptr)],
    [SHOP_SOLD_TABLES + 2 * i, pack('<H', t.shop_sold_ptr)],
  ];
}

export function loadGlobals(rom) {
  const arms = [];
  for (let k = 0; k < 2; k++) {
    const p = rom.word(OCTOPUS_ARM_SETS + 2 * k);
    const segs = Array.from({ length: 8 }, (_, j) => {
      const [delay, y, x, follower] = rom.bytes(p + 2 + 4 * j, 4);
      return { delay, y, x, follower };
    });
    arms.push({ ptr: p, pot_nametable_addr: rom.word(p), segments: segs });
  }
  const seq = [];
  let pos = FLOOR_PUZZLE_SEQUENCE;
  while (rom.byte(pos) !== 0xFF) {
    seq.push(rom.byte(pos));
    pos += 1;
  }
  return {
    question_box_items: Array.from(rom.bytes(QUESTION_BOX_ITEMS, 72)),
    floor_puzzle_sequence: seq,
    octopus_arm_sets: arms,
    demo_levels: Array.from(rom.bytes(DEMO_LEVELS, 4)),
  };
}

export function encodeGlobals(g) {
  const bytes = (v) => toBytes(v, 'bytes must be in range(0, 256)');
  const out = [[QUESTION_BOX_ITEMS, bytes(g.question_box_items)],
    [FLOOR_PUZZLE_SEQUENCE, bytes([...g.floor_puzzle_sequence, 0xFF])],
    [DEMO_LEVELS, bytes(g.demo_levels)]];
  g.octopus_arm_sets.forEach((a, k) => {
    out.push([OCTOPUS_ARM_SETS + 2 * k, pack('<H', a.ptr)]);
    const b = [...pack('<H', a.pot_nametable_addr)];
    for (const sgm of a.segments) b.push(sgm.delay, sgm.y, sgm.x, sgm.follower);
    out.push([a.ptr, toBytes(b)]);
  });
  return out;
}

// ---------------------------------------------------------------------------
// Top level
// ---------------------------------------------------------------------------
// Decodes the 17 levels into a JSON-serialisable object. Unlike levels.py, the
// levels have no "graphics" entry (the VRAM loader emulation is not ported).
export function load(romBytes) {
  const rom = new Rom(romBytes);
  const model = {
    rom: { size: rom.data.length, crc32: hex(crc32(rom.data), 8) },
    metatile_tables: Object.fromEntries(Object.keys(METATILE_TABLES).sort().map((k) => [k, loadMetatileTable(rom, k)])),
    globals: loadGlobals(rom),
    levels: [],
  };
  const [starts, ends] = entityTableBounds(rom);
  for (let lv = 1; lv <= LEVEL_COUNT; lv++) {
    const desc = loadDescriptor(rom, lv);
    const [layout, screens] = loadLevelLayout(rom, lv, desc);
    const [kind, cells] = deriveMap(lv, desc, layout);
    const level = {
      number: lv,
      name: LEVEL_NAMES[lv],
      kind,
      descriptor: desc,
      layout,
      screens,
      map: cells,
      entities: loadEntities(rom, lv, ends.get(starts[lv - 1])),
      tables: loadLevelTables(rom, lv),
    };
    if (lv in CASTLE_BLOCK_TABLES) level.castle_blocks = loadCastleBlocks(rom, lv);
    model.levels.push(level);
  }
  for (const [key, sa] of Object.entries(SUB_AREAS)) {
    const lv = Number(key);
    const owner = model.levels[sa.layout_level - 1];
    const row = owner.layout.rows[0].screens;
    model.levels[lv - 1].sub_area = {
      loader: sa.loader, layout_ptr: sa.layout_ptr, layout_level: sa.layout_level,
      graphics_level: sa.graphics_level, variants: sa.variants && sa.variants.slice(),
      cells: sa.screens.map((k, i) => ({
        x: i, y: 0, level: sa.layout_level, screen: pyItem(row, k), row_index: k, entity_index: sa.entity_index[i],
      })),
    };
  }
  return model;
}

function slot2Offset(addr, bank) {
  return (bank & 0x3F) * 0x4000 + (addr - 0x8000);
}

// Produces [[romOffset, bytes]] for every level structure described by `model`.
// Addresses (ptr fields) are honoured as they are in the model: the caller is
// responsible for choosing non-overlapping locations when data grows.
export function encode(model) {
  const out = [];
  const lvls = model.levels;
  out.push([LEVEL_DESCRIPTOR_TABLE, packWords(lvls.map((l) => l.descriptor.ptr))]);
  out.push([slot2Offset(ENTITY_DESCRIPTOR_TABLE, ENTITY_BANK), packWords(lvls.map((l) => l.entities.table_ptr))]);
  for (const level of lvls) {
    const lv = level.number;
    const d = level.descriptor;
    const bank = d.bank;
    out.push([d.rom_offset, pack('<BHHBBBBBH', d.bank_byte, d.rows_ptr, d.cols_ptr, d.start_screen_x,
      d.start_screen_y, d.width, d.height, d.scroll_flags, d.metatile_table_ptr)]);
    const lay = level.layout;
    const scr = level.screens;
    out.push([slot2Offset(d.rows_ptr, bank), packWords(lay.rows.map((r) => r.ptr))]);
    if (!lay.cols_is_rows) out.push([slot2Offset(d.cols_ptr, bank), packWords(lay.cols.map((c) => c.ptr))]);
    const seen = new Set();
    for (const tab of [...lay.rows, ...(lay.cols_is_rows ? [] : lay.cols)]) {
      if (seen.has(tab.ptr)) continue;
      seen.add(tab.ptr);
      out.push([slot2Offset(tab.ptr, bank), packWords(tab.screens.map((i) => pyItem(scr, i).ptr))]);
    }
    for (const s of scr) out.push([slot2Offset(s.ptr, bank), encodeScreen(s)]);
    const ent = level.entities;
    out.push([slot2Offset(ent.table_ptr, ENTITY_BANK), packWords(ent.screens.map((s) => s.ptr))]);
    const done = new Set();
    for (const s of ent.screens) {
      if (done.has(s.ptr)) continue;
      done.add(s.ptr);
      out.push([slot2Offset(s.ptr, ENTITY_BANK), encodeEntityStream(s.records)]);
    }
    out.push(...encodeLevelTables(lv, level.tables));
    if ('castle_blocks' in level) out.push([level.castle_blocks.rom_offset, encodeCastleBlocks(level.castle_blocks)]);
  }
  out.push(...encodeGlobals(model.globals));
  for (const tab of Object.values(model.metatile_tables)) {
    out.push([tab.rom_offset, packWords(tab.entries.map((e) => e.ptr))]);
    for (const e of tab.entries) out.push([e.rom_offset, packWords(e.words)]);
  }
  return out;
}

// SEGA header checksum ($7FFA): 16-bit sum of the ROM except the header at $7FF0-$7FFF,
// over the size given by the low nibble of $7FFF ($F = 128 KB here).
export function headerChecksum(romBytes) {
  const sizes = { 0xC: 0x8000, 0xE: 0x10000, 0xF: 0x20000, 0x0: 0x40000, 0x1: 0x80000, 0x2: 0x100000 };
  const nibble = romBytes[0x7FFF] & 0x0F;
  const end = Math.min(nibble in sizes ? sizes[nibble] : romBytes.length, romBytes.length);
  let sum = 0;
  for (let i = 0; i < 0x7FF0; i++) sum += romBytes[i];
  for (let i = 0x8000; i < end; i++) sum += romBytes[i];
  return sum & 0xFFFF;
}

// Returns a patched copy of the ROM. With fixChecksum the SEGA header checksum is
// recomputed (the export BIOS of real consoles checks it). Data written past the
// end grows the ROM, the gap being filled with $FF.
export function applyPatches(romBytes, patches, fixChecksum = true) {
  let size = romBytes.length;
  for (const [off, b] of patches) size = Math.max(size, off + b.length);
  const out = new Uint8Array(size);
  out.fill(0xFF, romBytes.length);
  out.set(romBytes);
  for (const [off, b] of patches) out.set(b, off);
  if (fixChecksum) {
    const c = headerChecksum(out);
    out[0x7FFA] = c & 0xFF;
    out[0x7FFB] = c >> 8;
  }
  return out;
}

export function entityTypeName(t) {
  const n = ENTITY_TYPES[t];
  return n ? `${n[0]}[${h8(t)}]` : h8(t);
}

// Entity types (entityTypeJumpTable at $2892, index = type - 1): [name, updater address,
// placed by level descriptors?, meaning of the data byte for placed types].
export const ENTITY_TYPES = {
  0x01: ['alex', 0x2958, false, ''],
  0x02: ['vehicle_bullet', 0x4489, false, ''],
  0x03: ['vehicle_explosion', 0x443F, false, ''],
  0x04: ['bullet_impact', 0x44CD, false, ''],
  0x05: ['capsule_a_thrown', 0x4689, false, ''],
  0x06: ['capsule_a_hatched', 0x46C2, false, ''],
  0x07: ['capsule_b_thrown', 0x4719, false, ''],
  0x08: ['capsule_b_barrier', 0x4885, false, ''],
  0x09: ['capsule_a_helper', 0x4768, false, ''],
  0x0A: ['capsule_a_helper2', 0x4863, false, ''],
  0x0B: ['janken_thought_cloud', 0x761F, false, ''],
  0x0C: ['battle_sprite_helper', 0x7982, false, ''],
  0x0D: ['gooseka_head', 0x799A, false, ''],
  0x0E: ['chokkinna_head', 0x7A89, false, ''],
  0x0F: ['parplin_head', 0x7B2E, false, ''],
  0x10: ['spiked_pillar_a', 0x49EB, true, 'initial delay (frames)'],
  0x11: ['spiked_pillar_b', 0x4A26, true, 'initial delay (frames)'],
  0x12: ['spiked_pillar_c', 0x4A32, true, 'initial delay (frames)'],
  0x13: ['spiked_pillar_d', 0x4A3E, true, 'initial delay (frames)'],
  0x14: ['spiked_pillar_active', 0x497D, false, ''],
  0x15: ['spiked_ceiling_band', 0x4B1C, true, 'band width in tiles'],
  0x16: ['collapsing_floor', 0x4A4A, true, 'hole half-width in tiles'],
  0x17: ['collapsing_floor_punch', 0x4AE7, true, 'hole half-width in tiles'],
  0x18: ['static_sprite', 0x0966, false, ''],
  0x19: ['janken_projectile', 0x74C7, false, ''],
  0x1A: ['chokkinna_spell', 0x789E, false, ''],
  0x1B: ['bracelet_shockwave', 0x4914, false, ''],
  0x1C: ['janken_the_great', 0x7143, true, 'opponent id (see spec)'],
  0x1D: ['gooseka', 0x778F, true, 'opponent id: 2 = first fight, 3 = rematch'],
  0x1E: ['chokkinna', 0x780F, true, 'opponent id: 4 = first fight, 5 = rematch'],
  0x1F: ['parplin', 0x78A1, true, 'opponent id: 6 = first fight, 7 = rematch'],
  0x20: ['bat_left', 0x4EE8, true, 'ignored'],
  0x21: ['item_select_arrow', 0x2439, false, ''],
  0x22: ['merman_bubbles', 0x4E96, false, ''],
  0x23: ['merman', 0x4E29, true, 'hits already taken (dies at 3)'],
  0x24: ['octopus_arm', 0x4C27, false, '(placed only through $82 records)'],
  0x25: ['blakwoods_bear', 0x52E0, true, 'ignored'],
  0x26: ['blakwoods_bear_walk_right', 0x5359, false, ''],
  0x27: ['blakwoods_bear_attack_left', 0x53C8, false, ''],
  0x28: ['blakwoods_bear_attack_right', 0x544A, false, ''],
  0x29: ['monkey_leaf', 0x55EC, false, ''],
  0x2A: ['monkey', 0x5573, true, 'ignored'],
  0x2B: ['smoke_puff', 0x567D, false, ''],
  0x2C: ['plant', 0x4FEA, true, 'ignored'],
  0x2D: ['monster_bird_left', 0x5030, true, 'ignored'],
  0x2E: ['killer_fish_left', 0x5158, true, 'ignored'],
  0x2F: ['monster_frog', 0x56C5, true, 'ignored'],
  0x30: ['small_fish_left', 0x50DA, true, 'ignored'],
  0x31: ['sea_horse_left', 0x57C7, true, 'overwritten with its base y'],
  0x32: ['sea_horse_right', 0x587C, false, ''],
  0x33: ['monster_bird_right', 0x5081, true, 'ignored'],
  0x34: ['small_fish_right', 0x512B, false, ''],
  0x35: ['killer_fish_right', 0x51EC, false, ''],
  0x36: ['bat_right', 0x4F7B, false, ''],
  0x37: ['monster_frog_jumping', 0x571C, false, ''],
  0x38: ['debris_top_left', 0x5901, false, ''],
  0x39: ['debris_bottom_left', 0x598F, false, ''],
  0x3A: ['debris_top_right', 0x59C1, false, ''],
  0x3B: ['debris_bottom_right', 0x59F4, false, ''],
  0x3C: ['money_bag', 0x5A2A, false, ''],
  0x3D: ['circular_flame', 0x5D8D, true, 'sub-pixel accumulator (ignored)'],
  0x3E: ['scorpion_or_flame_left', 0x5E0D, true, '0 = scorpion (killable), else walking flame'],
  0x3F: ['scorpion_or_flame_right', 0x5E74, false, ''],
  0x40: ['storm_cloud', 0x5EB3, true, 'ignored'],
  0x41: ['lightning', 0x5EFE, false, ''],
  0x42: ['leaping_fish', 0x5F45, true, 'ignored (always at y=$BF)'],
  0x43: ['boss_defeat_smoke', 0x5622, false, ''],
  0x44: ['rice_ball', 0x5BCA, true, 'ignored (ends the level)'],
  0x45: ['saint_nurari', 0x60B5, true, 'ignored'],
  0x46: ['namui_bull', 0x5C2F, true, 'ignored'],
  0x47: ['namui_bull_state2', 0x5CA9, false, ''],
  0x48: ['namui_bull_state3', 0x5CF0, false, ''],
  0x49: ['namui_bull_state4', 0x5D2F, false, ''],
  0x4A: ['blakwoods_bear_hurt', 0x550E, false, ''],
  0x4B: ['nametable_changer', 0x61C6, true, '1 = punch counter, 2 = on touch, else on punch'],
  0x4C: ['sub_area_trigger', 0x6279, true, 'value stored in $C07F (sub-area variant)'],
  0x4D: ['extra_life', 0x5A8F, false, ''],
  0x4E: ['power_bracelet', 0x5ADF, false, ''],
  0x4F: ['ghost', 0x5B30, false, ''],
  0x50: ['village_elder', 0x6077, true, 'ignored'],
  0x51: ['captive', 0x5FAA, true, '0 = Princess Lora, else Egle'],
  0x52: ['special_item', 0x6106, true, 'item id 0..9 (0 crown, 1 telepathy ball, 2 letter, 3 Hirotta '
    + 'stone, 4 moonlight stone, 5 extra life, 6 bracelet, 7 teleport powder, 8 sun stone)'],
  0x53: ['king_of_nibana', 0x616F, true, 'ignored'],
  0x54: ['ground_walker', 0x62A8, true, 'ignored'],
  0x55: ['hopping_walker', 0x6361, true, 'ignored'],
  0x56: ['map_arrow', 0x1B41, false, ''],
  0x57: ['static_flame', 0x63F4, true, 'ignored'],
  0x58: ['map_jankens_castle', 0x1B8E, false, ''],
  0x60: ['cragg_lake_final_room', 0x3E28, false, ''],
  0x61: ['cragg_lake_room_variant', 0x3EBA, false, ''],
  0x62: ['alex_eating_rice_ball', 0x39DB, false, ''],
  0x63: ['punch_target_cc08', 0x3EFC, true, 'ignored'],
};
