#!/usr/bin/env bash
# The product's bundles (plan phase 9F): the game.project's bootstrap (main/bootstrap.collection) with the converted
# data (main/product.settings), the native extensions built by the build server.
#   tools/product.sh [web|macos|android] [release|debug]
# Bundles go to dist/product/<platform>. The converter's output (generated/) must be current.
set -euo pipefail
cd "$(dirname "$0")/.."

PLATFORM="${1:-web}"
VARIANT="${2:-release}"
TOOLCHAIN=tools/toolchain
JAVA="${JAVA:-java}"
OUT=build/product
BUNDLE=dist/product # bob refuses bundle output inside build/
case "$PLATFORM" in
  web) TARGET=wasm-web ;;
  macos) TARGET=arm64-macos ;;
  android) TARGET=arm64-android ;;
  *) echo "product: unknown platform $PLATFORM" >&2; exit 2 ;;
esac
mkdir -p "$OUT"
"$JAVA" -jar "$TOOLCHAIN/bob.jar" --root . --settings main/product.settings --output "$OUT/$PLATFORM" --archive \
  --platform "$TARGET" --architectures "$TARGET" --variant "$VARIANT" \
  --bundle-output "$BUNDLE/$PLATFORM" resolve build bundle
echo "product: $(find "$BUNDLE/$PLATFORM" -maxdepth 2 -mindepth 1 | head -1)"
