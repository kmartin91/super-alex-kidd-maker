// The user's ROM: asked once, checked, then kept in this browser (it is never
// sent anywhere).

import { setRom } from './backend.js';
import { openModal, closeModal, h } from './modal.js';
import { icon } from './icons.js';

// Resolves once a valid ROM is in place. `first`: the Maker cannot start
// without one, so the sheet cannot be closed.
export function askRom({ first = true } = {}) {
  return new Promise((resolve) => {
    const message = h('p.rom-error', { hidden: true });
    // No `accept` filter: macOS does not know the .sms extension and would hide
    // the file. The ROM is checked by its CRC anyway.
    const input = h('input', { type: 'file', hidden: true });
    const take = async (file) => {
      if (!file) return;
      try {
        await setRom(new Uint8Array(await file.arrayBuffer()));
        closeModal({ force: true });
        resolve();
      } catch (err) {
        message.textContent = err.message;
        message.hidden = false;
      }
    };
    input.addEventListener('change', () => take(input.files[0]));
    const zone = h('button.rom-drop', { id: 'romDrop', onclick: () => input.click() },
      icon('box', 5), h('b', { textContent: 'Glisse ta ROM ici' }), h('span', { textContent: 'ou clique pour la choisir (fichier .sms)' }));
    zone.addEventListener('dragover', (ev) => { ev.preventDefault(); zone.classList.add('over'); });
    zone.addEventListener('dragleave', () => zone.classList.remove('over'));
    zone.addEventListener('drop', (ev) => { ev.preventDefault(); zone.classList.remove('over'); take(ev.dataTransfer.files[0]); });
    openModal(first ? 'Bienvenue !' : 'Changer de ROM', h('div.rom-setup', {},
      h('p', { textContent: 'Le Maker a besoin de ta ROM d\'Alex Kidd in Miracle World (version USA/Europe, révision 0), ' +
        'copiée depuis ta cartouche. Les graphismes, les sons et les niveaux du jeu viennent de là.' }),
      zone, input, message,
      h('p.hint', { textContent: 'Elle reste sur cet ordinateur, dans ce navigateur : elle n\'est envoyée nulle part.' })),
    { lock: first });
  });
}
