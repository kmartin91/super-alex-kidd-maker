// A single dialog sheet, filled by whoever opens it.

import { $ } from './dom.js';
import { t } from './i18n.js';

let onClose = null, locked = false;

// locked: no way to close it but finishing what it asks.
export function openModal(title, body, { wide = false, closed = null, lock = false } = {}) {
  $('modalTitle').textContent = title;
  $('modalBody').replaceChildren(body);
  $('modal').classList.toggle('wide', wide);
  $('modalClose').hidden = lock;
  $('modal').hidden = false;
  onClose = closed;
  locked = lock;
}

export function closeModal({ force = false } = {}) {
  if ($('modal').hidden || (locked && !force)) return false;
  locked = false;
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

// In-app confirm() / alert() / prompt(): the web view of the desktop app does
// not show the browser's own dialogs (confirm() there always says no).
function dialog(title, text, buttons, input = null) {
  return new Promise((resolve) => {
    let done = false;
    const finish = (v) => { if (!done) { done = true; resolve(v); closeModal({ force: true }); } };
    const actions = h('div.dialog-actions', {}, ...buttons.map(([label, value, cls]) =>
      h(`button.key${cls ? '.' + cls : ''}`, { textContent: label, onclick: () => finish(value === INPUT ? input.value.trim() || null : value) })));
    if (input) input.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') finish(input.value.trim() || null); });
    openModal(title, h('div.dialog', {}, text ? h('p', { textContent: text }) : null, input, actions),
      { closed: () => { if (!done) { done = true; resolve(buttons.find(([, , , cancel]) => cancel)?.[1] ?? null); } } });
    (input || actions.lastChild).focus();
    if (input) input.select();
  });
}
const INPUT = Symbol('input');

export function ask(text, { title = t('Confirmer'), ok = t('Oui'), cancel = t('Annuler'), danger = false } = {}) {
  return dialog(title, text, [[cancel, false, 'plain', true], [ok, true, danger ? 'danger' : 'go']]);
}

export function tell(text, title = t('Oups')) {
  return dialog(title, text, [['OK', undefined, 'go', true]]);
}

export function askText(title, value = '', { text = '', ok = 'OK', placeholder = '' } = {}) {
  const input = h('input.name-input', { value, placeholder, maxLength: 40 });
  return dialog(title, text, [[t('Annuler'), null, 'plain', true], [ok, INPUT, 'go']], input);
}
