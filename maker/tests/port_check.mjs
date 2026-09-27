// Compares the JavaScript port of the level tools (maker/public/js/rom/) with the
// Python originals (maker/tools/leveledit.py, levels.py, parts.py): same JSON for
// list/export/icontypes/retheme, byte-identical mod patches for build, same error
// messages. Run from the repository root (needs python3 and original.sms):
//   node maker/tests/port_check.mjs
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// maker/public/js has no package.json saying "type": "module": keep Node's
// warning about re-parsing the port as ES modules out of the report.
process.removeAllListeners('warning');
process.on('warning', (w) => { if (w.code !== 'MODULE_TYPELESS_PACKAGE_JSON') console.warn(w); });
const editor = await import('../public/js/rom/leveledit.js');
const levels = await import('../public/js/rom/levels.js');

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const ROM_PATH = path.join(ROOT, 'original.sms');
const TOOLS = path.join(ROOT, 'maker', 'tools');
const TOOL = path.join(TOOLS, 'leveledit.py');
const PYTHON = process.env.PYTHON || 'python3';
const rom = new Uint8Array(fs.readFileSync(ROM_PATH));
const romCrc = levels.crc32(rom);
const TMP = fs.mkdtempSync(path.join(os.tmpdir(), 'akmaker-port-'));

let passed = 0, failed = 0;
const failures = [];
function check(name, ok, detail = '') {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ' — ' + detail : ''}`);
  if (ok) passed++;
  else {
    failed++;
    failures.push(name);
  }
}

function python(args) {
  const r = spawnSync(PYTHON, args, { cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 28 });
  if (r.error) throw r.error;
  return r;
}

function pyJson(args) {
  const r = python([TOOL, ...args]);
  if (r.status !== 0) throw new Error(`leveledit.py ${args[0]} failed:\n${r.stderr}`);
  return JSON.parse(r.stdout);
}

// Runs a Python snippet with maker/tools on the path; returns its JSON output.
function pyScript(code) {
  const r = python(['-c', `import sys, json\nsys.path.insert(0, ${JSON.stringify(TOOLS)})\n${code}`]);
  if (r.status !== 0) throw new Error(`python snippet failed:\n${r.stderr}`);
  return JSON.parse(r.stdout);
}

// "ValueError: message" from the last line of a traceback.
function pyError(stderr) {
  const last = stderr.trim().split('\n').pop();
  const m = /^([\w.]+): (.*)$/.exec(last);
  return m ? { type: m[1], message: m[2] } : { type: '?', message: last };
}

// Path of the first difference between two JSON values (null when equal).
function diff(a, b, where = '$') {
  if (typeof a !== typeof b || Array.isArray(a) !== Array.isArray(b) || (a === null) !== (b === null)) {
    return `${where}: ${JSON.stringify(a)?.slice(0, 80)} vs ${JSON.stringify(b)?.slice(0, 80)}`;
  }
  if (Array.isArray(a)) {
    if (a.length !== b.length) return `${where}: length ${a.length} vs ${b.length}`;
    for (let i = 0; i < a.length; i++) {
      const d = diff(a[i], b[i], `${where}[${i}]`);
      if (d) return d;
    }
    return null;
  }
  if (a !== null && typeof a === 'object') {
    const ka = Object.keys(a).sort(), kb = Object.keys(b).sort();
    if (ka.join('\n') !== kb.join('\n')) return `${where}: keys ${ka} vs ${kb}`;
    for (const k of ka) {
      const d = diff(a[k], b[k], `${where}.${k}`);
      if (d) return d;
    }
    return null;
  }
  return a === b ? null : `${where}: ${JSON.stringify(a)} vs ${JSON.stringify(b)}`;
}

const json = (v) => JSON.parse(JSON.stringify(v));
const copy = (v) => JSON.parse(JSON.stringify(v));

function compare(name, want, got) {
  const d = diff(want, json(got));
  check(name, !d, d || '');
}

// Deterministic pseudo-random numbers (mulberry32).
function rng(seed) {
  return () => {
    seed = (seed + 0x6D2B79F5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// ------------------------------------------------------------ 0. decoded model
console.log('== levels.load (Python model without the VRAM "graphics" entry)');
{
  const want = pyScript(`import levels
m = levels.load(open(${JSON.stringify(ROM_PATH)}, 'rb').read())
for l in m['levels']:
    del l['graphics']
