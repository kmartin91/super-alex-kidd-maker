#!/bin/sh
# Runs inside a Debian container (see app/build-linux.mjs): builds the Linux
# .deb and .AppImage of the app from a copy of the repository (/src, read
# only) into /out. The engines in maker/public/engine/ must already be built.
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq libwebkit2gtk-4.1-dev build-essential curl wget file libxdo-dev libssl-dev \
  libayatana-appindicator3-dev librsvg2-dev nodejs npm xdg-utils squashfs-tools > /dev/null
mkdir -p /work && cd /work
tar -C /src -cf - --exclude=./node_modules --exclude=./app/src-tauri/target --exclude=./app/.deps \
  --exclude=./reference --exclude=./.git --exclude='./engine/build*' --exclude=./online . | tar -xf -
npm install --no-audit --no-fund > /dev/null
export APPIMAGE_EXTRACT_AND_RUN=1 CARGO_TARGET_DIR=/cache/target NO_STRIP=true
cd app
CONFIG=""
[ "$RELEASE" = 1 ] && CONFIG="--config src-tauri/tauri.release.json"  # + signed update bundles
npx tauri build $CONFIG --bundles deb
# The AppImage tools are AppImages themselves: their magic bytes in the ELF
# header stop Docker's emulation of another processor from running them, so
# they are downloaded (first try), cleared, then the AppImage is made again.
npx tauri bundle $CONFIG --bundles appimage > /tmp/appimage.log 2>&1 || {
  for f in /root/.cache/tauri/*.AppImage; do printf '\000\000\000' | dd of="$f" bs=1 seek=8 count=3 conv=notrunc 2>/dev/null; done
  npx tauri bundle $CONFIG --bundles appimage
}
cp /cache/target/release/bundle/deb/*.deb /cache/target/release/bundle/appimage/*.AppImage /out/
cp /cache/target/release/bundle/appimage/*.AppImage.sig /out/ 2>/dev/null || true
ls -la /out
