// Minimal Chrome DevTools Protocol driver for UI tests.
import { spawn } from 'node:child_process';
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
export async function openPage(url, { width = 1500, height = 900 } = {}) {
  const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--remote-debugging-port=9223', '--autoplay-policy=no-user-gesture-required',
    `--window-size=${width},${height}`, '--user-data-dir=/tmp/cdp-profile-akmw', 'about:blank'], { stdio: 'ignore' });
  let targets;
  for (let i = 0; i < 50; i++) {
    try { targets = await (await fetch('http://127.0.0.1:9223/json')).json(); break; } catch { await new Promise(r => setTimeout(r, 200)); }
  }
  const page = targets.find(t => t.type === 'page');
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise(r => ws.addEventListener('open', r, { once: true }));
  let id = 0; const pending = new Map(); const logs = [];
  ws.addEventListener('message', ev => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
    if (msg.method === 'Runtime.consoleAPICalled') logs.push(msg.params.type + ': ' + msg.params.args.map(a => a.value ?? a.description).join(' '));
    if (msg.method === 'Runtime.exceptionThrown') logs.push('EXCEPTION: ' + msg.params.exceptionDetails.exception?.description);
  });
  const send = (method, params = {}) => new Promise(r => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send('Runtime.enable'); await send('Page.enable');
  await send('Page.navigate', { url });
  const evaluate = async (expr) => (await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true })).result?.result?.value;
  const mouse = async (type, x, y, button = 'left') => send('Input.dispatchMouseEvent', { type, x, y, button, clickCount: 1 });
  const click = async (x, y) => { await mouse('mousePressed', x, y); await mouse('mouseReleased', x, y); };
  const screenshot = async (path) => { const r = await send('Page.captureScreenshot', { format: 'png' }); (await import('node:fs')).writeFileSync(path, Buffer.from(r.result.data, 'base64')); };
  const close = () => { ws.close(); proc.kill(); };
  return { send, evaluate, mouse, click, screenshot, close, logs };
}
