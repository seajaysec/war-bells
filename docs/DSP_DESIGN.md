# War Bells — DSP design principles

These are the rules the engine is built to. They exist because earlier work fixed audio breakage by
patching a master limiter to *mask* symptoms; that kept failing. The fix is to make the engine stable and
artifact-free **by design**, and to **measure** it. When changing the DSP, follow these and keep the
stress/artifact suite green.

## Principles

1. **Feedback gain < 1 by design.** Every recirculating path (reverb combs, space-delay/sustain, multitap,
   shimmer) must lose energy each pass — including the gain that damping filters/resonance add at peaks. A
   **soft saturator inside the loop** is the musical safety, not a downstream limiter. Stable-by-design,
   not caught-after-the-fact. (FDN theory: stable when the loop is norm-decreasing.)
2. **Gain-stage every stage.** Keep each stage in its useful range; normalize sums so nothing compounds
   past unity into the next stage (e.g. the space bus scales delay+reverb together).
3. **Block DC in feedback loops.** High-feedback combs amplify DC enormously — a Hall comb at ~0.94
   feedback has DC gain ~1/(1−0.94) ≈ **16×**, so a tiny offset rides up into a large inaudible-but-
   headroom-eating DC. DC-block the **input** of any high-feedback stage (reverb input, space-delay loop).
4. **Smooth parameters; soft-saturate, don't hard-clamp.** Hard clamps fold back → aliasing/crackle; use a
   smooth saturator. Slew jumpy params per sample so there's no zipper/click.
5. **Flush denormals** (FTZ/DAZ on the audio thread) + keep loops from trapping subnormal values, or decay
   tails cause 10–100× CPU stalls.
6. **Single source of truth for presets.** Presets live once, in `apply_preset` (params.c). The web demo
   runs that code via the WASM and reads values back — it cannot drift from the device.
7. **The limiter is the last-5% safety**, not the primary control. If the engine is bounded by 1–3, the
   tape ceiling (`wb_tape_limit`) should rarely engage.

## How it's measured

`scripts/wb_metrics.h` defines each artifact KIND as a reusable scored probe; `scripts/checkstress.c`
runs a matrix (presets ∪ param sweeps ∪ worst-case ∪ seeded Monte-Carlo ∪ spectral tone probes) and
scores every metric per case:

| metric | what it catches |
|---|---|
| silence | engine died / produced nothing |
| runaway | sustained level climbing to the rails |
| true_peak | inter-sample overs |
| discontinuity | clicks/zipper (outlier 2nd-difference, immune to broadband noise) |
| dc_offset | DC accumulation in feedback |
| non_finite | NaN/Inf |
| denormal | CPU stall on decay-to-silence |
| harshness | excess HF energy (harsh distortion) |
| aliasing | inharmonic energy in the HF band (imaging) |

Results go to `tests/metrics_report.json`; `tests/metrics_baseline.json` is the committed contract —
`run_tests.sh` fails on any metric **regression** or absolute breach, so each metric's health is tracked
over time. Add a case → it's scored on every metric automatically.

## References
- ValhallaDSP, *Getting Started With Reverb Design* (parts 1–4): Schroeder/Moorer/Gardner/Dattorro/Jot, FDN.
- Julius O. Smith, *Physical Audio Signal Processing* (CCRMA) — FDN stability, comb/allpass.
- `github.com/kunitoki/sonic-skills` (audio-numerics-review, audio-artifacts-debug).
- `github.com/codephunk/audio-dsp-expert` (THD/null/true-peak/spectrogram measurement approach).
