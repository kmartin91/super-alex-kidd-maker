// Run before `tauri dev` / `tauri build` (from app/): builds the browser
// engines the Maker page needs (maker/public/engine/), when missing.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');
const windows = process.platform === 'win32';

function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { cwd: root, stdio: 'inherit', ...opts });
  if (r.status !== 0) {
    console.error(`\nÉchec : ${cmd} ${args.join(' ')}`);
    process.exit(1);
  }
}

// Low priority: these builds are long and must not freeze the machine.
const make = (...args) => (windows ? run('make', args) : run('nice', ['-n', '19', 'make', ...args]));

const engineDir = path.join(root, 'maker/public/engine');
if (!fs.existsSync(path.join(engineDir, 'tools.js'))) make('-C', 'engine', 'web-tools');
if (!fs.existsSync(path.join(engineDir, 'alexkidd.js'))) {
  console.log('Compilation du moteur de jeu pour le navigateur : plusieurs minutes, une seule fois.');
  make('-C', 'engine', 'web');
}
