---
name: audio-dsp-resilience
description: >
  Build or fix real-time audio DSP that CANNOT clip, crash, run away, or artifact — without changing the
  intended sound. Use whenever working on feedback effects (reverb, delay, looper, comb, shimmer, sustain),
  saturation/waveshaping, filters, or pitch/time modulation; whenever audio "clips / crackles / clicks /
  builds up / gets harsh / spikes the CPU / sounds metallic"; or whenever tempted to fix audio by adding a
  master limiter. Stable-by-design first, prove fidelity second.
---

# Audio DSP resilience

The core lesson: **a master limiter masks LEVEL, not QUALITY.** Patching symptoms (limiter tweaks, gain
scalars, taming presets) fails repeatedly. Instead make the engine bounded + artifact-free *by design*, then
prove you didn't change the sound. (Worked example throughout: a granular/reverb/delay multi-effect that kept
crashing until rebuilt this way.)

## Protocol

1. **Reach for the test harness first.** Pair this with the `audio-dsp-testing` skill — it tells you *which*
   artifact is happening (clicks vs DC vs aliasing vs runaway vs denormal) instead of guessing. Fix the metric
   the harness flags, not the symptom you imagine.
2. **Apply the principle for that failure** → `references/principles.md`.
3. **Check the gotchas** — the expensive, non-obvious traps → `references/gotchas.md`.
4. **Reach for a known technique + constant** (denormal flush, DC blocker R, soft-sat, ADAA, RBJ biquad,
   one-pole smoothing, RT60 feedback) → `references/techniques.md`, copy-paste from `snippets/dsp_safety.h`.
5. **Prove fidelity**: clean/unaffected configs stay bit-similar; only the intended paths change → 
   `references/fidelity-guard.md`. Verify on the surface the user actually hears (e.g. the web demo, the device).

## The 7 principles (full text in references/principles.md)

1. **Feedback gain < 1 by design** (account for gain filters/resonance add at peaks); put a **soft saturator
   inside every feedback loop** as the musical safety.
2. **Gain-stage every stage** — normalize so nothing compounds past unity into the next stage.
3. **DC-block the INPUT of any high-feedback stage** — combs at ~0.94 fb amplify DC ~16×.
4. **Smooth every parameter** per sample (one-pole, perceptual/dB-log domain); **soft-saturate, never
   hard-clamp** (hard clamp folds → aliasing/crackle).
5. **Flush denormals** (FTZ/DAZ on the audio thread) or decay tails stall the CPU 10–100×.
6. **The limiter is the last-5% safety**, not the primary control.
7. **Single source of truth** for presets/state (don't duplicate values that can drift).

## Hardest-won gotchas (full text in references/gotchas.md)

- High-feedback combs amplify DC **~16×** → DC-block the reverb/loop *input*, not just the output.
- **DC inflates level meters but is inaudible** — a DC bug can read as "loud"; removing it drops the meter
  ~2× with no change in perceived loudness. **Don't chase makeup gain to compensate.**
- A **hard clamp** in a loop folds back → adds aliasing/crackle; replace with a smooth saturator.
- **Denormal tails = silent CPU killer** — the #1 mystery reverb/delay CPU spike on decay-to-silence.
- The **symptom-patching trap**: if "no-clip" tests pass but it still sounds bad, you're measuring level, not
  quality — add a quality probe (see `audio-dsp-testing`).
