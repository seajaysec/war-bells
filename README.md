# War Bells

**War Bells** is a granular-sampling / delay / looping multi-effect **module for the
Ableton Move**, built on the open-source [Schwung](https://github.com/charlesvestal/schwung)
framework. It is a hand-written C `audio_fx` plugin: eleven effects across four families,
forty-four variations, four grain envelopes, a Hold sampler, a four-mode stereo reverb, a
resonant low-pass filter, pitch modulation, a 60-second phrase looper, eight character
presets, and a 16-slot user-preset bank that saves your sounds *and* their loops.

🎛️ **[Interactive manual & live demo →](https://seajaysec.github.io/war-bells/)**

## The effects

Eleven effects in four families, each with four named variations:

| Family | Effects | Character |
|--------|---------|-----------|
| **Loops** | Glide · Seq · Stack | layered short loops at different speeds — rhythms & octaves |
| **Grains** | Cloud · Drone · Chain | clouds of tiny fragments — washes, drones, textures |
| **Glitch** | Arp · Cutup · Chop | real-time rearrangement — stutters & bursts |
| **Delays** | Taps · Warp | a multi-tap delay line — rhythmic, filtered, pitched echoes |

Each effect's four variations have their own short labels (e.g. Stack → `oct+ / oct- / x2 /
range`, Taps → `line / swng / trip / wide`).

## Controls

War Bells is a standard Schwung `audio_fx` module. The host renders menu **pages** and maps
the **8 knobs** per page; metadata is served from the plugin (`chain_params` + `ui_hierarchy`)
so every parameter is editable on the device.

- **Root** — Preset · Effect · Variation · Activity · Repeats · Shape · Mix · Space · Filter
- **Tone** — Resonance · FX Vol · **Grain Env** (Soft / Pluck / Swell / Gate)
- **Time** — Clock (Free / Sync / Manual) · Subdivision · Tempo
- **Space FX** — Reverb (Room / Dark / Hall / Vast) · Mod Depth · Mod Rate
- **Perform** — Reverse · Hold · Hold Mode (Latch / Gate)
- **Looper** — on/off · Transport · Reverse · Level · Fade · Speed · Route · Quantize
- **Looper 2** — Fade shape · Record order · Looper-Only · Burst
- **User Slots** — User Slot (1–16) · User Op (Save / Load / Del)
- **Config** — Input (Stereo / Mono) · Input Gain · Bypass · Bypass Mode (Buf / Trail / True)

The six macro knobs (**Activity, Repeats, Shape, Mix, Space, Filter**) re-interpret per
effect. **Activity** = more/less of the effect's thing; **Repeats** = how long it rings on.

### Timing

Timing **defaults to Free** — a free-running internal tempo that drifts gently, so the
rhythmic engines breathe rather than lock metronomically (a free-running, never-quite-locked
feel). **Sync** locks hard to the Move's host clock; **Manual** holds a fixed
BPM. Onset-driven effects (Arp, Chain) also trigger from audio transients and MIDI note-ons.

### Saving your sounds

- **Character presets** — the `Preset` row sets the effect + all macros to one of eight
  curated starting points (Init, Erase, Edit, Chor, Shimr, Birds, Glass, Pad).
- **User-preset bank** — on the *User Slots* page, pick a slot (1–16) and **Save**; it writes
  every parameter *and* the recorded loop to disk under the module folder. **Load** restores
  both; **Del** clears the slot.
- **Whole-chain patches** — War Bells serializes its full state, so a Signal-Chain patch saved
  via the Schwung Manager captures it too (parameters only — not the loop audio).

### MIDI

Channel-wide CCs map the main controls (6 Activity · 7 Shape · 8 Filter · 9 Mix · 11 Repeats ·
12 Space · 20 Reverb · 22 Looper · 28–35 transport · 47 Reverse · 48 Hold · 102 Bypass, plus
more). **Program Change** selects effect + variation directly (effect = PC ÷ 4, variation =
PC mod 4).

### Web control

War Bells ships a custom **web UI** (`web_ui.html`) — believed to be the first Schwung module to
do so. It's dual-purpose:

- **[Interactive demo + manual](https://seajaysec.github.io/war-bells/)** on GitHub Pages — the
  synth panel runs as a simulation alongside the docs (live device control is disabled there
  because a browser can't open an insecure socket from an https page).
- **Live controller** when opened *from the Move* (an http origin), with War Bells in a chain
  slot: `http://move.local:7700/api/remote-ui/module-assets/war_bells/web_ui.html`. It connects
  to the Schwung Manager WebSocket, finds War Bells in your slots, drives the DSP, and reflects
  hardware-knob moves back into the page.

## How it works

```
in -> input gain/mono -> capture ring (frozen by Hold)
   -> 6 granular voices + multitap delay -> wet
   MIX(dry,wet) -> chorus(pitch-mod) -> SPACE(delay + reverb) -> resonant LP filter
   -> looper (thru + base/overdub) -> bypass crossfade -> out
```

All DSP is hand-written, realtime-safe C (header-only primitives in `src/dsp/core/`):
windowed granular voices over a rolling live-input buffer (`grain.h`, four envelope shapes),
a 4-tap delay with a pitch shifter (`multitap.h`, `pitchshift.h`), a chorus (`chorus.h`), a
Schroeder–Moorer reverb with four mode presets (`reverb.h`), a TPT state-variable resonant
filter (`svf.h`), a two-buffer 60-second looper (`looper.h`, WAV save/load), and an onset
detector (`transient.h`). The per-variation "brain" lives in `effects.c`.

## Build / install

```sh
bash scripts/run_tests.sh      # host-side C tests (native cc)
bash scripts/build.sh          # cross-compile war_bells.so (aarch64) + tarball
MOVE_HOST=ableton@move.local bash scripts/deploy-dev.sh   # sideload to a Move
```

See [`INSTALL.md`](INSTALL.md). The shared object is `war_bells.so` and exports
`move_audio_fx_init_v2` (audio_fx ABI v2). `requires_continuous_processing` keeps the
granular / looper / delay state alive through silence.

## Status

DSP is verified by `tests/test_smoke.c`: dry passthrough is unity, all 44 variations render,
the looper records / plays / overdubs / undoes / erases / saves / loads, character presets and
the user-slot bank round-trip (params **and** loop audio), Hold↔Looper exclusivity holds, and
`state` / `chain_params` / `ui_hierarchy` serialize cleanly. The aarch64 `.so` cross-compiles
clean and exports the entry symbol. Final analog *voicing* is tuned on-device.

## License & credits

War Bells is released under the [GNU Affero General Public License v3.0](LICENSE) © Chris Farrell.
Because it's served over a network (the web UI), AGPL §13 applies — the in-app footer links back
to this source.

It links the Schwung host ABI: `src/host/audio_fx_api_v2.h` and `src/host/plugin_api_v1.h` are
redistributed unmodified from [Schwung](https://github.com/charlesvestal/schwung) (MIT ©
Charles Vestal) — see [`THIRD_PARTY.md`](THIRD_PARTY.md). The reverb is adapted from the
public-domain Freeverb (Jezar at Dreampoint). Documentation is illustrative.
