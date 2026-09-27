// Levels online, in the Maker: publishing the level being edited (Menu ☰),
// and the "Niveaux en ligne" screen of the game menu (browse, search by
// code, play, like, keep).

import { state, setDirty } from './state.js';
import { openModal, closeModal, h, ask, tell, askText } from './modal.js';
import { toast } from './toast.js';
import { icon } from './icons.js';
import { pref, setPref } from './prefs.js';
import { rateDifficulty, starsText } from './difficulty.js';
import { levelThumbnail } from './render.js';
import { save, persistDoc, flushAutosave, showModel } from './storage.js';
import { saveMyLevel } from './backend.js';
import { seconds } from './challenge.js';
import { mainModel } from './bonus-zone.js';
import {
  sharedLevel, openShared, levelHash, isUnmodified, listOnline, getOnline, countPlay,
  setLike, likedHere, reportOnline, publishOnline, updateOnline, removeOnline, thumbnailUrl,
} from './online.js';
import { t } from './i18n.js';

const names = () => Object.fromEntries(state.levels.map((l) => [l.level, l.name]));
const formatCode = (c) => c.toUpperCase().replace(/[^0-9A-Z]/g, '').replace(/(.{3})(?=.)/g, '$1-');

// ------------------------------------------------------------ publishing
function publishBody(name, author) {
  const m = mainModel();
  let thumbnail = null;
  try { thumbnail = levelThumbnail(); } catch { /* none */ }
  return {
    name, author, base: state.doc.base, theme: m.theme || state.doc.base, kind: m.kind,
    difficulty: rateDifficulty(m).stars, clearTime: state.doc.cleared.time,
    level: sharedLevel(m, state.doc.base), thumbnail,
  };
}

function showCode(code, again) {
  const copy = h('button.key.small.plain', { textContent: t('Copier le code'), onclick: async () => {
    try { await navigator.clipboard.writeText(code); toast(t('Code copié')); } catch { toast(code); }
  } });
  // The level's page on the website's gallery, whose Play button opens the app.
  const url = `https://maker.kma.studio/levels/?code=${code}`;
  const link = h('button.key.small.plain', { textContent: t('Copier le lien'), onclick: async () => {
    try { await navigator.clipboard.writeText(url); toast(t('Lien copié')); } catch { toast(url); }
  } });
  openModal(again ? t('Niveau mis à jour') : t('Niveau publié !'), h('div.dialog.publish-done', {},
    h('p', { textContent: t('Donne ce code à tes amis : ils le tapent dans « Niveaux en ligne » pour jouer ton niveau.') }),
    h('div.level-code', { textContent: code }),
    h('div.dialog-actions', {}, copy, link, h('button.key.go', { textContent: 'OK', onclick: () => closeModal() }))));
}

