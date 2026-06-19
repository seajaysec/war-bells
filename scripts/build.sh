#!/usr/bin/env bash
# Build the War Bells audio_fx module .so for Ableton Move (aarch64) and pack a
# Schwung Module Store tarball.
#
# audio_fx in a Signal Chain is loaded directly as <id>/<id>.so, so the shared
# object MUST be named war_bells.so (not dsp.so) and export move_audio_fx_init_v2.
#
# Honours CROSS_PREFIX (default aarch64-linux-gnu-); re-execs in Docker if absent.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MOD_ID="war_bells"
CROSS="${CROSS_PREFIX:-aarch64-linux-gnu-}"
OUT="$ROOT/dist/$MOD_ID"

if ! command -v "${CROSS}gcc" >/dev/null 2>&1; then
    if [ -z "${IN_DOCKER:-}" ] && command -v docker >/dev/null && [ -f "$ROOT/scripts/Dockerfile" ]; then
        echo "No ${CROSS}gcc on host; building inside Docker..."
        docker build -t war-bells-builder -f "$ROOT/scripts/Dockerfile" "$ROOT"
        docker run --rm -v "$ROOT:/work" -w /work -e IN_DOCKER=1 \
            war-bells-builder ./scripts/build.sh
        exit 0
    fi
    echo "ERROR: ${CROSS}gcc not found and Docker unavailable." >&2
    echo "       Install gcc-aarch64-linux-gnu, set CROSS_PREFIX, or run Docker." >&2
    exit 1
fi

rm -rf "$OUT" "$ROOT/dist/${MOD_ID}-module.tar.gz"
mkdir -p "$OUT"

CFLAGS=(-O3 -g -fPIC -shared -ffast-math -Wall -Wno-unused-parameter -Wno-parentheses)
INCLUDES=(-Isrc -Isrc/dsp -Isrc/dsp/core)
LIBS=(-lm)

SRC=(
    src/dsp/war_bells.c
    src/dsp/effects.c
    src/dsp/params.c
)

cd "$ROOT"
echo "Cross-compiling ${MOD_ID} -> $OUT/${MOD_ID}.so"
"${CROSS}gcc" "${CFLAGS[@]}" "${INCLUDES[@]}" "${SRC[@]}" -o "$OUT/${MOD_ID}.so" "${LIBS[@]}"

# Sanity: verify the audio_fx entry symbol is exported
if ! "${CROSS}nm" -D "$OUT/${MOD_ID}.so" | grep -q ' T move_audio_fx_init_v2$'; then
    echo "ERROR: move_audio_fx_init_v2 not exported from ${MOD_ID}.so" >&2
    "${CROSS}nm" -D "$OUT/${MOD_ID}.so" | grep -i move >&2 || true
    exit 1
fi

cp src/module.json "$OUT/"
cp ui_chain.js "$OUT/"
[ -f help.json ] && cp help.json "$OUT/"
[ -f web_ui.html ] && cp web_ui.html "$OUT/"
[ -f wbw.js ] && cp wbw.js "$OUT/"

tar -C "$ROOT/dist" -czf "$ROOT/dist/${MOD_ID}-module.tar.gz" "$MOD_ID"
ls -lh "$OUT/${MOD_ID}.so" "$ROOT/dist/${MOD_ID}-module.tar.gz"
echo "Built $ROOT/dist/${MOD_ID}-module.tar.gz"
