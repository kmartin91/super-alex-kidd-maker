// Updates of the desktop app (Tauri updater plugin): a newer release on
// GitHub is offered once the menu shows, or from Paramètres. Nothing in the
// browser version.

import { ask, tell } from './modal.js';
import { toast } from './toast.js';
import { t } from './i18n.js';

const tauri = window.__TAURI__;
export const canUpdate = () => !!(tauri && tauri.updater);

// quiet: say nothing when there is no update (the check at startup).
export async function checkUpdate({ quiet = false } = {}) {
  if (!canUpdate()) return;
  let update;
  try {
    update = await tauri.updater.check();
  } catch (err) {
    if (!quiet) tell(t('Impossible de vérifier les mises à jour : {message}', { message: err.message || err }));
    return;
  }
  if (!update) { if (!quiet) toast(t('Tu as déjà la dernière version')); return; }
  const news = update.body ? t('La version {version} est sortie : {body}', { version: update.version, body: update.body })
    : t('La version {version} est sortie.', { version: update.version });
  const yes = await ask(news + ' ' + t('L\'installer maintenant ? Le Maker redémarrera (tes niveaux sont gardés).'),
    { title: t('Nouvelle version'), ok: t('Installer') });
  if (!yes) return;
  toast(t('Téléchargement de la mise à jour…'));
  try {
    await update.downloadAndInstall();
    await tauri.process.relaunch();
  } catch (err) {
    tell(t('La mise à jour a échoué : {message}', { message: err.message || err }));
  }
}
