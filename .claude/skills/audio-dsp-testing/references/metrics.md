# Metric catalog — what each probe measures, thresholds, and calibration

All probes live in `templates/wb_metrics.h` (time-domain accumulator `wb_acc` + `wb_fft`/`wb_spectral`).
Scores are "higher = worse" unless noted. Thresholds are starting points — calibrate against a *clean* reference.

| metric | what it catches | how | start threshold |
|---|---|---|---|
| **silence** | engine died / no output | RMS over the run | rms ≥ 0.004 (higher=better) |
| **runaway** | sustained level climbing to the rails | peak of the LAST 10% of a long sustained run | ≤ 0.92 |
| **true_peak** | inter-sample overs | 2× (or 4×) oversampled peak | ≤ 0.999 (≈ −1 dBTP at 4×) |
| **discontinuity** | clicks / zipper / clamp-folds | count of \|2nd-difference\| OUTLIERS per sec | ≤ ~80/s |
| **dc_offset** | DC accumulation (combs ×16) | \|mean\| over the run | ≤ 0.02 |
| **non_finite** | NaN/Inf | count of out-of-range samples | 0 |
| **denormal** | CPU stall on decay-to-silence | **median** block time on silence ÷ mean active block time (median, not max — max amplifies OS jitter into false regressions) | ≤ ~6× |
| **harshness** | harsh distortion / broadband HF | FFT energy above 6 kHz ÷ total (tone in) | ≤ 0.25 |
| **aliasing** | imaging / inharmonic HF | FFT inharmonic energy **above 4 kHz** ÷ total (tone in) | ≤ 0.20 |

## Calibration gotchas (these WILL bite)
- **discontinuity must be an outlier detector.** Broadband test noise has large, *steady* sample-to-sample
  jumps → a fixed 2nd-difference threshold counts them all (4000+/s false positive). Count only `|d2| > 8×`
  the running-mean `|d2|`, with a floor (~0.15) to ignore near-silence. Then a clean engine reads ~15/s.
- **aliasing must be HF-band only.** A pure tone through a big reverb is *legitimately* inharmonic (comb modal
  density at low freqs). Measuring all-band inharmonic flags good reverb as aliasing (0.7+ false positive).
  Restrict to inharmonic energy above ~4 kHz ÷ total → clean reads ~0.001. Harshness (HF/total) is the
  independent confirm: if harshness is ~0, there's no real aliasing.
- **aliasing's harmonic comb must track the ACTUAL rendered pitch, not the requested note.** If the oscillator's
  true pitch differs from the nominal note (e.g. a naive triangle that quantizes its period and runs flat at high
  pitch — see `audio-dsp-resilience` gotchas), the real partials sit at multiples of the *actual* fundamental and
  miss a comb built from the nominal frequency → every harmonic reads as "aliasing" (0.3–0.7 false positive,
  worst at high pitch / wide stacks). Estimate the fundamental from the captured signal (strongest peak within
  ±1 semitone of nominal) and build the comb from that. **Smoking-gun symptom:** a pure SINE stack reads high
  "aliasing" — a sine can't foldback-alias, so it's the comb that's misaligned, not the engine that's aliasing.
- **separate real foldback from a metric artifact with the oversample-invariance test.** Render the same case at
  1× and at 4–8× internal oversampling. Genuine foldback collapses; a metric artifact (misaligned comb, intended
  wide/inharmonic stack) barely moves (saw 0.47 → 0.43 under 4× → it was NOT foldback). If oversampling doesn't
  help, don't bandlimit — fix the probe or the pitch. This one experiment can save a pointless oversampling pass.
- **level metrics pass while quality fails** — the whole point. Always include the quality probes.

## Efficacy — does each param actually DO something? (artifacts ≠ effect)
Artifact probes prove nothing BREAKS; the fidelity guard proves neutral is UNCHANGED. Neither proves a knob
has an audible EFFECT — so a param that is a **no-op in the state you're auditioning** passes every test while
doing nothing. (A real case: spectral params that are silent on a unison patch — the user "couldn't hear them"
and every test was green.) Add an **efficacy probe**:
- For each param, render two values in a **FAVORABLE config** (where it's meant to work) and measure the audible
  delta = **spectral-shape distance** (L2 of the L2-normalized magnitude spectra — catches timbre AND pitch),
  combined with level (|rmsΔ dB|/6) and, for movement params, envelope wobble.
- **GATE**: every param clears an audibility floor (no-ops read ~0.000; real params 0.3–1.2; floor ~0.04).
- **FLAG**: params that are a no-op in the **DEFAULT** state but audible in the favorable one — a *discoverability*
  hole the presets/docs must cover, not a bug.
- **Calibration (these bite):** pick **non-mirror endpoints** (a skew param reads 0 if you compare 0.1↔0.9 — its
  magnitude spectrum is mirror-symmetric; use 0.3↔0.95). A **static** spectrum can't see **movement** (slow
  chorus/drift below the window's resolution) — flag those for a long-window/by-ear check, don't gate them.
  **Re-trigger pluck modes** or you compare a decayed silence to a ring (a bogus huge delta).

## Input signals (drive the engine realistically)
- **Sustained mix** (tone + noise + periodic transient bursts) for ~4 s → drives feedback to steady state
  (catches runaway/DC/clicks), then ~1 s of **silence** → denormal-stall timing.
- **Pure tone** (e.g. 220 Hz) settle + capture for the **spectral** probes (harshness/aliasing).
- **Transitions**: switch presets/effects mid-stream (~every 0.5 s) — state must settle, not click/runaway.

## Deep spectral method (folded into scripts/analyze.py)
- **THD**: FFT (flat-top window for amplitude accuracy), find fundamental (strongest peak above a 20 Hz guard),
  integrate harmonic *clusters* (`±half-mainlobe` bins) for leakage robustness:
  `THD = sqrt(Σ P_harmonics) / sqrt(P_fundamental)`; `THD+N = sqrt(residual / total)`.
- **True peak**: oversample ≥4× then read peak (ITU-R BS.1770) → dBTP.
- **Window choice**: Hann (general), flat-top (accurate level/THD), Blackman-Harris (small tone near big tone).
- **Crest factor** = peak−RMS (dB): track it; a fix that crushes it killed the transients.
- FFT bins: `f(k)=k·fs/N`, resolution `Δf=fs/N` (set by window *duration*, not zero-padding).
