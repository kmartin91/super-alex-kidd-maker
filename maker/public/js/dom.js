// Tiny DOM helpers.

export const $ = (id) => document.getElementById(id);

// Draws a canvas into a new canvas of the same size (palettes show copies).
export function copyCanvas(src, className) {
  const c = document.createElement('canvas');
  c.width = src.width;
  c.height = src.height;
  if (className) c.className = className;
  c.getContext('2d').drawImage(src, 0, 0);
  return c;
}
