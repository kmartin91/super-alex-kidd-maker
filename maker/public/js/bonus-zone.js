// A bonus zone (model.zone), a sub-area of the level: a few more screens
// of the same setting with their own enemies, reached through doors (entity
// $4C: data 0 in the level goes in, data 1 in the zone comes back; see
// engine/src/game/states/zone.c). Horizontal levels whose shape can change.
//
// The editor shows one area at a time: editing the zone swaps its grid,
// entities and start with the level's (model.inZone). Everything that saves,
// builds or shares a level uses mainModel(), the level's own arrangement.

import { $ } from './dom.js';
import { state, SCREEN_W, SCREEN_H, setDirty } from './state.js';
import { openModal, closeModal, h, ask, tell } from './modal.js';
import { clearHistory } from './history.js';
import { render } from './render.js';
import { t } from './i18n.js';

export const DOOR_TYPE = 0x4C;
const AREA = ['grid', 'columns', 'rows', 'entities', 'specials', 'start'];

export const canHaveZone = () => state.model.kind === 'horizontal' && state.model.canExtend;

// The level with its own arrays in place (not those of the zone being edited).
export function mainModel(m = state.model) {
  if (!m || !m.inZone) return m;
  const out = { ...m, zone: { ...m.zone } };
  for (const k of AREA) [out[k], out.zone[k]] = [m.zone[k], m[k]];
  delete out.inZone;
  return out;
}

function swap() {
  const m = state.model;
  for (const k of AREA) [m[k], m.zone[k]] = [m.zone[k], m[k]];
  m.inZone = !m.inZone;
  state.selected = null;
  state.selection = null;
  clearHistory(); // undo does not cross areas
  document.body.classList.toggle('in-zone', !!m.inZone);
  document.body.style.setProperty('--zone-label', JSON.stringify(t('Zone bonus')));
  $('mapWrap').scrollTo(0, 0);
  render();
}

export function showMainArea() {
  if (state.model && state.model.inZone) swap();
}

// A new zone: `count` screens of sky over the setting's ground, Alex coming
// in on the left.
function createZone(count) {
  const m = mainModel();
  const ground = m.parts.terrains[0];
  const rows = ground && ground.tiles[15] !== ground.tiles[14] ? 2 : 1;
  const grid = [];
  for (let i = 0; i < count; i++) {
    const blocks = new Array(SCREEN_W * SCREEN_H).fill(m.parts.eraser);
    if (ground) for (let y = SCREEN_H - rows; y < SCREEN_H; y++) for (let x = 0; x < SCREEN_W; x++) blocks[y * SCREEN_W + x] = ground.fill;
    m.screens.push({ blocks });
    grid.push({ screen: m.screens.length - 1, entities: i });
  }
  const groundY = (SCREEN_H - (ground ? rows : 0)) * 16;
  state.model.zone = {
    grid: [grid], columns: count, rows: 1,
    entities: Array.from({ length: count }, () => []),
    // The way back, near the right end of the zone.
    specials: Array.from({ length: count }, () => []),
    start: { col: 0, row: 0, x: 32, y: Math.max(0, groundY - 24 - (ground ? 0 : 0)) },
  };
  state.model.zone.entities[count - 1].push({ type: DOOR_TYPE, x: 216, y: groundY, data: 1 });
  setDirty(true);
}

function deleteZone() {
  showMainArea();
  delete state.model.zone;
  // Doors into it lead nowhere now.
  for (const list of state.model.entities) for (let i = list.length - 1; i >= 0; i--) if (list[i].type === DOOR_TYPE) list.splice(i, 1);
  clearHistory();
  setDirty(true);
  render();
}

export function openZoneSheet() {
  if (!canHaveZone()) {
    tell(t('Les zones bonus vont dans les niveaux horizontaux qu\'on peut rallonger (les nouveaux niveaux).'));
    return;
  }
  const m = state.model;
  if (m.inZone) { swap(); return; } // back to the level
  if (m.zone) {
    openModal(t('Zone bonus'), h('div.zone-sheet', {},
      h('p.hint', { textContent: t('Pose des portes « vers la zone bonus » dans ton niveau (onglet Ennemis) : Alex y entre en les touchant, et revient par la porte de retour.') }),
      h('div.choices', {},
        h('button.choice.main', { onclick: () => { closeModal(); swap(); } }, h('b', { textContent: t('Éditer la zone') }),
          h('span', { textContent: t('{n} écrans', { n: m.zone.columns }) })),
        h('button.choice', { onclick: async () => {
          closeModal();
          if (await ask(t('Supprimer la zone bonus et ses portes ?'), { title: t('Zone bonus'), ok: t('Supprimer'), danger: true })) deleteZone();
        } }, h('b', { textContent: t('Supprimer la zone') }), h('span', { textContent: t('et les portes qui y mènent') })))), { wide: true });
    return;
  }
  openModal(t('Nouvelle zone bonus'), h('div.zone-sheet', {},
    h('p.hint', { textContent: t('Une zone à part : Alex y entre par une porte de ton niveau et en ressort par une autre, là où il était entré. Ce qu\'il y gagne est gardé.') }),
    h('div.choices', {}, ...[1, 2, 3].map((n) => h(`button.choice${n === 2 ? '.main' : ''}`, {
      onclick: () => { closeModal(); createZone(n); swap(); },
    }, h('b', { textContent: t('{n} écrans', { n }) }), h('span', { textContent: n === 1 ? t('une petite salle') : n === 2 ? t('une zone moyenne') : t('une grande zone') }))))), { wide: true });
}

// Label of the side button.
export function zoneText() {
  const m = state.model;
  if (m.inZone) return t('en cours d\'édition');
  return m.zone ? t('{n} écrans', { n: m.zone.columns }) : t('aucune');
}
