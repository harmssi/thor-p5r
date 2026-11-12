#!/bin/bash
# Builds the deck app (Gradle + Android SDK from local.properties) into dist/ThorP5R.apk.
# Optional: --install (adb install -r; adb uses ANDROID_SERIAL if set).
set -euo pipefail
cd "$(dirname "$0")"
./gradlew -q assembleRelease
mkdir -p dist
cp app/build/outputs/apk/release/app-release.apk dist/ThorP5R.apk
echo "APK: $PWD/dist/ThorP5R.apk"
if [ "${1:-}" = "--install" ]; then adb install -r dist/ThorP5R.apk; fi
