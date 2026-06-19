#!/usr/bin/env bash
# ratchet.sh — lock in a VERIFIED-GREEN improvement: copy the fresh metrics report to the committed baseline,
# so the gate will fail any future regression. Only run this when the suite is green and the change is a real
# improvement (never to hide a regression). Adapt paths to your repo.
set -e
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"   # adapt: repo root from the skill dir
REPORT="${1:-$ROOT/tests/metrics_report.json}"
BASELINE="${2:-$ROOT/tests/metrics_baseline.json}"
[ -f "$REPORT" ] || { echo "no report at $REPORT — run the test suite first"; exit 1; }
echo "current baseline:"; [ -f "$BASELINE" ] && cat "$BASELINE" || echo "(none)"
echo "new report:"; cat "$REPORT"
printf "ratchet baseline to this report? [y/N] "; read -r ans
[ "$ans" = "y" ] || { echo "aborted"; exit 0; }
cp "$REPORT" "$BASELINE"
echo "baseline updated → commit $BASELINE"
