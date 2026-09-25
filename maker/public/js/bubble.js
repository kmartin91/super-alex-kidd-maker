// Bubble above the selected entity: its name, its variant, delete.

import { $ } from './dom.js';
import { state } from './state.js';
import { listOrigin } from './level.js';
import { entityBox, entityName, specialName, selectedRecord, deleteSelected } from './entities.js';
import { pushUndo } from './history.js';
import { render } from './render.js';

export function renderBubble() {
  const bubble = $('bubble');
  const sel = state.selected;
  const e = sel && selectedRecord(sel);
  const o = e && listOrigin(sel.list);
  if (!e || !o || state.gesture) { bubble.hidden = true; return; }
  const z = state.zoom;
  const box = entityBox(e, o);
  bubble.hidden = false;
  const pad = $('map').offsetLeft; // the map sits inside a padded frame
  bubble.style.left = `${pad + (box.x + box.w / 2) * z}px`;
  bubble.style.top = `${pad + box.y * z - 10}px`;
  $('bubbleName').textContent = sel.special ? specialName(e.type) : entityName(e.type);
  $('bubbleLock').hidden = !sel.special;
  $('bubbleVariant').hidden = !!sel.special;
  $('variantValue').textContent = e.data;
}

function changeVariant(delta) {
  const e = selectedRecord();
  if (!e) return;
  pushUndo();
  e.data = (e.data + delta + 256) % 256;
  render();
}

export function bindBubble() {
  $('variantDown').addEventListener('click', () => changeVariant(-1));
  $('variantUp').addEventListener('click', () => changeVariant(1));
  $('bubbleDelete').addEventListener('click', deleteSelected);
  $('bubble').addEventListener('mousedown', (ev) => ev.stopPropagation());
}
