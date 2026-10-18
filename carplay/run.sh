#!/bin/sh
set -eu
umask 077
carplay_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$carplay_dir/receiver"
carplay_identity=${CARPLAY_IDENTITY_DIR:-$carplay_dir/runtime/identity}
carplay_state=${CARPLAY_STATE_DIR:-${XDG_STATE_HOME:-$HOME/.local/state}/opendash/carplay}
if [ ! -s "$carplay_identity/offline-mfi/identity.pk8" ] || [ ! -s "$carplay_identity/offline-mfi/certificate.p7b" ]; then
  echo "CarPlay identity missing. Set CARPLAY_IDENTITY_DIR to the directory containing offline-mfi/." >&2
  exit 1
fi
if [ ! -x server/build/install/server/bin/server ]; then
  echo "Receiver is not built. Run dash/carplay/build.sh first." >&2
  exit 1
fi
if [ -n "${CARPLAY_BIND_ADDRESS:-}" ]; then
  set -- --bind "$CARPLAY_BIND_ADDRESS" "$@"
fi
exec server/build/install/server/bin/server \
  --wireless --identity-dir "$carplay_identity" --state-dir "$carplay_state" \
  --device-name OpenDash --width 800 --height 480 --fps 30 --silent-microphone --http --web-bind 127.0.0.1 "$@"
