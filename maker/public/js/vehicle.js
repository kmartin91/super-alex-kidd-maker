// The vehicle a level starts on (model.vehicle: bike, boat, peticopter, or
// none: on foot), horizontal levels only. The level tools set the game's
// start table (rom/leveledit.js VEHICLES); the engine handles the rest.
// The boat floats on water at the bottom of the screen (rows 10 and 11).

import { state, SCREEN_W, SCREEN_H } from './state.js';
import { openModal, closeModal, h, tell } from './modal.js';
import { pushUndo } from './history.js';
import { render } from './render.js';
import { exportLevelOf } from './backend.js';
import { putBlock } from './level.js';
import { t } from './i18n.js';

export const VEHICLES = [
  { id: null, name: 'À pied', text: 'Alex marche, saute et nage.' },
  { id: 'bike', name: 'Moto', text: 'Il fonce tout seul vers la droite : saute les trous et les ennemis. Attention, la moto ne nage pas.' },
  { id: 'boat', name: 'Bateau', text: 'Il navigue sur l\'eau du bas de l\'écran et tire des boulets. Contre un mur, Alex saute à l\'eau.' },
  { id: 'peticopter', name: 'Peticopter', text: 'Il vole et tire des boulets. S\'il touche l\'eau ou le plafond, Alex tombe.' },
];

export const vehicleName = (id = state.model.vehicle) => t((VEHICLES.find((v) => v.id === (id || null)) || VEHICLES[0]).name);
export const canHaveVehicle = () => state.model.kind === 'horizontal';

// The water blocks for the two bottom rows (the surface, then the depth):
// the ones the setting's own level uses there most, else any water block of
// the setting. Null when the setting has no water.
function waterBlocks() {
  const theme = state.model.theme || state.level;
  const ref = exportLevelOf(theme);
  const isWater = (b) => (ref.metatileClasses[b] || '').includes('water');
  const rows = [SCREEN_H - 2, SCREEN_H - 1].map((y) => {
    const count = new Map();
    for (const s of ref.screens) for (let x = 0; x < SCREEN_W; x++) {
      const b = s.blocks[y * SCREEN_W + x];
      if (isWater(b)) count.set(b, (count.get(b) || 0) + 1);
    }
    return [...count].sort((a, b) => b[1] - a[1])[0]?.[0];
  });
  const any = state.model.metatileClasses.findIndex((c) => c && c.includes('water'));
  const top = rows[0] ?? rows[1] ?? (any >= 0 ? any : null);
  if (top === null || top === undefined) return null;
  return [top, rows[1] ?? top];
}

// Water on the two bottom rows of every screen, for the boat.
function flood() {
  const water = waterBlocks();
  if (!water) {
    tell(t('Ce décor n\'a pas d\'eau : choisis un décor avec de l\'eau (un lac, la rivière…) pour le bateau.'));
    return false;
  }
  const width = state.model.grid[0].length * SCREEN_W;
  for (let gx = 0; gx < width; gx++) {
    putBlock(gx, SCREEN_H - 2, water[0]);
    putBlock(gx, SCREEN_H - 1, water[1]);
  }
  return true;
}

export function openVehicleSheet() {
  if (!canHaveVehicle()) {
    tell(t('Les véhicules ne vont que dans les niveaux horizontaux.'));
    return;
  }
  let chosen = state.model.vehicle || null;
  let wantWater = false;
  const cards = h('div.choices.vehicles', {});
  const waterRow = h('button.menu-row', {});
  const draw = () => {
    cards.replaceChildren(...VEHICLES.map((v) => h(`button.choice${v.id === chosen ? '.main' : ''}`, {
      onclick: () => { chosen = v.id; draw(); },
    }, h('b', { textContent: t(v.name) }), h('span', { textContent: t(v.text) }))));
    waterRow.hidden = chosen !== 'boat';
    waterRow.replaceChildren(h('span.menu-row-label', { textContent: t('Mettre de l\'eau sur les deux rangées du bas') }),
      h('b', { textContent: wantWater ? t('oui') : t('non') }));
  };
  waterRow.addEventListener('click', () => { wantWater = !wantWater; draw(); });
  draw();
  openModal(t('Véhicule de départ'), h('div.vehicle-sheet', {},
    h('p.hint', { textContent: t('Alex commence le niveau sur ce véhicule. S\'il meurt, le niveau recommence avec.') }),
    cards, waterRow,
    h('div.dialog-actions', {},
      h('button.key.plain', { textContent: t('Annuler'), onclick: () => closeModal() }),
      h('button.key.go', { textContent: 'OK', onclick: () => {
        pushUndo();
        if (chosen) state.model.vehicle = chosen; else delete state.model.vehicle;
        if (chosen === 'boat' && wantWater) flood();
        closeModal();
        render();
      } }))), { wide: true });
}