export async function openPublish() {
  const m = mainModel();
  if (m.kind !== 'horizontal' && m.kind !== 'vertical') {
    tell(t('Seuls les niveaux horizontaux et verticaux peuvent être publiés pour l\'instant.'));
    return;
  }
  if (isUnmodified(m, state.doc.base)) {
    tell(t('C\'est un niveau du jeu tel quel : fais-en ton propre niveau avant de le publier.'));
    return;
  }
  if (!state.doc.id) {
    await save();
    if (!state.doc.id) return;
  }
  const hash = levelHash(m, state.doc.base);
  if (!state.doc.cleared || state.doc.cleared.hash !== hash) {
    tell(t('Pour publier, termine d\'abord ton niveau en le jouant depuis le début (et en réussissant son défi s\'il en a un), comme dans Mario Maker.'),
      t('Pas encore réussi'));
    return;
  }
  const published = state.doc.published;
  const name = h('input.name-input', { value: state.doc.name, maxLength: 40, placeholder: t('Nom du niveau') });
  const author = h('input.name-input', { value: pref('author', ''), maxLength: 24, placeholder: t('Ton pseudo (facultatif)') });
  const go = async () => {
    const n = name.value.trim(), a = author.value.trim();
    if (!n) { name.focus(); return; }
    setPref('author', a);
    closeModal();
    toast(published ? t('Mise à jour…') : t('Publication…'));
    try {
      if (published) {
        await updateOnline(published.code, published.token, publishBody(n, a));
        showCode(published.code, true);
      } else {
        const r = await publishOnline(publishBody(n, a));
        state.doc.published = { code: r.code, token: r.editToken };
        await persistDoc();
        showCode(r.code, false);
      }
    } catch (err) {
      tell(err.message);
    }
  };
  const remove = published && h('button.key.danger', { textContent: t('Retirer'), onclick: async () => {
    closeModal();
    if (!(await ask(t('Retirer ce niveau des niveaux en ligne ? Son code ne marchera plus.'),
      { title: t('Retirer'), ok: t('Retirer'), danger: true }))) return;
    try {
      await removeOnline(published.code, published.token);
      delete state.doc.published;
      await persistDoc();
      toast(t('Niveau retiré'));
    } catch (err) { tell(err.message); }
  } });
  openModal(published ? t('Mettre à jour en ligne') : t('Publier en ligne'), h('div.dialog.publish', {},
    h('p.hint', { textContent: published
      ? t('Ton niveau est en ligne (code {code}). Remplace-le par cette version, ou retire-le.', { code: published.code })
      : t('Tout le monde pourra le jouer, avec sa propre ROM : le fichier publié ne contient que ce que tu as créé.') }),
    h('label.field', {}, h('span', { textContent: t('Nom') }), name),
    h('label.field', {}, h('span', { textContent: t('Auteur') }), author),
    h('p.hint', { textContent: t('Réussi en {time} s · difficulté {stars}',
      { time: seconds(state.doc.cleared.time), stars: starsText(rateDifficulty(m).stars) }) }),
    h('div.dialog-actions', {}, remove || null,
      h('button.key.plain', { textContent: t('Annuler'), onclick: () => closeModal() }),
      h('button.key.go', { textContent: published ? t('Mettre à jour') : t('Publier'), onclick: go }))));
  name.focus();
}

// ------------------------------------------------------------ browsing
// Sort orders (labels translated when shown).
const SORTS = [['recent', 'Récents'], ['popular', 'Populaires'], ['likes', 'Les plus aimés'], ['easy', 'Faciles'], ['hard', 'Difficiles']];
let query = { sort: 'recent', q: '', offset: 0 };

function thumb(s, cls) {
  const url = thumbnailUrl(s);
  return url ? h(`img.${cls}`, { src: url, alt: '' }) : h(`span.${cls}.none`, {});
}

function summary(s) {
  const n = names();
  return [s.author ? t('par {author}', { author: s.author }) : null, n[s.theme], starsText(s.difficulty), `▶ ${s.plays}`, `♥ ${s.likes}`]
    .filter(Boolean).join(' · ');
}

