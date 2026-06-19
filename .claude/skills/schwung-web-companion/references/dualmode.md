# Dual-mode: demo off-device, live controller on-device

## Detection
```js
// https (GitHub Pages) or off-device  -> demo + manual
// http served from the Move           -> live controller
const onMove = location.protocol !== "https:";
function defaultHost(){
  const saved = localStorage.getItem("host"); if (saved) return saved;
  if (location.host && location.protocol.startsWith("http") && location.host.indexOf("github.io")<0)
    return location.host;             // served from the Move -> its own host:port
  return "move.local:7700";
}
```
Boot: if https → demo (+ a hand-off button, below); else `connectLive(defaultHost())`.

## The live bridge (schwung-manager WebSocket)
```js
const ws = new WebSocket("ws://" + host + "/ws/remote-ui");
ws.onopen = () => { for (let s=0;s<8;s++) ws.send(JSON.stringify({type:"subscribe", slot:s})); };
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.type === "slot_info") {
    // a module can be loaded as fx1 AND fx2 in a slot -> 2x per track, up to 8 instances.
    for (const comp of ["fx1","fx2"]) {
      if (m[comp] === "<your_module>") { /* register {slot, comp}; auto-lock to first; show in picker */ }
    }
  } else if (m.type === "param_update") {
    // cache m.params for EVERY slot (instant switching); if it's the active slot, ingest -> UI.
    // set: ws.send({type:"set_param", slot, key: comp+":"+key, value:String(v)})
  }
};
```
- Auto-lock to the first instance found; if >1, show a "Demo ↔ slot N · FX1/FX2" picker.
- Cache all slots' params from `param_update` so switching targets is instant (seed from cache, then re-subscribe
  for a fresh dump).
- On ws error with no instances → fall back to demo gracefully (don't leave the user staring at "error").

## The https ↔ ws wall (and the hand-off)
A page on **https cannot open an insecure `ws://`** (browser mixed-content block), and schwung-manager serves
plain `ws`. So the public Pages URL *cannot* talk to a local Move — this is unfixable in JS. Solution: on https,
show the host field + an **"Open on Move"** button that navigates to the http copy served by the Move:
```js
if (location.protocol === "https:") {
  location.href = "http://" + host + "/api/remote-ui/module-assets/<module>/web_ui.html";
  return;                                  // same app, right origin -> ws works there
}
```
Document the reliable path for users: bookmark the Move's http URL; the Pages link is demo + launcher. (The only
way to control from the https link itself would be the Move serving `wss://` with a real cert — a manager change.)

## Live preset picks
In live mode, applying a preset = `set_param("preset", name)` (the hardware runs its own `apply_preset`), then
re-`subscribe` to the slot to force a fresh param dump so the UI knobs reflect it. No WASM needed in live mode.
