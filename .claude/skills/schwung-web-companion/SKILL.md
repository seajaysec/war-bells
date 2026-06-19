---
name: schwung-web-companion
description: >
  Ship ONE web app alongside a Schwung (Ableton Move) module that adapts to context: an in-browser demo +
  manual when off-device, and a LIVE controller of the real plugin when served from the Move. Use when building
  or fixing a module's web UI, a "web demo", controlling the Move from a browser, single-source presets between
  engine and web, the WASM demo engine, or deploying/serving the web UI. Complements the Schwung-Module-Creator
  skill (that one covers the native module; this one covers its web companion).
---

# Schwung web companion

One HTML app, two contexts, no duplication. Served from GitHub Pages (https) it's a demo + manual; served from
the Move (http) it's a live controller of the real plugin in Schwung. The same file detects which and adapts.

## Architecture at a glance

- **Context detection** (`references/dualmode.md`): `location.protocol === "https:"` → demo; `http` (served by
  the Move) → live. The Move serves the file at
  `http://<host>:7700/api/remote-ui/module-assets/<module>/web_ui.html` (static asset — `scp` updates it, **no
  restart**).
- **Live bridge**: WebSocket to schwung-manager `ws://<host>/ws/remote-ui`; subscribe to all 8 slots; `slot_info`
  reveals which slots hold your module (check `fx1`/`fx2` for the 2×-per-track / 8× case); `param_update` syncs
  hardware ↔ UI. Cache all slots for instant switching; offer a Demo↔slot picker.
- **The https↔ws wall**: a browser **cannot open `ws://` from an https page** (mixed content), and the Move
  serves plain `ws`. So on Pages, the connect button **hands off** (navigates) to the Move's http URL where the
  socket works — not a bug, a browser security rule. Graceful no-device fallback → demo.
- **Single-source presets** (`references/single-source-presets.md`): presets are defined ONCE in the engine
  (`apply_preset`). The web sets the `preset` param to run the real code, then reads values back — never a
  duplicate JS table (it WILL drift and crash one surface while tests pass on the other).
- **Fast loading** (`references/build-deploy.md`): inline everything into one self-contained file; instantiate
  the WASM **lazily** (only in demo/manual, deferred past first paint) and **never in live mode** (the hardware
  is the engine). Beats "inline + instantiate at boot".

## When something's wrong

- "web demo crashes but device is fine / tests pass" → preset drift. Single-source it + add a parity test.
- "can't control my Move from the GitHub link" → expected (https→ws blocked). Use/serve the Move's http URL;
  add a hand-off button on Pages. See `references/dualmode.md`.
- "slow to load / sluggish on the Move" → lazy-instantiate the WASM; skip it entirely in live mode.
- "iOS won't scroll" → a `position:sticky` element taller than the viewport traps touch scroll; make it static
  under ~820px. (General web gotcha, lives here because it bit this UI.)
