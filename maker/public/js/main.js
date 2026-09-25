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
//   tools.js         right column and keyboard shortcuts
//   render.js        map drawing and preview of the item in hand
//   map-input.js     mouse on the map
//   bubble.js        bubble above the selected entity
//   minimap.js       bottom bar: the whole level in small
//   screens.js       adding and removing screens
//   history.js       undo / redo
//   storage.js       load / save / revert through the server
//   play.js, player.js  play mode (WebAssembly engine)
//   modal.js, toast.js, icons.js, dom.js, api.js  small helpers

import { $ } from './dom.js';
import { state } from './state.js';
import { api } from './api.js';
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
import { loadLevel } from './storage.js';

async function main() {
  window.editorState = state; // for automated tests (maker/tests/)
  renderIcons();
  bindMap();
  bindBubble();
  bindMinimap();
  bindScreens();
  bindLevelPanel();
  bindTools();
  bindModal();
  bindPlay();
  await loadIcons();
  const levels = await api('/api/levels');
  const wanted = Number(new URLSearchParams(location.search).get('level'));
  await loadLevel(levels.some((l) => l.level === wanted) ? wanted : levels[0].level);
}

main().catch((err) => {
  $('status').textContent = 'erreur : ' + err.message;
  console.error(err);
});
