// Left column: which level, its theme, its music, the question box surprises.

import { $ } from './dom.js';
import { state, setDirty } from './state.js';
import { api } from './api.js';
import { levelBlocks } from './level.js';
import { blockClass } from './blocks.js';
import { pushUndo } from './history.js';
import { loadLevel, showModel } from './storage.js';
import { openModal, closeModal, h } from './modal.js';
import { toast } from './toast.js';

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
  $('levelNum').textContent = state.level;
  $('levelName').textContent = names[state.level];
  $('themeName').textContent = theme() === state.level ? 'd\'origine' : names[theme()];
  const own = theme() === state.level;
  $('musicBtn').disabled = own;
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

function chooseLevel() {
  openModal('Choisir un niveau', levelCards(state.level, async (n) => {
    if (n === state.level) return closeModal();
    if (state.dirty && !confirm('Des modifications ne sont pas enregistrées. Changer de niveau quand même ?')) return;
    closeModal();
    await loadLevel(n);
  }), { wide: true });
}

function chooseTheme() {
  const body = h('div', {},
    h('p.hint', { textContent: 'Le thème donne au niveau les graphismes, les ennemis et la musique d\'un autre niveau. ' +
      'Le sol, les boîtes et l\'eau sont gardés ; le décor propre à l\'ancien thème est effacé.' }),
    levelCards(theme(), async (t) => { closeModal(); await changeTheme(t); }, (k) => (k === state.level ? 'd\'origine' : '')));
  openModal('Thème du niveau', body, { wide: true });
}

// Converts the level to another level's graphics and enemies (server side).
async function changeTheme(t) {
  if (t === theme()) return;
  $('status').textContent = 'conversion du thème…';
  try {
    const { model, video } = await api(`/api/retheme/${state.level}?theme=${t}`,
      { method: 'POST', body: JSON.stringify(state.model) });
    showModel(model, video);
    setDirty(true);
    toast(`Thème : ${state.model.levelNames[t]}`);
  } catch (err) {
    $('status').textContent = 'erreur : ' + err.message;
    alert(err.message);
  }
}

function toggleMusic() {
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
  $('levelBtn').addEventListener('click', chooseLevel);
  $('themeBtn').addEventListener('click', chooseTheme);
  $('musicBtn').addEventListener('click', toggleMusic);
  $('surpriseBtn').addEventListener('click', openSurprises);
}
