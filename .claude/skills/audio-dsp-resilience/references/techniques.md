# Techniques & constants (self-contained cookbook)

Copy-paste building blocks with the numbers that matter. Ready-to-use C is in `../snippets/dsp_safety.h`.

## Denormal flush (do this every block, audio thread)
- x86: set MXCSR FTZ (bit 15) + DAZ (bit 6) → `mxcsr |= 0x8040`.
- ARM/Apple: set FPCR FZ (bit 24).
- Software belt-and-suspenders: inject ~1e-18 noise or a tiny DC into feedback state; or zero state below ~1e-30.
- Float32 denormal floor ≈ 1.18e-38.

## DC blocker (one-pole HPF) — `y = x − x1 + R·y1`
Corner `f_c ≈ (1−R)·fs/(2π)`.

| R | corner @44.1k | use |
|---|---|---|
| 0.9995 | ~3.5 Hz | default: kill DC/subsonic, keep bass |
| 0.999 | ~7 Hz | a touch firmer |
| 0.995 | ~35 Hz | only if you need subsonic control (thins bass) |

Apply after asymmetric nonlinearities and on **high-feedback stage inputs**.

## Soft saturation (bounded, click-free) — replace hard clamps
- Cubic: `f(x) = x − x³/6.75` for |x|<1.2, clamp beyond → smooth, cheap, ~±0.94 ceiling.
- `tanh(x)` — smoother, slightly more CPU; great in feedback loops.
- A program-dependent "tape ceiling" (master safety): peak envelope (fast attack ~0.5 ms / slow release),
  gain-ride toward a threshold, gentle HF rolloff that increases with how hard it's pushed (warmer, never
  brighter), then a soft asymptote so it can never hard-clip. Keep it as the *last* stage only.

## Anti-aliasing a nonlinearity
- **Oversample** the nonlinear block only: upsample → interpolate-LPF → nonlinearity → decimate-LPF → downsample.
  Factors: soft-clip/tanh 2×, hard clip 4×, waveshaper 2–4×, FM 4–8×, S&H/bitcrush 8–16×.
- **ADAA (antiderivative antialiasing)** — cheap alternative for memoryless `f`, no oversampling:
  `y[n] = (F(x[n]) − F(x[n−1])) / (x[n] − x[n−1])`, F = antiderivative of f (guard the small-denominator case).
- Verify: residual aliasing should sit below the noise floor (~−100 dBFS). Linear stages (EQ, gain) never alias.

## Parameter smoothing (no zipper) — `y = y + α·(target − y)`
`α = 1 − exp(−2π·f_c/fs)`, `f_c = 1/(2π·τ)`.

| τ | use |
|---|---|
| 1–5 ms | fast modulation (LFO targets, fast sweeps) |
| 5–20 ms | typical knob sweeps (kills zipper) |
| 50–200 ms | smooth automation |

Smooth **gains/frequencies in dB/log space**, convert to linear after. (The codebase pattern: a per-sample
one-pole `cur += (target − cur)*k` on grain gain, looper level, filter cutoff.)

## RBJ biquad (cookbook) / SVF
`ω₀ = 2π f₀/fs; α = sinω₀/(2Q)`. **LPF:** b0=(1−cosω)/2, b1=1−cosω, b2=b0; a0=1+α, a1=−2cosω, a2=1−α.
**HPF:** b0=(1+cosω)/2, b1=−(1+cosω), b2=b0; same a's. **Notch:** b0=1,b1=−2cosω,b2=1; same a's.
**Peaking:** A=10^(dB/40); b0=1+αA, b1=−2cosω, b2=1−αA; a0=1+α/A, a1=−2cosω, a2=1−α/A. Normalize by a0.
Use **TDF-II** in float (best stability); DF-I or cascaded biquads in fixed-point. For modulated cutoff prefer
a **zero-delay-feedback (ZDF/TPT) SVF** — stable under fast modulation; precompute `g=tan(π fc/fs)` (LUT it if
you recompute per sample).

## Crest factor (peak/RMS), dB, true-peak
- Crest = peak − RMS (dB). Sine 3 dB, square 0 dB, drums 15–20+ dB, heavily-limited 6–9 dB. **Preserve crest
  factor to preserve transients** — a limiter that crushes it sounds flat/fatiguing.
- dB: amplitude `20·log10(A)`, power `10·log10(P)`. +6 dB ≈ 2×. 
- **True/intersample peak (dBTP):** oversample ≥4× then read peak (ITU-R BS.1770). Target −1 dBTP for streaming.

## Reverb feedback from RT60
`g = 10^(−3·D·T/RT60)` per delay line (D samples, T=1/fs). Bound by design; longer RT60 ≠ unbounded.
