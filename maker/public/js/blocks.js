// What each block does in the game (classes from maker/tools/levels.py).

import { state } from './state.js';
import { t } from './i18n.js';

const CLASS_LABELS = {
  empty: 'vide', background: 'décor', solid: 'solide', water: 'eau', money: 'argent',
  shop_item: 'objet de boutique', ladder: 'échelle', ladder_top: 'haut d\'échelle', shop_door: 'porte',
  deadly: 'mortel', breakable: 'cassable', skull_box: 'boîte tête de mort', question_box: 'boîte ?',
  star_box: 'boîte étoile', box: 'boîte', puzzle_floor: 'sol à énigme', ghost_floor: 'sol fantôme',
  enter_down: 'passage vers le bas', invalid: 'invalide',
};

export function blockClass(m) {
  return state.model.metatileClasses[m] || '';
}

export function blockIsSolid(m) {
  return blockClass(m).includes('solid');
}

export function classLabel(c) {
  return c.replace(/mixed\((.*)\)/, '$1').split(/[,/]/).map((k) => (CLASS_LABELS[k] ? t(CLASS_LABELS[k]) : k))
    .filter((v, i, a) => a.indexOf(v) === i).join(' + ');
}
