// The game menu, Mario Maker style: a level of the game scrolls behind the
// logo, big tiles to play, create, change the settings or quit (desktop app).
// At startup it is first the title screen ("Appuie sur une touche").
// Mouse, keyboard (arrows, Enter, Échap) or gamepad (d-pad, A, B).

import { state } from './state.js';
import { h, closeModal } from './modal.js';
import { logoImage } from './logo.js';
import { icon } from './icons.js';
import { listMyLevels } from './backend.js';
import { openLevel } from './storage.js';
import { openLevelSheet } from './new-level.js';
import { playLevel, playHooks } from './play.js';
import { askRom } from './rom-setup.js';
import { starsText } from './difficulty.js';
import { pref, setPref } from './prefs.js';
import { startPanorama } from './panorama.js';

const desktop = window.__TAURI__;
let root = null, panel = null, back = null, redrawBackground = null;
let editorReady = Promise.resolve();

// The editor loads its first level behind the menu: choices wait for it.
export function setEditorReady(promise) {
  editorReady = promise;
}

// Once the enemy pictures are known, they appear in the level behind.
export function menuIconsReady() {
  if (redrawBackground) redrawBackground();
}

const names = () => Object.fromEntries(state.levels.map((l) => [l.level, l.name]));

function tile(kind, iconName, label, onclick) {
  return h(`button.menu-tile.${kind}`, { onclick },
    h('span.menu-tile-icon', {}, icon(iconName, 6)),
    h('span.menu-tile-label', { textContent: label }));
}

function row(label, onclick, extra = null) {
  return h('button.menu-row', { onclick }, h('span', { textContent: label }), extra);
}

function card(title, ...children) {
  return h('div.menu-card', {},
    h('div.menu-card-head', {}, h('h2', { textContent: title })),
    h('div.menu-card-body', {}, ...children.filter(Boolean)),
    h('button.menu-back', { textContent: '‹ Retour', onclick: mainPanel }));
}

function show(el) {
  panel.replaceChildren(el);
  const first = panel.querySelector('button');
  if (first) first.focus();
}

function mainPanel() {
  back = null;
  show(h('div.menu-tiles', {},
    tile('play', 'play', 'Jouer un niveau', playPanel),
    tile('create', 'pencil', 'Créer un niveau', createLevel),
    tile('settings', 'gear', 'Paramètres', settingsPanel),
    desktop ? tile('quit', 'power', 'Quitter', quit) : null));
}

async function playPanel() {
  back = mainPanel;
  const docs = await listMyLevels();
  const n = names();
  show(card('Jouer un niveau',
    docs.length
      ? h('div.menu-list', {}, ...docs.map((d) => row(d.name || 'Sans nom', () => play(d.id),
        h('small', { textContent: `${n[d.theme] || ''}${d.difficulty ? ' · ' + starsText(d.difficulty) : ''}` }))))
      : h('p.menu-hint', { textContent: 'Tu n\'as pas encore de niveau : crée le premier !' }),
    docs.length ? null : row('Créer un niveau', createLevel)));
}

function settingsPanel() {
  back = mainPanel;
  const intro = () => (pref('skipIntro', false) ? 'non' : 'oui');
  const value = h('b', { textContent: intro() });
  show(card('Paramètres',
    row('Intro au lancement', () => { setPref('skipIntro', !pref('skipIntro', false)); value.textContent = intro(); }, value),
    row('Plein écran', toggleFullscreen),
    row('Changer de ROM', () => askRom({ first: false }))));
}

async function toggleFullscreen() {
  if (desktop) {
    const win = desktop.window.getCurrentWindow();
    await win.setFullscreen(!(await win.isFullscreen()));
  } else if (document.fullscreenElement) {
    await document.exitFullscreen();
  } else {
    await document.documentElement.requestFullscreen();
  }
}

async function createLevel() {
  await editorReady;
  closeMenu();
  openLevelSheet();
}

// Plays a level in the page, then back to the menu.
async function play(id) {
  await editorReady;
  closeMenu();
  await openLevel(id);
  playHooks.done = () => openMenu();
  await playLevel(null);
}

function quit() {
  desktop.core.invoke('quit');
}

// --------------------------------------------------------------- navigation
const titleShown = () => root.classList.contains('title');
const modalOpen = () => !document.getElementById('modal').hidden;

function leaveTitle() {
  root.classList.remove('title');
  mainPanel();
}

function move(step) {
  const items = [...panel.querySelectorAll('button')];
  if (!items.length) return;
  const i = items.indexOf(document.activeElement);
  items[(i + step + items.length) % items.length].focus();
}

function onKey(ev) {
  if (!root || root.hidden || modalOpen()) return;
  if (titleShown()) leaveTitle();
  else if (ev.key === 'ArrowDown' || ev.key === 'ArrowRight') move(1);
  else if (ev.key === 'ArrowUp' || ev.key === 'ArrowLeft') move(-1);
  else if (ev.key === 'Escape' && back) back();
  else return;
  ev.preventDefault();
  ev.stopPropagation();
}

// Gamepad: d-pad or stick to move, A to choose, B to go back.
let pad = { prev: false, next: false, a: true, b: true }, polling = false;
function pollPad() {
  if (!root || root.hidden) { polling = false; return; }
  const g = [...(navigator.getGamepads ? navigator.getGamepads() : [])].find(Boolean);
  if (g && !modalOpen()) {
    const now = {
      prev: g.buttons[12]?.pressed || g.buttons[14]?.pressed || g.axes[1] < -0.5 || g.axes[0] < -0.5,
      next: g.buttons[13]?.pressed || g.buttons[15]?.pressed || g.axes[1] > 0.5 || g.axes[0] > 0.5,
      a: g.buttons[0]?.pressed || g.buttons[9]?.pressed,
      b: g.buttons[1]?.pressed,
    };
    if (titleShown()) {
      if (now.a && !pad.a) leaveTitle();
    } else {
      if (now.prev && !pad.prev) move(-1);
      if (now.next && !pad.next) move(1);
      if (now.a && !pad.a && panel.contains(document.activeElement)) document.activeElement.click();
      if (now.b && !pad.b && back) back();
    }
    pad = now;
  }
  requestAnimationFrame(pollPad);
}

// title: the title screen first (at startup).
export async function openMenu({ title = false } = {}) {
  if (!root) {
    const bg = h('canvas.menu-bg', {});
    root = h('div', { id: 'menu' },
      bg,
      h('div.menu-box', {},
        await logoImage('menu-logo'),
        h('div.menu-press', { textContent: 'Appuie sur une touche' }),
        panel = h('div.menu-panel', {})),
      h('div.menu-by', { textContent: 'By Studio KMA' }));
    root.addEventListener('pointerdown', () => { if (titleShown()) leaveTitle(); });
    document.body.appendChild(root);
    window.addEventListener('keydown', onKey, true);
    redrawBackground = startPanorama(bg);
  }
  closeModal();
  root.hidden = false;
  pad = { prev: false, next: false, a: true, b: true }; // wait for the buttons to be released
  root.classList.toggle('title', title);
  if (title) panel.replaceChildren(); else mainPanel();
  if (!polling) { polling = true; requestAnimationFrame(pollPad); }
}

export function closeMenu() {
  if (root) root.hidden = true;
}
