# War Bells — Design

War Bells is a hand-written C `audio_fx` module for the Ableton Move (Schwung framework): a
granular / glitch / delay / looper multi-effect. This document describes its architecture and
the on-device control model. The DSP is a C port of a sibling monome norns build.

---

## 1. Signal path

```
            ┌────────── HOLD SAMPLER (freeze the capture ring, keep feeding the FX) ──┐
            │                                                                          ▼
IN ─► input gain / mono ─► capture ring ─► EFFECTS (granular / glitch / delay) ─► wet
            │                                          │
            └────────────── MIX (dry vs wet) ─────────┘
                                  ▼
        chorus (pitch-mod) ─► SPACE (delay + reverb) ─► resonant low-pass filter
                                  ▼
        phrase looper (thru + base/overdub) ─► bypass crossfade ─► OUT
```

- **Mix** balances dry input against the wet effect; at 100% wet some effects fully cut the dry.
- **Hold** freezes the capture ring so the effects keep reprocessing one captured moment.
- **Pitch-mod → reverb → filter** are a global post-effects chain.
- **Filter** is a resonant low-pass at the end; fully clockwise = open.
- **Looper** sits last (post-FX by default; a pre-FX route feeds the loop back through the FX).

---

## 2. Root macros (the 8 root knobs)

`Effect · Variation · Activity · Repeats · Shape · Mix · Space · Filter`

- **Effect / Variation** pick the sound (11 effects × 4 variations).
- **Activity** — density / complexity ("more of what this effect does").
- **Repeats** — how long the effect rings on (grain length, feedback, drone decay).
- **Shape** — tone & contour (grain size / brightness).
- **Mix** — dry vs effect. **Space** — reverb + delay wash. **Filter** — low-pass cutoff.

Secondary controls live on sub-pages: Resonance + FX Vol (Tone), Mod Depth + Mod Rate
(Space FX), loop fade/speed/route/etc. (Looper pages).

---

## 3. Effects — 4 families, 11 effects, 44 variations

Variation labels are per-effect (shown on screen). Activity re-interprets per effect.

### Glitch — real-time rearrangement of the incoming signal
- **Arp** — arpeggios of recent note onsets. `basic / spds / filt / crush`
- **Cutup** — glitches interrupt the live signal. `cuts / pitch / sweep / crush`
- **Chop** — rearranges playing into sequenced bursts. `runs / pitch / soft / crush`

### Loops — layered short loops at various speeds → new rhythms & octaves
- **Glide** — short loops glide in pitch over time. `half / down / up / both`
- **Seq** — looped samples rearranged into rhythms. `filt / half / sweep / crush`
- **Stack** — overlapping loopers at multiple speeds. `oct+ / oct- / x2 / range`

### Grains — clouds of fragments → washes, drones, textures
- **Cloud** — clusters of grains form a wash. `short / dense / hi / lo`
- **Drone** — cyclical micro-loops → hypnotic drones. `morph / sub / bpf / env`
- **Chain** — rhythmic chains of recent onsets. `hold / phase / casc / casc2`

### Delays — a multi-tap delay line
- **Taps** — taps arranged into rhythmic patterns. `line / swng / trip / wide`
- **Warp** — taps shaped with filters & pitch. `env / bpf / pitch / grain`

---

## 4. Grain envelope, timing, space

- **Grain Env** (Tone page) — each grain's amplitude shape: **Soft** (Hann), **Pluck**
  (abrupt onset, fade), **Swell** (ramp up; reverse feel), **Gate** (flat on/off).
- **Clock** (Time page) — **Free** (default: a free-running internal tempo with gentle drift,
  so rhythmic engines breathe), **Sync** (locks to the host clock), **Man** (fixed BPM).
  **Subdiv** = 1/4 · 1/2 · 1x · 2x · 4x · 8x.
- **Reverb** (Space FX) — four modes: Room / Dark / Hall / Vast, with **Width** (stereo) and
  **Shimmer** (the reverb tail fed back through a pitch-shifter, Oct± / 5th). **Pitch mod** = Mod
  Depth + Mod Rate (gentle chorus → detuned wobble).
