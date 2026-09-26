// Setting up a janken opponent (the "Paramétrer" button of its bubble): what
// it says before the match, and the throws it plays. Kept in its special
// record (text, moves); backend.js writes them into the level's patch.

import { state } from './state.js';
import { openModal, closeModal, h } from './modal.js';
import { bossText } from './backend.js';
import { wrapText, encodeMessage } from './rom/text.js';
import { textBoxImage } from './graphics.js';
import { specialName, BOSSES } from './entities.js';
import { pushUndo } from './history.js';
import { render } from './render.js';

const MAX_MOVES = 15;
const THROWS = [
  { id: 0, name: 'Pierre', icon: '✊' },
  { id: 2, name: 'Feuille', icon: '✋' },
  { id: 1, name: 'Ciseaux', icon: '✌️' },
];
const throwOf = (id) => THROWS.find((t) => t.id === id);

export const isBoss = (type) => BOSSES.some((b) => b.type === type);

// The box as the game will show it, in its own font.
function preview(lines) {
  const w = Math.max(3, ...lines.map((l) => l.length));
  const blank = () => new Array(w + 2).fill(0xB0);
  const rows = [blank()];
  for (const line of lines) {
    rows.push([0xB0, ...[...line.padEnd(w)].map((c) => c.charCodeAt(0) + 0x90), 0xB0]);
    rows.push(blank());
  }
  const c = textBoxImage(state.video, rows);
  c.className = 'boss-preview';
  c.style.width = `${c.width * 2}px`;
  return c;
}

export function openBossSheet(rec) {
  const original = bossText(rec.data);
  const originalText = original.lines.join(' ').replace(/ +/g, ' ').trim();
  let moves = (rec.moves || []).slice();

  const text = h('textarea.boss-text', { value: rec.text || originalText, rows: 4, spellcheck: false });
  const shown = h('div.boss-preview-wrap', {});
  const count = h('div.boss-count', {});
  const ok = h('button.key.go', { textContent: 'OK' });

  const refresh = () => {
    const lines = wrapText(text.value);
    const fits = encodeMessage(lines).length <= original.size;
    shown.replaceChildren(preview(lines));
    count.textContent = fits ? `${lines.length} ligne${lines.length > 1 ? 's' : ''}`
      : `Trop long pour la boîte du jeu : raccourcis le texte`;
    count.classList.toggle('bad', !fits);
    ok.disabled = !fits;
  };
  text.addEventListener('input', refresh);

  const seq = h('div.boss-moves', {});
  const addButtons = THROWS.map((t) => h('button.key.small.boss-add', {
    onclick: () => { if (moves.length < MAX_MOVES) { moves.push(t.id); drawMoves(); } },
  }, h('span.boss-hand', { textContent: t.icon }), `+ ${t.name}`));
  const drawMoves = () => {
    seq.replaceChildren(...(moves.length ? moves.map((m, i) => h('button.boss-move', {
      title: 'Retirer ce coup',
      onclick: () => { moves.splice(i, 1); drawMoves(); },
    }, h('small', { textContent: i + 1 }), h('span.boss-hand', { textContent: throwOf(m).icon }), throwOf(m).name))
      : [h('span.boss-empty', { textContent: 'Aucun coup choisi : il joue comme dans le jeu.' })]));
    addButtons.forEach((b) => { b.disabled = moves.length >= MAX_MOVES; });
  };

  const same = state.model.specials.flat().filter((r) => r !== rec && r.type === rec.type && (r.data >> 1) === (rec.data >> 1)).length;

  ok.addEventListener('click', () => {
    pushUndo();
    const t = text.value.trim();
    if (t && t !== originalText) rec.text = t; else delete rec.text;
    if (moves.length) rec.moves = moves; else delete rec.moves;
    closeModal();
    render();
  });

  openModal(`${specialName(rec.type)} : paramétrer`, h('div.boss-sheet', {},
    h('h3', { textContent: 'Ce qu\'il dit avant le match' }),
    text,
    h('div.boss-row', {}, count,
      h('button.key.small.plain', { textContent: 'Texte d\'origine', onclick: () => { text.value = originalText; refresh(); } })),
    shown,
    h('h3', { textContent: 'Ses coups à pierre-feuille-ciseaux' }),
    h('p.hint', { textContent: `Joués dans l'ordre, égalités comprises, puis la liste recommence. ${MAX_MOVES} coups au plus.` }),
    seq,
    h('div.boss-row', {}, h('div.boss-adds', {}, ...addButtons),
      h('button.key.small.plain', { textContent: 'Effacer', onclick: () => { moves = []; drawMoves(); } })),
    same ? h('p.hint', { textContent: `Les autres ${specialName(rec.type)} de ce niveau partagent ces réglages (ceux du premier comptent).` }) : null,
    h('div.dialog-actions', {}, h('button.key.plain', { textContent: 'Annuler', onclick: () => closeModal() }), ok)), { wide: true });
  refresh();
  drawMoves();
  text.focus();
}
