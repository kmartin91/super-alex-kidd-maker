// Adding and removing screens (simple horizontal levels only), around the
// screen in the middle of the view.

import { $ } from './dom.js';
import { state, SCREEN_PX_W } from './state.js';
import { viewCell } from './level.js';
import { pushUndo } from './history.js';
import { render } from './render.js';
import { setTip } from './tooltip.js';
import { toast } from './toast.js';
import { ask } from './modal.js';

// Horizontal levels: entity list index = column.
function renumberColumns() {
  state.model.grid[0].forEach((c, i) => { c.entities = i; });
  state.model.columns = state.model.grid[0].length;
}

function scrollToColumn(col) {
  const wrap = $('mapWrap');
  wrap.scrollLeft = (col + 0.5) * SCREEN_PX_W * state.zoom - wrap.clientWidth / 2;
}

// Inserts a copy of the screen in view right after it.
function addScreen() {
  if (!state.model.canExtend) { toast('Ce niveau ne peut pas être rallongé'); return; }
  if (state.model.kind === 'vertical') { addScreenVertical(); return; }
  const { col } = viewCell();
  pushUndo();
  const src = state.model.screens[state.model.grid[0][col].screen];
  state.model.screens.push({ blocks: src.blocks.slice() });
  state.model.grid[0].splice(col + 1, 0, { screen: state.model.screens.length - 1, entities: 0 });
  state.model.entities.splice(col + 1, 0, []);
  state.model.specials.splice(col + 1, 0, []);
  if (state.model.start.col > col) state.model.start.col++;
  renumberColumns();
  state.selected = null;
  render();
  scrollToColumn(col + 1);
}

// Removes the screen in view; its special objects move to the previous one.
async function removeScreen() {
  if (!state.model.canExtend) { toast('Ce niveau ne peut pas être raccourci'); return; }
  if (state.model.kind === 'vertical') { await removeScreenVertical(); return; }
  if (state.model.grid[0].length <= 2) { toast('Un niveau garde au moins 2 écrans'); return; }
  const { col } = viewCell();
  if (col === 0) { toast('Le premier écran, celui du départ, ne peut pas être retiré'); return; }
  const keep = state.model.specials[col].filter((r) => r.type !== undefined);
  if (!(await ask(`Retirer l'écran ${col + 1} et ses ennemis ?` +
    (keep.length ? ' Ses objets spéciaux (boss, boule de riz…) iront sur l\'écran d\'à côté.' : ''),
  { title: 'Retirer un écran', ok: 'Retirer', danger: true }))) return;
  pushUndo();
  const target = col - 1;
  state.model.specials[target].push(...state.model.specials[col]);
  state.model.grid[0].splice(col, 1);
  state.model.entities.splice(col, 1);
  state.model.specials.splice(col, 1);
  if (state.model.start.col >= col) state.model.start.col--;
  renumberColumns();
  state.selected = null;
  render();
}

// Vertical levels (rom/leveledit.js verticalShape): one column of screens,
// then a row at the bottom. Entity lists follow the screens, column first.
function renumberVertical(oldEntities, oldSpecials) {
  const m = state.model, cells = [];
  m.grid.forEach((row) => { if (row[0]) cells.push(row[0]); });
  m.grid[m.grid.length - 1].slice(1).forEach((c) => { if (c) cells.push(c); });
  m.entities = cells.map((c) => (c.entities >= 0 ? oldEntities[c.entities] : []) || []);
  m.specials = cells.map((c) => (c.entities >= 0 ? oldSpecials[c.entities] : []) || []);
  cells.forEach((c, i) => { c.entities = i; });
  m.rows = m.grid.length;
  m.columns = Math.max(...m.grid.map((r) => r.length));
  m.grid.forEach((r) => { while (r.length < m.columns) r.push(null); });
}

function addScreenVertical() {
  const m = state.model, { row, col } = viewCell(), last = m.grid.length - 1;
  const cell = m.grid[row][col] || m.grid[row][0];
  pushUndo();
  m.screens.push({ blocks: m.screens[cell.screen].blocks.slice() });
  const fresh = { screen: m.screens.length - 1, entities: -1 };
  const oldE = m.entities, oldS = m.specials;
  if (row < last) m.grid.splice(row + 1, 0, [fresh]);          // one more screen to go down
  else m.grid[last].splice(Math.max(col, 0) + 1, 0, fresh);     // one more at the bottom, on the right
  renumberVertical(oldE, oldS);
  state.selected = null;
  render();
}

async function removeScreenVertical() {
  const m = state.model, { row, col } = viewCell(), last = m.grid.length - 1;
  if (row === 0 && col === 0) { toast('Le premier écran, celui du départ, ne peut pas être retiré'); return; }
  if (row < last && m.grid.length <= 2) { toast('Un niveau vertical garde au moins 2 écrans en hauteur'); return; }
  const gone = row < last ? m.grid[row][0] : m.grid[last][col];
  if (!gone) return;
  const keep = m.specials[gone.entities].filter((r) => r.type !== undefined);
  if (!(await ask('Retirer l\'écran visé et ses ennemis ?' +
    (keep.length ? ' Ses objets spéciaux (boss, boule de riz…) iront sur l\'écran d\'à côté.' : ''),
  { title: 'Retirer un écran', ok: 'Retirer', danger: true }))) return;
  pushUndo();
  const oldE = m.entities.map((l) => l.slice()), oldS = m.specials.map((l) => l.slice());
  if (row < last) m.grid.splice(row, 1); else m.grid[last].splice(col, 1);
  // Special objects move to the neighbour: the screen above, or on the left.
  const neighbour = row < last ? m.grid[Math.max(row - 1, 0)][0] : m.grid[last][Math.max(col - 1, 0)];
  oldS[neighbour.entities].push(...oldS[gone.entities]);
  m.grid.forEach((r) => { for (let i = r.length - 1; i > 0 && !r[i]; i--) r.pop(); });
  renumberVertical(oldE, oldS);
  state.selected = null;
  render();
}

export function renderScreenTools() {
  const fixed = !state.model.canExtend;
  const why = 'Ce niveau du jeu garde sa forme d\'origine';
  $('screenAdd').classList.toggle('is-off', fixed);
  const vertical = state.model.kind === 'vertical';
  $('screenDel').classList.toggle('is-off', fixed || (!vertical && state.model.grid[0].length <= 2));
  setTip($('screenAdd'), 'Ajouter un écran', fixed ? why : vertical
    ? 'Dans la colonne : un écran de plus en dessous. En bas : un écran de plus à droite'
    : 'Copie l\'écran du milieu de la vue et le place juste après');
  setTip($('screenDel'), 'Retirer un écran', fixed ? why : !vertical && state.model.grid[0].length <= 2
    ? 'Un niveau garde au moins 2 écrans' : 'Retire du niveau l\'écran du milieu de la vue');
}

export function bindScreens() {
  $('screenAdd').addEventListener('click', addScreen);
  $('screenDel').addEventListener('click', removeScreen);
}
