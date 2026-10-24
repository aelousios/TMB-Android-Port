#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out="${TMPDIR:-/tmp}/tmb_iso_test"
g++ -std=c++17 -Wall -Wextra -Werror \
  -Iapp/src/main/cpp \
  tests/iso9660_test.cpp app/src/main/cpp/iso9660.cpp \
  -o "$out"
"$out"
