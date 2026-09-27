// Builds the Linux app (.deb and .AppImage) in Docker, from a Mac or any
// machine with Docker: `npm run app:linux` (x86_64 and ARM), or
// `npm run app:linux -- amd64` / `-- arm64` for one processor.
// Results in app/src-tauri/target/linux/<processor>/. Builds of the other
// processor run emulated: slower (the first one takes a while).
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');
// --release: also the signed update bundles (see release.mjs).
const release = process.argv.includes('--release');
const picked = process.argv.slice(2).filter((a) => !a.startsWith('--'));
const archs = picked.length ? picked : ['amd64', 'arm64'];
const keyFile = path.join(os.homedir(), '.tauri', 'super-alex-kidd-maker.key');
if (release && !fs.existsSync(keyFile)) { console.error(`Clé de mise à jour introuvable : ${keyFile}`); process.exit(1); }

if (!fs.existsSync(path.join(root, 'maker/public/engine/alexkidd.wasm'))) {
  console.error('Construis d\'abord les moteurs du navigateur : npm run web');
  process.exit(1);
}
for (const arch of archs) {
  const out = path.join(here, 'src-tauri/target/linux', arch);
  fs.mkdirSync(out, { recursive: true });
  console.log(`\n== Linux ${arch}`);
  const env = release ? ['-e', 'RELEASE=1', '-e', 'TAURI_SIGNING_PRIVATE_KEY', '-e', 'TAURI_SIGNING_PRIVATE_KEY_PASSWORD='] : [];
  const r = spawnSync('docker', ['run', '--rm', '--platform', `linux/${arch}`, ...env,
    '-v', `${root}:/src:ro`, '-v', `${out}:/out`,
    '-v', `akmaker-cargo-${arch}:/cache`, '-v', `akmaker-registry-${arch}:/usr/local/cargo/registry`,
    'rust:1-bookworm', 'sh', '/src/app/linux/build.sh'],
    { stdio: 'inherit', env: release ? { ...process.env, TAURI_SIGNING_PRIVATE_KEY: fs.readFileSync(keyFile, 'utf8') } : process.env });
  if (r.status !== 0) { console.error(`Échec pour ${arch}`); process.exit(1); }
}
