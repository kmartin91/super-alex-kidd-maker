// Building parts learnt from a level's own screens: a port of
// maker/tools/parts.py. For each level (= theme: its graphics decide how
// metatiles look), derives:
//   eraser    the level's background metatile (sky, water...)
//   terrains  families of solid metatiles that the original screens place next
//             to each other, with an auto-tiling table: for each 4-neighbour mask
//             (bit 0 up, 1 right, 2 down, 3 left = same family), the metatile the
//             original level uses most for that situation
//   blocks    single special blocks: boxes, money, breakable rock, water, danger,
//             ladder, invisible floor...
//   stamps    decorations made of several metatiles (clouds, bushes, trees...)
// Only metatiles that the level itself uses are offered (they are the only ones
// guaranteed to have their graphics loaded), plus the item boxes, which every
// level loads.
//
// The results must match the Python ones exactly, ties included: dicts and
// Counters become Maps (insertion order), and the one Python set whose order
// matters is rebuilt with pySetOrder.

export const UP = 1, RIGHT = 2, DOWN = 4, LEFT = 8;
// Metatile 20 is the unbreakable rock in every level (a red ball, a grey stone...):
// levels stack it on their ground, but it is an object, not ground to paint.
export const ROCK = 20;

export const BLOCK_KINDS = [
  ['question_box', 'Boîte ?'], ['star_box', 'Boîte étoile (argent)'], ['skull_box', 'Boîte tête de mort'],
  ['money', 'Argent'], ['breakable', 'Roche cassable'], ['water', 'Eau'], ['deadly', 'Danger (mortel)'],
  ['ladder', 'Échelle'], ['ladder_top', 'Haut d\'échelle'],
];

// Map cells are keyed by one number (x, y may be -1 when looking at neighbours).
const key = (x, y) => (x + 256) * 65536 + (y + 256);
const keyX = (k) => Math.floor(k / 65536) - 256;
const keyY = (k) => (k % 65536) - 256;

// Iteration order of a CPython (3.7+) set of small non-negative ints built by
// adding `values` in order (hash(n) == n): open addressing with 9 linear probes,
// then perturbed probing; the table grows x4 when 3/5 full.
const EMPTY = -1;
const LINEAR_PROBES = 9;

function insertClean(table, mask, v) {
  let perturb = v;
  let i = v & mask;
  for (;;) {
    if (table[i] === EMPTY) {
      table[i] = v;
      return;
    }
    if (i + LINEAR_PROBES <= mask) {
      for (let j = 1; j <= LINEAR_PROBES; j++) {
        if (table[i + j] === EMPTY) {
          table[i + j] = v;
          return;
        }
      }
    }
    perturb = Math.floor(perturb / 32);
    i = (i * 5 + 1 + perturb) & mask;
  }
}

export function pySetOrder(values) {
  let mask = 7;
  let table = new Array(8).fill(EMPTY);
  let fill = 0;
  for (const v of values) {
    let i = v & mask, perturb = v, slot = -1, found = false;
    search: for (;;) {
      const probes = i + LINEAR_PROBES <= mask ? LINEAR_PROBES : 0;
      for (let j = 0; j <= probes; j++) {
        const e = table[i + j];
        if (e === EMPTY) {
          slot = i + j;
          break search;
        }
        if (e === v) {
          found = true;
          break search;
        }
      }
      perturb = Math.floor(perturb / 32);
      i = (i * 5 + 1 + perturb) & mask;
    }
    if (found) continue;
    table[slot] = v;
    fill++;
    if (fill * 5 < mask * 3) continue;
    const minused = fill > 50000 ? fill * 2 : fill * 4;
    let size = 8;
    while (size <= minused) size *= 2;
    const old = table;
    mask = size - 1;
    table = new Array(size).fill(EMPTY);
    for (const e of old) if (e !== EMPTY) insertClean(table, mask, e);
  }
  return table.filter((e) => e !== EMPTY);
}

// Counter helpers (a Counter is a Map value -> count, in insertion order).
function count(counter, v) {
  counter.set(v, (counter.get(v) || 0) + 1);
}

// most_common(): by count, ties in insertion order (sorted() is stable).
function mostCommon(counter) {
  return [...counter].sort((a, b) => b[1] - a[1]);
}

