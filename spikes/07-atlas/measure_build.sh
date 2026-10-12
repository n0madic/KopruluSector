#!/usr/bin/env bash
# Bob build cost of the spike-07 atlases: clean build, no-op rebuild, archive size and
# per-texture sizes. Output: build/spikes/07-atlas/build-measure.log
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=build/spikes/07-atlas
BOB=("${JAVA:-java}" -jar tools/toolchain/bob.jar --root . --settings spikes/07-atlas/spike.settings)
LOG="$OUT/build-measure.log"
mkdir -p "$OUT"
seconds() { local t0 t1; t0=$(python3 -c 'import time;print(time.time())'); "$@" >/dev/null 2>&1; t1=$(python3 -c 'import time;print(time.time())'); python3 -c "print(round($t1-$t0,2))"; }
{
  rm -rf "$OUT/measure-build"
  echo "MEASURE clean_build_s=$(seconds "${BOB[@]}" --output "$OUT/measure-build" --variant release --archive build)"
  echo "MEASURE noop_rebuild_s=$(seconds "${BOB[@]}" --output "$OUT/measure-build" --variant release --archive build)"
  touch generated/spikes/07-atlas/terran_index_crop.atlas
  echo "MEASURE one_atlas_touched_rebuild_s=$(seconds "${BOB[@]}" --output "$OUT/measure-build" --variant release --archive build)"
  for f in "$OUT"/measure-build/game.arc*; do echo "MEASURE archive $(basename "$f") bytes=$(stat -f %z "$f")"; done
  find "$OUT/measure-build/generated/spikes/07-atlas" -name '*.texturec' -o -name '*.texturesetc' | sort | while read -r f; do
    echo "MEASURE file $(basename "$f") bytes=$(stat -f %z "$f") gzip_bytes=$(gzip -c "$f" | wc -c | tr -d ' ')"
  done
} | tee "$LOG"
