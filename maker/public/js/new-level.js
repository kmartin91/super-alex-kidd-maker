// The level sheet (shown when the Maker opens, and by the level button):
// a new level with any of the game's 17 settings, one of my levels, or a
// copy of a level of the game to start from.

import { $ } from './dom.js';
import { state, SCREEN_W, SCREEN_H, SCREEN_PX_W } from './state.js';
import { editNew, openLevel } from './storage.js';
import { setDirty } from './state.js';
import { themedLevel, templateLevel, listMyLevels, deleteMyLevel } from './backend.js';
import { fillBlocks } from './parts.js';
import { placeGoal } from './entities.js';
import { clearHistory } from './history.js';
import { render } from './render.js';
import { openModal, closeModal, h, ask } from './modal.js';
import { toast } from './toast.js';
import { icon } from './icons.js';
import { starsText } from './difficulty.js';

// New levels take the place of level 2 (horizontal: a plain level on foot) or
// level 1 (vertical: a column of screens going down, then a row at the
// bottom), whose shapes can grow, and wear the graphics, enemies and music of
// the chosen setting.
const BASE = 2, VERTICAL_BASE = 1;
const SCREENS = 3;

const names = () => Object.fromEntries(state.levels.map((l) => [l.level, l.name]));

// Empties the level: every screen is sky, no enemies.
function emptyLevel(m) {
  for (const s of m.screens) s.blocks.fill(m.parts.eraser);
  // Some levels have fewer screens than a new level starts with.
  while (m.screens.length < SCREENS) m.screens.push({ blocks: new Array(SCREEN_W * SCREEN_H).fill(m.parts.eraser) });
  m.grid = [Array.from({ length: SCREENS }, (_, i) => ({ screen: i, entities: i }))];
  m.rows = 1;
  m.columns = SCREENS;
  m.entities = Array.from({ length: SCREENS }, () => []);
  m.specials = Array.from({ length: SCREENS }, () => []);
  m.surprises = null;
}

async function unsavedOk() {
  return !state.dirty || ask('Des modifications ne sont pas enregistrées. Continuer quand même ?',
    { title: 'Pas enregistré', ok: 'Continuer' });
}

// Vertical: 3 screens down (Alex falls from the top), then one more on the
// right at the bottom; one entity list per screen, the column first.
function emptyVerticalLevel(m) {
  for (const s of m.screens) s.blocks.fill(m.parts.eraser);
  while (m.screens.length < 4) m.screens.push({ blocks: new Array(SCREEN_W * SCREEN_H).fill(m.parts.eraser) });
  m.grid = [[{ screen: 0, entities: 0 }, null], [{ screen: 1, entities: 1 }, null], [{ screen: 2, entities: 2 }, { screen: 3, entities: 3 }]];
  m.rows = 3;
  m.columns = 2;
  m.entities = [[], [], [], []];
  m.specials = [[], [], [], []];
  m.surprises = null;
}

async function createLevel(theme, vertical) {
  if (!(await unsavedOk())) return;
  closeModal();
  $('status').textContent = 'préparation…';
  const base = vertical ? VERTICAL_BASE : BASE;
  const { model, video } = await themedLevel(base, theme);
  if (vertical) emptyVerticalLevel(model); else emptyLevel(model);
  editNew({ name: '', base, model, video }); // named when first saved
  // A ground line along the bottom (two blocks high when the setting has
  // blocks for the inside of the ground), and the level end at the far end.
  const ground = state.model.parts.terrains[0];
  const bottomRow = state.model.rows - 1, width = vertical ? 2 : SCREENS;
  let groundRows = 0;
  if (ground) {
    groundRows = ground.tiles[15] !== ground.tiles[14] ? 2 : 1;
    const y1 = (bottomRow + 1) * SCREEN_H - 1;
    fillBlocks({ x0: 0, y0: y1 - groundRows + 1, x1: width * SCREEN_W - 1, y1 }, { kind: 'terrain', index: 0 });
  }
  // The rice ball sits on the ground, within reach of Alex walking by.
  placeGoal({ list: state.model.entities.length - 1, lx: SCREEN_PX_W - 56, ly: (SCREEN_H - groundRows - 1) * 16 });
  // Alex starts on the left of the first screen, standing on the ground (a
  // vertical level keeps the game's start: he falls from the top).
  if (!vertical) state.model.start = { col: 0, row: 0, x: 32, y: (SCREEN_H - (groundRows || 2)) * 16 - 24 };
  state.selected = null;
  clearHistory();
  render();
  toast('Niveau vide : à toi de jouer !');
}

