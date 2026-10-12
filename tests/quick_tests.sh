#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Glitchbox: the host tests that need no firmware build (no JieLi toolchain, no Docker): the DSP, the drum kit,
# the FX layer, the UI and the regression renders. Run from the repo root after the headers exist in build/gen
# (tools/gen_only.sh). The full suite is tests/run_tests.sh (needs ./build.sh first).
#   tests/quick_tests.sh            all of them
#   tests/quick_tests.sh drum_test  one (or several) by name
set -e
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT" build/demo
CC="${CC:-cc}"
[ -f build/gen/felucca_tables.h ] || { echo "quick_tests: run tools/gen_only.sh first (build/gen)"; exit 1; }
fail=0
one() {
    name=$1
    shift
    echo "== $name"
    flags=-DGLITCHBOX_DEFAULTS=0                     # upstream's tests: written against Felucca's power-on
    [ "$name" = glitch_test ] && flags=
    $CC -O2 -w $flags -Ibuild/gen -Ifirmware/src -o "$OUT/$name" "tests/$name.c" -lm || { fail=1; return; }
    "$OUT/$name" "$@" || fail=1
}
want() { [ -z "$SEL" ] || echo " $SEL " | grep -q " $1 "; }
SEL="$*"
want glitch_test && one glitch_test build/demo/glitch
want drum_test && one drum_test build/demo/drum
want perform_test && one perform_test build/demo/perform build/demo/fx build/demo/throw
want ui_test && one ui_test
want midi_control_test && one midi_control_test
want mod_test && one mod_test build/demo/mod
want project_test && one project_test
want union_test && one union_test tests/union_golden.txt
if want ui_render; then                          # every screen drawn: the layout lint, alignment, the splash
    echo "== ui_render"
    FV=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)
    mkdir -p build/ui_new/ppm build/ui_slot
    $CC -O1 -w -DGLITCHBOX_DEFAULTS=0 "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/ui_render" \
        tests/ui_render.c -lm || fail=1
    if "$OUT/ui_render" build/ui_new build/ui_slot > "$OUT/ui_render.log" 2>&1; then
        tail -3 "$OUT/ui_render.log"
    else
        fail=1
        echo "ui_render: FAILED (build/ui_new/report.txt, $OUT/ui_render.log)"
    fi
fi
# CPU: instruction counts depend on the host compiler; this Mac's own baseline (build/host/cpu_local.txt, made on
# the first run from the current sources: BUDGET_UPDATE=1) guards later changes. tests/cpu_baseline.txt is upstream's
[ -f "$OUT/cpu_local.txt" ] || { cp tests/cpu_baseline.txt "$OUT/cpu_local.txt"; BUDGET_UPDATE=1; export BUDGET_UPDATE; }
want regress && one regress tests/golden.txt "$OUT/cpu_local.txt"
unset BUDGET_UPDATE
[ "$fail" = 0 ] && echo "quick_tests: all passed" || { echo "quick_tests: FAILED"; exit 1; }
