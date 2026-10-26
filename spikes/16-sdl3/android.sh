#!/usr/bin/env bash
# Builds spike 16 as an Android APK from SDL's Gradle template (android-project of SDL
# 3.4.18) and, with "run", installs and starts it on the attached device and prints its
# SPIKE lines from logcat.
# Output: build/spikes/16-sdl3/android/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/spikes/16-sdl3/android"
TEMPLATE="$ROOT/tools/toolchain/sdl/SDL3-3.4.18/android-project"
OGG="${SPIKE_OGG:-$ROOT/generated/sounds/terran/terran_frigate_tvkyes00.ogg}"
NDK_VERSION="${NDK_VERSION:-29.0.14206865}"
GRADLE_VERSION="${GRADLE_VERSION:-9.7.1}"
AGP_VERSION="${AGP_VERSION:-9.1.1}"
# The SDK's CMake 3.22 is older than the projects need: use the host's CMake and Ninja.
CMAKE_BIN="$(dirname "$(command -v cmake)")"
CMAKE_VERSION="$(cmake --version | head -1 | awk '{print $3}')"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"

"$ROOT/tools/toolchain/fetch.sh" >/dev/null

rm -rf "$OUT"
mkdir -p "$OUT"
cp -R "$TEMPLATE/." "$OUT/"
rm -rf "$OUT/app/jni"
mkdir -p "$OUT/app/jni"

# The native part is the spike's own CMake project.
cat >"$OUT/app/jni/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.24)
project(koprulu_spike_android C)
set(KP_SPIKE_OGG "$OGG" CACHE FILEPATH "" FORCE)
add_subdirectory("$ROOT/spikes/16-sdl3" spike)
EOF

sed -i '' "s/gradle-[0-9.]*-bin.zip/gradle-${GRADLE_VERSION}-bin.zip/" "$OUT/gradle/wrapper/gradle-wrapper.properties"
sed -i '' "s/com.android.tools.build:gradle:[0-9.]*/com.android.tools.build:gradle:${AGP_VERSION}/" "$OUT/build.gradle"
sed -i '' 's/^task clean.*/tasks.register("clean", Delete) {/; s/delete rootProject.buildDir/delete rootProject.layout.buildDirectory/' "$OUT/build.gradle"

cat >"$OUT/app/build.gradle" <<EOF
plugins {
    id 'com.android.application'
}

android {
    namespace = "org.libsdl.app"
    compileSdk = 36
    ndkVersion = "${NDK_VERSION}"
    defaultConfig {
        applicationId = "com.koprulusector.spike"
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
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

# The template's GLES 2 feature line: the runtime needs GLES 3.0.
sed -i '' 's/android:glEsVersion="0x00020000"/android:glEsVersion="0x00030000"/' "$OUT/app/src/main/AndroidManifest.xml"

(cd "$OUT" && ./gradlew --offline -q assembleRelease)
APK="$OUT/app/build/outputs/apk/release/app-release.apk"
echo "built $APK"

if [ "${1:-}" = run ]; then
  adb install -r "$APK"
  adb logcat -c
  adb shell am start -n com.koprulusector.spike/org.libsdl.app.SDLActivity
  timeout 120 adb logcat -s SDL:V SDL/APP:V | grep --line-buffered SPIKE | sed '/SPIKE DONE/q'
fi
