// The game is played in the desktop app only. In a browser (npm run maker),
// the Maker still edits levels and shows the online ones, but its Play
// buttons send to the app: an online level opens there on its page (the link
// superalexkiddmaker://play/CODE, main.js), the others get a word about it.
// The UI tests (maker/tests/, Chrome started with --enable-automation) play
// in the page.

import { h, openModal, closeModal } from './modal.js';
import { t } from './i18n.js';

export const canPlay = Boolean(window.__TAURI__) || navigator.webdriver === true;
const DOWNLOAD = 'https://github.com/kmartin91/super-alex-kidd-maker/releases/latest';

export const downloadButton = (cls = 'key.plain') =>
  h(`button.${cls}`, { textContent: t('Télécharger l\'app'), onclick: () => window.open(DOWNLOAD, '_blank', 'noopener') });

// An online level, in the app.
export function playInApp(code) {
  h('a', { href: `superalexkiddmaker://play/${code}` }).click();
}

export function tellPlayInApp() {
  const ok = h('button.key.go', { textContent: 'OK', onclick: () => closeModal() });
  openModal(t('Jouer dans l\'app'), h('div.dialog', {},
    h('p', { textContent: t('Le jeu se lance dans l\'app Super Alex Kidd Maker, pas dans le navigateur.') }),
    h('div.dialog-actions', {}, downloadButton(), ok)));
  ok.focus();
}
