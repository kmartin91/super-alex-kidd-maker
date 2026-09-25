// Tiny assertion helper for the UI tests.
let failures = 0;
export function check(name, ok, detail = '') {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail !== '' ? ' — ' + detail : ''}`);
  if (!ok) failures++;
}
export function done(page) {
  const errors = page.logs.filter((l) => l.startsWith('EXCEPTION') || l.startsWith('error'));
  check('no page errors', errors.length === 0, errors.join(' | '));
  page.close();
  process.exit(failures ? 1 : 0);
}
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
export async function waitFor(page, expr, ms = 20000) {
  for (let t = 0; t < ms; t += 100) {
    if (await page.evaluate(expr)) return true;
    await sleep(100);
  }
  return false;
}
