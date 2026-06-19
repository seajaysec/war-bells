# Single-source presets (engine is the source of truth)

**The bug to never repeat:** presets defined twice — once in the engine's `apply_preset` (C) and once as a JS
object in the web UI — silently drift. The web played a hotter preset that crashed; the device's tamed preset
was fine; host tests (using the engine values) passed. You debug a crash that doesn't exist on the device.

## The pattern: the engine IS the oracle
Define presets ONCE in the engine. The web compiles that same engine to WASM (it already does, for the demo)
and uses it as a "preset oracle": set the `preset` param to run the real `apply_preset`, then read every value
back. The JS keeps only names + descriptions, never values.

```js
function applyPreset(i){
  P.preset.v = i;
  if (Bridge.mode === "live") {                 // hardware runs apply_preset + reports back via param_update
    pushToDevice("preset");
    ws.send(JSON.stringify({type:"subscribe", slot:Bridge.slot}));  // fresh dump so knobs reflect it
    return;
  }
  // demo/manual: the WASM oracle (lazy-loaded)
  const run = () => { engine.ccall("wbw_set",null,["string","string"],["preset", PRESETS[i].name]);
                      readback(); notify(); };
  if (engine) run(); else ensureEngine().then(ok => ok && run());
}
```

## Reading values back from the WASM
`get_param(key, buf, n)` writes the value as a string into a heap buffer. **This Emscripten build exposes
`HEAP16`, not `UTF8ToString`/`HEAPU8`** — so read the C string straight off the heap:
```js
function readback(){
  const buf = engine._malloc(64);
  const rd = (k) => { const n = engine.ccall("wbw_get","number",["string","number","number"],[k,buf,64]);
    if (n<=0) return null; const u8 = new Uint8Array(engine.HEAP16.buffer);
    let s=""; for (let i=buf; u8[i]; i++) s += String.fromCharCode(u8[i]); return s; };
  ingest("effect", rd("effect"));               // read effect BEFORE variation (var labels depend on it)
  for (const k in P) if (k!=="effect" && k!=="preset") { const v = rd(k); if (v!==null) ingest(k, v); }
  engine._free(buf);
}
```
`ingest()` already parses both enum option-strings and numeric indices, so it round-trips `get_param` output.

## The parity guard (cheap, prevents regression)
A test that the web's preset NAME list equals the engine's preset enum — so the list can't drift even if
someone re-adds JS values. Extract both and compare in the test runner:
```sh
c=$(sed -n '/PRESET_OPTS\[/,/};/p' src/dsp/params.c | grep -oE '"[A-Za-z]+"' | tr -d '"' | tr '\n' ' ')
j=$(sed -n '/const PRESETS=/,/^];/p' web_ui.html | grep -oE 'n:"[A-Za-z]+"' | sed -E 's/n:"//;s/"//' | tr '\n' ' ')
[ "$c" = "$j" ] && echo OK || { echo "FAIL parity"; exit 1; }
```
