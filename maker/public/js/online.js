// Levels online (the API in online/, at maker.kma.studio): publishing,
// browsing, playing, liking. A shared level holds only what its maker made
// (online/README.md): whoever plays it rebuilds the rest from their own ROM.

import { pref, setPref } from './prefs.js';
import { exportLevelOf, themedLevel } from './backend.js';
import { t } from './i18n.js';

// Where the API is: the page's own server on maker.kma.studio, else the
// public one; ?api=http://localhost:8787 (kept) for a local server.
const PUBLIC = 'https://maker.kma.studio';
const param = new URLSearchParams(location.search).get('api');
if (param !== null) setPref('apiBase', param);
export function apiBase() {
  const set = pref('apiBase', null);
  if (set) return set.replace(/\/$/, '');
  return location.hostname === 'maker.kma.studio' ? '' : PUBLIC;
}

// A random id for this player (likes, counts), kept here.
function clientId() {
  let id = pref('clientId', null);
  if (!id) {
    id = crypto.randomUUID ? crypto.randomUUID() : Math.random().toString(36).slice(2) + Date.now().toString(36);
    setPref('clientId', id);
  }
  return id;
}

async function call(method, path, body = null, headers = {}) {
  let res;
  try {
    res = await fetch(apiBase() + path, {
      method,
      headers: { 'X-Client-Id': clientId(), ...(body ? { 'Content-Type': 'application/json' } : {}), ...headers },
      body: body ? JSON.stringify(body) : undefined,
    });
  } catch {
    throw new Error(t('Le serveur des niveaux en ligne n\'est pas joignable. Vérifie ta connexion.'));
  }
  const data = await res.json().catch(() => ({}));
  // The server's own messages (data.error) are in French: shown as they are.
  if (!res.ok) {
    throw Object.assign(new Error(data.error || t('Erreur du serveur ({status})', { status: res.status })), { code: data.code, status: res.status });
  }
  return data;
}

export const thumbnailUrl = (s) => (s.thumbnail ? apiBase() + s.thumbnail : null);

// ------------------------------------------------------------ level format
// What a player made: the rest (graphics tables, parts...) comes back from
// the ROM through the base level and the theme.
const SHARED_FIELDS = ['kind', 'columns', 'rows', 'grid', 'screens', 'entities', 'specials', 'surprises', 'start', 'clear', 'themeMusic', 'vehicle', 'zone'];

export function sharedLevel(model, base) {
  const out = { format: 'super-alex-kidd-maker/shared-level', version: 1, base, theme: model.theme || base };
  for (const k of SHARED_FIELDS) if (model[k] !== undefined) out[k] = model[k];
  return out;
}

// { model, video } of a shared level, ready for the editor or play.
export async function openShared(shared) {
  const { model, video } = await themedLevel(shared.base, shared.theme || shared.base);
  for (const k of SHARED_FIELDS) if (shared[k] !== undefined) model[k] = JSON.parse(JSON.stringify(shared[k]));
  return { model, video };
}

// A short fingerprint of a level (what was cleared is what gets published).
export function levelHash(model, base) {
  const s = JSON.stringify(sharedLevel(model, base));
  let h = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 0x01000193) >>> 0;
  return h.toString(36);
}

// True for a level of the game left as it is (not the player's work).
export function isUnmodified(model, base) {
  if ((model.theme || base) !== base) return false;
  const original = exportLevelOf(base);
  const pick = (m) => JSON.stringify([m.screens.map((s) => s.blocks), m.entities, m.specials, m.grid]);
  return pick(model) === pick(original);
}

// ------------------------------------------------------------ calls
export const health = () => call('GET', '/api/health');
export const listOnline = (q = {}) => call('GET', '/api/levels?' + new URLSearchParams(Object.entries(q).filter(([, v]) => v !== '' && v != null)));
export const getOnline = (code) => call('GET', `/api/levels/${encodeURIComponent(code)}`);
export const countPlay = (code) => call('POST', `/api/levels/${code}/plays`).catch(() => null);
export const countClear = (code, time) => call('POST', `/api/levels/${code}/clears`, { time }).catch(() => null);
export const reportOnline = (code, reason) => call('POST', `/api/levels/${code}/reports`, { reason });

// Likes are remembered here (the server does not say who liked what).
export const likedHere = (code) => (pref('liked', []) || []).includes(code);
export async function setLike(code, on) {
  const r = await call(on ? 'POST' : 'DELETE', `/api/levels/${code}/likes`);
  const liked = new Set(pref('liked', []) || []);
  if (on) liked.add(code); else liked.delete(code);
  setPref('liked', [...liked]);
  return r;
}

// body: { name, author, base, theme, kind, difficulty, clearTime, level, thumbnail }
export const publishOnline = (body) => call('POST', '/api/levels', body);
export const updateOnline = (code, token, body) => call('PUT', `/api/levels/${code}`, body, { 'X-Edit-Token': token });
export const removeOnline = (code, token) => call('DELETE', `/api/levels/${code}`, null, { 'X-Edit-Token': token });
