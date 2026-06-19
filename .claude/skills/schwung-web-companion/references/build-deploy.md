# Build, lazy WASM, and deploy

## One self-contained file (no waterfall)
Inline the WASM-loader JS into the HTML so the shipped app is a single file (one fetch; works offline,
origin-agnostic — Pages https AND the Move http). A tiny build step:
```sh
# build_web.sh — inline wbw.js (a SINGLE_FILE/base64 Emscripten build) into web_ui.html -> docs/index.html
python3 - <<'PY'
html=open('web_ui.html').read(); js=open('wbw.js').read()
tag='<script src="wbw.js"></script>'
open('docs/index.html','w').write(html.replace(tag,'<script>'+js+'</script>'))
PY
rm -f docs/wbw.js     # no longer referenced
```
Wire it after the WASM build; ship `docs/index.html` to Pages and as the module's `web_ui.html` to the Move.

## Lazy WASM instantiation (the real speed win)
The web UI's controls come from JS, not the WASM — so don't instantiate the engine at boot. Instantiate it
**lazily**, only when actually needed, and **never in live mode** (the Move is the engine):
- Boot: paint immediately; ~0.8 s later, if NOT live, lazy-load the engine and canonicalize the current preset
  (so the manual shows real device values). In live mode this never runs → the Move never spins up a WASM.
- `applyPreset` / "start demo audio" lazy-load on demand.
This beats epimore's "inline + instantiate at boot": one file AND instant first paint AND zero WASM cost on
the device.

## Build into the module + serve from the Move
- `build.sh` copies the single `docs/index.html` to `dist/<module>/web_ui.html` (drop the separate wbw.js).
- Deploy ships it with the module; schwung-manager serves it at
  `http://move.local:7700/api/remote-ui/module-assets/<module>/web_ui.html`.
- It's a **static asset**: to update just the web UI, `scp` it over — **no Move restart needed** (unlike the
  `.so`). Restart only for native/DSP changes.

## Mobile scroll gotchas (these all bit this UI)
- **Tall sticky traps touch scroll on iOS:** a `position:sticky` element TALLER than the viewport pins and
  iOS can't scroll past it. Make it static on small screens: `@media (max-width:820px){ .synthbar{position:static} }`.
- **Don't auto-hide/collapse on scroll.** A scroll listener that collapses a top panel as the user scrolls
  *fights the scroll* — jumpy and unpredictable, worst on iOS (the layout shift moves the scroll position).
  People know how to scroll. Drop the auto behavior; keep a manual collapse button if you want one.
- **Horizontal scroll = something wider than the viewport.** Usual cause: a CSS grid that doesn't collapse on
  mobile — especially an **inline `grid-template-columns` that overrides your media query**, or grid items that
  won't shrink below content (use `minmax(0,1fr)` or a `@media{...1fr}` rule). Belt-and-suspenders:
  `html{overflow-x:clip}` (clip, not hidden — keeps `position:sticky` working). Find the culprit by scanning
  for elements whose `getBoundingClientRect().right > clientWidth`.

## Install / discoverability
Add an "Install" section to the manual: catalog/Module Store path ("search the module, once listed") + manual
install (download the release tarball from GitHub releases, `scp` + `tar` to `…/schwung/modules/audio_fx/`,
reload). Link the repo. Keeps the one shippable web page self-documenting.