json.dump(m, sys.stdout)`);
  compare('levels.load model', want, levels.load(rom));
  const model = levels.load(rom);
  const bad = levels.encode(model).filter(([off, b]) => !levels.bytesEqual(rom.subarray(off, off + b.length), b));
  check('levels.encode(load(rom)) reproduces the ROM', bad.length === 0, bad.length ? `${bad.length} blocks differ` : '');
}

// ------------------------------------------------------------ 1. list
console.log('== list');
const list = pyJson(['list', ROM_PATH]);
compare('list', list, editor.listLevels(rom));

// ------------------------------------------------------------ 2. export
console.log('== export');
const exported = {};
for (let lv = 1; lv <= 17; lv++) {
  exported[lv] = pyJson(['export', ROM_PATH, String(lv)]);
  compare(`export ${lv}`, exported[lv], editor.exportLevel(rom, lv));
}
{
  const a = editor.exportLevel(rom, 2);
  a.screens[0].blocks[0] = 999;
  a.specials.flat().forEach((r) => { r.x = 999; });
  compare('export returns fresh objects (cache untouched)', exported[2], editor.exportLevel(rom, 2));
}

// ------------------------------------------------------------ 3. icontypes
console.log('== icontypes');
{
  const r = python([TOOL, 'icontypes', ROM_PATH]);
  compare('icontypes', r.stdout.trim().split(' '), editor.iconTypes(rom));
}

// ------------------------------------------------------------ 4. retheme
console.log('== retheme');
const rethemed = {};
for (const [lv, theme] of [[2, 7], [7, 2], [6, 14], [12, 10], [1, 17], [11, 3]]) {
  const file = path.join(TMP, `retheme_${lv}.json`);
  fs.writeFileSync(file, JSON.stringify(exported[lv]));
  const want = pyJson(['retheme', ROM_PATH, file, String(theme)]);
  const input = copy(exported[lv]);
  const got = editor.retheme(rom, input, theme);
  compare(`retheme ${lv} -> ${theme}`, want, got);
  check(`retheme ${lv} -> ${theme} leaves its argument untouched`, !diff(exported[lv], input));
  rethemed[`${lv}:${theme}`] = want;
}
{
  // Every (level, theme) pair, in one Python process.
  const want = pyScript(`import levels, leveledit
rom = open(${JSON.stringify(ROM_PATH)}, 'rb').read()
out = {}
for lv in range(1, 18):
    for t in range(1, 18):
        m = levels.load(rom)
        out['%d:%d' % (lv, t)] = leveledit.retheme(m, leveledit.export_level(m, lv), t)
