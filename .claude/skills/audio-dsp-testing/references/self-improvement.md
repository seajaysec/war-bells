# Self-improvement loop — how the harness gets better over time

The harness is "self-improving" not because it rewrites itself, but because it's structured so improvements are
one-line cheap, locked in by a baseline, and propagated across projects. Three mechanisms:

## 1. Extensible by construction (1 probe = 1 function)
- A new artifact KIND = one scoring function in `wb_metrics.h` + one row in the driver's metric table.
- A new CASE = one row in the matrix; it's automatically scored on every metric.
- Keep probes pure (feed samples → number) so they're trivially unit-testable and reusable in any project.

## 2. Ratcheting baseline (locks gains, catches regressions)
- `tests/metrics_baseline.json` is the committed contract: per-metric `worst` score + threshold.
- The driver fails on **absolute breach** OR **regression** (a metric worse than baseline by tolerance).
- When you legitimately improve a metric and the run is green, run `scripts/ratchet.sh` to copy the fresh
  `metrics_report.json` → baseline. Now that improvement can never silently regress.
- **Only ratchet on a verified-green improvement.** Never ratchet to hide a regression.

## 3. The discipline (write it down, do it every time)
- **Escaped bug → new probe.** Any audible bug that shipped past the suite means a missing/mis-calibrated
  metric. Add the probe that *would* have caught it, confirm it fails on the bad build, then fix + re-baseline.
  (This is exactly how DC, clicks, and aliasing got added here.)
- **Speedup/insight realized → fold it in.** Found a faster or more sensitive way to measure? Update the
  template, bump the version comment, and it propagates to other projects via `scripts/skill-update.sh`.
- **Record wished-for metrics.** Keep a short "metrics we wish we had" list at the top of the driver; promote
  them when you find a cheap implementation.
- **Completeness check.** Periodically ask: what failure mode is NOT covered by any probe? (e.g. stereo
  collapse, latency drift, loop-boundary clicks, modulation aliasing.) Add the gap.

## Propagation across projects (the "self-update")
- The CANONICAL copy of this skill + templates lives in the source repo (e.g. `seajaysec/war-bells`).
- Each machine's global skill carries `scripts/skill-update.sh`, which pulls the latest from that repo
  (SHA-compare, back up, throttle, fail-soft). Improve once → every future project benefits.
- So the workflow is: improve the harness in the repo → commit → other projects' skills self-update.
