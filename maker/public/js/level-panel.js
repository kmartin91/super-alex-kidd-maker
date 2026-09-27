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
import { clearText, openChallengeSheet } from './challenge.js';
import { vehicleName, canHaveVehicle, openVehicleSheet } from './vehicle.js';
import { mainModel, zoneText, canHaveZone, openZoneSheet } from './bonus-zone.js';
import { t, tName, tError } from './i18n.js';

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
  $('levelName').textContent = state.doc.name || t('Sans nom');
  const { stars } = rateDifficulty(mainModel());
  $('levelStars').textContent = starsText(stars);
  setTip($('levelBtn'), t('{name} · difficulté {stars}/5', { name: state.doc.name, stars }),
    t('Estimée d\'après les ennemis, les boss, les trous, les pièges et la longueur. Clique pour tes autres niveaux'));
  $('themeName').textContent = names[theme()];
  const own = theme() === state.doc.base;
  $('musicBtn').classList.toggle('is-off', own);
  setTip($('musicBtn'), t('Musique'), own ? t('Change d\'abord de thème : ce niveau joue déjà sa propre musique')
    : t('Choisis entre la musique du thème et celle du niveau d\'origine'));
  $('musicName').textContent = !own && state.model.themeMusic !== false ? t('du thème') : t('d\'origine');
  const n = questionBoxCount();
  $('surpriseName').textContent = !n ? t('aucune boîte') : state.model.surprises ? t('personnalisées') : t('d\'origine');
  $('clearName').textContent = clearText();
  $('vehicleName').textContent = vehicleName();
  $('vehicleBtn').classList.toggle('is-off', !canHaveVehicle());
  $('zoneName').textContent = zoneText();
  $('zoneBtn').classList.toggle('is-off', !canHaveZone());
  $('zoneBtn').classList.toggle('active', !!state.model.inZone);
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
    h('p.hint', { textContent: t('Le thème donne au niveau les graphismes, les ennemis et la musique d\'un autre niveau.') + ' ' +
      t('Le sol, les boîtes et l\'eau sont gardés ; le décor propre à l\'ancien thème est effacé.') }),
    levelCards(theme(), async (to) => { closeModal(); await changeTheme(to); }));
  openModal(t('Thème du niveau'), body, { wide: true });
}

// Converts the level to another level's graphics and enemies.
async function changeTheme(to) {
  if (to === theme()) return;
  $('status').textContent = t('conversion du thème…');
  try {
    const { model, video } = await retheme(state.level, state.model, to);
    showModel(model, video);
    setDirty(true);
    toast(t('Thème : {name}', { name: state.model.levelNames[to] }));
  } catch (err) {
    $('status').textContent = t('erreur : {message}', { message: tError(err.message) });
    tell(tError(err.message));
  }
}

function toggleMusic() {
  if (theme() === state.doc.base) { toast(t('Change d\'abord de thème : ce niveau joue déjà sa propre musique')); return; }
  pushUndo();
  state.model.themeMusic = state.model.themeMusic === false;
  renderLevelPanel();
  toast(state.model.themeMusic ? t('Musique du thème') : t('Musique du niveau d\'origine'));
}

function surpriseSheet() {
  const n = questionBoxCount();
  const items = state.model.surpriseItems;
  if (!n) return h('p.hint', { textContent: t('Ce niveau n\'a aucune boîte ?. Pose-en avec la catégorie Blocs.') });
  const slots = [];
  for (let i = 0; i < n; i++) {
    const cur = state.model.surprises ? state.model.surprises[i] : '';
    const select = h('select', { id: `surprise${i}` },
      h('option', { value: '', textContent: t('comme à l\'origine') }),
      ...items.map((it) => h('option', { value: it.id, textContent: tName(it.name), selected: cur === it.id })));
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
    h('p.hint', { textContent: t('Ce que donnent les boîtes ?, dans l\'ordre où Alex les casse.') }),
    h('div.surprises', {}, ...slots),
    h('div.sheet-actions', {}, h('button.key', {
      id: 'surpriseReset', textContent: t('Tout remettre comme à l\'origine'),
      onclick: () => { pushUndo(); state.model.surprises = null; renderLevelPanel(); openSurprises(); },
    })));
}

function openSurprises() {
  openModal(t('Surprises des boîtes ?'), surpriseSheet());
}

export function bindLevelPanel() {
  $('levelBtn').addEventListener('click', openLevelSheet);
  $('themeBtn').addEventListener('click', chooseTheme);
  $('musicBtn').addEventListener('click', toggleMusic);
  $('surpriseBtn').addEventListener('click', openSurprises);
  $('clearBtn').addEventListener('click', openChallengeSheet);
  $('vehicleBtn').addEventListener('click', openVehicleSheet);
  $('zoneBtn').addEventListener('click', openZoneSheet);
}
