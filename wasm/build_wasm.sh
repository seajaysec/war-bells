#!/usr/bin/env bash
# Build the in-browser WASM engine: the real War Bells DSP compiled with Emscripten,
# inlined (SINGLE_FILE) into a self-contained wbw.js that runs from any origin.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
command -v emcc >/dev/null || { echo "emcc not found — install Emscripten"; exit 1; }

emcc wasm/wbw.c src/dsp/war_bells.c src/dsp/effects.c src/dsp/params.c \
  -Isrc -O3 -ffast-math \
  -sEXPORTED_FUNCTIONS=_wbw_init,_wbw_set,_wbw_get,_wbw_render,_wbw_midi,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAP16 \
  -sMODULARIZE=1 -sEXPORT_NAME=createWarBells \
  -sALLOW_MEMORY_GROWTH=1 -sSINGLE_FILE=1 -sENVIRONMENT=web \
  -o wbw.js

ls -lh wbw.js
echo "built wbw.js — self-contained WASM engine"
bash scripts/build_web.sh   # inline wbw.js into the single-file docs/index.html (one to ship)
