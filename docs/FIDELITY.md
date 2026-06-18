# War Bells — feature reference

Everything War Bells does, and where it lives in the C module. The DSP is a port of a sibling
monome norns build.

Legend: **✅ implemented** · **≈ modeled** (faithful in behavior; exact analog voicing tuned on
device) · **N/A** (not meaningful on the Move surface).

## Param surface
39 editable params, all served from the DSP (`get_param("chain_params")` + a duplicate-free
`get_param("ui_hierarchy")`) — `module.json` is a minimal manifest. Function-first, glanceable
names. Pages: **Root** (Preset · Effect · Variation · Activity · Repeats · Shape · Mix · Space ·
Filter) · **Tone** · **Time** · **Space FX** · **Perform** · **Looper** · **Looper 2** ·
**User Slots** · **Config**.

## Controls
| Feature | Status | Notes |
|---|---|---|
| Effect / Variation browse | ✅ | Root enums; per-effect variation labels |
| Activity / Repeats macros | ✅ | per-effect mapping in `effects.c` |
| Shape, Mix, Space, Filter | ✅ | root knobs + Tone / Space FX pages |
| Resonance, FX Vol, Mod Depth/Rate, Loop Fade (secondaries) | ✅ | their own params on the relevant pages |
| Reverse | ✅ | grain voices play at negative rate (delays don't reverse) |
| Subdivision (1/4…8x) | ✅ | Time page |
| Clock source | ✅ | **Free** (default, free-running + gentle drift) **/ Sync** (host clock) **/ Man** (fixed BPM) |
| Bypass (Buffered / Trails / True) | ✅ | Config → Bypass + Byp Mode (crossfade lag per style) |

## Effects
| Feature | Status | Notes |
|---|---|---|
| 11 effects, 4 families, 44 variations | ✅ | `effects.c` `WB_EFFECTS[]` |
| Per-variation speeds (½/×1/×2/×4 octaves) | ✅ | `wb_var_t.speeds` |
| Pitch-glide incl. bidirectional (Glide) | ✅ | `grain.h` triangle glide |
| Filter variations (sweep / bandpass / random) | ✅ | per-voice `fmode` (`svf.h`) |
| Bit-crush variations | ✅ | `crush_bits` |
| Drone / cascade / phasing / sub-octave | ≈ | modeled; voicing tuned on device |
| Multidelay patterns + tap count (Taps: line/swng/trip/wide) | ✅ | `multitap.h` |
| Warp filter / pitch / grain taps | ✅ | `multitap.h` + `pitchshift.h` |
| **Grain envelope** (Soft / Pluck / Swell / Gate) | ✅ | `grain.h` 4-way window |

## Hold sampler
| Freeze recent audio, looped into effects | ✅ | `ringbuf.h` freeze |
| Latch / Gate | ✅ | `hold_style` |
| Activating looper clears Hold; Hold blocked while looping | ✅ | enforced in `params.c` |

## Pitch-mod / reverb / filter
| Pitch modulation (depth + rate) | ✅ | `chorus.h` |
| Stereo reverb, 4 modes (Room / Dark / Hall / Vast) | ≈ | `reverb.h` per-mode size/damp presets |
| Resonant low-pass, CW = open | ✅ | `svf.h` TPT low-pass |

## Phrase looper
| 60 s record · infinite overdub · Undo (keep base) · Stop / Erase | ✅ | `looper.h` |
| Reverse / varispeed (0.25–4×) | ✅ | `loop_reverse`, `loop_speed` |
| Pre-FX / Post-FX routing | ✅ | `loop_route` |
| Quantize · Looper-Only · Order · Fade-shape · Burst | ✅ | `loop_quantize`, `loop_only`, `loop_order`, `loop_fademode`, `loop_burst` |
| Save / recall loop audio | ✅ | Transport → Save/Load writes a WAV (overdubs mixed down) |

## Saving
| Character presets (8) | ✅ | `preset` enum sets effect + macros |
| User-preset bank (16 slots) | ✅ | User Slots page; Save/Load/Del store **params *and* the recorded loop** per slot |
| Whole-chain patch recall | ✅ | full `state` (de)serialization → captured by Signal-Chain patches (params only) |

## MIDI
| Note-ons drive onset effects | ✅ | `on_midi` + transient detector (`transient.h`) |
| Full CC map | ✅ | `war_bells.c` `midi_cc()` |
| Program Change (effect/variation) | ✅ | effect = PC ÷ 4, variation = PC mod 4 |
| Clock sync | ✅ | host `get_bpm` / `get_clock_status` (Clock = Sync) |

## Web UI
| Custom browser controller + manual | ✅ | `web_ui.html` — live over the Schwung Manager WebSocket when served from the device; interactive demo on GitHub Pages |

## Global config
| Input mono/stereo, input gain | ✅ | Config page |
| Reverb / filter persistence | ✅ | via Schwung patch state |

## Status
All listed features are implemented and host-tested (`tests/test_smoke.c`). The remaining work
is non-functional: exact analog **voicing** of the effects/reverbs, tuned on-device (constants
in `src/dsp/core/*.h` + `src/dsp/effects.c`).
