# Gotchas — the expensive, non-obvious traps

Each of these cost real debugging time. They recur across feedback-heavy DSP.

## DC explodes in high-feedback combs (~16×)
A Schroeder/Freeverb comb at feedback 0.94 has DC gain ≈ 1/(1−0.94) ≈ 16×. Any small DC at the reverb input
(from asymmetric grains, waveshaping, etc.) rides up over seconds into a huge offset. **Symptom:** the output
"gets loud / asymmetric / eats headroom" but doesn't sound louder. **Fix:** one-pole DC blocker on the reverb
*input*. This single fix can drop a stress metric from 0.155 → 0.0004.

## DC inflates meters but NOT loudness — don't chase it with makeup
DC is 0 Hz: inaudible, but it counts in peak and RMS. A DC bug makes the meters read ~2× hot. When you fix it,
the meter drops a lot while the *audible* level is unchanged. **Trap:** "it got quieter, add makeup gain." No —
you removed an inaudible artifact. Verify loudness by ear / by an A-weighted or DC-blocked measure, not raw RMS.

## Hard clamp in a loop = aliasing/crackle
`if (x > 1.5) x = 1.5;` inside a feedback path folds the waveform at the clamp → broadband harmonics → audible
crackle + aliasing, especially with a pitch-shifter in the loop. **Fix:** smooth saturator (cubic softclip /
tanh). The clamp barely changes RMS, so level tests won't catch it — only a discontinuity/aliasing probe will.

## Denormal tails = the #1 mystery CPU spike
Reverb/delay decaying into silence drifts into subnormal range → microcode stalls → dropouts only *after* the
signal stops. **Symptom:** CPU spikes on silence, not on loud passages. **Fix:** FTZ/DAZ every block. Note
Apple Silicon often flushes by default (so it won't reproduce on a Mac host) — but Linux/ARM targets (e.g. a
device) will stall. Test denormal behavior on the *target*, or trust the flush.

## Symptom-patching trap (level ≠ quality)
If your tests check "produces sound" + "doesn't hard-clip" and they PASS while it still sounds broken, you are
measuring level, not quality. A limiter caps level and hides runaway. The fix is a **quality** probe
(discontinuity, DC, aliasing, denormal) — see the `audio-dsp-testing` skill. This realization is the whole
reason these skills exist.

## Duplicated definitions drift (the web/engine preset bug)
Presets defined twice (engine `apply_preset` + a JS table in the web UI) silently diverged: the web played a
hotter preset that crashed while the device preset was fine, and host tests (using the engine values) passed.
**Fix:** single source of truth + a parity test. See `schwung-web-companion`.

## Test-source artifacts masquerade as engine bugs
A naive click detector (fixed 2nd-difference threshold) fires on broadband test NOISE (which has large
sample-to-sample jumps) — a false positive. Make discontinuity an *outlier* (2nd-diff ≫ local average). Same
for "aliasing": a tone through a big reverb is *legitimately* inharmonic (modal density) — measure inharmonic
energy in the **HF band only**, or you'll flag good reverb as aliasing. Calibrate metrics against a *clean*
reference before trusting them.

## Build portability: M_PI under -std=c11
`-std=c11` puts glibc in strict-ISO mode and hides `M_PI` (and other X/Open symbols). Add
`-D_XOPEN_SOURCE=600` (no-op on Apple libm). Otherwise host tests fail to compile only on Linux.
