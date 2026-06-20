# Install — War Bells (Ableton Move / Schwung)

## Requirements
- An Ableton Move running **Schwung**.
- To build from source: a cross-toolchain (`gcc-aarch64-linux-gnu`) **or** Docker (the
  build script falls back to a hermetic Docker build via `scripts/Dockerfile`).

## Build
```sh
bash scripts/run_tests.sh    # optional: host-side C tests
bash scripts/build.sh        # -> dist/war_bells/war_bells.so + dist/war_bells-module.tar.gz
```
The build verifies the `.so` is named `war_bells.so` and exports `move_audio_fx_init_v2`
(the audio_fx v2 entry point). `module.json` and `ui_chain.js` are packed into the tarball.

## Install on the Move

### Option A — dev sideload (fastest iteration)
```sh
MOVE_HOST=ableton@move.local bash scripts/deploy-dev.sh
```
This builds, scps `war_bells.so` + `module.json` + `ui_chain.js` to
`/data/UserData/schwung/modules/audio_fx/war_bells/`, and restarts the Move to load it.
Set `MOVE_NO_RESTART=1` to copy only.

### Option B — Schwung Manager (no build required)
Open the Schwung Manager at `http://move.local:7700` → **Modules**:

- **Catalog** *(once War Bells is listed)* — find it under audio FX and tap **Install**.
- **Custom → from GitHub URL** — paste `seajaysec/war-bells`. The Manager fetches
  `release.json` from the repo and downloads the matching `war_bells-module.tar.gz` asset.
- **Custom → from file** — download `war_bells-module.tar.gz` from the
  [latest Release](https://github.com/seajaysec/war-bells/releases/latest) and upload it
  ("Install from File"). When building locally, the same file is at
  `dist/war_bells-module.tar.gz`.

> **Don't upload GitHub's "Source code (tar.gz)".** That archive keeps `module.json` under
> `src/`, so the Manager's installer (which expects `module.json` at the top of the single
> bundled folder) reports **"No module.json found in tarball."** Use the
> `war_bells-module.tar.gz` asset, whose layout is `war_bells/module.json` + `war_bells.so` + …

On-device layout (audio_fx in a Signal Chain is loaded directly as `<id>/<id>.so`):
```
/data/UserData/schwung/modules/audio_fx/war_bells/
├── war_bells.so
├── module.json
└── ui_chain.js
```

## Use
Add **War Bells** to a Signal Chain (it's an audio effect — it processes whatever feeds
the chain). Open it to get the macro quick-editor; the full menu pages
(Effect / Time / Tone / Space / Hold / Looper / Config) are under the module's UI. Pick a
category/effect/variation on the root page; `Mix` blends dry vs effect.

## Troubleshooting
- **Module missing after install:** confirm the `.so` is named `war_bells.so` under
  `.../audio_fx/war_bells/` and restart the Move.
- **No effect / silence:** `Mix` > 0, `Bypass` Off, and check the chain's input level.
- **Build can't find `aarch64-linux-gnu-gcc`:** install it, set `CROSS_PREFIX`, or ensure
  Docker is running (the script will use `scripts/Dockerfile`).
- **Memory:** the 60s stereo looper (base + overdub) uses ~21 MB; the capture + delay
  lines add ~3 MB. Shorten `WB_LOOP_SEC` in `src/dsp/wb_internal.h` if needed.
