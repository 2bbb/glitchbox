#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Glitchbox: publish the browser emulator (build/emu, from web/emu/build.sh) to the gh-pages branch on origin.
# GitHub Pages then serves it (Settings > Pages > Deploy from a branch: gh-pages, / (root)).
set -e
cd "$(dirname "$0")/.."
[ -f build/emu/index.html ] || { echo "publish_pages: run sh web/emu/build.sh first"; exit 1; }
SRC=$(git rev-parse --short HEAD)
TMP=$(mktemp -d)
cp -R build/emu/. "$TMP"
touch "$TMP/.nojekyll"
cd "$TMP"
git init -q -b gh-pages
git add -A
git commit -qm "Emulator build from $SRC"
git push -f "$(cd - >/dev/null && git remote get-url origin)" gh-pages
rm -rf "$TMP"
