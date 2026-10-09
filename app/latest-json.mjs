// Writes latest.json, the file the app checks for updates, from the update
// bundles of a release build (npm run release:mac / release:win /
// release:linux): each bundle's signature (.sig) and its download URL on the
// GitHub release. Upload latest.json and the listed bundles to that release.
//   node app/latest-json.mjs [--tag beta-4] [notes]
// The version is the one in the bundles' names (the build's, even when
// tauri.conf.json was only changed for the build); the tag defaults to
// v<version>.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const tagAt = args.indexOf('--tag');
const tagArg = tagAt >= 0 ? args.splice(tagAt, 2)[1] : null;
const notes = args[0] || '';
const repo = 'https://github.com/kmartin91/super-alex-kidd-maker/releases/download';
const target = path.join(here, 'src-tauri/target');

// Update bundles and the platforms they serve.
const kinds = [
  { dir: 'universal-apple-darwin/release/bundle/macos', ext: '.app.tar.gz', platforms: ['darwin-aarch64', 'darwin-x86_64'] },
  { dir: 'linux/amd64', ext: '.AppImage', platforms: ['linux-x86_64'] },
  { dir: 'linux/arm64', ext: '.AppImage', platforms: ['linux-aarch64'] },
  { dir: 'release/bundle/nsis', ext: '-setup.exe', platforms: ['windows-x86_64'] },
];

// "Super Alex Kidd Maker_0.3.1_x64-setup.exe" -> [0, 3, 1]. The Mac bundle's
// name has none: its .app beside it has (Info.plist).
function versionOf(dir, f) {
  let m = /_(\d+)\.(\d+)\.(\d+)[_.]/.exec(f);
  const plist = path.join(dir, f.replace(/\.tar\.gz$/, ''), 'Contents', 'Info.plist');
  if (!m && fs.existsSync(plist)) {
    m = /<key>CFBundleShortVersionString<\/key>\s*<string>(\d+)\.(\d+)\.(\d+)<\/string>/.exec(fs.readFileSync(plist, 'utf8'));
  }
  return (m || []).slice(1).map(Number);
}
function newer(a, b) {
  const i = a.findIndex((n, j) => n !== b[j]);
  return i >= 0 && a[i] > b[i];
}

// Each platform: its newest signed bundle.
const found = [];
for (const k of kinds) {
  const dir = path.join(target, k.dir);
  if (!fs.existsSync(dir)) continue;
  let best = null;
  for (const f of fs.readdirSync(dir)) {
    if (!f.endsWith(k.ext) || !fs.existsSync(path.join(dir, f + '.sig'))) continue;
    const v = versionOf(dir, f);
    if (v.length === 3 && (!best || newer(v, best.v))) best = { f, v };
  }
  if (best) found.push({ ...k, ...best });
}
if (!found.length) {
  console.error('Aucun paquet de mise à jour signé : construis avec npm run release:win (ou release:mac, release:linux) d\'abord,');
  console.error('avec la clé de mise à jour (app/RELEASE.md) : sans elle, les paquets n\'ont pas de .sig.');
  process.exit(1);
}
// Bundles of older builds stay in target/: only the newest version goes out.
const top = found.reduce((a, b) => (newer(b.v, a.v) ? b : a)).v;
const version = top.join('.');
const tag = tagArg || `v${version}`;

const platforms = {};
for (const k of found) {
  if (k.v.join('.') !== version) {
    console.warn(`ignoré (version ${k.v.join('.')}, pas ${version}) : ${path.join(k.dir, k.f)}`);
    continue;
  }
  // GitHub turns spaces in asset names into dots.
  const asset = k.f.replace(/ /g, '.');
  for (const p of k.platforms) {
    platforms[p] = { signature: fs.readFileSync(path.join(target, k.dir, k.f + '.sig'), 'utf8').trim(), url: `${repo}/${tag}/${asset}` };
  }
  console.log(`${k.platforms.join(', ')}: ${path.join(k.dir, k.f)} (+ .sig)`);
}
const out = path.join(target, 'latest.json');
fs.writeFileSync(out, JSON.stringify({ version, notes, pub_date: new Date().toISOString(), platforms }, null, 2) + '\n');
console.log(`\n${out} (version ${version}) : à joindre à la release ${tag} avec les fichiers ci-dessus.`);
