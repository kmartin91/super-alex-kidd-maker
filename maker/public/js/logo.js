// The "Super Alex Kidd Maker" logo, lettered like the title of Alex Kidd in
// Miracle World: bouncy slanted red letters with a black outline and a green
// relief going down to the right, in big pixels. Drawn at the Master System's
// resolution, shown scaled up (canvas { image-rendering: pixelated }).

const FONT = '"Luckiest Guy"';
const LINES = [
  { text: 'SUPER', size: 30, dx: -40 },
  { text: 'ALEX KIDD', size: 52, dx: 0 },
  { text: 'MAKER', size: 52, dx: 34 },
];
const NARROW = 0.86; // the game's letters are tall
const RELIEF = 3;

const RED = [255, 0, 0], DARK = [0, 170, 0], LIGHT = [0, 255, 0], BLACK = [0, 0, 0];

// Pixels of one letter (1 = letter) drawn at (x, y) on the baseline.
function letterMask(ch, size, x, y, angle, W, H) {
  const c = document.createElement('canvas');
  c.width = W;
  c.height = H;
  const ctx = c.getContext('2d');
  ctx.font = `${size}px ${FONT}`;
  ctx.translate(x, y);
  ctx.rotate(angle);
  ctx.transform(NARROW, 0, -0.2, 1, 0, 0); // tall, slanted to the right
  ctx.fillText(ch, 0, 0);
  const a = ctx.getImageData(0, 0, W, H).data;
  const m = new Uint8Array(W * H);
  for (let i = 0; i < m.length; i++) m[i] = a[i * 4 + 3] >= 110 ? 1 : 0;
  return m;
}

// Paints a letter over what is already drawn: relief, outline, then red.
function paintLetter(img, m, W, H) {
  const inside = (px, py) => px >= 0 && py >= 0 && px < W && py < H;
  const relief = new Uint8Array(W * H);
  for (let py = 0; py < H; py++) {
    for (let px = 0; px < W; px++) {
      if (!m[py * W + px]) continue;
      for (let d = 0; d <= RELIEF; d++) if (inside(px + d, py + d)) relief[(py + d) * W + px + d] = 1;
    }
  }
  const put = (i, [r, g, b]) => { img[i * 4] = r; img[i * 4 + 1] = g; img[i * 4 + 2] = b; img[i * 4 + 3] = 255; };
  for (let py = 0; py < H; py++) {
    for (let px = 0; px < W; px++) {
      const i = py * W + px;
      if (relief[i]) {
        if (m[i]) { put(i, RED); continue; }
        // Under a letter edge: the lit face; beside it: the dark face.
        let under = false;
        for (let j = 1; j <= RELIEF && !under; j++) under = py - j >= 0 && m[(py - j) * W + px] === 1;
        put(i, under ? LIGHT : DARK);
        continue;
      }
      let edge = false;
      for (let dy = -1; dy <= 1 && !edge; dy++) {
        for (let dx = -1; dx <= 1 && !edge; dx++) edge = inside(px + dx, py + dy) && relief[(py + dy) * W + px + dx] === 1;
      }
      if (edge) put(i, BLACK);
    }
  }
}

let cached = null;

// A canvas with the logo, cropped to it (same canvas every time: copy it with
// drawImage to show it in several places).
export async function logoCanvas() {
  if (cached) return cached;
  try { await document.fonts.load(`50px ${FONT}`); } catch { /* drawn with a fallback font */ }
  const W = 320, H = 190;
  const out = new ImageData(W, H);
  const measure = document.createElement('canvas').getContext('2d');
  let y = 6, n = 0;
  for (const line of LINES) {
    measure.font = `${line.size}px ${FONT}`;
    const advance = (ch) => ch === ' ' ? line.size * 0.22 : measure.measureText(ch).width * NARROW * 1.02;
    const width = [...line.text].reduce((s, ch) => s + advance(ch), 0);
    let x = (W - width) / 2 + line.dx;
    y += line.size * 0.86;
    for (const ch of line.text) {
      if (ch !== ' ') {
        // Each letter bounces a little, as if drawn by hand.
        const angle = (((n * 37) % 7) - 3) * 0.022;
        const bounce = (((n * 53) % 5) - 2) * line.size * 0.03;
        paintLetter(out.data, letterMask(ch, line.size, x, y + bounce, angle, W, H), W, H);
        n++;
      }
      x += advance(ch);
    }
  }
  // Crop to the drawing.
  let x0 = W, y0 = H, x1 = 0, y1 = 0;
  for (let py = 0; py < H; py++) {
    for (let px = 0; px < W; px++) {
      if (!out.data[(py * W + px) * 4 + 3]) continue;
      x0 = Math.min(x0, px); x1 = Math.max(x1, px); y0 = Math.min(y0, py); y1 = Math.max(y1, py);
    }
  }
  const c = document.createElement('canvas');
  c.width = x1 - x0 + 1;
  c.height = y1 - y0 + 1;
  c.getContext('2d').putImageData(out, -x0, -y0);
  cached = c;
  return c;
}

// A new canvas showing the logo (for the title screen and the menu).
export async function logoImage(className) {
  const src = await logoCanvas();
  const c = document.createElement('canvas');
  c.width = src.width;
  c.height = src.height;
  c.className = className;
  c.getContext('2d').drawImage(src, 0, 0);
  return c;
}
