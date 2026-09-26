// Super Alex Kidd Maker: serves the page (maker/public/) on this computer.
// Everything else happens in the browser: the ROM and the levels stay there.
//
//   node maker/server.js [--port 8080] [--no-open]
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf('--' + name);
  return i >= 0 && args[i + 1] ? args[i + 1] : def;
};
const PORT = Number(opt('port', '8080'));
const PUBLIC_DIR = path.join(__dirname, 'public');
const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.wasm': 'application/wasm', '.png': 'image/png', '.svg': 'image/svg+xml',
};

const server = http.createServer((req, res) => {
  const p = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  const file = path.join(PUBLIC_DIR, p === '/' ? 'index.html' : p);
  if (!file.startsWith(PUBLIC_DIR + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    return res.end('not found');
  }
  res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
  fs.createReadStream(file).pipe(res);
});

server.on('error', (err) => {
  if (err.code === 'EADDRINUSE') {
    console.error(`Le port ${PORT} est déjà utilisé : le Maker tourne sans doute déjà dans un autre terminal ` +
      `(ferme-le avec Ctrl+C), ou lance-le sur un autre port avec --port 8081.`);
  } else {
    console.error('Le serveur ne peut pas démarrer :', err.message);
  }
  process.exit(1);
});

server.listen(PORT, '127.0.0.1', () => {
  const url = `http://localhost:${PORT}`;
  console.log(`Super Alex Kidd Maker : ${url}`);
  if (!args.includes('--no-open')) {
    const opener = process.platform === 'darwin' ? 'open' : process.platform === 'win32' ? 'start' : 'xdg-open';
    spawn(opener, process.platform === 'win32' ? ['""', url] : [url], { stdio: 'ignore', detached: true, shell: process.platform === 'win32' }).unref();
  }
});
