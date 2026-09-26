// Draws the level map (blocks, grid, entities), the preview of the item in
// hand (ghost canvas on top of the map), and refreshes the bubble and minimap.

import { $ } from './dom.js';
import { state, SCREEN_W, BLOCK, SCREEN_PX_W, SCREEN_PX_H } from './state.js';
import { levelSize, listOrigin, cellUsage } from './level.js';
import { blockIsSolid } from './blocks.js';
import { entityBox, entityName, specialName, entityColor, isSelected, GOAL_TYPE, startBox, startIcon } from './entities.js';
import { partCanvas } from './parts.js';
import { isBlockPart } from './brush.js';
import { renderBubble } from './bubble.js';
import { renderMinimap } from './minimap.js';
import { renderScreenTools } from './screens.js';
import { renderLevelPanel } from './level-panel.js';

const INK = '#3b2f22', YELLOW = '#ff8a1f', RED = '#ff4d3d'; // YELLOW: what is selected (orange)

function drawScreens(ctx, z) {
  for (let row = 0; row < state.model.rows; row++) {
    for (let col = 0; col < state.model.columns; col++) {
      const cell = state.model.grid[row][col];
      const ox = col * SCREEN_PX_W, oy = row * SCREEN_PX_H;
      if (!cell) continue;
      const blocks = state.model.screens[cell.screen].blocks;
      for (let i = 0; i < blocks.length; i++) {
        const bx = ox + (i % SCREEN_W) * BLOCK, by = oy + Math.floor(i / SCREEN_W) * BLOCK;
        ctx.drawImage(state.blockCanvases[blocks[i]] || state.blockCanvases[0], bx * z, by * z, BLOCK * z, BLOCK * z);
        if (state.showSolid && blockIsSolid(blocks[i])) {
          ctx.fillStyle = 'rgba(228, 65, 47, 0.45)';
          ctx.fillRect(bx * z, by * z, BLOCK * z, BLOCK * z);
        }
      }
      // Levels whose layout is fixed repeat some screens: editing one copy
      // changes the others, so they are marked.
      if (!state.model.canExtend && cellUsage(cell.screen) > 1) {
        ctx.fillStyle = 'rgba(255, 201, 51, 0.10)';
        ctx.fillRect(ox * z, oy * z, SCREEN_PX_W * z, SCREEN_PX_H * z);
      }
    }
  }
}

function drawGrid(ctx, z, w, h) {
  const line = (x0, y0, x1, y1) => { ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke(); };
  ctx.strokeStyle = 'rgba(255,255,255,0.10)';
  ctx.lineWidth = 1;
  for (let x = BLOCK; x < w; x += BLOCK) line(x * z + 0.5, 0, x * z + 0.5, h * z);
  for (let y = BLOCK; y < h; y += BLOCK) line(0, y * z + 0.5, w * z, y * z + 0.5);
}

// Screen boundaries and numbers, always shown.
function drawScreenEdges(ctx, z, w, h) {
  ctx.strokeStyle = 'rgba(25, 18, 28, 0.55)';
  ctx.lineWidth = 2;
  ctx.setLineDash([6, 6]);
  for (let x = SCREEN_PX_W; x < w; x += SCREEN_PX_W) { ctx.beginPath(); ctx.moveTo(x * z, 0); ctx.lineTo(x * z, h * z); ctx.stroke(); }
  for (let y = SCREEN_PX_H; y < h; y += SCREEN_PX_H) { ctx.beginPath(); ctx.moveTo(0, y * z); ctx.lineTo(w * z, y * z); ctx.stroke(); }
  ctx.setLineDash([]);
}

function label(ctx, text, x, y) {
  ctx.font = '600 12px Fredoka, system-ui, sans-serif';
  const w = ctx.measureText(text).width + 8;
  ctx.fillStyle = INK;
  ctx.fillRect(x, y - 15, w, 15);
  ctx.fillStyle = '#ffffff';
  ctx.fillText(text, x + 4, y - 4);
}

