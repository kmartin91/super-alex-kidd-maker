// Everything the server used to do, now in the browser: the user's ROM and
// saved levels live in IndexedDB, levels are decoded and mods built by
// rom/leveledit.js, graphics are captured by the game itself (capture.js).

import { dbGet, dbSet, dbDelete, dbKeys } from './db.js';
import { useRom, levelVideo, entityIcons as captureIcons } from './capture.js';
import { listLevels, exportLevel, iconTypes, retheme as rethemeModel, buildMod } from './rom/leveledit.js';
import { opponentMessage, decodeMessage, messagePatch, wrapText } from './rom/text.js';

// Alex Kidd in Miracle World, USA/Europe, revision 0.
const ROM_CRC32 = 0x17A40E29;

let rom = null, levels = null;

function crc32(bytes) {
  let c, crc = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) {
    c = (crc ^ bytes[i]) & 0xFF;
    for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xEDB88320 : c >>> 1;
    crc = (crc >>> 8) ^ c;
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

async function activate(bytes) {
  rom = bytes;
  levels = listLevels(rom);
  await useRom(rom, ROM_CRC32.toString(16));
  await migrate();
}

// Loads the ROM kept in this browser; false when there is none yet.
export async function initBackend() {
  const saved = await dbGet('rom');
  if (!saved) return false;
  await activate(saved);
  return true;
}

// Checks and keeps the user's ROM. Throws with a readable message.
export async function setRom(bytes) {
  // Some dumps carry a 512-byte copier header.
  if (bytes.length % 0x4000 === 512) bytes = bytes.slice(512);
  if (crc32(bytes) !== ROM_CRC32) {
    throw new Error('Ce n\'est pas la bonne ROM : il faut Alex Kidd in Miracle World, version USA/Europe (révision 0).');
  }
  await dbSet('rom', bytes);
  await activate(bytes);
}

export const romBytes = () => rom;
export const getLevels = () => levels;

// ------------------------------------------------------------ my levels
// Every level of the Maker stands alone: { id, name, base, model, updated }.
// `base` is the level of the game whose place it takes when played (the
// model's `theme` gives its graphics); only that level is changed then.

const docKey = (id) => `mylevel:${id}`;

// The game's level N as a starting point (not saved).
export async function templateLevel(n) {
  const model = exportLevel(rom, n);
  return { model, video: await levelVideo(n) };
}

// A copy of level `base` with the graphics, enemies and music of `theme`.
// Alex's start in level `base` of the game (levels saved before it could be moved).
export function defaultStart(base) {
  return exportLevel(rom, base).start;
}

export async function themedLevel(base, theme) {
  const model = base === theme ? exportLevel(rom, base) : rethemeModel(rom, exportLevel(rom, base), theme);
  return { model, video: await levelVideo(theme) };
}

export async function listMyLevels() {
  const docs = [];
  for (const k of await dbKeys()) if (String(k).startsWith('mylevel:')) docs.push(await dbGet(k));
  return docs.sort((a, b) => b.updated - a.updated)
    .map(({ id, name, base, updated, model, difficulty }) => ({ id, name, base, updated, difficulty, theme: model.theme || base }));
}

export async function openMyLevel(id) {
  const doc = await dbGet(docKey(id));
  if (!doc) throw new Error('niveau introuvable');
  return { doc, video: await levelVideo(doc.model.theme || doc.base) };
}

// Saves (creating it when `doc.id` is missing); refuses what the game cannot hold.
export async function saveMyLevel(doc) {
  buildMod(rom, { [doc.base]: doc.model });
  const saved = { ...doc, id: doc.id || `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`, updated: Date.now() };
  await dbSet(docKey(saved.id), saved);
  return saved;
}

export const deleteMyLevel = (id) => dbDelete(docKey(id));

export async function retheme(base, model, theme) {
  return { model: rethemeModel(rom, model, theme), video: await levelVideo(theme) };
}

// Maker block (engine/src/rt/maker.h): the level each enemy comes from, so
// that the engine draws enemies from other settings with their own tiles.
const MAKER_BLOCK_OFFSET = 0x7FE00;

// Janken opponents: the levels that load their sprites.
const BOSS_LEVELS = { 0x1c: [16], 0x1d: [2, 11], 0x1e: [7, 12], 0x1f: [10, 15] };

const ascii = (text) => [...text].map((c) => c.charCodeAt(0));

// Janken opponents of the level set up in the editor (text, throws), the
// first one of each opponent: [opponent index (data >> 1), record].
function customBosses(model) {
  const found = new Map();
  for (const recs of model.specials) {
    for (const r of recs) {
      if (!(r.type in BOSS_LEVELS) || found.has(r.data >> 1)) continue;
      if ((r.text && r.text.trim()) || (r.moves && r.moves.length)) found.set(r.data >> 1, r);
    }
  }
  return found;
}

function makerBlock(model, base) {
  const theme = model.theme || base;
  const block = new Uint8Array(8 + 256 + 8 + 4 * 16);
  block.set(ascii('AKMAKER1'));
  const types = model.entityTypes.map((t) => [t.id, t.levels || []]).concat(Object.entries(BOSS_LEVELS).map(([t, l]) => [Number(t), l]));
  for (const [type, levels] of types) if (levels.length && !levels.includes(theme)) block[8 + type] = levels[0];
  // The throws of the janken opponents (engine/src/rt/maker.h).
  block.set(ascii('AKJANKEN'), 264);
  for (const [i, r] of customBosses(model)) {
    const moves = (r.moves || []).slice(0, 15);
    block[272 + 16 * i] = moves.length;
    moves.forEach((m, k) => { block[272 + 16 * i + 1 + k] = m; });
  }
  return block;
}

// Text a janken opponent (its data) says before the match: {lines, size}.
export function bossText(data) {
  return decodeMessage(rom, opponentMessage(rom, data));
}

// Adds one "write these bytes at this ROM offset" record to an AKMOD1 patch.
function appendRecord(patch, offset, bytes) {
  const out = new Uint8Array(patch.length + 8 + bytes.length);
  out.set(patch);
  const view = new DataView(out.buffer);
  view.setUint32(patch.length, offset, true);
  view.setUint32(patch.length + 4, bytes.length, true);
  out.set(bytes, patch.length + 8);
  return out;
}

// Mod patch to play `model` in place of level `base`; startColumn starts it
// at that screen.
export function testPatch(base, model, startColumn = null) {
  let { patch } = buildMod(rom, { [base]: model }, startColumn === null ? null : { level: base, column: startColumn });
  // The janken opponents' own texts, written over the game's.
  for (const r of customBosses(model).values()) {
    if (r.text && r.text.trim()) patch = appendRecord(patch, ...messagePatch(rom, opponentMessage(rom, r.data), wrapText(r.text)));
  }
  return appendRecord(patch, MAKER_BLOCK_OFFSET, makerBlock(model, base));
}

// One level as a file, and back (also accepts the files of the earlier
// Maker: several levels, or one level_NN.json).
export function levelFile(doc) {
  return { format: 'super-alex-kidd-maker/level', version: 1, name: doc.name, base: doc.base, difficulty: doc.difficulty, model: doc.model };
}

export async function importLevelFile(data) {
  let docs;
  if (data && data.format === 'super-alex-kidd-maker/level') docs = [{ name: data.name, base: data.base, difficulty: data.difficulty, model: data.model }];
  else if (data && data.format === 'super-alex-kidd-maker/levels') {
    docs = Object.entries(data.levels).map(([n, model]) => ({ name: `Niveau ${n} modifié`, base: Number(n), model }));
  } else if (data && Number.isInteger(data.level) && Array.isArray(data.screens)) {
    docs = [{ name: `Niveau ${data.level} modifié`, base: data.level, model: data }];
  } else throw new Error('Ce fichier ne contient pas de niveau du Maker.');
  const saved = [];
  for (const d of docs) saved.push(await saveMyLevel(d));
  return saved;
}

// Levels saved by the earlier Maker (one per level of the game) become levels
// of their own, once.
async function migrate() {
  for (const k of await dbKeys()) {
    const m = /^level:(\d+)$/.exec(k);
    if (!m) continue;
    const model = await dbGet(k);
    try { await saveMyLevel({ name: `Niveau ${m[1]} modifié`, base: Number(m[1]), model }); } catch { /* kept as is */ continue; }
    await dbDelete(k);
  }
}

export function entityIcons(progress) {
  const bosses = Object.entries(BOSS_LEVELS).map(([t, l]) => `${t}:${l[0]}`);
  return captureIcons(iconTypes(rom).concat(bosses), progress);
}
