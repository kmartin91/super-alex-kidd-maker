// Super Alex Kidd Maker: local server (no dependencies).
//
//   node maker/server.js [--rom original.sms] [--mod mymod] [--port 8080] [--no-open]
//
// Serves the editor UI and a small JSON API. Level data is decoded from the
// user's ROM by maker/tools/leveledit.py; edits are saved as JSON in mods/<mod>/ and
// compiled into mods/<mod>/patch.bin, which the game loads with --mod.
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { execFile, spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf('--' + name);
  return i >= 0 && args[i + 1] ? args[i + 1] : def;
};
const ROM = path.resolve(ROOT, opt('rom', 'original.sms'));
const MOD = opt('mod', 'mymod');
const PORT = Number(opt('port', '8080'));
const MOD_DIR = path.join(ROOT, 'mods', MOD);
const CACHE_DIR = path.join(__dirname, 'cache');
const PUBLIC_DIR = path.join(__dirname, 'public');
const LEVELEDIT_PY = path.join(__dirname, 'tools', 'leveledit.py');
const PYTHON = process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3');
const EXE = process.platform === 'win32' ? '.exe' : '';
const engineTool = (name) => path.join(ROOT, 'engine', 'build', name + EXE);
const GAME = engineTool('alexkidd');
const LEVELDUMP = engineTool('leveldump');
const ENTITYICONS = engineTool('entityicons');

fs.mkdirSync(MOD_DIR, { recursive: true });
fs.mkdirSync(CACHE_DIR, { recursive: true });

function run(cmd, cmdArgs) {
  return new Promise((resolve, reject) => {
    execFile(cmd, cmdArgs, { cwd: ROOT, maxBuffer: 64 << 20 }, (err, stdout, stderr) => {
      if (err) reject(new Error((stderr || err.message).toString().trim()));
      else resolve(stdout);
    });
  });
}

