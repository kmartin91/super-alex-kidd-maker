// The first-run tutorial: a few bubbles pointing at the editor's parts, the
// first time a level is opened (again from the Commandes window).

import { h } from './modal.js';
import { pref, setPref } from './prefs.js';
import { state } from './state.js';
import { startBox } from './entities.js';
import { t } from './i18n.js';

// Texts in French, translated when shown.
const STEPS = [
  { at: '#palette', title: 'Les pièces', text: 'Choisis ici ce que tu poses : du sol qui se raccorde tout seul, des blocs, du décor, des ennemis et des boss.' },
  { at: '#stage', title: 'Le niveau', text: 'Clique ou glisse pour poser. Clic droit pour effacer. Maj + glisser remplit un rectangle.' },
  { at: '#stage', title: 'Le départ', text: 'Alex marqué « Départ » est l\'endroit où le niveau commence : attrape-le et pose-le où tu veux.', start: true },
  { at: '#left', title: 'Le réglage du niveau', text: 'Son décor, sa musique, ce que donnent les boîtes ?, et un défi : un temps limite ou « sans mourir ».' },
  { at: '#right', title: 'Les outils', text: 'Annuler, gomme, sélection (copier-coller une zone), grille, zoom, enregistrer. La sauvegarde se fait aussi toute seule.' },
  { at: '#corner', title: 'Jouer', text: 'Teste ton niveau quand tu veux, même pas fini (touche Espace). Échap pour revenir à l\'édition.' },
];

let current = null;

// Where a step points: an element, or Alex's start marker on the map.
function targetRect(step) {
  const b = step.start && state.model && startBox();
  if (!b) return document.querySelector(step.at).getBoundingClientRect();
  const m = document.getElementById('map').getBoundingClientRect(), z = state.zoom;
  return { left: m.left + b.x * z, top: m.top + b.y * z, right: m.left + (b.x + b.w) * z, bottom: m.top + (b.y + b.h) * z,
    width: b.w * z, height: b.h * z };
}

function place(card, ring, r) {
  Object.assign(ring.style, { left: `${r.left - 6}px`, top: `${r.top - 6}px`, width: `${r.width + 12}px`, height: `${r.height + 12}px` });
  // The card beside the target, where there is room.
  const cw = card.offsetWidth, ch = card.offsetHeight, m = 16;
  let x = r.right + m, y = r.top;
  if (x + cw > innerWidth - m) x = r.left - cw - m;
  if (x < m) { x = Math.min(Math.max(m, r.left), innerWidth - cw - m); y = r.bottom + m; }
  if (y + ch > innerHeight - m) y = Math.max(m, r.top - ch - m);
  if (r.height > innerHeight * 0.6) y = Math.max(m, r.top + r.height / 2 - ch / 2);
  Object.assign(card.style, { left: `${x}px`, top: `${y}px` });
}

export function startTutorial() {
  if (current) return;
  let i = 0;
  const ring = h('div.tuto-ring', {});
  const title = h('b', {}), text = h('p', {}), count = h('small', {});
  const next = h('button.key.small.go', {});
  const card = h('div.tuto-card', {}, count, title, text,
    h('div.tuto-actions', {}, h('button.key.small.plain', { textContent: t('Passer'), onclick: () => end() }), next));
  const root = h('div.tuto', {}, ring, card);
  const show = () => {
    const step = STEPS[i];
    count.textContent = `${i + 1} / ${STEPS.length}`;
    title.textContent = t(step.title);
    text.textContent = t(step.text);
    next.textContent = i === STEPS.length - 1 ? t('C\'est parti !') : t('Suivant');
    place(card, ring, targetRect(step));
  };
  const end = () => {
    root.remove();
    window.removeEventListener('resize', show);
    current = null;
    setPref('tutorialDone', true);
  };
  next.addEventListener('click', () => { if (++i >= STEPS.length) end(); else show(); });
  document.body.appendChild(root);
  window.addEventListener('resize', show);
  current = root;
  show();
  next.focus();
}

// The first time a level shows in the editor (not behind the menu).
export function maybeStartTutorial() {
  if (pref('tutorialDone', false) || new URLSearchParams(location.search).has('level')) return;
  setTimeout(() => {
    const menu = document.getElementById('menu');
    if ((menu && !menu.hidden) || !document.getElementById('modal').hidden) return;
    startTutorial();
  }, 700);
}
