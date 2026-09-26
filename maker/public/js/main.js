// Super Alex Kidd Maker, browser side (no dependencies).
//
//   state.js         shared state and level model description
//   level.js         level geometry (pixels, screens, blocks)
//   graphics.js      block images from the game's video memory
//   blocks.js        what each block does in the game
//   parts.js         building parts: terrains, blocks, decorations
//   brush.js         the item in hand
//   entities.js      enemies, items, bosses, level end
//   palette.js       top bar: the parts shelf
//   level-panel.js   left column: level, theme, music, surprises
//   intro.js, logo.js, menu.js  SEGA screen, title, game menu
//   new-level.js     empty new level, and the level sheet
//   difficulty.js    estimated difficulty, 1 to 5 stars
//   tools.js         right column and keyboard shortcuts
//   render.js        map drawing and preview of the item in hand
//   map-input.js     mouse on the map
//   bubble.js        bubble above the selected entity
//   minimap.js       bottom bar: the whole level in small
//   screens.js       adding and removing screens
//   history.js       undo / redo
//   storage.js       the level being edited, saving (my levels)
//   play.js, player.js  play mode (WebAssembly engine)
//   backend.js       the ROM and saved levels (in this browser), mods, captures
//   rom/*.js         level decoding and mod building (ported from the Python tools)
//   capture.js, capture-worker.js  captures from the game, in a Web Worker
//   rom-setup.js     asking for the user's ROM
//   modal.js, toast.js, tooltip.js, icons.js, prefs.js, db.js, dom.js  small helpers

import { $ } from './dom.js';
import { state } from './state.js';
import { initBackend, getLevels } from './backend.js';
import { askRom } from './rom-setup.js';
import { renderPalette } from './palette.js';
import { render } from './render.js';
import { renderIcons } from './icons.js';
import { loadIcons } from './entities.js';
import { bindMap } from './map-input.js';
import { bindBubble } from './bubble.js';
import { bindMinimap } from './minimap.js';
import { bindScreens } from './screens.js';
import { bindLevelPanel } from './level-panel.js';
import { bindTools } from './tools.js';
import { bindModal } from './modal.js';
import { bindPlay } from './play.js';
import { openLevel } from './storage.js';
import { showPlaceholder } from './new-level.js';
import { listMyLevels } from './backend.js';
import { bindTooltips } from './tooltip.js';
import { pref } from './prefs.js';
import { playIntro } from './intro.js';
import { openMenu, setEditorReady, menuIconsReady } from './menu.js';
import { logoImage } from './logo.js';

async function main() {
  window.editorState = state; // for automated tests (maker/tests/)
  renderIcons();
  bindTooltips();
  bindMap();
  bindBubble();
  bindMinimap();
  bindScreens();
  bindLevelPanel();
  bindTools();
  bindModal();
  bindPlay();
  $('brand').addEventListener('click', () => openMenu());
  // Automated tests (maker/tests/) open ?level=N: straight to the editor.
  const quick = new URLSearchParams(location.search).has('level');
  const intro = !quick && !pref('skipIntro', false);
  $('brand').replaceChildren(await logoImage('brand-logo'));
  const ready = initBackend();
  if (intro) await playIntro();
  if (!(await ready)) await askRom();
  state.levels = getLevels();
  if (!quick) openMenu({ title: intro });
  // Behind the menu: my last level, else a level of the game.
  const editor = (async () => {
    const last = pref('lastLevel');
    const mine = await listMyLevels();
    if (mine.some((d) => d.id === last)) await openLevel(last); else await showPlaceholder();
  })();
  setEditorReady(editor);
  await editor;
  // Enemy pictures: captured from the game the first time (a few seconds).
  await loadIcons();
  menuIconsReady();
  renderPalette();
  render();
}

main().catch((err) => {
  $('status').textContent = 'erreur : ' + err.message;
  console.error(err);
});
