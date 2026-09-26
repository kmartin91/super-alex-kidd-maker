// npm run maker: builds the browser engines if they are missing, then serves
// the Maker. Nothing else is needed: no Python, no native build.
//
// The player engine (maker/public/engine/alexkidd.*) takes several minutes and
// a lot of memory to build, so it is only built when missing; rebuild it on
// purpose with `npm run web`.
import { spawnSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const engine = (f) => path.join(root, 'maker/public/engine', f);

function make(target, message) {
  console.log(message);
  const unix = process.platform !== 'win32';
  const [cmd, ...args] = unix ? ['nice', '-n', '19', 'make', '-C', 'engine', target] : ['make', '-C', 'engine', target];
  if (spawnSync(cmd, args, { cwd: root, stdio: 'inherit' }).status !== 0) {
    console.error('\nLa compilation a échoué (Emscripten est nécessaire : https://emscripten.org).');
    process.exit(1);
  }
}

if (!fs.existsSync(engine('tools.js'))) make('web-tools', 'Compilation des outils du Maker pour le navigateur (moins d\'une minute)…');
if (!fs.existsSync(engine('alexkidd.js'))) {
  make('web', 'Compilation du moteur de jeu pour le navigateur (bouton Jouer).\n' +
    'Elle prend plusieurs minutes et beaucoup de mémoire, une seule fois.\n');
}

const server = spawn(process.execPath, [path.join(root, 'maker/server.js'), ...process.argv.slice(2)], { cwd: root, stdio: 'inherit' });
server.on('exit', (code) => process.exit(code ?? 0));
