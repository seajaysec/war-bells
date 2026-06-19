# The 7 resilience principles

Grounded in FDN stability theory (Stautner & Puckette; Jot; Smith's *Physical Audio Signal Processing*),
the ValhallaDSP reverb-design series, and hard practice. The rule of thumb: **bound it by design; the limiter
is insurance, not the plan.**

## 1. Feedback gain < 1 by design
Every recirculating path (reverb combs, delay/sustain loops, shimmer, multitap) must *lose energy each pass*.
- Account for gain that **filters/resonance add at peaks** — a damping filter or resonant SVF can add local
  gain above unity even if the raw coefficient is < 1.
- Put a **soft saturator inside the loop** (tanh / cubic softclip) so any excursion is bounded *musically*,
  not by a click. This is the insurance that makes "store-up / sound-on-sound" safe.
- For reverb decay, set per-line feedback from an **RT60 target** instead of by ear:
  `g_i = 10^(−3 · D_i · T / RT60)` where `D_i` = delay length (samples), `T` = 1/fs. Mathematically bounded.
- FDN view: stable when the loop is *norm-decreasing* (feedback matrix unitary × a sub-unity gain).

## 2. Gain-stage every stage
Manage levels so each stage sits in its useful range and nothing compounds past unity into the next.
- When you SUM two wet sources (e.g. delay + reverb), scale the pair so the sum stays ≤ ~unity — and scale
  them **together** (equal weights) so their balance/texture is preserved while the level drops.
- Headroom is cheap; reclaim loudness with a controlled makeup *after* the bound, not by removing the bound.

## 3. DC-block the INPUT of any high-feedback stage
A comb at feedback `f` has DC gain ≈ `1/(1−f)`. At `f=0.94` that's **~16×** — a tiny input offset rides up
into a large, headroom-eating, asymmetric-clipping offset over seconds.
- One-pole HPF on the input: `y = x − x1 + R·y1` (R≈0.9995 → ~3.5 Hz at 44.1k). See techniques for R table.
- Block at the INPUT (stops the amplification at the source) — output blocking only mops up.

## 4. Smooth params; soft-saturate, never hard-clamp
- **Zipper/clicks/pitch-glitch** come from per-block parameter jumps. Slew every audible param per sample with
  a one-pole (τ 5–50 ms typical); smooth gains/frequencies in **dB/log space**, convert to linear after.
- **Hard clamp (`if x>1: x=1`) folds the waveform** → broadband harmonics → aliasing/crackle. Use a smooth
  saturator (cubic softclip or tanh). If a nonlinearity drives hard, anti-alias it (ADAA or oversample).

## 5. Flush denormals
Subnormal floats (< ~1.18e−38) in reverb/IIR/delay tails trigger microcode → **10–100× CPU stalls** on
decay-to-silence. Set FTZ/DAZ (x86 MXCSR) / FZ (ARM FPCR) on the audio thread every block. Optionally inject
~1e−18 anti-denormal noise / DC into feedback paths as belt-and-suspenders.

## 6. Limiter = last-5% safety
A warm program-dependent ceiling (gain-ride + gentle HF rolloff + soft asymptote) is great as the final catch
— but if principles 1–5 hold, it should rarely engage. If your limiter is doing heavy lifting, the engine is
under-designed. Never use it to *create* loudness or to hide runaway.

## 7. Single source of truth
Define presets/state ONCE (e.g. in the engine's `apply_preset`); never duplicate the values somewhere else
(a JS table, a doc) that can silently drift. Re-derive the duplicate at runtime, and add a parity test.
