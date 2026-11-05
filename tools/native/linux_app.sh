#!/usr/bin/env bash
# The product's Linux build (x86_64 or the host's arm64, in a container of tools/native/linux.Dockerfile) and its
# launch check under Xvfb with Mesa's software GL: tools/product.sh linux calls it.
#   tools/native/linux_app.sh <release|debug> <staged data dir> <output dir> [run ARGS...]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VARIANT="$1"
STAGE="$(cd "$2" && pwd)"
DEST="$3"
shift 3
case "$VARIANT" in release | debug) ;; *) echo "${0##*/}: the variant is release or debug" >&2; exit 2 ;; esac
BUILD_TYPE=Release
[ "$VARIANT" = debug ] && BUILD_TYPE=RelWithDebInfo
IMAGE=koprulu-linux-build
docker build -q -t "$IMAGE" -f "$ROOT/tools/native/linux.Dockerfile" "$ROOT/tools/native" >/dev/null
docker run --rm -v "$ROOT:/src" -w /src "$IMAGE" bash -c "
  cmake -S . -B build/app-linux -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DKP_APP=ON -DKP_TOOLS=OFF ${KP_CINE_CMAKE:--DKP_CINEMATICS=OFF} >/dev/null &&
  cmake --build build/app-linux --target koprulu"
mkdir -p "$DEST"
cp "$ROOT/build/app-linux/koprulu" "$DEST/koprulu"
rm -rf "$DEST/data"
cp -R "$STAGE" "$DEST/data"
# the icon and the desktop entry of a desktop that installs applications (the file names are the application id,
# which SDL gives the window: SDL_SetAppMetadata); the entry is the installed one's: koprulu on the PATH, the icon in the
# icon theme (hicolor/256x256/apps), not a launcher of this directory
if [ -f "$STAGE/generated/icon/icon-256.png" ]; then
  cp "$STAGE/generated/icon/icon-256.png" "$DEST/com.koprulusector.game.png"
  cat >"$DEST/com.koprulusector.game.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Koprulu Sector
Exec=koprulu
Icon=com.koprulusector.game
Categories=Game;StrategyGame;
Terminal=false
DESKTOP
fi
echo "product: $DEST/koprulu"
if [ "${1:-}" = run ]; then
  shift
  REL="${DEST#"$ROOT"/}"
  # Xvfb started by hand: xvfb-run waits for Xvfb's ready signal, which never comes in a container
  docker run --rm -v "$ROOT:/src" -w "/src/$REL" -e LIBGL_ALWAYS_SOFTWARE=1 -e SDL_AUDIODRIVER=dummy "$IMAGE" \
    bash -c 'Xvfb :99 -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 & sleep 2; DISPLAY=:99 ./koprulu "$@"' koprulu "$@"
fi
