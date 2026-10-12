#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Glitchbox: generate build/gen (fonts, icons, tables, samples) without the JieLi toolchain or Docker, for the host
# tests (tests/quick_tests.sh) and the browser emulator (web/emu/build.sh). Python 3 with Pillow (built with raqm:
# on macOS `brew install fribidi`) and fontTools; uses .venv/bin/python when it exists.
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
[ -x .venv/bin/python ] && PY=.venv/bin/python
[ "$(uname -s)" = Darwin ] && export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}"
"$PY" -c 'import PIL.features as f, sys; sys.exit(0 if f.check("raqm") else 1)' 2>/dev/null ||
    { echo "gen_only: Pillow has no raqm (macOS: brew install fribidi)"; exit 1; }
exec "$PY" -c 'import importlib.util, sys
sys.argv = ["build.py"]
s = importlib.util.spec_from_file_location("build", "tools/build.py")
b = importlib.util.module_from_spec(s)
s.loader.exec_module(b)
b.generate()'
