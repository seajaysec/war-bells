---
name: audio-dsp-testing
description: >
  Reusable, self-improving per-metric test harness for audio/DSP. Use when an audio engine "kills the sound /
  clips / crackles / clicks / builds up / runs away", when "the tests aren't robust enough" or pass while it
  still sounds bad, when you need STRESS testing (parameter sweeps, worst-case combos, random combinations),
  or when you want artifact/quality regression tracking over time. Measures QUALITY (clicks, DC, aliasing,
  harshness, denormal, runaway, true-peak), not just level — and tracks each metric against a committed baseline.
---

# Audio DSP testing (per-metric, stress, self-improving)

The lesson that birthed this: **"produces sound" + "doesn't hard-clip" tests pass while the engine audibly
breaks**, because a limiter caps LEVEL not QUALITY. So: make every artifact KIND a *scored probe*, run a big
matrix of cases through it, and track each metric against a committed baseline that ratchets.

## The model

```
cases  =  presets ∪ param-sweeps ∪ worst-case-combo ∪ seeded Monte-Carlo ∪ spectral-tone ∪ transitions
metrics =  silence · runaway · true_peak · discontinuity · dc_offset · non_finite · denormal · harshness · aliasing
run     =  for each case: feed representative input → score every metric → scorecard
gate    =  fail on absolute breach OR regression vs tests/metrics_baseline.json   (each metric tracked over time)
```

Add a case → it's scored on every metric automatically. Add a metric → every case gets it. One probe = one
function. That's the extensibility that makes it self-improving.

## Use it (drop-in)

1. Copy `templates/wb_metrics.h` (the scored probes — already module-agnostic) into your `scripts/`.
2. Copy `templates/checkstress.c`, adapt the marked spots: the case matrix (your preset names / param keys) and
   the engine API calls (`create_instance` / `set_param` / `process_block`). Everything else is generic.
3. Wire it into your test runner with `templates/run_tests.snippet.sh` (audit stage that compiles + runs every
   `scripts/check*.c`, gates on exit code; plus a single-source parity guard). Note the `-D_XOPEN_SOURCE=600`
   build flag (see snippet) — `-std=c11` hides `M_PI` on glibc.
4. First run **should FAIL** on a broken engine — that proves the probes catch what shipped. Fix the DSP
   (pair with `audio-dsp-resilience`) until green, then commit the baseline.

## Calibrate the probes (critical — see references/metrics.md)

- **discontinuity** = OUTLIER 2nd-difference (≫ local average), not a fixed threshold — else broadband test
  noise trips it (false positive).
- **aliasing** = inharmonic energy in the **HF band only** — else a tone through a big reverb reads as
  "aliasing" (legit modal density, false positive). Calibrate against a *clean* reference first.
- **dc_offset / true_peak** are cheap and catch the sneaky stuff (DC ×16 in combs; inter-sample overs).
- **Wall-clock metrics (e.g. `denormal` = CPU/block on decay) must aggregate with MEDIAN, not MAX.** A real
  denormal stall slows *every* decay block (median rises); a single slow chunk is just OS scheduler jitter
  (median rejects it). Taking the max turns the probe into a machine-load gauge that throws false `REGRESSION`
  failures under CI load — it'll fail ~50% of runs on unchanged code, worst *under* load. Median-of-6 cut the
  idle variance ~20× here and held flat under 4 busy cores. If only a timing metric regresses, re-run 3–5× and
  `git stash`-test pristine before believing it; deterministic metrics (silence/dc/aliasing/…) don't do this.

## Self-improve (references/self-improvement.md + scripts/)

- **Ratchet**: `scripts/ratchet.sh` copies a green `metrics_report.json` → `metrics_baseline.json` to lock a
  verified improvement. The gate then fails any future regression.
- **Triage a "regression" before believing it.** A flagged metric is often a STALE baseline, not your change.
  Two-step check: (1) is the metric's `worst_case` a NEUTRAL/off setting (the param you added at 0, a non-feature
  case)? (2) does a `git stash` pristine build reproduce the same worst value? If both yes, your change is
  innocent and the baseline drifted — re-capture **only that metric** (edit its `worst` in the baseline), don't
  blanket-`ratchet` (that also overwrites noisy timing metrics like `denormal` with a tight one-off value and
  re-introduces flakiness). Confirm the value is deterministic (same across 2 runs) before committing it.
  (Both bit us adding Drift: `silence`/`dc_offset` were stale on main, worst-cases were depth=0.)
- **Loop**: every escaped bug → add the probe that would've caught it → re-baseline. Every speedup → fold it
  into the template + bump version (and it propagates via `scripts/skill-update.sh`).
- **Deep offline analysis**: `scripts/analyze.py` (THD/THD+N, true-peak @4×, spectral, null-test) for when the
  C probes flag something and you want the spectrum. Self-contained (numpy/scipy/soundfile).
