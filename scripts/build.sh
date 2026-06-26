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

# --- ABI gate: the .so MUST dlopen on the Move, or the slot silently stays blank ---
# The Move runs glibc 2.35 and ships NO libmvec.so.1. A toolchain newer than the
# device (e.g. ubuntu-latest = glibc 2.39) combined with -O3 -ffast-math makes GCC
# auto-vectorize sinf/cosf/powf into libmvec calls versioned GLIBC_2.38/2.39 — a .so
# the host cannot dlopen, so the chain host drops the plugin with no error. We fail
# the build HERE rather than ship that. The fix is always to correct the toolchain
# (build in the pinned Docker image); never relax this gate to make a build pass.
MOVE_GLIBC="${MOVE_GLIBC:-2.35}"
RE="${CROSS}readelf"
SO="$OUT/${MOD_ID}.so"

if "$RE" -d "$SO" | grep -qE 'NEEDED.*\blibmvec\b'; then
    echo "ERROR: ${MOD_ID}.so links libmvec.so.1 — absent on the Move (it would never load)." >&2
    "$RE" -d "$SO" | grep NEEDED >&2
    echo "  Cause: glibc>=2.39 toolchain auto-vectorized libm under -ffast-math." >&2
    echo "  Fix: build in the pinned Docker image (debian:bookworm-slim), not a host gcc." >&2
    exit 1
fi

MAXG="$("$RE" -V "$SO" 2>/dev/null | grep -oE 'GLIBC_2\.[0-9]+' | sort -uV | tail -1)"
MAXG="${MAXG#GLIBC_}"
if [ -n "$MAXG" ] && [ "$(printf '%s\n%s\n' "$MAXG" "$MOVE_GLIBC" | sort -V | tail -1)" != "$MOVE_GLIBC" ]; then
    echo "ERROR: ${MOD_ID}.so needs glibc $MAXG but the Move has only $MOVE_GLIBC." >&2
    echo "  Required versions:" >&2
    "$RE" -V "$SO" 2>/dev/null | grep -oE 'GLIBC_2\.[0-9]+' | sort -uV | sed 's/^/    /' >&2
    echo "  Fix: build in the pinned Docker image (debian:bookworm-slim), not a host gcc." >&2
    exit 1
fi
echo "ABI gate OK: max glibc = GLIBC_${MAXG:-none} (Move has $MOVE_GLIBC), no libmvec."

cp src/module.json "$OUT/"
cp ui_chain.js "$OUT/"
[ -f help.json ] && cp help.json "$OUT/"
# ship the SINGLE-FILE web app (wbw.js inlined by build_web.sh) as the module's web_ui.html
if [ -f docs/index.html ]; then cp docs/index.html "$OUT/web_ui.html"
elif [ -f web_ui.html ]; then cp web_ui.html "$OUT/"; [ -f wbw.js ] && cp wbw.js "$OUT/"; fi

tar -C "$ROOT/dist" -czf "$ROOT/dist/${MOD_ID}-module.tar.gz" "$MOD_ID"
ls -lh "$OUT/${MOD_ID}.so" "$ROOT/dist/${MOD_ID}-module.tar.gz"
echo "Built $ROOT/dist/${MOD_ID}-module.tar.gz"
