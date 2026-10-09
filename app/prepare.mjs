// Run before `tauri dev` / `tauri build` (from app/): builds the browser
// engines the Maker page needs (maker/public/engine/) when they are missing,
// older than the engine's sources, or lack a function the page calls, so that
// an app never ships an old engine ("engine._web_… is not a function").
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

// Low priority: these builds are long and must not freeze the machine. On
// Windows, make and Emscripten come with MSYS2 (README): when make is not on
// the PATH (PowerShell), its UCRT64 shell runs the build.
const msys2 = path.join(process.env.MSYS2_ROOT || 'C:\\msys64', 'usr', 'bin', 'bash.exe');
function make(...args) {
  if (!windows) return run('nice', ['-n', '19', 'make', ...args]);
  if (!spawnSync('make', ['--version'], { stdio: 'ignore' }).error) return run('make', args);
  if (!fs.existsSync(msys2)) {
    console.error('\nmake introuvable : installez MSYS2 (README, « On Windows »), ou compilez les moteurs avec `npm run web` depuis son terminal UCRT64.');
    process.exit(1);
  }
  run(msys2, ['-lc', `make ${args.join(' ')}`], { env: { ...process.env, MSYSTEM: 'UCRT64', CHERE_INVOKING: '1' } });
}

// Last change to the engines' sources (what engine/Makefile's web and
// web-tools targets depend on).
function newest(p) {
  const st = fs.statSync(p);
  if (!st.isDirectory()) return st.mtimeMs;
  return Math.max(0, ...fs.readdirSync(p).map((f) => newest(path.join(p, f))));
}
const sources = Math.max(...['src/rt', 'src/gen', 'src/game', 'src/platform/main_web.c',
  'src/platform/tools_web.c', 'tools/capture.c', 'tools/capture.h'].map((p) => newest(path.join(root, 'engine', p))));

const engineDir = path.join(root, 'maker/public/engine');
const ENGINES = { 'tools.js': 'web-tools', 'alexkidd.js': 'web' };
const old = (f) => !fs.existsSync(path.join(engineDir, f)) || fs.statSync(path.join(engineDir, f)).mtimeMs < sources;

// The engine functions the page calls (engine._web_…) that the built engines
// don't export.
const jsDir = path.join(root, 'maker/public/js');
const calls = new Set(fs.readdirSync(jsDir, { recursive: true }).filter((f) => f.endsWith('.js'))
  .flatMap((f) => fs.readFileSync(path.join(jsDir, f), 'utf8').match(/\b_web_\w+/g) || []));
function missing() {
  const built = Object.keys(ENGINES).map((f) => (fs.existsSync(path.join(engineDir, f)) ? fs.readFileSync(path.join(engineDir, f), 'utf8') : '')).join('\n');
  return [...calls].filter((name) => !built.includes(`Module["${name}"]`));
}

const lacking = missing().length > 0;
for (const [f, target] of Object.entries(ENGINES)) {
  if (!lacking && !old(f)) continue;
  if (target === 'web') console.log('Compilation du moteur de jeu pour le navigateur : plusieurs minutes.');
  make('-B', '-C', 'engine', target); // -B: even when make finds it up to date
}
const still = missing();
if (still.length) {
  console.error(`\nLe moteur compilé n'exporte pas ${still.join(', ')}, que la page appelle : ajoutez-les à EXPORTED_FUNCTIONS dans engine/Makefile.`);
  process.exit(1);
}
