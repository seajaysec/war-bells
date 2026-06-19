#!/usr/bin/env bash
# run_tests.snippet.sh — drop these stages into your test runner. They (a) build+run every unit test,
# (b) build+run every scripts/check*.c audit probe and GATE on its exit code, (c) guard single-source parity.
# Adapt: $DSP (your engine .c files), the parity grep paths.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
CC="${CC:-cc}"
INC="-Isrc -Isrc/dsp"
# NOTE: -std=c11 puts glibc in strict-ISO mode and HIDES M_PI (and other X/Open symbols) used by the
# probes; -D_XOPEN_SOURCE=600 re-exposes them (no-op on Apple libm). Without it, Linux-only build failure.
OPT="-O2 -g -std=c11 -D_XOPEN_SOURCE=600 -Wall -Wextra -Wno-unused-parameter"
OBJ="$(mktemp -d)"
DSP="src/dsp/engine.c"          # <-- adapt: your engine source(s)

DSP_OBJS=""
for s in $DSP; do o="$OBJ/$(basename "$s" .c).o"; $CC $OPT $INC -c "$s" -o "$o"; DSP_OBJS="$DSP_OBJS $o"; done
fail=0

# unit tests
for t in tests/test_*.c; do name="$(basename "$t" .c)"
  $CC $OPT $INC -c "$t" -o "$OBJ/$name.o"; $CC "$OBJ/$name.o" $DSP_OBJS -lm -o "$OBJ/$name"
  if "$OBJ/$name"; then echo "OK  $name"; else echo "FAIL $name"; fail=1; fi
done

# single-source parity guard (example: web preset names == engine enum). Adapt the two extractions.
# c_names=$(sed -n '/PRESET_OPTS\[/,/};/p' src/dsp/params.c | grep -oE '"[A-Za-z]+"' | tr -d '"' | tr '\n' ' ')
# js_names=$(sed -n '/const PRESETS=/,/^];/p' web_ui.html | grep -oE 'n:"[A-Za-z]+"' | sed -E 's/n:"//;s/"//' | tr '\n' ' ')
# [ -n "$c_names" ] && [ "$c_names" = "$js_names" ] && echo "OK  parity" || { echo "FAIL parity"; fail=1; }

# audit probes: every scripts/check*.c is compiled + run; a non-zero exit GATES the build.
# checkstress.c exits non-zero on an absolute artifact breach OR a regression vs tests/metrics_baseline.json.
for a in scripts/check*.c; do [ -e "$a" ] || continue; name="audit-$(basename "$a" .c)"
  $CC $OPT $INC -c "$a" -o "$OBJ/$name.o"; $CC "$OBJ/$name.o" $DSP_OBJS -lm -o "$OBJ/$name"
  echo "=== $name ==="; if "$OBJ/$name"; then echo "OK  $name"; else echo "FAIL $name"; fail=1; fi
done

rm -rf "$OBJ"
[ "$fail" = 0 ] && echo "all tests passed." || { echo "TESTS FAILED"; exit 1; }
