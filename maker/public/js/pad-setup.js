// Paramètres > Manette: which button or direction of the pad does what. The
// lines light up as the pad is used; "Configurer" asks for each action in
// turn (press it on the pad), for pads the browser doesn't know.

import { openModal, closeModal, h } from './modal.js';
import { toast } from './toast.js';
import { t } from './i18n.js';
import { ACTIONS, connectedPads, bindingsOf, isSetUp, saveBindings, forgetBindings, readPads, snapshot, firstChange } from './gamepad.js';

const LABELS = { up: 'Haut', down: 'Bas', left: 'Gauche', right: 'Droite', jump: 'Sauter', punch: 'Coup de poing', start: 'Start / pause' };

function bindingText(b) {
  if (b.button !== undefined) return t('bouton {n}', { n: b.button + 1 });
  if (Math.abs(b.value) > 0.9) return t('axe {n} {dir}', { n: b.axis + 1, dir: b.value < 0 ? '−' : '+' });
  return t('axe {n} = {value}', { n: b.axis + 1, value: b.value });
}

export function openPadSetup() {
  const name = h('p.hint.pad-name', {});
  const rows = Object.fromEntries(ACTIONS.map((a) => [a, h('div.pad-row', {},
    h('b', { textContent: t(LABELS[a]) }), h('span.pad-binding', {}))]));
  const setUp = h('button.key.go', { textContent: t('Configurer') });
  const reset = h('button.key.plain', { textContent: t('Par défaut') });
  const body = h('div.pad-setup', {},
    name,
    h('p.hint', { textContent: t('Dans les menus, Sauter valide et Coup de poing revient en arrière.') }),
    h('div.pad-rows', {}, ...ACTIONS.map((a) => rows[a])),
    h('div.dialog-actions', {}, reset, setUp, h('button.key.plain', { textContent: 'OK', onclick: () => closeModal() })));
  openModal(t('Manette'), body);

  let asking = null; // { index, rest, bindings, pad, released }
  const pad = () => connectedPads()[0] || null;

  const draw = () => {
    const g = pad();
    name.textContent = g ? g.id : t('Aucune manette détectée : branche-la, puis appuie sur un de ses boutons.');
    setUp.disabled = !g;
    reset.disabled = !g || !isSetUp(g);
    if (!asking) {
      const map = g ? bindingsOf(g) : {};
      for (const a of ACTIONS) rows[a].querySelector('.pad-binding').textContent = (map[a] || []).map(bindingText).join(t(' ou ')) || '—';
    }
  };

  const ask = (index) => {
    for (const a of ACTIONS) rows[a].classList.remove('asking');
    if (index >= ACTIONS.length) {
      saveBindings(asking.pad, asking.bindings);
      asking = null;
      setUp.textContent = t('Configurer');
      toast(t('Manette configurée'));
      draw();
      return;
    }
    const a = ACTIONS[index];
    rows[a].classList.add('asking');
    rows[a].querySelector('.pad-binding').textContent = t('Appuie…');
    asking.index = index;
    asking.released = false;
  };

  setUp.addEventListener('click', () => {
    if (asking) { asking = null; setUp.textContent = t('Configurer'); for (const a of ACTIONS) rows[a].classList.remove('asking'); draw(); return; }
    const g = pad();
    if (!g) return;
    asking = { pad: g, bindings: {}, rest: snapshot(g) };
    setUp.textContent = t('Annuler');
    ask(0);
  });
  reset.addEventListener('click', () => { const g = pad(); if (g) forgetBindings(g); draw(); });

  // Each frame: the pad's state (the lines light up), or the next answer.
  const tick = () => {
    if (!body.isConnected) return;
    if (asking) {
      const g = connectedPads().find((p) => p.index === asking.pad.index);
      if (g) {
        if (!asking.released) {
          // Wait for everything to be back at rest before the next question.
          if (!firstChange(g, asking.rest)) asking.released = true;
        } else {
          const b = firstChange(g, asking.rest);
          if (b) {
            asking.bindings[ACTIONS[asking.index]] = [b];
            rows[ACTIONS[asking.index]].querySelector('.pad-binding').textContent = bindingText(b);
            ask(asking.index + 1);
          }
        }
      }
    } else {
      const now = readPads();
      for (const a of ACTIONS) rows[a].classList.toggle('on', now[a]);
      draw();
    }
    requestAnimationFrame(tick);
  };
  draw();
  requestAnimationFrame(tick);
}
