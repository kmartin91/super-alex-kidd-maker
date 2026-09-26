// Left column: which level, its theme, its music, the question box surprises.

import { $ } from './dom.js';
import { state, setDirty } from './state.js';
import { retheme } from './backend.js';
import { levelBlocks } from './level.js';
import { blockClass } from './blocks.js';
import { pushUndo } from './history.js';
import { showModel } from './storage.js';
import { openLevelSheet } from './new-level.js';
import { openModal, closeModal, h, tell } from './modal.js';
import { toast } from './toast.js';
import { setTip } from './tooltip.js';
import { rateDifficulty, starsText } from './difficulty.js';

const theme = () => state.model.theme || state.level;

function questionBoxCount() {
  let n = 0;
  for (const m of levelBlocks()) if (blockClass(m) === 'question_box') n++;
  return n;
}

// Custom surprises must list one item per question box.
export function normalizeSurprises() {
  const s = state.model.surprises;
  if (!s) return;
  const n = questionBoxCount();
  while (s.length < n) s.push(state.model.surpriseItems[0].id);
  s.length = n;
  if (!n) state.model.surprises = null;
}

export function renderLevelPanel() {
  const names = state.model.levelNames;
  $('levelName').textContent = state.doc.name || 'Sans nom';
  const { stars } = rateDifficulty(state.model);
  $('levelStars').textContent = starsText(stars);
  setTip($('levelBtn'), `${state.doc.name} · difficulté ${stars}/5`,
    'Estimée d\'après les ennemis, les boss, les trous, les pièges et la longueur. Clique pour tes autres niveaux');
  $('themeName').textContent = names[theme()];
  const own = theme() === state.doc.base;
  $('musicBtn').classList.toggle('is-off', own);
  setTip($('musicBtn'), 'Musique', own ? 'Change d\'abord de thème : ce niveau joue déjà sa propre musique'
    : 'Choisis entre la musique du thème et celle du niveau d\'origine');
  $('musicName').textContent = !own && state.model.themeMusic !== false ? 'du thème' : 'd\'origine';
  const n = questionBoxCount();
  $('surpriseName').textContent = !n ? 'aucune boîte' : state.model.surprises ? 'personnalisées' : 'd\'origine';
}

// Grid of level cards; `current` is highlighted, `pick(n)` called on click.
function levelCards(current, pick, tag) {
  const names = state.model.levelNames;
  return h('div.cards', {}, ...Object.keys(names).map((k) => h('button.card' + (Number(k) === current ? '.active' : ''),
    { onclick: () => pick(Number(k)) },
    h('span.card-num', { textContent: k }), h('span.card-name', { textContent: names[k] }),
    tag && tag(Number(k)) ? h('span.card-tag', { textContent: tag(Number(k)) }) : null)));
}

function chooseTheme() {
  const body = h('div', {},
    h('p.hint', { textContent: 'Le thème donne au niveau les graphismes, les ennemis et la musique d\'un autre niveau. ' +
      'Le sol, les boîtes et l\'eau sont gardés ; le décor propre à l\'ancien thème est effacé.' }),
    levelCards(theme(), async (t) => { closeModal(); await changeTheme(t); }));
  openModal('Thème du niveau', body, { wide: true });
}

// Converts the level to another level's graphics and enemies.
async function changeTheme(t) {
  if (t === theme()) return;
  $('status').textContent = 'conversion du thème…';
  try {
    const { model, video } = await retheme(state.level, state.model, t);
    showModel(model, video);
    setDirty(true);
    toast(`Thème : ${state.model.levelNames[t]}`);
  } catch (err) {
    $('status').textContent = 'erreur : ' + err.message;
    tell(err.message);
  }
}

function toggleMusic() {
  if (theme() === state.doc.base) { toast('Change d\'abord de thème : ce niveau joue déjà sa propre musique'); return; }
  pushUndo();
  state.model.themeMusic = state.model.themeMusic === false;
  renderLevelPanel();
  toast(state.model.themeMusic ? 'Musique du thème' : 'Musique du niveau d\'origine');
}

function surpriseSheet() {
  const n = questionBoxCount();
  const items = state.model.surpriseItems;
  if (!n) return h('p.hint', { textContent: 'Ce niveau n\'a aucune boîte ?. Pose-en avec la catégorie Blocs.' });
  const slots = [];
  for (let i = 0; i < n; i++) {
    const cur = state.model.surprises ? state.model.surprises[i] : '';
    const select = h('select', { id: `surprise${i}` },
      h('option', { value: '', textContent: 'comme à l\'origine' }),
      ...items.map((it) => h('option', { value: it.id, textContent: it.name, selected: cur === it.id })));
    if (!state.model.surprises) select.value = '';
    select.addEventListener('change', () => {
      pushUndo();
      if (!state.model.surprises) state.model.surprises = new Array(n).fill(items[0].id);
      normalizeSurprises();
      state.model.surprises[i] = select.value === '' ? items[0].id : Number(select.value);
      renderLevelPanel();
      openSurprises();
    });
    slots.push(h('label.surprise', {}, h('span.surprise-num', { textContent: i + 1 }), select));
  }
  return h('div', {},
    h('p.hint', { textContent: 'Ce que donnent les boîtes ?, dans l\'ordre où Alex les casse.' }),
    h('div.surprises', {}, ...slots),
    h('div.sheet-actions', {}, h('button.key', {
      id: 'surpriseReset', textContent: 'Tout remettre comme à l\'origine',
      onclick: () => { pushUndo(); state.model.surprises = null; renderLevelPanel(); openSurprises(); },
    })));
}

function openSurprises() {
  openModal('Surprises des boîtes ?', surpriseSheet());
}

export function bindLevelPanel() {
  $('levelBtn').addEventListener('click', openLevelSheet);
  $('themeBtn').addEventListener('click', chooseTheme);
  $('musicBtn').addEventListener('click', toggleMusic);
  $('surpriseBtn').addEventListener('click', openSurprises);
}