json.dump(out, sys.stdout)`);
  let bad = null;
  for (let lv = 1; lv <= 17 && !bad; lv++) {
    for (let t = 1; t <= 17 && !bad; t++) {
      const d = diff(want[`${lv}:${t}`], json(editor.retheme(rom, exported[lv], t)));
      if (d) bad = `${lv}:${t} ${d}`;
    }
  }
  check('retheme, all 17 x 17 (level, theme) pairs', !bad, bad || '');
}

// ------------------------------------------------------------ 5. build
console.log('== build');
let buildCount = 0;

// Builds `edited` ({level: model}) with both tools. With `error` (true for a
// ValueError, else the Python exception name), both must fail with the same message.
function buildBoth(name, edited, start = null, error = false) {
  const dir = path.join(TMP, `mod${++buildCount}`);
  fs.mkdirSync(dir);
  for (const [lv, m] of Object.entries(edited)) {
    fs.writeFileSync(path.join(dir, `level_${String(lv).padStart(2, '0')}.json`), JSON.stringify(m));
  }
  const out = path.join(TMP, `mod${buildCount}.bin`);
  const args = [TOOL, 'build', ROM_PATH, dir, out];
  if (start) args.push('--start', `${start.level}:${start.column}`);
  const r = python(args);
  const before = JSON.stringify(edited);
  let js = null, jsError = null;
  try {
    js = editor.buildMod(rom, edited, start);
  } catch (e) {
    jsError = e;
  }
  const untouched = JSON.stringify(edited) === before;
  if (r.status !== 0) {
    const pe = pyError(r.stderr);
    const same = jsError !== null && jsError.message === pe.message && pe.type === (error === true ? 'ValueError' : error);
    check(`${name}${error ? '' : ' (unexpected error)'}`, same && error && untouched,
      `python ${pe.type}: ${pe.message} / js: ${jsError ? jsError.message : 'no error'}`);
    return null;
  }
  if (jsError) {
    check(name, false, `python built it, js threw: ${jsError.stack}`);
    return null;
  }
  const want = new Uint8Array(fs.readFileSync(out));
  const info = JSON.parse(r.stdout);
  const same = levels.bytesEqual(want, js.patch);
  const infoDiff = diff(info, json(js.info));
  check(name, same && !infoDiff && untouched && !error,
    [error ? 'expected an error' : '', same ? '' : `patch differs (${want.length} vs ${js.patch.length} bytes)`,
      infoDiff || '', untouched ? '' : 'input mutated', `${info.records} records, ${info.bytes} bytes`]
      .filter(Boolean).join('; '));
  return info;
}

const model = levels.load(rom);
const kindOf = (lv) => model.levels[lv - 1].kind;
// The horizontal ones: the scenarios below grow or shrink their row of screens.
const canExtend = (lv) => list[lv - 1].canExtend && exported[lv].kind === 'horizontal';
const extendableLevels = list.filter((l) => canExtend(l.level)).map((l) => l.level);
const boxes = (lv) => editor.countQuestionBoxes(model, model.levels[lv - 1]);

// a. each level exported unmodified, built alone; then all together
for (let lv = 1; lv <= 17; lv++) buildBoth(`a. level ${lv} (${kindOf(lv)}) unmodified`, { [lv]: exported[lv] });
buildBoth('a. all 17 levels unmodified together', Object.fromEntries(Object.entries(exported).map(([k, m]) => [k, m])));

// b. one block changed in a screen
for (const lv of [1, 2, 3, 11, 13, 16, 17]) {
  const m = copy(exported[lv]);
  const s = m.grid.flat().find(Boolean).screen;
  m.screens[s].blocks[5 * 16 + 7] = m.parts.blocks.length ? m.parts.blocks[0].metatile : (m.screens[s].blocks[80] + 1) & 0xFF;
  buildBoth(`b. level ${lv} (${kindOf(lv)}): one block changed`, { [lv]: m });
}

// c. an enemy added to a non-start screen list
for (const lv of [2, 8, 1, 3]) {
  const m = copy(exported[lv]);
  const t = m.entityTypes.find((e) => e.levels.includes(lv)) || m.entityTypes[0];
  m.entities[2].push({ type: t.id, x: 120, y: 96, data: 0 });
  buildBoth(`c. level ${lv}: enemy $${t.id.toString(16)} added to list 2`, { [lv]: m });
}

// d. custom surprises
const boxLevels = [];
for (let lv = 1; lv <= 17; lv++) if (boxes(lv)) boxLevels.push(lv);
console.log(`   (levels with question boxes: ${boxLevels.map((lv) => `${lv}:${boxes(lv)}`).join(' ')})`);
const surprises = (n, k = 0) => Array.from({ length: n }, (_, i) => [0x4D, 0x4E, 0x4F][(i + k) % 3]);
for (const lv of boxLevels.slice(0, 3)) {
  const m = copy(exported[lv]);
  m.surprises = surprises(boxes(lv));
  buildBoth(`d. level ${lv}: custom surprises (${boxes(lv)} boxes)`, { [lv]: m });
}
{
  const [a, b] = boxLevels;
  const ma = copy(exported[a]), mb = copy(exported[b]);
  ma.surprises = surprises(boxes(a), 1);
  mb.surprises = surprises(boxes(b), 2);
  buildBoth(`d. levels ${a} and ${b}: custom surprises together`, { [a]: ma, [b]: mb });
}

// e. rethemed levels
for (const [k, m] of Object.entries(rethemed)) buildBoth(`e. rethemed ${k.replace(':', ' -> ')}`, { [k.split(':')[0]]: m });
{
  const m = copy(rethemed['2:7']);
  m.themeMusic = false;
  buildBoth('e. rethemed 2 -> 7, own music', { 2: m });
}

// f. emptied extendable levels, as new-level.js does, with the rice ball at the end
function emptyLevel(m, ground = false) {
  for (const s of m.screens) s.blocks.fill(m.parts.eraser);
  m.grid = [Array.from({ length: 3 }, (_, i) => ({ screen: i, entities: i }))];
  m.rows = 1;
  m.columns = 3;
  m.entities = Array.from({ length: 3 }, () => []);
  m.specials = Array.from({ length: 3 }, () => []);
  m.surprises = null;
  if (ground && m.parts.terrains.length) {
    for (let i = 0; i < 3; i++) m.screens[i].blocks.fill(m.parts.terrains[0].fill, 160);
  }
  m.specials[2].push({ kind: 'fixed_slot', code: 0x84, type: 0x44, data: 0, x: 200, y: 144 });
  return m;
}
for (const lv of extendableLevels) {
  // Level 14 has only 2 screens: the 3-screen grid points at a missing screen,
  // and leveledit.py stops with an IndexError.
  const short = exported[lv].screens.length < 3;
  buildBoth(`f. level ${lv}: emptied new level${short ? ` (${exported[lv].screens.length} screens: fails)` : ''}`,
    { [lv]: emptyLevel(copy(exported[lv])) }, null, short ? 'IndexError' : false);
}
buildBoth(`f. level ${extendableLevels[1]}: emptied new level with ground`, { [extendableLevels[1]]: emptyLevel(copy(exported[extendableLevels[1]]), true) });

// f2. vertical level 1 reshaped: 4 screens down, then one more at the bottom
{
  const m = copy(exported[1]);
  const E = m.entities, Sp = m.specials;
  m.grid = [[{ screen: 0, entities: 0 }, null], [{ screen: 1, entities: 1 }, null], [{ screen: 2, entities: 2 }, null],
    [{ screen: 9, entities: 3 }, { screen: 10, entities: 4 }]];
  m.rows = 4; m.columns = 2;
  m.entities = [0, 1, 2, 9, 10].map((i) => E[i] || []);
  m.specials = [0, 1, 2, 9, 10].map((i) => Sp[i] || []);
  buildBoth('f2. level 1: vertical shape changed', { 1: m });
}

// g. screens inserted / removed, as screens.js does
function renumber(m) {
  m.grid[0].forEach((c, i) => { c.entities = i; });
  m.columns = m.grid[0].length;
}
function addScreen(m, col) {
  const src = m.screens[m.grid[0][col].screen];
  m.screens.push({ blocks: src.blocks.slice() });
  m.grid[0].splice(col + 1, 0, { screen: m.screens.length - 1, entities: 0 });
  m.entities.splice(col + 1, 0, []);
  m.specials.splice(col + 1, 0, []);
  renumber(m);
  return m;
}
function removeScreen(m, col) {
  const target = col > 0 ? col - 1 : 1;
  m.specials[target].push(...m.specials[col]);
  m.grid[0].splice(col, 1);
  m.entities.splice(col, 1);
  m.specials.splice(col, 1);
  renumber(m);
  return m;
}
buildBoth('g. level 7: screen inserted after column 2', { 7: addScreen(copy(exported[7]), 2) });
buildBoth('g. level 12: screen inserted at the end', { 12: addScreen(copy(exported[12]), exported[12].columns - 1) });
{
  const m = copy(exported[14]);
  for (let k = 0; k < 5; k++) addScreen(m, k);
  m.entities[3].push({ type: 0x2A, x: 64, y: 64, data: 0 });
  buildBoth('g. level 14: five screens inserted, enemy on a new one', { 14: m });
}
buildBoth('g. level 10: screen 1 removed', { 10: removeScreen(copy(exported[10]), 1) });
// leveledit.py refuses this one: screen 0, no longer in the row, sits right after the
// row table, and the decode-back check then reads it as more table entries.
buildBoth('g. level 10: first screen removed (refused by both)', { 10: removeScreen(copy(exported[10]), 0) }, null, true);
{
  const m = addScreen(copy(exported[15]), 0);
  m.screens[m.screens.length - 1].blocks[100] = m.parts.eraser;
  buildBoth('g. level 15: copied screen then edited', { 15: m });
}
{
  // Many incompressible screens: still fits one bank.
  const rand = rng(7);
  const m = copy(exported[6]);
  for (let k = 0; k < 40; k++) {
    addScreen(m, m.columns - 1);
    m.screens[m.screens.length - 1].blocks = Array.from({ length: 192 }, () => Math.floor(rand() * 256));
  }
  buildBoth('g. level 6: 40 random screens appended', { 6: m });
}

// h. two levels edited together
{
  const m3 = copy(exported[3]), m4 = copy(exported[4]);
  m3.screens[0].blocks[20] = m3.parts.eraser;
  m4.screens[1].blocks[30] = m4.parts.eraser;
  buildBoth('h. levels 3 + 4 (sub-area layout copy)', { 3: m3, 4: m4 });
  buildBoth('h. levels 3 alone, edited', { 3: m3 });
  buildBoth('h. levels 2 + 17', { 2: emptyLevel(copy(exported[2])), 17: exported[17] });
  buildBoth('h. levels 7 + 12 (inserted screen + retheme)', { 7: addScreen(copy(exported[7]), 1), 12: rethemed['12:10'] });
}

// i. --start LEVEL:COLUMN test builds
buildBoth('i. start 6:3, level 6 not edited', {}, { level: 6, column: 3 });
buildBoth('i. start 8:2, level 8 edited', { 8: addScreen(copy(exported[8]), 0) }, { level: 8, column: 2 });
buildBoth('i. start 1:0 (vertical level, start unchanged)', {}, { level: 1, column: 0 });
buildBoth('i. start 2:1 with level 3 edited', { 3: exported[3] }, { level: 2, column: 1 });
buildBoth('i. start 12:99 (column out of range)', { 12: exported[12] }, { level: 12, column: 99 });

// i3. levels that start on a vehicle
for (const vehicle of ['bike', 'boat', 'peticopter']) {
  buildBoth(`i3. level 2 on the ${vehicle}`, { 2: { ...copy(exported[2]), vehicle } });
}
buildBoth('i3. level 1 (vertical) on the peticopter, rethemed', { 1: { ...copy(rethemed['1:5'] || exported[1]), vehicle: 'peticopter' } });

// i4. a bonus zone: one more layout row, its entity lists after the level's
{
  const m = copy(exported[2]);
  const blank = m.screens[0].blocks.map(() => m.parts.eraser);
  m.screens.push({ blocks: blank.slice() }, { blocks: blank.slice() });
  const s0 = m.screens.length - 2;
  m.zone = { grid: [[{ screen: s0, entities: 0 }, { screen: s0 + 1, entities: 1 }]], columns: 2, rows: 1,
    entities: [[{ type: m.entityTypes[0].id, x: 100, y: 120, data: 0 }], []], specials: [[], []], start: { col: 0, row: 0, x: 32, y: 100 } };
  m.entities[0].push({ type: 0x4C, x: 200, y: 136, data: 0 });
  m.zone.entities[1].push({ type: 0x4C, x: 200, y: 136, data: 1 });
  buildBoth('i4. level 2 with a bonus zone of 2 screens', { 2: m });
}

// i2. Alex's start position chosen in the editor
{
  const m2 = copy(exported[2]);
  m2.start = { col: 3, row: 0, x: 80, y: 120 };
  buildBoth('i2. level 2: start moved to screen 4', { 2: m2 });
  const m1 = copy(exported[1]);
  m1.start = { col: 0, row: 0, x: 200, y: 40 };
  buildBoth('i2. level 1: start moved in the top screen', { 1: m1 });
  const m7 = copy(exported[7]);
  m7.start = { col: 99, row: 0, x: 48, y: 136 };
  buildBoth('i2. level 7: start column out of range, pixel kept', { 7: m7 });
}

// j. random edits (seeded): blocks, entities, specials, screens, surprises,
// themes, several levels at once, start positions.
{
  const rand = rng(2024);
  const pick = (list) => list[Math.floor(rand() * list.length)];
  const int = (n) => Math.floor(rand() * n);
  for (let round = 0; round < 40; round++) {
    const edited = {};
    const count = 1 + int(3);
    for (let k = 0; k < count; k++) {
      const lv = 1 + int(17);
      const m = copy(exported[lv]);
      const used = [...new Set(m.screens.flatMap((s) => s.blocks))];
      for (let n = int(30); n > 0; n--) pick(m.screens).blocks[int(192)] = rand() < 0.8 ? pick(used) : int(256);
      if (canExtend(lv) && rand() < 0.5) {
        for (let n = 1 + int(3); n > 0; n--) {
          if (rand() < 0.7) addScreen(m, int(m.columns));
          else if (m.columns > 2) removeScreen(m, int(m.columns));
        }
      }
      for (let n = int(4); n > 0; n--) {
        const t = pick(m.entityTypes);
        pick(m.entities).push({ type: t.id, x: int(256), y: int(256), data: int(4) });
      }
      if (rand() < 0.3) {
        const lists = m.entities.filter((l) => l.length);
        if (lists.length) pick(lists).pop();
      }
      const specials = m.specials.flat().filter((r) => 'x' in r);
      if (specials.length && rand() < 0.3) pick(specials).x = int(256);
      if (boxes(lv) && rand() < 0.3) m.surprises = surprises(boxes(lv), int(3));
      if (rand() < 0.3) m.theme = 1 + int(17);
      if (rand() < 0.2) m.themeMusic = false;
      edited[lv] = m;
    }
    const start = rand() < 0.3 ? { level: 1 + int(17), column: int(6) } : null;
    const name = `j. random ${round}: levels ${Object.keys(edited).join('+')}${start ? `, start ${start.level}:${start.column}` : ''}`;
    // Random edits may be refused (e.g. too many entities); both tools must agree.
    const dir = path.join(TMP, `probe${round}`);
    fs.mkdirSync(dir);
    for (const [lv, m] of Object.entries(edited)) fs.writeFileSync(path.join(dir, `level_${String(lv).padStart(2, '0')}.json`), JSON.stringify(m));
    const args = [TOOL, 'build', ROM_PATH, dir, path.join(TMP, `probe${round}.bin`)];
    if (start) args.push('--start', `${start.level}:${start.column}`);
    const r = python(args);
    buildBoth(name, edited, start, r.status === 0 ? false : pyError(r.stderr).type === 'ValueError' ? true : pyError(r.stderr).type);
  }
}

// Invalid edits: same ValueError message.
{
  const horizontalFixed = list.find((l) => kindOf(l.level) === 'horizontal' && !l.canExtend);
  const m1 = copy(exported[2]);
  m1.screens.pop();
  buildBoth('error: screen removed from the list', { 2: m1 }, null, true);
  const m2 = copy(exported[2]);
  m2.screens[0].blocks.pop();
  buildBoth('error: screen with 191 blocks', { 2: m2 }, null, true);
  if (horizontalFixed) {
    const m3 = copy(exported[horizontalFixed.level]);
    m3.grid[0].reverse();
    buildBoth(`error: layout of level ${horizontalFixed.level} changed`, { [horizontalFixed.level]: m3 }, null, true);
  }
  const m4 = copy(exported[17]); // level 1 can change shape now; 17 cannot
  m4.entities.pop();
  m4.specials.pop();
  buildBoth('error: entity list removed from a fixed level', { 17: m4 }, null, true);
  const m5 = copy(exported[2]);
  m5.grid = [[m5.grid[0][0]]];
  buildBoth('error: one-screen level', { 2: m5 }, null, true);
  const m6 = copy(exported[boxLevels[0]]);
  m6.surprises = surprises(80);
  buildBoth('error: too many surprises', { [boxLevels[0]]: m6 }, null, true);
  const m7 = copy(exported[2]);
  m7.entities[1] = Array.from({ length: 128 }, (_, i) => ({ type: 0x2A, x: i, y: 50, data: 0 }));
  buildBoth('error: 128 entities in one screen', { 2: m7 }, null, true);
  const m8 = copy(exported[2]);
  m8.entities = m8.entities.map((_, k) => Array.from({ length: 100 }, (__, i) => ({ type: 0x2A, x: i, y: k, data: 0 })));
  buildBoth('error: bank 2 full of entities', { 2: m8 }, null, true);
  const rand = rng(3);
  const m9 = copy(exported[10]);
  for (let k = 0; k < 90; k++) {
    addScreen(m9, m9.columns - 1);
    m9.screens[m9.screens.length - 1].blocks = Array.from({ length: 192 }, () => Math.floor(rand() * 256));
  }
  buildBoth('error: level larger than a bank', { 10: m9 }, null, true);
}

check('the ROM bytes were not modified', levels.crc32(rom) === romCrc);
fs.rmSync(TMP, { recursive: true, force: true });

console.log(`\n${passed} passed, ${failed} failed${failed ? ':\n  ' + failures.join('\n  ') : ''}`);
process.exit(failed ? 1 : 0);
