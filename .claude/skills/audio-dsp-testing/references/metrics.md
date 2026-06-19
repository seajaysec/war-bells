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
| **denormal** | CPU stall on decay-to-silence | max block time on silence ÷ mean active block time | ≤ ~6× |
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
- **level metrics pass while quality fails** — the whole point. Always include the quality probes.

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
