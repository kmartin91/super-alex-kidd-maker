// Small preferences kept in this browser (they may be unavailable: private
// windows, blocked storage), with defaults.

const KEY = 'super-alex-kidd-maker';

function read() {
  try { return JSON.parse(localStorage.getItem(KEY)) || {}; } catch { return {}; }
}

export function pref(name, fallback = null) {
  const v = read()[name];
  return v === undefined ? fallback : v;
}

export function setPref(name, value) {
  try { localStorage.setItem(KEY, JSON.stringify({ ...read(), [name]: value })); } catch { /* not kept */ }
}
