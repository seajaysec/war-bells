#!/usr/bin/env bash
# skill-update.sh — self-update this skill from its canonical source (the war-bells repo).
# Generic: derives the skill name from its own path; syncs every file listed in the skill's files.txt
# (SHA-compare, back up changed files, throttle ~1h, fail-soft). Improve a skill/harness once in the repo →
# every machine's copy pulls it. Run manually, or wire an opt-in hook (see bottom).
#
# Usage:  bash scripts/skill-update.sh            # this skill
#         FORCE=1 bash scripts/skill-update.sh     # ignore the throttle
set -e
SKILL_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SKILL_NAME="$(basename "$SKILL_DIR")"
BASE="https://raw.githubusercontent.com/seajaysec/war-bells/main/.claude/skills/$SKILL_NAME"
CACHE="$HOME/.cache/wb-skill-update/$SKILL_NAME"; mkdir -p "$CACHE"
STAMP="$CACHE/last"; INTERVAL=3600

if [ -z "$FORCE" ] && [ -f "$STAMP" ]; then
  now=$(date +%s); last=$(cat "$STAMP" 2>/dev/null || echo 0)
  [ $((now - last)) -lt $INTERVAL ] && exit 0
fi
command -v curl >/dev/null || exit 0
date +%s > "$STAMP"

manifest="$(curl -fsS --max-time 8 "$BASE/files.txt" 2>/dev/null || true)"
[ -z "$manifest" ] && { echo "[skill-update] $SKILL_NAME: source unreachable, skipping"; exit 0; }

changed=0
while IFS= read -r rel; do
  [ -z "$rel" ] && continue; case "$rel" in \#*) continue;; esac
  remote="$(curl -fsS --max-time 8 "$BASE/$rel" 2>/dev/null || true)"; [ -z "$remote" ] && continue
  local="$SKILL_DIR/$rel"
  if [ -f "$local" ]; then
    rh=$(printf '%s' "$remote" | shasum -a 256 | cut -d' ' -f1)
    lh=$(shasum -a 256 < "$local" | cut -d' ' -f1)
    [ "$rh" = "$lh" ] && continue
    mkdir -p "$CACHE/backup/$(dirname "$rel")"; cp "$local" "$CACHE/backup/$rel" 2>/dev/null || true
  else mkdir -p "$(dirname "$local")"; fi
  printf '%s' "$remote" > "$local"; echo "[skill-update] $SKILL_NAME: updated $rel"; changed=1
done <<< "$manifest"

[ "$changed" = 0 ] && echo "[skill-update] $SKILL_NAME: up to date"
exit 0

# --- Optional auto-run (opt-in) ---
# Add a hook to ~/.claude/settings.json so it refreshes before use, e.g. a SessionStart hook:
#   { "hooks": { "SessionStart": [ { "hooks": [ { "type": "command",
#       "command": "bash ~/.claude/skills/audio-dsp-testing/scripts/skill-update.sh" } ] } ] } }
# Left manual by default — some prefer not to auto-run scripts.
