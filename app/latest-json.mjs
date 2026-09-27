// Writes latest.json, the file the app checks for updates, from the update
// bundles of a release build (npm run release:mac / release:linux): each
// bundle's signature (.sig) and its download URL on the GitHub release.
// Upload latest.json and the listed bundles to the release (tag v<version>).
//   node app/latest-json.mjs [notes]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const conf = JSON.parse(fs.readFileSync(path.join(here, 'src-tauri/tauri.conf.json'), 'utf8'));
const version = conf.version;
const repo = 'https://github.com/kmartin91/super-alex-kidd-maker/releases/download';
const target = path.join(here, 'src-tauri/target');

// Update bundles and the platforms they serve.
const kinds = [
  { dir: 'universal-apple-darwin/release/bundle/macos', ext: '.app.tar.gz', platforms: ['darwin-aarch64', 'darwin-x86_64'] },
  { dir: 'linux/amd64', ext: '.AppImage', platforms: ['linux-x86_64'] },
  { dir: 'linux/arm64', ext: '.AppImage', platforms: ['linux-aarch64'] },
  { dir: 'release/bundle/nsis', ext: '-setup.exe', platforms: ['windows-x86_64'] },
];

const platforms = {};
for (const k of kinds) {
  const dir = path.join(target, k.dir);
  if (!fs.existsSync(dir)) continue;
  for (const f of fs.readdirSync(dir)) {
    if (!f.endsWith(k.ext) || !fs.existsSync(path.join(dir, f + '.sig'))) continue;
    // GitHub turns spaces in asset names into dots.
    const asset = f.replace(/ /g, '.');
    for (const p of k.platforms) {
      platforms[p] = { signature: fs.readFileSync(path.join(dir, f + '.sig'), 'utf8').trim(), url: `${repo}/v${version}/${asset}` };
    }
    console.log(`${k.platforms.join(', ')}: ${path.join(k.dir, f)} (+ .sig)`);
  }
}
if (!Object.keys(platforms).length) {
  console.error('Aucun paquet de mise à jour signé : construis avec npm run release:mac (ou release:linux) d\'abord.');
  process.exit(1);
}
const out = path.join(target, 'latest.json');
fs.writeFileSync(out, JSON.stringify({ version, notes: process.argv[2] || '', pub_date: new Date().toISOString(), platforms }, null, 2) + '\n');
console.log(`\n${out} : à joindre à la release v${version} avec les fichiers ci-dessus.`);
