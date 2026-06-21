#!/usr/bin/env bash
# Host-side test build/run (native clang/gcc — not cross-compiled).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CC="${CC:-cc}"
INC="-Isrc -Isrc/dsp"
# -std=c11 puts glibc in strict-ISO mode and hides X/Open symbols (e.g. M_PI used by
# the tests and audit probes); _XOPEN_SOURCE=600 re-exposes them. No-op on Apple libm.
OPT="-O2 -g -std=c11 -D_XOPEN_SOURCE=600 -Wall -Wextra -Wno-unused-parameter"
OBJ="$(mktemp -d)"
DSP="src/dsp/war_bells.c src/dsp/effects.c src/dsp/params.c"

echo "compiling dsp..."
DSP_OBJS=""
for s in $DSP; do
  o="$OBJ/$(basename "$s" .c).o"
  $CC $OPT $INC -c "$s" -o "$o"
  DSP_OBJS="$DSP_OBJS $o"
done

fail=0
for t in tests/test_*.c; do
  name="$(basename "$t" .c)"
  $CC $OPT $INC -c "$t" -o "$OBJ/$name.o"
  $CC "$OBJ/$name.o" $DSP_OBJS -lm -o "$OBJ/$name"
  if "$OBJ/$name"; then echo "OK  $name"; else echo "FAIL $name"; fail=1; fi
done

# --- preset parity: the web demo's preset NAMES must match the C engine enum (single source of truth) ---
c_names=$(sed -n '/PRESET_OPTS\[[0-9]/,/};/p' src/dsp/params.c | grep -oE '"[A-Za-z]+"' | tr -d '"' | tr '\n' ' ')
js_names=$(sed -n '/const PRESETS=/,/^];/p' web_ui.html | grep -oE 'n:"[A-Za-z]+"' | sed -E 's/n:"//;s/"//' | tr '\n' ' ')
if [ -n "$c_names" ] && [ "$c_names" = "$js_names" ]; then echo "OK  preset-parity ($c_names)";
else echo "FAIL preset-parity (web != engine)"; echo "  C : $c_names"; echo "  JS: $js_names"; fail=1; fi

# --- audit stage: every diagnostic probe in scripts/check*.c runs on every test run ---
# checkstability (runaway-to-rails) and checkstress (per-metric artifact scorecard vs baseline) exit
# non-zero on a real failure and GATE the build; checkpresets/checklimit print diagnostics for the record.
for a in scripts/check*.c; do
  [ -e "$a" ] || continue
  name="audit-$(basename "$a" .c)"
  $CC $OPT $INC -c "$a" -o "$OBJ/$name.o"
  $CC "$OBJ/$name.o" $DSP_OBJS -lm -o "$OBJ/$name"
  echo "=== $name ==="
  if "$OBJ/$name"; then echo "OK  $name"; else echo "FAIL $name"; fail=1; fi
done

rm -rf "$OBJ"
[ "$fail" = 0 ] && echo "all tests passed." || { echo "TESTS FAILED"; exit 1; }
