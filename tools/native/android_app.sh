#!/usr/bin/env bash
# The product's Android APK (plan phase S, stage S6): SDL 3.4's Gradle template (android-project) with kp_app as
# libmain.so and the staged data as the APK's assets (generated/...), which SDL_LoadFile reads.
#   tools/native/android_app.sh <release|debug> <staged data dir> <output dir> [run ARGS...]
#   tools/native/android_app.sh launch ARGS...
# With "run" the APK is installed and started on the attached phone with ARGS as the program's arguments (the intent
# extra "args"), and its log is printed until the program's check line ("launch" starts the installed APK without
# building or installing it). tools/product.sh android calls this.
set -euo pipefail

APP_ID=com.koprulusector.game

# Starts the installed APK with the arguments and prints its log up to a development check's last line, or what came
# before ANDROID_TIMEOUT seconds passed or the connection dropped (perl's alarm: macOS has no timeout(1)). The log goes
# to a file, so the reading ends with that line (a pipe's logcat would wait for the next line, or the time limit).
launch() {
  local last='SPIKE (autotest|frame bench|play check: no game|fog check: readback|palette check|video check: [0-9]+ steps)' log pid
  log="$(mktemp)"
  adb wait-for-device
  adb logcat -c
  adb shell am start -S -n "$APP_ID/org.libsdl.app.SDLActivity" ${*:+--es args "'$*'"} >/dev/null
  perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "${ANDROID_TIMEOUT:-600}" adb logcat -s SDL:V SDL/APP:V \
    >"$log" 2>/dev/null &
  pid=$!
  while kill -0 "$pid" 2>/dev/null && ! grep -qE "$last" "$log"; do sleep 1; done
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  grep -E 'SPIKE|ERROR|Error' "$log" | sed -E "/$last/q" || true
  rm -f "$log"
}

if [ "${1:-}" = launch ]; then
  shift
  launch "$@"
  exit 0
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VARIANT="${1:?usage: android_app.sh <release|debug> <data> <out> [run ARGS...]}"
STAGE="$(cd "${2:?staged data}" && pwd)"
DEST="${3:?output dir}"
shift 3
OUT="$ROOT/build/app-android"
TEMPLATE="$ROOT/tools/toolchain/sdl/SDL3-3.4.18/android-project"
NDK_VERSION="${NDK_VERSION:-29.0.14206865}"
GRADLE_VERSION="${GRADLE_VERSION:-9.7.1}"
AGP_VERSION="${AGP_VERSION:-9.1.1}"
# The SDK's CMake 3.22 is older than the project needs: the host's CMake and Ninja.
CMAKE_BIN="$(dirname "$(command -v cmake)")"
CMAKE_VERSION="$(cmake --version | head -1 | awk '{print $3}')"
BUILD_TYPE=Release
[ "$VARIANT" = debug ] && BUILD_TYPE=RelWithDebInfo
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"

# The template's edition: a project of an earlier one is made again.
TEMPLATE_EDITION=3
if [ ! -d "$OUT/app/jni" ] || [ "$(cat "$OUT/.kp_template" 2>/dev/null)" != "$TEMPLATE_EDITION" ]; then
  rm -rf "$OUT"
  mkdir -p "$OUT"
  cp -R "$TEMPLATE/." "$OUT/"
  rm -rf "$OUT/app/jni"
  mkdir -p "$OUT/app/jni"
  sed -i '' "s/gradle-[0-9.]*-bin.zip/gradle-${GRADLE_VERSION}-bin.zip/" "$OUT/gradle/wrapper/gradle-wrapper.properties"
  sed -i '' "s/com.android.tools.build:gradle:[0-9.]*/com.android.tools.build:gradle:${AGP_VERSION}/" "$OUT/build.gradle"
  sed -i '' 's/^task clean.*/tasks.register("clean", Delete) {/; s/delete rootProject.buildDir/delete rootProject.layout.buildDirectory/' \
    "$OUT/build.gradle"
  # the template's GLES 2 feature line: the runtime needs GLES 3.0
  sed -i '' 's/android:glEsVersion="0x00020000"/android:glEsVersion="0x00030000"/' "$OUT/app/src/main/AndroidManifest.xml"
  # the program's arguments from the intent extra "args" (the development checks and benchmarks)
  python3 - "$OUT/app/src/main/java/org/libsdl/app/SDLActivity.java" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "    protected String[] getArguments() {\n        return new String[0];\n    }"
