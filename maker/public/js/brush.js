// The item in hand (state.part) and what it paints.

import { state } from './state.js';
import { renderPalette } from './palette.js';
import { renderGhost } from './render.js';

export function selectPart(part) {
  if (state.part.kind !== 'eraser' && state.part.kind !== 'select') state.lastPart = state.part;
  state.part = part;
  state.selected = null;
  if (part.kind !== 'select') state.selection = null;
  renderPalette();
  renderGhost();
}

export const isBlockPart = (part) => ['terrain', 'block', 'stamp', 'eraser'].includes(part.kind);

// Block id painted by a single-block part (stamps have no single block).
export function brushBlock(part) {
  const p = state.model.parts;
  return part.kind === 'terrain' ? p.terrains[part.index].fill
    : part.kind === 'eraser' ? p.eraser : part.id;
}

export function samePart(a, b) {
  return JSON.stringify(a) === JSON.stringify(b);
}
