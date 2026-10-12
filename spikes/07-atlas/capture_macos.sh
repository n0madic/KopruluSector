#!/usr/bin/env bash
# Builds a spike like tools/spike.sh, runs the windowed macOS engine with optional extra
# engine arguments (e.g. --graphics-adapter=opengl) and captures the window once the spike
# prints "SPIKE capture". Usage: capture_macos.sh spikes/<name> <tag> [engine args...]
# Writes build/spikes/<name>/macos-<tag>.log and macos-<tag>.png.
set -euo pipefail
cd "$(dirname "$0")/../.."
SPIKE="${1:?spike directory}"
TAG="${2:?tag}"
shift 2
NAME="$(basename "$SPIKE")"
OUT="build/spikes/$NAME"
TOOLCHAIN=tools/toolchain
mkdir -p "$OUT"
WINID="build/tools/winid"
if [ ! -x "$WINID" ]; then
  mkdir -p build/tools
  swiftc -O spikes/07-atlas/winid.swift -o "$WINID"
fi
"${JAVA:-java}" -jar "$TOOLCHAIN/bob.jar" --root . --settings "$SPIKE/spike.settings" \
  --output "$OUT/macos-build" --variant debug build >/dev/null
RAW="$OUT/macos-$TAG.raw.log"
"$TOOLCHAIN/dmengine" "$@" "$OUT/macos-build/game.projectc" >"$RAW" 2>&1 &
pid=$!
for _ in $(seq 240); do
  grep -q "SPIKE capture" "$RAW" 2>/dev/null && break
  kill -0 "$pid" 2>/dev/null || break
  sleep 0.5
done
if grep -q "SPIKE capture" "$RAW"; then
  # Other agents may run dmengine windows too: pick ours by its title.
  title="$(sed -n 's/^title = //p' "$SPIKE/spike.settings" | head -1)"
  id="$("$WINID" dmengine | grep -F "$title" | head -1 | cut -d' ' -f1)"
  for attempt in 1 2 3 4 5 6 7 8; do
    screencapture -l "$id" -o -x "$OUT/macos-$TAG.png" 2>/dev/null && break
    echo "capture attempt $attempt failed (window $id)"
    sleep 0.25
  done
fi
for _ in $(seq "${SPIKE_TIMEOUT:-120}"); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
kill "$pid" 2>/dev/null || true
wait "$pid" || true
grep -o "SPIKE .*" "$RAW" | tee "$OUT/macos-$TAG.log"
grep -E "Installed graphics device|ERROR" "$RAW" || true
