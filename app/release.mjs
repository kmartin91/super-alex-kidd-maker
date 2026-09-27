// Release build: like npm run app:mac / app, plus the signed update bundles
// the app downloads to update itself (tauri.release.json). The update key is
// ~/.tauri/super-alex-kidd-maker.key (keep it, and a copy of it: without it,
// the installed apps can't be updated any more).
//   node app/release.mjs mac | win
// Then: node app/latest-json.mjs "notes", and upload (app/RELEASE.md).
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const key = process.env.TAURI_SIGNING_PRIVATE_KEY_FILE || path.join(os.homedir(), '.tauri', 'super-alex-kidd-maker.key');
if (!fs.existsSync(key)) {
  console.error(`Clé de mise à jour introuvable : ${key}`);
  process.exit(1);
}
const env = { ...process.env, TAURI_SIGNING_PRIVATE_KEY: fs.readFileSync(key, 'utf8'), TAURI_SIGNING_PRIVATE_KEY_PASSWORD: process.env.TAURI_SIGNING_PRIVATE_KEY_PASSWORD || '' };
const which = process.argv[2] || (process.platform === 'darwin' ? 'mac' : 'win');
const args = ['tauri', 'build', '--config', 'src-tauri/tauri.release.json'];
if (which === 'mac') args.push('--target', 'universal-apple-darwin');
const r = spawnSync('npx', args, { cwd: here, env, stdio: 'inherit', shell: process.platform === 'win32' });
process.exit(r.status ?? 1);
