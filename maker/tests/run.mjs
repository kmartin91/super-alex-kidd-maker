// Runs the Maker UI tests against a throw-away mod (mods/_test).
//   node maker/tests/run.mjs [screenshot dir]
// Needs Google Chrome, and `make -C engine web` for the play test.
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../..');
fs.rmSync(path.join(root, 'mods', '_test'), { recursive: true, force: true });
const server = spawn(process.execPath, [path.join(root, 'maker/server.js'), '--mod', '_test', '--port', '8099', '--no-open'],
  { cwd: root, stdio: 'ignore' });
await new Promise((r) => setTimeout(r, 1000));
let failed = 0;
for (const test of ['editing.mjs', 'play.mjs']) {
  console.log(`\n== ${test}`);
  const r = spawnSync(process.execPath, [path.join(here, test), ...process.argv.slice(2)], { stdio: 'inherit', timeout: 180000 });
  if (r.status !== 0) failed++;
}
server.kill();
fs.rmSync(path.join(root, 'mods', '_test'), { recursive: true, force: true });
console.log(failed ? `\n${failed} test file(s) failed` : '\nall tests passed');
process.exit(failed ? 1 : 0);