// The online screen of the menu: `show(el)` puts an element in the menu,
// `ctx` gives back() and play(level) from the menu.
// code: open that level's page first (a link /?play=CODE).
export function onlinePanel(show, ctx, { code: first = null } = {}) {
  const list = h('div.menu-list.online-list', {});
  const more = h('button.menu-back.online-more', { textContent: t('Plus de niveaux'), hidden: true });
  const code = h('input.name-input.online-code', { placeholder: t('Code (XXX-XXX-XXX)'), maxLength: 11 });
  const search = h('input.name-input.online-search', { placeholder: t('Chercher un nom, un auteur'), value: query.q });
  const tabs = h('div.online-sorts', {});
  let levels = [];

  const load = async (append = false) => {
    if (!append) { query.offset = 0; levels = []; list.replaceChildren(h('p.menu-hint', { textContent: t('Chargement…') })); }
    try {
      const r = await listOnline({ sort: query.sort, q: query.q, limit: 24, offset: query.offset });
      levels = levels.concat(r.levels);
      query.offset = levels.length;
      list.replaceChildren(...(levels.length ? levels.map((s) => h('button.menu-row', { onclick: () => details(s.code) },
        thumb(s, 'menu-thumb'), h('span.menu-row-label', {}, h('b', { textContent: s.name }), h('small', { textContent: summary(s) }))))
        : [h('p.menu-hint', { textContent: query.q ? t('Aucun niveau ne correspond.')
          : t('Pas encore de niveau en ligne : publie le premier !') })]));
      more.hidden = levels.length >= r.total;
    } catch (err) {
      list.replaceChildren(h('p.menu-hint', { textContent: err.message }));
      more.hidden = true;
    }
  };
  const drawTabs = () => tabs.replaceChildren(...SORTS.map(([k, label]) => h('button.tab' + (k === query.sort ? '.active' : ''),
    { textContent: t(label), onclick: () => { query.sort = k; drawTabs(); load(); } })));
  let timer = 0;
  search.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(() => { query.q = search.value.trim(); load(); }, 350); });
  const openCode = () => { const c = formatCode(code.value); if (c.length === 11) details(c); else code.focus(); };
  code.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') openCode(); });
  more.addEventListener('click', () => load(true));

  // One level: its picture, its numbers, play / like / keep / report.
  async function details(c) {
    show(h('div.menu-card.wide', {}, h('div.menu-card-head', {}, h('h2', { textContent: t('Chargement…') }))));
    let s;
    try { s = await getOnline(c); } catch (err) {
      tell(err.status === 404 ? t('Aucun niveau avec le code {code}.', { code: c }) : err.message);
      onlinePanel(show, ctx);
      return;
    }
    let liked = likedHere(s.code);
    const like = h('button.key.small.plain', {});
    const drawLike = () => {
      like.textContent = `${liked ? t('♥ Aimé') : t('♡ J\'aime')} · ${s.likes}`;
      like.classList.toggle('liked', liked);
    };
    like.addEventListener('click', async () => {
      try { const r = await setLike(s.code, !liked); liked = r.liked; s.likes = r.likes; drawLike(); } catch (err) { tell(err.message); }
    });
    drawLike();
    const keep = h('button.key.small.plain', { textContent: t('Garder dans Mes niveaux'), onclick: async () => {
      try {
        const { model } = await openShared(s.level);
        await saveMyLevel({ name: s.name, base: s.level.base, model, difficulty: s.difficulty });
        toast(t('Ajouté à Mes niveaux'));
      } catch (err) { tell(err.message); }
    } });
    const report = h('button.key.small.plain', { textContent: t('Signaler'), onclick: async () => {
      const reason = await askText(t('Signaler ce niveau'), '', { text: t('Pourquoi ? (contenu choquant, niveau cassé…)'), ok: t('Signaler') });
      if (reason === null) return;
      try { await reportOnline(s.code, reason); toast(t('Merci, c\'est noté')); } catch (err) { tell(err.message); }
    } });
    const rate = s.plays ? Math.round((100 * s.clears) / s.plays) : null;
    const clears = rate !== null ? t('Réussi {n} fois ({rate} % des parties)', { n: s.clears, rate }) : t('Réussi {n} fois', { n: s.clears });
    const best = s.bestTime ? ' · ' + t('record {time} s', { time: seconds(s.bestTime) }) : '';
    show(h('div.menu-card.wide', {},
      h('div.menu-card-head', {}, h('h2', { textContent: s.name })),
      h('div.menu-card-body.online-detail', {},
        thumb(s, 'online-big'),
        h('div.online-info', {},
          h('div.level-code.small', { textContent: s.code }),
          h('p', { textContent: summary(s) }),
          h('p', { textContent: clears + best + ' · ' + t('son auteur : {time} s', { time: seconds(s.clearTime || 0) }) }),
          h('button.menu-row.online-play', { onclick: () => play(s) }, icon('play', 3), h('span.menu-row-label', { textContent: t('Jouer') })),
          h('div.online-actions', {}, like, keep, report))),
      h('button.menu-back', { textContent: t('‹ Retour'), onclick: () => onlinePanel(show, ctx) })));
  }

  async function play(s) {
    await flushAutosave();
    const { model, video } = await openShared(s.level);
    state.doc = { id: null, name: s.name, base: s.level.base, online: s.code };
    state.level = s.level.base;
    showModel(model, video);
    setDirty(false); // nothing to keep unless it is kept on purpose
    countPlay(s.code);
    ctx.play();
  }

  show(h('div.menu-card.wide', {},
    h('div.menu-card-head', {}, h('h2', { textContent: t('Niveaux en ligne') })),
    h('div.menu-card-body', {},
      h('div.online-bar', {}, code, h('button.key.small.go', { textContent: t('Jouer ce code'), onclick: openCode }), search),
      tabs, list, more),
    h('button.menu-back', { textContent: t('‹ Retour'), onclick: ctx.back })));
  drawTabs();
  load();
  if (first) details(formatCode(first));
}
