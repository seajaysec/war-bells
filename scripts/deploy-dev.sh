#!/usr/bin/env bash
# Convenience: scp a freshly-built War Bells module straight to a Move device.
# Requires SSH access; intended for module dev iteration only — production
# installs should go through the Module Store / a published release.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MOD_ID="war_bells"
HOST="${MOVE_HOST:-ableton@move.local}"
DEST="${MOVE_DEST:-/data/UserData/schwung/modules/audio_fx/${MOD_ID}}"

"$ROOT/scripts/build.sh"

ssh "$HOST" "mkdir -p '$DEST'"
scp "$ROOT/dist/$MOD_ID/${MOD_ID}.so" "$ROOT/dist/$MOD_ID/module.json" \
    "$ROOT/dist/$MOD_ID/ui_chain.js" "$HOST:$DEST/"
[ -f "$ROOT/dist/$MOD_ID/help.json" ] && scp "$ROOT/dist/$MOD_ID/help.json" "$HOST:$DEST/"
[ -f "$ROOT/dist/$MOD_ID/web_ui.html" ] && scp "$ROOT/dist/$MOD_ID/web_ui.html" "$HOST:$DEST/"
echo "Deployed to $HOST:$DEST"

RESTART_HOOK="/data/UserData/schwung/restart-move.sh"
if [ "${MOVE_NO_RESTART:-0}" = "1" ]; then
  echo "Skipped restart (MOVE_NO_RESTART=1). To load it on-device, run:"
  echo "  ssh $HOST '$RESTART_HOOK'"
else
  echo "Restarting Move to load the new module ($RESTART_HOOK)…"
  if ssh "$HOST" "$RESTART_HOOK"; then
    echo "Restart triggered — Move relaunches with the new build in a few seconds."
  else
    echo "Restart hook failed; load it on-device manually:"
    echo "  ssh $HOST '$RESTART_HOOK'"
  fi
fi
