// Info bubbles: any element with data-tip="Title|Explanation" shows one while
// hovered, next to it (towards the middle of the screen).

let tip = null;
let current = null;

function show(el) {
  const [title, more] = el.dataset.tip.split('|');
  tip.replaceChildren(Object.assign(document.createElement('b'), { textContent: title }));
  if (more) tip.appendChild(Object.assign(document.createElement('span'), { textContent: more }));
  tip.hidden = false;
  const r = el.getBoundingClientRect(), t = tip.getBoundingClientRect();
  const gap = 10, vw = innerWidth, vh = innerHeight;
  let x, y;
  if (r.left > vw * 0.75) { x = r.left - t.width - gap; y = r.top + r.height / 2 - t.height / 2; }       // right column
  else if (r.right < vw * 0.12) { x = r.right + gap; y = r.top + r.height / 2 - t.height / 2; }         // left column
  else if (r.top > vh * 0.7) { x = r.left + r.width / 2 - t.width / 2; y = r.top - t.height - gap; }   // bottom bar
  else { x = r.left + r.width / 2 - t.width / 2; y = r.bottom + gap; }                                 // elsewhere
  tip.style.left = `${Math.max(8, Math.min(vw - t.width - 8, x))}px`;
  tip.style.top = `${Math.max(8, Math.min(vh - t.height - 8, y))}px`;
}

function hide() {
  current = null;
  if (tip) tip.hidden = true;
}

export function bindTooltips() {
  tip = document.getElementById('tip');
  document.addEventListener('mouseover', (ev) => {
    const el = ev.target.closest && ev.target.closest('[data-tip]');
    if (el === current) return;
    current = el;
    if (el && el.dataset.tip) show(el); else hide();
  });
  document.addEventListener('mousedown', hide);
  window.addEventListener('blur', hide);
}

// Sets an element's info bubble.
export function setTip(el, title, more = '') {
  el.dataset.tip = more ? `${title}|${more}` : title;
  el.removeAttribute('title');
}