function drawEntities(ctx, z) {
  const draw = (lists, special) => lists.forEach((recs, list) => {
    const o = listOrigin(list);
    if (!o) return;
    recs.forEach((e, i) => {
      if (e.type === undefined) return;
      const box = entityBox(e, o);
      const icon = state.icons[e.type];
      const sel = isSelected(list, i, special);
      if (icon) {
        ctx.drawImage(icon.canvas, box.x * z, box.y * z, box.w * z, box.h * z);
      } else {
        ctx.fillStyle = entityColor(e.type);
        ctx.fillRect(box.x * z, box.y * z, box.w * z, box.h * z);
      }
      if (sel) {
        ctx.strokeStyle = YELLOW;
        ctx.lineWidth = 3;
        ctx.strokeRect(box.x * z - 2, box.y * z - 2, box.w * z + 4, box.h * z + 4);
      }
      // Bosses and fixed objects keep their type; the level end is obvious.
      if (special && e.type !== GOAL_TYPE && !sel) label(ctx, specialName(e.type), box.x * z, box.y * z - 2);
      if (!special && !icon) label(ctx, entityName(e.type), box.x * z, box.y * z - 2);
    });
  });
  draw(state.model.specials, true);
  draw(state.model.entities, false);
}

function drawStart(ctx, z) {
  const b = startBox();
  if (!b) return;
  ctx.drawImage(startIcon(), b.x * z, (b.y + 1) * z, b.w * z, b.h * z); // sprites are drawn a line lower
  label(ctx, 'Départ', b.x * z, b.y * z - 2);
}

export function render() {
  const { w, h } = levelSize();
  const z = state.zoom;
  const canvas = $('map');
  if (canvas.width !== w * z || canvas.height !== h * z) {
    canvas.width = $('ghost').width = w * z;
    canvas.height = $('ghost').height = h * z;
  }
  const ctx = canvas.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  ctx.fillStyle = '#262a3d';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  drawScreens(ctx, z);
  if (state.showGrid) drawGrid(ctx, z, w, h);
  drawScreenEdges(ctx, z, w, h);
  drawEntities(ctx, z);
  drawStart(ctx, z);
  renderGhost();
  renderBubble();
  renderMinimap();
  renderScreenTools();
  renderLevelPanel();
}

// Preview of the item in hand under the mouse, and the rectangle being drawn.
export function renderGhost() {
  const canvas = $('ghost');
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.imageSmoothingEnabled = false;
  const z = state.zoom;
  if (state.rect) {
    const r = state.rect;
    const x = r.x0 * BLOCK * z, y = r.y0 * BLOCK * z;
    const w = (r.x1 - r.x0 + 1) * BLOCK * z, h = (r.y1 - r.y0 + 1) * BLOCK * z;
    ctx.fillStyle = 'rgba(255, 138, 31, 0.25)';
    ctx.fillRect(x, y, w, h);
    ctx.strokeStyle = YELLOW;
    ctx.lineWidth = 3;
    ctx.strokeRect(x, y, w, h);
    return;
  }
  const hv = state.hover;
  if (!hv || state.gesture) return;
  const part = state.part;
  if (isBlockPart(part)) {
    const x = Math.floor(hv.x / BLOCK) * BLOCK * z, y = Math.floor(hv.y / BLOCK) * BLOCK * z;
    if (part.kind === 'eraser') {
      ctx.strokeStyle = RED;
      ctx.lineWidth = 3;
      ctx.strokeRect(x + 1, y + 1, BLOCK * z - 2, BLOCK * z - 2);
      return;
    }
    const pic = part.kind === 'terrain'
      ? state.blockCanvases[state.model.parts.terrains[part.index].fill] : partCanvas(part);
    ctx.globalAlpha = 0.75;
    ctx.drawImage(pic, x, y, pic.width * z, pic.height * z);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = YELLOW;
    ctx.lineWidth = 2;
    ctx.strokeRect(x, y, pic.width * z, pic.height * z);
  } else {
    const type = part.kind === 'goal' ? GOAL_TYPE : part.type;
    const p = snapEntity(hv);
    const box = entityBox({ type, x: p.x, y: p.y }, { x: 0, y: 0 });
    const icon = state.icons[type];
    ctx.globalAlpha = 0.75;
    if (icon) ctx.drawImage(icon.canvas, box.x * z, box.y * z, box.w * z, box.h * z);
    else { ctx.fillStyle = entityColor(type); ctx.fillRect(box.x * z, box.y * z, box.w * z, box.h * z); }
    ctx.globalAlpha = 1;
  }
}

// Entities are placed on a half-block grid.
export function snapEntity(p) {
  return { x: Math.round(p.x / 8) * 8, y: Math.round(p.y / 8) * 8 };
}
