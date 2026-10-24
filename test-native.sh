#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out="${TMPDIR:-/tmp}/tmb_iso_test"
g++ -std=c++17 -Wall -Wextra -Werror \
  -Iapp/src/main/cpp \
  tests/iso9660_test.cpp app/src/main/cpp/iso9660.cpp app/src/main/cpp/ps2_elf.cpp \
  -o "$out"
"$out"
elfout="${TMPDIR:-/tmp}/tmb_ps2_elf_test"
g++ -std=c++17 -Wall -Wextra -Werror \
  -Iapp/src/main/cpp \
  tests/ps2_elf_test.cpp app/src/main/cpp/ps2_elf.cpp \
  -o "$elfout"
memout="${TMPDIR:-/tmp}/tmb_ps2_memory_test"
g++ -std=c++17 -Wall -Wextra -Werror \
  -Iapp/src/main/cpp \
  tests/ps2_memory_test.cpp app/src/main/cpp/ps2_memory.cpp app/src/main/cpp/ps2_elf.cpp \
  -o "$memout"
"$memout"
"$elfout"