- **Scale** (Tone page) — snaps pitched grains/glides to a scale (Maj / Min / Pent / Oct / 5th)
  so movement stays in key; Off = bit-identical to no quantization.

## 4b. Movement & generative

- **Motion** (Motion page) — one tempo-synced LFO (Sine / Tri / Ramp / Rand, 8 bar…1/8) that
  modulates a chosen macro (**Mod Dest** = Activity / Filter / Space / Mix / Mod). Applied as a
  per-block, non-destructive overlay — the stored param value is untouched.
- **Evolve / Dice** (Generate page) — on a tempo-synced clock (faster as Evolve → 1) the engine
  re-rolls per-voice scatter, and (by **Range**: Soft / Mid / Wild) the variation and small
  activity/shape nudges, so textures drift and never repeat. **Dice** fires one re-roll now.
- **Duck** (Perform page) — sidechains the wet level to the input envelope (`transient.h`) so the
  effect blooms in the gaps between notes.

---

## 5. Hold, looper, presets

- **Hold** (Perform page) — Latch or Gate; freezes the capture ring. Enabling the looper
  clears Hold (and Hold is unavailable while looping).
- **Looper** — 60 s, infinite overdub, Undo (overdub-only), Stop/Erase, Reverse, varispeed
  (0.25–4×), pre/post route, quantize, Burst (record-while-held), fade shape, Looper-Only,
  record order. Save/Load writes a WAV (overdubs mixed down) under the module dir.
- **Character presets** — `Init / Erase / Edit / Chor / Shimr / Birds / Glass / Pad` set the
  effect + macros to a starting point.
- **User-preset bank** — 16 slots; Save/Load/Del store every parameter **and** the recorded
  loop per slot under the module dir.

---

## 6. MIDI

Channel-wide CCs (selected): `5` Subdiv · `6` Activity · `7` Shape · `8` Filter · `9` Mix ·
`10` Tempo · `11` Repeats · `12` Space · `13` Loop Level · `14` Mod Rate · `15` Resonance ·
`16` FX Vol · `17` Loop Speed · `19` Mod Depth · `20` Reverb · `21` Loop Fade · `22` Looper ·
`23` Loop Reverse · `24` Route · `25` Looper-Only · `26` Burst · `27` Quantize · `28–31` Rec/
Play/Dub/Stop · `34/35` Erase/Undo · `46` Loop Save · `47` Reverse · `48` Hold · `102` Bypass.

**Program Change** selects effect + variation: effect = PC ÷ 4, variation = PC mod 4. MIDI
note-ons drive the onset-triggered effects (Arp, Chain).

---

## 7. Move / Schwung integration

War Bells is a Schwung `audio_fx` (entry `move_audio_fx_init_v2`, ABI v2). `module.json` is a
minimal manifest; the plugin serves its edit metadata at runtime via `get_param("chain_params")`
(typed, units, per-effect variation options) and a duplicate-free `get_param("ui_hierarchy")`,
so all 39 params are editable on the device with short, glanceable labels.

Pages: **Root · Tone · Time · Space FX · Motion · Generate · Perform · Looper · Looper 2 ·
User Slots · Config** (≤ 8 knobs each). A compact `ui_chain.js` exposes the macros in the Signal-Chain quick editor,
and `web_ui.html` provides a browser controller + manual. The looper transport is menu-driven
(the Transport enum acts as the footswitch). Onset effects fire from audio transients + MIDI
note-ons.

## 8. DSP primitives (`src/dsp/core/`)

Realtime-safe, header-only: `grain.h` (windowed granular voices, 4 envelope shapes),
`multitap.h` + `pitchshift.h` (4-tap delay + pitch shifter), `chorus.h`, `reverb.h`
(Schroeder–Moorer, public-domain Freeverb topology), `svf.h` (TPT resonant low-pass),
`looper.h` (two-buffer 60 s looper, WAV save/load), `ringbuf.h`, `transient.h` (onset
detector), `wav.h`, `util.h`. No allocation / logging / file I/O on the audio thread; file I/O
(loop + user-slot save/load) happens only on `set_param`. The per-variation "brain" is in
`effects.c`.