// most_common(1)[0][0]: the first value with the highest count.
function mostCommonOne(counter) {
  let best = null, bestN = -Infinity;
  for (const [v, n] of counter) {
    if (n > bestN) {
      best = v;
      bestN = n;
    }
  }
  return best;
}

// Python tuple comparison (numbers only).
function tupleLess(a, b) {
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return a[i] < b[i];
  }
  return false;
}

const popcount = (v) => {
  let n = 0;
  for (; v; v &= v - 1) n++;
  return n;
};

// Metatile ids of the level's reachable map, as a Map key(x, y) -> id, in cells.
export function levelGrid(level) {
  const grid = new Map();
  for (const cell of level.map) {
    const blocks = level.screens[cell.screen].metatiles;
    blocks.forEach((m, i) => grid.set(key(cell.x * 16 + (i % 16), cell.y * 12 + Math.floor(i / 16)), m));
  }
  return grid;
}

export function classesOf(model, level) {
  const entries = model.metatile_tables[level.descriptor.metatile_table].entries;
  return entries.map((e) => ('class' in e ? e.class : ''));
}

export function isSolidClass(c) {
  return c === 'solid' || c.startsWith('solid') || c.endsWith('solid') || c.split('(').pop().includes('solid');
}

export function learnParts(model, level) {
  const grid = levelGrid(level);
  const cls = classesOf(model, level);
  const used = new Map();
  for (const m of grid.values()) count(used, m);

  // Eraser: the most common non-solid, non-special background block.
  let best = null; // max((n, m))
  for (const [m, n] of used) {
    if (!['background', 'empty', 'water'].includes(cls[m])) continue;
    if (best === null || tupleLess(best, [n, m])) best = [n, m];
  }
  const eraser = best ? best[1] : 0;

  // Terrain families: solid blocks joined when they touch often enough.
  const solid = new Set(pySetOrder([...used.keys()].filter((m) => cls[m].includes('solid') && !cls[m].endsWith('_box') && m !== ROCK)));
  const parent = new Map([...solid].map((m) => [m, m]));
  const find = (a) => {
    while (parent.get(a) !== a) {
      parent.set(a, parent.get(parent.get(a)));
      a = parent.get(a);
    }
    return a;
  };

  const touch = new Map(); // (a, b) with a < b, as a * 256 + b -> count
  for (const [k, m] of grid) {
    if (!solid.has(m)) continue;
    const x = keyX(k), y = keyY(k);
    for (const [dx, dy] of [[1, 0], [0, 1]]) {
      const n = grid.get(key(x + dx, y + dy));
      if (n !== undefined && solid.has(n) && n !== m) count(touch, Math.min(m, n) * 256 + Math.max(m, n));
    }
  }
  for (const [pair, k] of touch) {
    if (k >= 2) {
      const rootB = find(pair % 256); // Python evaluates the right-hand side first
      parent.set(find(Math.floor(pair / 256)), rootB);
    }
  }
  const families = new Map();
  for (const m of solid) {
    const root = find(m);
    if (!families.has(root)) families.set(root, new Set());
    families.get(root).add(m);
  }

  const terrains = [];
  const sides = [[UP, 0, -1], [RIGHT, 1, 0], [DOWN, 0, 1], [LEFT, -1, 0]];
  for (const members of families.values()) {
    let total = 0;
    for (const m of members) total += used.get(m);
    if (total < 6) continue;
    const byMask = Array.from({ length: 16 }, () => new Map());
    for (const [k, m] of grid) {
      if (!members.has(m)) continue;
      const x = keyX(k), y = keyY(k);
      let mask = 0;
      for (const [bit, dx, dy] of sides) {
        const n = grid.get(key(x + dx, y + dy));
        if (n === undefined || members.has(n)) mask |= bit; // outside the map counts as "more of the same"
      }
      count(byMask[mask], m);
    }
    const table = new Map();
    for (let mask = 0; mask < 16; mask++) {
      if (byMask[mask].size) table.set(mask, mostCommonOne(byMask[mask]));
    }
    // Missing situations borrow the closest known one (fewest differing sides,
    // preferring to keep the top side, which usually carries grass or edges).
    // Masks filled here become candidates for the next ones, as in Python.
    for (let mask = 0; mask < 16; mask++) {
      if (table.has(mask)) continue;
      let bestMask = null, bestKey = null;
      for (const k of table.keys()) {
        let uses = 0;
        for (const n of byMask[k].values()) uses += n;
        const rank = [popcount(k ^ mask), (k ^ mask) & UP ? 1 : 0, -uses];
        if (bestKey === null || tupleLess(rank, bestKey)) {
          bestMask = k;
          bestKey = rank;
        }
      }
      table.set(mask, table.get(bestMask));
    }
    const fill = table.get(UP | RIGHT | DOWN | LEFT);
    const top = table.get(RIGHT | DOWN | LEFT);
    terrains.push({ members: [...members].sort((a, b) => a - b), tiles: Array.from({ length: 16 }, (_, m) => table.get(m)),
      preview: top, fill, count: total });
  }
  terrains.sort((a, b) => b.count - a.count);
  terrains.forEach((t, i) => { t.name = `Terrain ${i + 1}`; });

  // Single special blocks.
  const blocks = [];
  const allClasses = model.metatile_tables[level.descriptor.metatile_table].entries;
  const byUse = mostCommon(used).map(([m]) => m);
  for (const [kind, label] of BLOCK_KINDS) {
    let candidates = byUse.filter((m) => cls[m] === kind);
    if (!candidates.length && kind.endsWith('_box')) {
      // Boxes use tiles 1-36, loaded in every level.
      candidates = [];
      allClasses.forEach((e, i) => { if (e.class === kind) candidates.push(i); });
    }
    if (candidates.length) blocks.push({ kind, name: label, metatile: candidates[0] });
  }
  blocks.unshift({ kind: 'rock', name: 'Rocher', metatile: ROCK });

  // Decorations: connected groups of non-background, non-solid, non-special blocks.
  const decoOk = (m) => m !== eraser && cls[m] === 'background' && (used.get(m) || 0) > 0;
  const seen = new Set();
  const shapes = new Map(); // JSON of the cells -> { cells, n }, in first-seen order
  for (const [pos, m] of grid) {
    if (seen.has(pos) || !decoOk(m)) continue;
    const comp = [], stack = [pos];
    seen.add(pos);
    while (stack.length) {
      const p = stack.pop();
      comp.push(p);
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const q = key(keyX(p) + dx, keyY(p) + dy);
        if (!seen.has(q) && grid.has(q) && decoOk(grid.get(q))) {
          seen.add(q);
          stack.push(q);
        }
      }
    }
    const xs = comp.map(keyX), ys = comp.map(keyY);
    const x0 = Math.min(...xs), y0 = Math.min(...ys);
    const w = Math.max(...xs) - x0 + 1, h = Math.max(...ys) - y0 + 1;
    if (!(1 < comp.length && w <= 8 && h <= 8)) continue;
    const inComp = new Set(comp);
    const cells = Array.from({ length: h }, (_, j) => Array.from({ length: w }, (_, i) => {
      const q = key(x0 + i, y0 + j);
      return inComp.has(q) ? grid.get(q) : -1;
    }));
    const id = JSON.stringify(cells);
    if (shapes.has(id)) shapes.get(id).n += 1;
    else shapes.set(id, { cells, n: 1 });
  }
  // Keep decorations that look like objects: repeated in the level, or made of
  // several different blocks (a single repeated backdrop block is not one).
  const interesting = (c, n) => {
    const flat = c.flat().filter((m) => m >= 0);
    const kinds = new Set(flat).size;
    return kinds >= 2 && (n >= 2 || (flat.length >= 4 && kinds >= 3));
  };
  const filled = (c) => c.flat().filter((m) => m >= 0).length;
  const ranked = [...shapes.values()].filter(({ cells, n }) => interesting(cells, n))
    .sort((a, b) => (b.n - a.n) || (filled(b.cells) - filled(a.cells)));
  const stamps = ranked.slice(0, 12).map(({ cells, n }) => ({
    w: cells[0].length, h: cells.length, cells: cells.map((row) => row.slice()), count: n,
  }));
  return { eraser, terrains, blocks, stamps };
}
