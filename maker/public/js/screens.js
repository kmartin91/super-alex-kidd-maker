// Adding and removing screens (simple horizontal levels only), around the
// screen in the middle of the view.

import { $ } from './dom.js';
import { state, SCREEN_PX_W } from './state.js';
import { viewCell } from './level.js';
import { pushUndo } from './history.js';
import { render } from './render.js';

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
  if (!state.model.canExtend) return;
  const { col } = viewCell();
  pushUndo();
  const src = state.model.screens[state.model.grid[0][col].screen];
  state.model.screens.push({ blocks: src.blocks.slice() });
  state.model.grid[0].splice(col + 1, 0, { screen: state.model.screens.length - 1, entities: 0 });
  state.model.entities.splice(col + 1, 0, []);
  state.model.specials.splice(col + 1, 0, []);
  renumberColumns();
  state.selected = null;
  render();
  scrollToColumn(col + 1);
}

// Removes the screen in view; its special objects move to the previous one.
function removeScreen() {
  if (!state.model.canExtend || state.model.grid[0].length <= 2) return;
  const { col } = viewCell();
  const keep = state.model.specials[col].filter((r) => r.type !== undefined);
  if (!confirm(`Retirer l'écran ${col + 1} et ses ennemis ?` +
    (keep.length ? ' Ses objets spéciaux (boss, boule de riz…) iront sur l\'écran d\'à côté.' : ''))) return;
  pushUndo();
  const target = col > 0 ? col - 1 : 1;
  state.model.specials[target].push(...state.model.specials[col]);
  state.model.grid[0].splice(col, 1);
  state.model.entities.splice(col, 1);
  state.model.specials.splice(col, 1);
  renumberColumns();
  state.selected = null;
  render();
}

export function renderScreenTools() {
  const why = state.model.canExtend ? '' : 'Ce niveau ne peut pas encore être rallongé';
  $('screenAdd').disabled = !state.model.canExtend;
  $('screenDel').disabled = !state.model.canExtend || state.model.grid[0].length <= 2;
  $('screenAdd').title = why || 'Ajouter une copie de l\'écran du milieu juste après';
  $('screenDel').title = why || 'Retirer l\'écran du milieu';
}

export function bindScreens() {
  $('screenAdd').addEventListener('click', addScreen);
  $('screenDel').addEventListener('click', removeScreen);
}