async function copyGameLevel(n) {
  if (!(await unsavedOk())) return;
  closeModal();
  const { model, video } = await templateLevel(n);
  editNew({ name: `${names()[n]} (copie)`, base: n, model, video });
}

const back = () => h('button.key.small.sheet-back', { textContent: '‹ Retour', onclick: () => openLevelSheet() });

function card(num, name, onclick, extra = null) {
  return h('div.card-wrap', {},
    h('button.card', { onclick }, h('span.card-num', { textContent: num }), h('span.card-name', { textContent: name })),
    extra);
}

function chooseShape() {
  openModal('Nouveau niveau', h('div', {}, back(),
    h('div.choices', {},
      h('button.choice', { id: 'shapeHorizontal', onclick: () => chooseSetting(false) },
        icon('play', 5), h('b', { textContent: 'Horizontal' }), h('span', { textContent: 'On avance vers la droite (et on peut revenir)' })),
      h('button.choice', { id: 'shapeVertical', onclick: () => chooseSetting(true) },
        icon('flag', 5), h('b', { textContent: 'Vertical' }), h('span', { textContent: 'On descend, puis on finit vers la droite en bas' })))),
  { wide: true });
}

function chooseSetting(vertical) {
  const n = names();
  openModal(vertical ? 'Nouveau niveau vertical' : 'Nouveau niveau horizontal', h('div', {},
    h('button.key.small.sheet-back', { textContent: '‹ Retour', onclick: chooseShape }),
    h('p.hint', { textContent: 'Choisis le décor de ton niveau : ses graphismes, ses ennemis et sa musique. ' +
      (vertical ? 'Tu pars de 3 écrans à descendre, puis un en bas à droite, avec la boule de riz.'
        : 'Tu pars d\'un niveau vide de 3 écrans, avec un sol et la boule de riz au bout.') }),
    h('div.cards', {}, ...Object.keys(n).map((k) => card(k, n[k], () => createLevel(Number(k), vertical))))), { wide: true });
}

async function chooseMine() {
  const docs = await listMyLevels();
  const n = names();
  const remove = (doc) => h('button.mini.danger.card-delete', {
    title: 'Supprimer',
    onclick: async () => {
      if (!(await ask(`Supprimer « ${doc.name} » ?`, { title: 'Supprimer', ok: 'Supprimer', danger: true }))) { chooseMine(); return; }
      await deleteMyLevel(doc.id);
      toast('Niveau supprimé');
      chooseMine();
    },
  }, icon('trash'));
  const open = async (doc) => { if (await unsavedOk()) { closeModal(); await openLevel(doc.id); } };
  openModal('Mes niveaux', h('div', {}, back(),
    docs.length ? h('div.cards', {}, ...docs.map((d, i) => card(i + 1,
      `${d.name} · ${n[d.theme]}${d.difficulty ? ' · ' + starsText(d.difficulty) : ''}`, () => open(d), remove(d))))
      : h('p.hint', { textContent: 'Aucun niveau pour l\'instant : crée-en un avec « Nouveau niveau ».' }),
    h('h3.sheet-sub', { textContent: 'Partir d\'un niveau du jeu' }),
    h('p.hint', { textContent: 'Une copie du niveau, à modifier comme tu veux.' }),
    h('div.cards.small', {}, ...Object.keys(n).map((k) => card(k, n[k], () => copyGameLevel(Number(k)))))), { wide: true });
}

export function openLevelSheet() {
  openModal('Super Alex Kidd Maker', h('div', {},
    h('div.choices', {},
      h('button.choice.main', { id: 'choiceNew', onclick: chooseShape },
        icon('plus', 5), h('b', { textContent: 'Nouveau niveau' }), h('span', { textContent: 'Pars d\'une page blanche, dans le décor de ton choix' })),
      h('button.choice', { id: 'choiceMine', onclick: chooseMine },
        icon('flag', 5), h('b', { textContent: 'Mes niveaux' }), h('span', { textContent: 'Continue un niveau, ou pars d\'un niveau du jeu' })))),
  { wide: true });
}

// Something to show behind the sheet at startup, before any choice.
export async function showPlaceholder() {
  const { model, video } = await templateLevel(BASE);
  editNew({ name: `${names()[BASE]} (copie)`, base: BASE, model, video });
  setDirty(false); // nothing to lose yet
}
