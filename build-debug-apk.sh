#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

if ! command -v gradle >/dev/null 2>&1; then
  echo "Gradle was not found. Install it (macOS: brew install gradle) or build from Android Studio." >&2
  exit 2
fi
if [[ -z "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}" ]]; then
  echo "ANDROID_HOME or ANDROID_SDK_ROOT is not set. Install Android Studio's SDK, NDK 27.0.12077973, and CMake 3.22.1." >&2
  exit 2
fi

gradle --no-daemon assembleDebug
printf '\nDebug APK: %s\n' "$PWD/app/build/outputs/apk/debug/app-debug.apk"
