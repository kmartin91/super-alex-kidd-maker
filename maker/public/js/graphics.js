// Block images, decoded from the video memory of the running game.

import { state, BLOCK } from './state.js';

function cramColor(v) {
  const lv = [0, 85, 170, 255];
  return [lv[v & 3], lv[(v >> 2) & 3], lv[(v >> 4) & 3]];
}

// 512 tiles of 8x8 palette indices from VRAM (4 bitplanes per row).
function decodeTiles(vram) {
  const tiles = [];
  for (let t = 0; t < 512; t++) {
    const px = new Uint8Array(64);
    for (let y = 0; y < 8; y++) {
      const o = t * 32 + y * 4;
      for (let x = 0; x < 8; x++) {
        const bit = 7 - x;
        px[y * 8 + x] = ((vram[o] >> bit) & 1) | (((vram[o + 1] >> bit) & 1) << 1) |
          (((vram[o + 2] >> bit) & 1) << 2) | (((vram[o + 3] >> bit) & 1) << 3);
      }
    }
    tiles.push(px);
  }
  return tiles;
}

function drawWord(img, tiles, word, ox, oy, palette) {
  const tile = tiles[word & 0x1FF];
  const hflip = word & 0x200, vflip = word & 0x400;
  const pal = word & 0x800 ? 16 : 0;
  for (let y = 0; y < 8; y++) {
    for (let x = 0; x < 8; x++) {
      const sx = hflip ? 7 - x : x, sy = vflip ? 7 - y : y;
      const c = palette[pal + tile[sy * 8 + sx]];
      const o = ((oy + y) * BLOCK + ox + x) * 4;
      img.data[o] = c[0]; img.data[o + 1] = c[1]; img.data[o + 2] = c[2]; img.data[o + 3] = 255;
    }
  }
}

// Images of the blocks of `metatiles`, as the game draws them with `video`.
export function blockImages(video, metatiles) {
  const tiles = decodeTiles(video.vram);
  const palette = video.cram.map((v) => cramColor(v & 0x3F));
  return metatiles.map((words) => {
    const c = document.createElement('canvas');
    c.width = c.height = BLOCK;
    const ctx = c.getContext('2d');
    const img = ctx.createImageData(BLOCK, BLOCK);
    drawWord(img, tiles, words[0], 0, 0, palette);
    drawWord(img, tiles, words[1], 8, 0, palette);
    drawWord(img, tiles, words[2], 0, 8, palette);
    drawWord(img, tiles, words[3], 8, 8, palette);
    ctx.putImageData(img, 0, 0);
    return c;
  });
}

// Alex as the game shows him at the level start: sprite tiles 0-5 (two
// across, three down) with the sprite palette; transparent where color 0.
export function alexImage(video) {
  const tiles = decodeTiles(video.vram);
  const palette = video.cram.map((v) => cramColor(v & 0x3F));
  const c = document.createElement('canvas');
  c.width = 16;
  c.height = 24;
  const ctx = c.getContext('2d');
  const img = ctx.createImageData(16, 24);
  for (let t = 0; t < 6; t++) {
    const tile = tiles[256 + t], ox = (t % 2) * 8, oy = Math.floor(t / 2) * 8;
    for (let i = 0; i < 64; i++) {
      if (!tile[i]) continue;
      const col = palette[16 + tile[i]], o = ((oy + (i >> 3)) * 16 + ox + (i & 7)) * 4;
      img.data[o] = col[0]; img.data[o + 1] = col[1]; img.data[o + 2] = col[2]; img.data[o + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  return c;
}

// A text box as the game draws it: `rows` of name-table tiles (ASCII + $90
// for the characters, $B0 the box), background palette.
export function textBoxImage(video, rows) {
  const tiles = decodeTiles(video.vram);
  const palette = video.cram.map((v) => cramColor(v & 0x3F));
  const w = Math.max(...rows.map((r) => r.length));
  const c = document.createElement('canvas');
  c.width = w * 8;
  c.height = rows.length * 8;
  const ctx = c.getContext('2d');
  const img = ctx.createImageData(c.width, c.height);
  rows.forEach((row, ty) => row.forEach((code, tx) => {
    const tile = tiles[code];
    for (let i = 0; i < 64; i++) {
      const col = palette[tile[i]], o = ((ty * 8 + (i >> 3)) * c.width + tx * 8 + (i & 7)) * 4;
      img.data[o] = col[0]; img.data[o + 1] = col[1]; img.data[o + 2] = col[2]; img.data[o + 3] = 255;
    }
  }));
  ctx.putImageData(img, 0, 0);
  return c;
}

// Rebuilds the 256 block images from `state.video` and the level's metatiles.
export function buildBlockCanvases() {
  state.blockCanvases = blockImages(state.video, state.model.metatiles);
  // Average colour of each block, for the minimap.
  state.blockColors = state.blockCanvases.map((c) => {
    const d = c.getContext('2d').getImageData(0, 0, BLOCK, BLOCK).data;
    let r = 0, g = 0, b = 0;
    for (let i = 0; i < d.length; i += 4) { r += d[i]; g += d[i + 1]; b += d[i + 2]; }
    const n = d.length / 4;
    return `rgb(${r / n | 0},${g / n | 0},${b / n | 0})`;
  });
}

// Image of a w x h group of blocks (-1 = transparent).
export function blocksCanvas(cells, w, h) {
  const c = document.createElement('canvas');
  c.width = w * BLOCK;
  c.height = h * BLOCK;
  const ctx = c.getContext('2d');
  for (let j = 0; j < h; j++)
    for (let i = 0; i < w; i++)
      if (cells[j][i] >= 0) ctx.drawImage(state.blockCanvases[cells[j][i]], i * BLOCK, j * BLOCK);
  return c;
}
