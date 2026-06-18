#!/usr/bin/env bash
# Host-side test build/run (native clang/gcc — not cross-compiled).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CC="${CC:-cc}"
INC="-Isrc -Isrc/dsp"
OPT="-O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter"
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

rm -rf "$OBJ"
[ "$fail" = 0 ] && echo "all tests passed." || { echo "TESTS FAILED"; exit 1; }