new = ("    protected String[] getArguments() {\n"
       "        String args = getIntent() != null ? getIntent().getStringExtra(\"args\") : null;\n"
       "        return args == null || args.trim().isEmpty() ? new String[0] : args.trim().split(\"\\\\s+\");\n"
       "    }")
assert old in s, "SDLActivity.getArguments changed"
open(p, "w").write(s.replace(old, new))
PY
  # the safe area (SDL_GetWindowSafeArea) is what the system bars, the display's cutout and the mandatory gesture zones
  # (the home bar's, which takes every touch) leave, where the game's panels and buttons lie (ADR 0014 point 6); the
  # back gesture's zones along the sides take swipes only, the picture and its taps go under them
  python3 - "$OUT/app/src/main/java/org/libsdl/app/SDLSurface.java" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = ("            Insets combined = insets.getInsets(WindowInsets.Type.systemBars() |\n"
       "                                               WindowInsets.Type.systemGestures() |\n"
       "                                               WindowInsets.Type.mandatorySystemGestures() |\n"
       "                                               WindowInsets.Type.tappableElement() |\n"
       "                                               WindowInsets.Type.displayCutout());")
new = ("            Insets combined = insets.getInsets(WindowInsets.Type.systemBars() |\n"
       "                                               WindowInsets.Type.mandatorySystemGestures() |\n"
       "                                               WindowInsets.Type.displayCutout());")
assert old in s, "SDLSurface.onApplyWindowInsets changed"
open(p, "w").write(s.replace(old, new))
PY
  echo "$TEMPLATE_EDITION" >"$OUT/.kp_template"
fi

cat >"$OUT/app/jni/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.24)
project(koprulu_android C CXX)
set(KP_APP ON CACHE BOOL "" FORCE)
set(KP_TOOLS OFF CACHE BOOL "" FORCE)
set(CMAKE_BUILD_TYPE ${BUILD_TYPE} CACHE STRING "" FORCE)
add_subdirectory("$ROOT" koprulu)
EOF

cat >"$OUT/app/build.gradle" <<EOF
plugins {
    id 'com.android.application'
}

android {
    namespace = "org.libsdl.app"
    compileSdk = 36
    ndkVersion = "${NDK_VERSION}"
    defaultConfig {
        applicationId = "${APP_ID}"
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
        externalNativeBuild {
            cmake {
                arguments "-DANDROID_PLATFORM=android-24", "-DANDROID_STL=c++_static"
                abiFilters 'arm64-v8a'
            }
        }
    }
    buildTypes {
        release {
            minifyEnabled = false
            signingConfig = signingConfigs.debug
        }
    }
    sourceSets {
        main {
            assets.srcDirs = ['${STAGE}']
        }
    }
    androidResources {
        noCompress += ['kpd', 'kpm']
    }
    externalNativeBuild {
        cmake {
            path = 'jni/CMakeLists.txt'
            version = "${CMAKE_VERSION}"
        }
    }
    lint {
        abortOnError = false
    }
}
EOF
printf 'sdk.dir=%s\ncmake.dir=%s\n' "$ANDROID_HOME" "$(dirname "$CMAKE_BIN")" >"$OUT/local.properties"
sed -i '' 's/<string name="app_name">.*</<string name="app_name">KopruluSector</' "$OUT/app/src/main/res/values/strings.xml" || true

(cd "$OUT" && ./gradlew --offline -q assembleRelease)
APK="$OUT/app/build/outputs/apk/release/app-release.apk"
mkdir -p "$DEST"
cp "$APK" "$DEST/KopruluSector.apk"
echo "product: $DEST/KopruluSector.apk ($(du -h "$DEST/KopruluSector.apk" | cut -f1))"

if [ "${1:-}" = run ]; then
  shift
  adb wait-for-device
  adb install -r "$DEST/KopruluSector.apk" >/dev/null
  launch "$@"
fi
