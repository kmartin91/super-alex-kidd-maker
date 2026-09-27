// Undo / redo: snapshots of the editable parts of the level model.

import { state, setDirty } from './state.js';
import { render } from './render.js';

const FIELDS = ['screens', 'entities', 'specials', 'grid', 'columns', 'surprises', 'themeMusic', 'start', 'clear', 'vehicle'];
const LIMIT = 200;

function snapshot() {
  const d = {};
  for (const k of FIELDS) d[k] = state.model[k];
  return JSON.stringify(d);
}

function restore(snap) {
  Object.assign(state.model, JSON.parse(snap));
  state.cell = null;
  state.selected = null;
  setDirty(true);
  render();
}

let wasDirty = false;

// Call before every change.
export function pushUndo() {
  wasDirty = state.dirty;
  state.undo.push(snapshot());
  if (state.undo.length > LIMIT) state.undo.shift();
  state.redo = [];
  setDirty(true);
}

// Forgets the last pushUndo() when nothing changed after all.
export function dropUndo() {
  state.undo.pop();
  setDirty(wasDirty);
}

export function undo() {
  if (!state.undo.length) return;
  state.redo.push(snapshot());
  restore(state.undo.pop());
}

export function redo() {
  if (!state.redo.length) return;
  state.undo.push(snapshot());
  restore(state.redo.pop());
}

export function clearHistory() {
  state.undo = [];
  state.redo = [];
}
