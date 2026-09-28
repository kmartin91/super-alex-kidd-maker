// Game controllers, for the menu and the game: what is pressed, as actions
// (the four directions, jump, punch, start). A pad the browser knows (its
// "standard" layout) works as it is; any other one (an arcade stick, a USB
// pad...) is set up once in Paramètres > Manette (js/pad-setup.js): each
// action is then one of its buttons, or a position of one of its axes.
// Settings are kept per model of pad (its id).

import { pref, setPref } from './prefs.js';

export const ACTIONS = ['up', 'down', 'left', 'right', 'jump', 'punch', 'start'];

// A binding is { button: i } or { axis: i, value: v }: the axis near v.
const STANDARD = {
  up: [{ button: 12 }, { axis: 1, value: -1 }],
  down: [{ button: 13 }, { axis: 1, value: 1 }],
  left: [{ button: 14 }, { axis: 0, value: -1 }],
  right: [{ button: 15 }, { axis: 0, value: 1 }],
  jump: [{ button: 0 }, { button: 3 }],
  punch: [{ button: 1 }, { button: 2 }],
  start: [{ button: 9 }],
};
// Other pads until they are set up: most have their d-pad on the first two
// axes; their buttons are anyone's guess.
const GENERIC = {
  up: [{ axis: 1, value: -1 }],
  down: [{ axis: 1, value: 1 }],
  left: [{ axis: 0, value: -1 }],
  right: [{ axis: 0, value: 1 }],
  jump: [{ button: 0 }],
  punch: [{ button: 1 }],
  start: [{ button: 9 }],
};

export const connectedPads = () => [...(navigator.getGamepads ? navigator.getGamepads() : [])].filter(Boolean);

const saved = () => pref('pads', {}) || {};
export const isSetUp = (g) => Object.hasOwn(saved(), g.id);
export const bindingsOf = (g) => saved()[g.id] || (g.mapping === 'standard' ? STANDARD : GENERIC);

export function saveBindings(g, bindings) {
  setPref('pads', { ...saved(), [g.id]: bindings });
}

export function forgetBindings(g) {
  const all = saved();
  delete all[g.id];
  setPref('pads', all);
}

const buttonDown = (b) => !!b && (b.pressed || b.value > 0.5);

// Axes reporting a d-pad as one value (a "hat") rest beyond ±1: never pressed then.
function active(g, b) {
  if (b.button !== undefined) return buttonDown(g.buttons[b.button]);
  const v = g.axes[b.axis];
  if (v === undefined || Math.abs(v) > 1.05) return false;
  // ±1: past half-way (sticks, d-pads on two axes); other values: near it (hats).
  return Math.abs(b.value) > 0.9 ? v * Math.sign(b.value) > 0.5 : Math.abs(v - b.value) < 0.3;
}

// Every action: pressed on any pad?
export function readPads() {
  const out = Object.fromEntries(ACTIONS.map((a) => [a, false]));
  for (const g of connectedPads()) {
    const map = bindingsOf(g);
    for (const a of ACTIONS) if ((map[a] || []).some((b) => active(g, b))) out[a] = true;
  }
  return out;
}

// Any button of any pad (to leave a title screen).
export const anyButton = () => connectedPads().some((g) => g.buttons.some(buttonDown));

// ------------------------------------------------------------ setting up
// What the pad shows at rest, then the first thing that differs from it.
export function snapshot(g) {
  return { buttons: g.buttons.map(buttonDown), axes: [...g.axes] };
}

export function firstChange(g, rest) {
  for (let i = 0; i < g.buttons.length; i++) if (buttonDown(g.buttons[i]) && !rest.buttons[i]) return { button: i };
  for (let i = 0; i < g.axes.length; i++) {
    const v = g.axes[i];
    if (Math.abs(v) <= 1.05 && Math.abs(v - rest.axes[i]) > 0.5) {
      return { axis: i, value: Math.abs(v) > 0.9 ? Math.sign(v) : Math.round(v * 1000) / 1000 };
    }
  }
  return null;
}
