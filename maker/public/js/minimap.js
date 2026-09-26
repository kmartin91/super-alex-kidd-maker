// Bottom bar: the whole level in small, with the visible part framed.
// Click or drag on it to move around.

import { $ } from './dom.js';
import { state, SCREEN_W, SCREEN_H, SCREEN_PX_W, SCREEN_PX_H } from './state.js';

let scale = 1; // minimap pixels per block

export function renderMinimap() {
  const canvas = $('minimap');
  const bw = state.model.columns * SCREEN_W, bh = state.model.rows * SCREEN_H;
  const room = canvas.parentElement.getBoundingClientRect();
  scale = Math.max(1, Math.min(Math.floor((room.height - 12) / bh), Math.floor((room.width - 12) / bw))) || 1;
  canvas.width = bw * scale;
  canvas.height = bh * scale;
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#262a3d';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  for (let row = 0; row < state.model.rows; row++) {
    for (let col = 0; col < state.model.columns; col++) {
      const cell = state.model.grid[row][col];
      if (!cell) continue;
      const blocks = state.model.screens[cell.screen].blocks;
      for (let i = 0; i < blocks.length; i++) {
        ctx.fillStyle = state.blockColors[blocks[i]];
        ctx.fillRect((col * SCREEN_W + i % SCREEN_W) * scale, (row * SCREEN_H + Math.floor(i / SCREEN_W)) * scale, scale, scale);
      }
    }
  }
  renderViewport();
}

// Frame of the visible part of the map.
export function renderViewport() {
  const wrap = $('mapWrap'), frame = $('viewport');
  const k = scale / (16 * state.zoom); // minimap pixels per map canvas pixel
  const levelW = state.model.columns * SCREEN_PX_W * state.zoom, levelH = state.model.rows * SCREEN_PX_H * state.zoom;
  frame.style.left = `${$('minimap').offsetLeft + wrap.scrollLeft * k}px`;
  frame.style.top = `${$('minimap').offsetTop + wrap.scrollTop * k}px`;
  frame.style.width = `${Math.min(wrap.clientWidth, levelW) * k}px`;
  frame.style.height = `${Math.min(wrap.clientHeight, levelH) * k}px`;
}

function scrollTo(ev) {
  const r = $('minimap').getBoundingClientRect();
  const wrap = $('mapWrap');
  const k = (16 * state.zoom) / scale;
  wrap.scrollLeft = (ev.clientX - r.left) * k - wrap.clientWidth / 2;
  wrap.scrollTop = (ev.clientY - r.top) * k - wrap.clientHeight / 2;
}

export function bindMinimap() {
  const mm = $('minimap');
  let dragging = false;
  mm.addEventListener('mousedown', (ev) => { dragging = true; scrollTo(ev); });
  window.addEventListener('mousemove', (ev) => { if (dragging) scrollTo(ev); });
  window.addEventListener('mouseup', () => { dragging = false; });
  $('mapWrap').addEventListener('scroll', renderViewport);
  window.addEventListener('resize', renderMinimap);
}
