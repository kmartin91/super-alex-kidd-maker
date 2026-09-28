// Estimated difficulty of a level, 1 to 5 stars (kept with the level, for
// sharing): danger per screen from enemies, bosses, traps (spikes, collapsing
// floors, flames, skull boxes), pits, deadly blocks and obstacles (walls to
// jump over, breakable walls, water), plus a little for length. Pure function
// of the level model.

const SCREEN_W = 16, SCREEN_H = 12;

// How dangerous each enemy type is (default 1).
const HARMLESS = new Set([0x50, 0x51, 0x52, 0x53, 0x63]);        // villager, prisoner, item, king, target
const TOUGH = new Set([0x25, 0x26, 0x27, 0x28, 0x46, 0x2a, 0x40]); // swordsman, bull, monkey, storm cloud
const BOSSES = new Set([0x1c, 0x1d, 0x1e, 0x1f]);
// Traps placed as entities: spike pillars and ceiling, collapsing floors, flames.
const TRAPS = new Set([0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x17, 0x3d, 0x57]);

function enemyWeight(type) {
  if (HARMLESS.has(type)) return 0;
  if (TRAPS.has(type)) return 1;
  return TOUGH.has(type) ? 1.2 : 0.6;
}

// Level-wide block columns of a horizontal level (row 0 of the grid).
function blockAt(m, gx, gy) {
  const cell = m.grid[0][Math.floor(gx / SCREEN_W)];
  return cell ? m.screens[cell.screen].blocks[gy * SCREEN_W + (gx % SCREEN_W)] : 0;
}

export function rateDifficulty(m) {
  const cls = (b) => m.metatileClasses[b] || '';
  const cells = m.grid.flat().filter(Boolean);
  const screens = Math.max(1, cells.length);

  let danger = 0;
  for (const list of m.entities) for (const e of list) danger += enemyWeight(e.type);
  for (const list of m.specials) for (const r of list) if (BOSSES.has(r.type)) danger += 3;

  // Blocks: deadly ones counted by column (a row of spikes is one hazard per
  // block wide), skull boxes (they free a ghost), breakable rocks (walls to
  // punch through) and water (swimming is slower and trickier).
  const deadlyColumns = new Set();
  let skulls = 0, breakable = 0, water = 0;
  for (const c of cells) {
    m.screens[c.screen].blocks.forEach((b, i) => {
      const k = cls(b);
      if (k.includes('deadly')) deadlyColumns.add(`${c.screen}:${i % SCREEN_W}`);
      else if (k === 'skull_box') skulls++;
      else if (k === 'breakable') breakable++;
      else if (k.includes('water')) water++;
    });
  }
  danger += deadlyColumns.size * 0.3 + skulls * 0.3 + breakable * 0.02 + (water / (SCREEN_W * SCREEN_H)) * 0.5;

  // Horizontal levels: pits (runs of columns with nothing to stand on in the
  // lower rows; Alex steps over a one-block gap, wide ones need a good jump)
  // and walls (the ground rising by 3 blocks or more: a jump to get right).
  if (m.kind === 'horizontal') {
    let run = 0, lastTop = null;
    const width = m.grid[0].length * SCREEN_W;
    const endPit = () => { if (run) danger += run === 1 ? 0.3 : run <= 3 ? 1.2 : 2; run = 0; };
    for (let gx = 0; gx < width; gx++) {
      let top = null;
      for (let gy = 0; gy < SCREEN_H && top === null; gy++) if (cls(blockAt(m, gx, gy)).includes('solid')) top = gy;
      const ground = top !== null && top >= SCREEN_H - 4 - 4; // something to stand on in the lower two thirds
      if (ground && lastTop !== null && lastTop - top >= 3) danger += 0.4;
      if (ground) { endPit(); lastTop = top; } else run++;
    }
    endPit();
  }

  let perScreen = danger / screens + Math.min(screens, 20) * 0.03;
  // The time limit (challenge.js): a tight clock.
  const clear = m.clear || {};
  if (clear.time) perScreen += Math.max(0, 0.9 - clear.time / (screens * 25));
  // Calibrated on the game's 17 levels: the first ones get 2 stars, the
  // middle 3, the late ones 4 or 5.
  const thresholds = [0.8, 2.1, 3.0, 3.9];
  let stars = 1;
  for (const t of thresholds) if (perScreen >= t) stars++;
  return { stars, perScreen: Math.round(perScreen * 100) / 100 };
}

export const starsText = (n) => '★'.repeat(n) + '☆'.repeat(5 - n);
