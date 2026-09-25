// A single dialog sheet, filled by whoever opens it.

import { $ } from './dom.js';

let onClose = null;

export function openModal(title, body, { wide = false, closed = null } = {}) {
  $('modalTitle').textContent = title;
  $('modalBody').replaceChildren(body);
  $('modal').classList.toggle('wide', wide);
  $('modal').hidden = false;
  onClose = closed;
}

export function closeModal() {
  if ($('modal').hidden) return false;
  $('modal').hidden = true;
  if (onClose) onClose();
  onClose = null;
  return true;
}

export function bindModal() {
  $('modalClose').addEventListener('click', closeModal);
  $('modal').addEventListener('mousedown', (ev) => { if (ev.target === $('modal')) closeModal(); });
}

// Small DOM builder: h('div.card.active', { onclick }, child, 'text', ...)
export function h(spec, props = {}, ...children) {
  const [tag, ...classes] = spec.split('.');
  const el = document.createElement(tag || 'div');
  if (classes.length) el.className = classes.join(' ');
  for (const [k, v] of Object.entries(props)) {
    if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(el.dataset, v);
    else el[k] = v;
  }
  el.append(...children.filter((c) => c !== null && c !== undefined && c !== false));
  return el;
}
