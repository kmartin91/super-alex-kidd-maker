// Play mode (WebAssembly engine), from the start and from a screen, after a
// theme change and custom surprises.
// Needs the server: node maker/server.js --mod _test --port 8099 --no-open
import { openPage } from './cdp.mjs';
import { check, done, waitFor, sleep } from './check.mjs';

const p = await openPage('http://localhost:8099/?level=2');
await waitFor(p, "document.getElementById('status').textContent.includes('niveau')");
await p.evaluate('window.confirm = () => true; window.alert = (m) => console.log("ALERT " + m)');
const playing = "['en jeu'].includes(document.getElementById('playStatus').textContent) || document.getElementById('playStatus').textContent.startsWith('erreur')";
const status = () => p.evaluate("document.getElementById('playStatus').textContent");

await p.evaluate("document.getElementById('playBtn').click()");
await waitFor(p, playing, 30000);
check('plays from the start', (await status()) === 'en jeu', await status());
await sleep(1500);
if (process.argv[2]) await p.screenshot(`${process.argv[2]}/play.png`);
await p.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape' });
await waitFor(p, "document.getElementById('playView').hidden");
check('Escape goes back to editing', await p.evaluate("document.getElementById('playView').hidden"));

await p.evaluate("const w = document.getElementById('mapWrap'); w.scrollLeft = 5.5 * 256 * window.editorState.zoom - w.clientWidth / 2");
await sleep(200);
await p.evaluate("document.getElementById('playBtn').click()");
await waitFor(p, playing, 30000);
check('plays from the screen in view', (await status()) === 'en jeu', await p.evaluate("document.getElementById('playFrom').textContent"));
await p.evaluate("document.getElementById('playBtn').click()");
await waitFor(p, "document.getElementById('playView').hidden");

await p.evaluate("document.getElementById('themeBtn').click()");
await p.evaluate("[...document.querySelectorAll('#modalBody .card')].find((c) => c.querySelector('.card-num').textContent === '7').click()");
await waitFor(p, "!document.getElementById('status').textContent.includes('conversion')");
check('theme changed', (await p.evaluate('window.editorState.model.theme')) === 7);
await p.evaluate("document.getElementById('surpriseBtn').click()");
const boxes = await p.evaluate("document.querySelectorAll('#modalBody select').length");
if (boxes) {
  await p.evaluate("const s = document.querySelector('#modalBody select'); s.value = s.options[2].value; s.dispatchEvent(new Event('change'))");
  check('surprise set', (await p.evaluate('JSON.stringify(window.editorState.model.surprises)')) !== 'null');
}
await p.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape' });
await p.evaluate("document.getElementById('playBtn').click()");
await waitFor(p, playing, 30000);
check('plays after theme change', (await status()) === 'en jeu', await status());
await p.evaluate("document.getElementById('playBtn').click()");
await waitFor(p, "document.getElementById('playView').hidden");
await p.evaluate("document.getElementById('save').click()");
// First save: the level's name is asked.
check('name asked', await waitFor(p, "!!document.getElementById('nameOk')"));
await p.evaluate("document.getElementById('levelNameInput').value = 'Test'; document.getElementById('nameOk').click()");
check('saved', await waitFor(p, "document.getElementById('status').textContent === 'enregistré'"));
done(p);