// Video state of a level (tiles in VRAM, palettes) from the running game.
async function levelVideo(n) {
  const file = path.join(CACHE_DIR, `video_${String(n).padStart(2, '0')}.json`);
  if (!fs.existsSync(file)) await run(LEVELDUMP, [ROM, String(n), file]);
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

// Appearance of each placeable entity type, captured from the running game.
async function entityIcons() {
  const file = path.join(CACHE_DIR, 'entity_icons.json');
  if (!fs.existsSync(file)) {
    const specs = (await run(PYTHON, [LEVELEDIT_PY, 'icontypes', ROM])).trim().split(/\s+/);
    await run(ENTITYICONS, [ROM, file, ...specs]);
  }
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function modLevelPath(n) {
  return path.join(MOD_DIR, `level_${String(n).padStart(2, '0')}.json`);
}

async function levelModel(n) {
  const saved = modLevelPath(n);
  if (fs.existsSync(saved)) return JSON.parse(fs.readFileSync(saved, 'utf8'));
  return JSON.parse(await run(PYTHON, [LEVELEDIT_PY, 'export', ROM, String(n)]));
}

// Patch for playing in the editor: the saved mod plus the level being edited
// (not saved yet). Returns the patch bytes (empty when nothing is modified).
async function testPatch(n, model, startColumn) {
  const dir = path.join(CACHE_DIR, 'testmod');
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  for (const f of fs.readdirSync(MOD_DIR)) {
    if (/^level_\d\d\.json$/.test(f)) fs.copyFileSync(path.join(MOD_DIR, f), path.join(dir, f));
  }
  if (model) fs.writeFileSync(path.join(dir, `level_${String(n).padStart(2, '0')}.json`), JSON.stringify(model));
  const hasLevels = fs.readdirSync(dir).some((f) => /^level_\d\d\.json$/.test(f));
  if (!hasLevels && startColumn === null) return Buffer.alloc(0);
  const out = path.join(dir, 'patch.bin');
  const extra = startColumn === null ? [] : ['--start', `${n}:${startColumn}`];
  await run(PYTHON, [LEVELEDIT_PY, 'build', ROM, dir, out, ...extra]);
  return fs.readFileSync(out);
}

async function buildPatch() {
  const patch = path.join(MOD_DIR, 'patch.bin');
  const hasLevels = fs.readdirSync(MOD_DIR).some((f) => /^level_\d\d\.json$/.test(f));
  if (!hasLevels) {
    if (fs.existsSync(patch)) fs.unlinkSync(patch);
    return;
  }
  await run(PYTHON, [LEVELEDIT_PY, 'build', ROM, MOD_DIR, patch]);
}

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.wasm': 'application/wasm' };

function send(res, code, body, type = 'application/json') {
  res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' });
  res.end(typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body));
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');
  const p = url.pathname;
  try {
    if (p === '/api/info') {
      return send(res, 200, { mod: MOD, modDir: path.relative(ROOT, MOD_DIR), rom: path.relative(ROOT, ROM) });
    }
    if (p === '/api/rom') {
      // Only ever served to this machine (the server listens on 127.0.0.1).
      return send(res, 200, fs.readFileSync(ROM), 'application/octet-stream');
    }
    const rt = p.match(/^\/api\/retheme\/(\d+)$/);
    if (rt && req.method === 'POST') {
      const theme = Number(url.searchParams.get('theme'));
      const tmp = path.join(CACHE_DIR, 'retheme.json');
      fs.writeFileSync(tmp, await readBody(req));
      const model = JSON.parse(await run(PYTHON, [LEVELEDIT_PY, 'retheme', ROM, tmp, String(theme)]));
      return send(res, 200, { model, video: await levelVideo(theme) });
    }
    let tm = p.match(/^\/api\/testpatch\/(\d+)$/);
    if (tm && req.method === 'POST') {
      const body = await readBody(req);
      const start = url.searchParams.has('start') ? Number(url.searchParams.get('start')) : null;
      return send(res, 200, await testPatch(Number(tm[1]), body ? JSON.parse(body) : null, start),
        'application/octet-stream');
    }
    if (p === '/api/entity-icons') {
      return send(res, 200, await entityIcons());
    }
    if (p === '/api/levels') {
      return send(res, 200, JSON.parse(await run(PYTHON, [LEVELEDIT_PY, 'list', ROM])));
    }
    let m = p.match(/^\/api\/level\/(\d+)$/);
    if (m && req.method === 'GET') {
      const n = Number(m[1]);
      const model = await levelModel(n);
      const video = await levelVideo(model.theme || n);
      return send(res, 200, { model, video, modified: fs.existsSync(modLevelPath(n)) });
    }
    if (m && req.method === 'PUT') {
      const n = Number(m[1]);
      const model = JSON.parse(await readBody(req));
      fs.writeFileSync(modLevelPath(n), JSON.stringify(model));
      await buildPatch();
      return send(res, 200, { ok: true });
    }
    if (m && req.method === 'DELETE') {
      const n = Number(m[1]);
      if (fs.existsSync(modLevelPath(n))) fs.unlinkSync(modLevelPath(n));
      await buildPatch();
      return send(res, 200, { ok: true });
    }
    m = p.match(/^\/api\/play\/(\d+)$/);
    if (m && req.method === 'POST') {
      const gameArgs = [ROM, '--level', m[1]];
      const patch = path.join(MOD_DIR, 'patch.bin');
      if (fs.existsSync(patch)) gameArgs.push('--mod', patch);
      const child = spawn(GAME, gameArgs, { cwd: ROOT, stdio: 'ignore', detached: true });
      child.unref();
      return send(res, 200, { ok: true });
    }
    // Static files.
    const file = path.join(PUBLIC_DIR, p === '/' ? 'index.html' : p);
    if (!file.startsWith(PUBLIC_DIR) || !fs.existsSync(file)) return send(res, 404, { error: 'not found' });
    return send(res, 200, fs.readFileSync(file), MIME[path.extname(file)] || 'application/octet-stream');
  } catch (err) {
    return send(res, 500, { error: err.message });
  }
});

server.listen(PORT, '127.0.0.1', () => {
  const url = `http://localhost:${PORT}`;
  console.log(`Super Alex Kidd Maker: ${url}  (mod: ${path.relative(ROOT, MOD_DIR)})`);
  if (!args.includes('--no-open')) {
    const opener = process.platform === 'darwin' ? 'open' : process.platform === 'win32' ? 'start' : 'xdg-open';
    spawn(opener, process.platform === 'win32' ? ['""', url] : [url], { stdio: 'ignore', detached: true, shell: process.platform === 'win32' }).unref();
  }
});
