// Languages of the Maker: French (the texts written in the code are the
// source) and English (lang/en.js, keyed by the French text).
//
// The language: ?lang=fr|en in the URL (then kept), else the one chosen
// before (Paramètres), else the browser's. Changing it reloads the page.

import { pref, setPref } from './prefs.js';
import en from './lang/en.js';

const LANGS = ['fr', 'en'];

function pick() {
  const asked = new URLSearchParams(location.search).get('lang');
  if (LANGS.includes(asked)) {
    setPref('lang', asked);
    return asked;
  }
  const saved = pref('lang');
  if (LANGS.includes(saved)) return saved;
  return (navigator.language || '').toLowerCase().startsWith('fr') ? 'fr' : 'en';
}

export const lang = pick();

const warned = new Set();

// The text in the page's language; {name} placeholders come from `params`.
export function t(text, params = null) {
  let out = text;
  if (lang === 'en') {
    if (Object.hasOwn(en, text)) out = en[text];
    else if (!warned.has(text)) {
      warned.add(text);
      console.warn('missing translation:', text);
    }
  }
  if (!params) return out;
  return out.replace(/\{(\w+)\}/g, (m, k) => (k in params ? String(params[k]) : m));
}

// Names that come as data from the level tools (rom/*.js: block kinds, enemy
// types, surprises), in French there: translated where they are shown.
export function tName(name) {
  const m = /^Objet (\$[0-9A-F]{2})$/.exec(name);
  return m ? t('Objet {code}', { code: m[1] }) : t(name);
}

// Error messages: those of the level tools are mostly in English already, a
// few in French; anything unknown is shown as it is.
export function tError(message) {
  if (lang !== 'en') return message;
  if (Object.hasOwn(en, message)) return en[message];
  const m = /^pas assez de place pour (\d+) surprises dans le niveau (\d+)$/.exec(message);
  return m ? t('pas assez de place pour {n} surprises dans le niveau {level}', { n: m[1], level: m[2] }) : message;
}

// Keeps the choice and reloads the page in that language.
export function setLang(l) {
  setPref('lang', l);
  const url = new URL(location.href);
  if (url.searchParams.has('lang')) {
    url.searchParams.delete('lang'); // the choice just made wins from now on
    location.replace(url);
  } else {
    location.reload();
  }
}

const ATTRIBUTES = ['data-tip', 'placeholder', 'title', 'alt'];
const WORDS = /\p{L}{2}/u; // leaves out arrows, digits and single keys (Z)

// Translates the static texts of index.html, once at startup: whole text
// nodes (trimmed) and the attributes above (a data-tip "Title|Explanation"
// is one text).
export function translateDom(root = document) {
  if (root === document) document.documentElement.lang = lang;
  if (lang === 'fr') return;
  if (root === document) document.title = t(document.title);
  const walker = document.createTreeWalker(root === document ? document.body : root, NodeFilter.SHOW_TEXT);
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    const text = node.nodeValue.trim();
    if (!WORDS.test(text) || node.parentElement.closest('script, style')) continue;
    node.nodeValue = node.nodeValue.replace(text, () => t(text));
  }
  const selector = ATTRIBUTES.map((a) => `[${a}]`).join(',');
  for (const el of root.querySelectorAll(selector)) {
    for (const a of ATTRIBUTES) {
      const value = el.getAttribute(a);
      if (value && WORDS.test(value)) el.setAttribute(a, t(value));
    }
  }
}
