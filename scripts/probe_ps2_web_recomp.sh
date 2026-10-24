#!/usr/bin/env bash
set -euo pipefail

REV="333999564ad0fe84f24b504416869dc182404a61"
REPO_URL="https://github.com/snuri00/ps2-web-recomp.git"
NAME="tmb"

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/probe_ps2_web_recomp.sh /absolute/path/to/Twisted-Metal-Black-US.iso

Optional environment variables:
  TMB_RECOMP_WORKDIR   External work directory (default: OS cache directory)
  TMB_PROBE_FRAMES     Frames per exploration run (default: 3600)
  TMB_PROBE_ROUNDS     Max exploration rounds (default: 4)

This tests the pinned PS2 Web Recomp native runner against the US NTSC-U ISO.
It does not modify the TMB Android app, copy the ISO into this Git repository,
or claim Android compatibility. Output and extracted files stay in the external
work directory, not this repo.
USAGE
}

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
  usage
  exit 0
fi
if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi

ISO_INPUT="$1"
[[ -f "$ISO_INPUT" ]] || { echo "ERROR: ISO file not found: $ISO_INPUT" >&2; exit 2; }
ISO="$(cd "$(dirname "$ISO_INPUT")" && pwd)/$(basename "$ISO_INPUT")"
case "$ISO" in
  *.[iI][sS][oO]) ;;
  *) echo "ERROR: supply a plain .iso file. CHD/BIN-CUE conversion is not handled by this probe." >&2; exit 2 ;;
esac

OS="$(uname -s)"
CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}"
if [[ "$OS" == "Darwin" ]]; then
  CACHE_ROOT="$HOME/Library/Caches"
fi
WORKDIR="${TMB_RECOMP_WORKDIR:-$CACHE_ROOT/tmb-ps2-web-recomp}"
UPSTREAM="$WORKDIR/upstream"

for cmd in git cmake ninja python3; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "ERROR: required command '$cmd' not found. Install Xcode Command Line Tools and Homebrew build tools." >&2
    exit 2
  }
done

if [[ "$OS" == "Darwin" ]]; then
  if ! command -v brew >/dev/null 2>&1; then
    echo "ERROR: Homebrew is required for the documented Apple Silicon native build. See https://brew.sh/" >&2
    exit 2
  fi
  BREW_PREFIX="$(brew --prefix bash 2>/dev/null || true)"
  BASH4="$BREW_PREFIX/bin/bash"
  if [[ ! -x "$BASH4" ]] || ! "$BASH4" -c '(( BASH_VERSINFO[0] >= 4 ))' 2>/dev/null; then
    echo "ERROR: install modern Bash and native prerequisites first:" >&2
    echo "  brew install bash cmake ninja sdl2" >&2
    exit 2
  fi
  if ! pkg-config --exists sdl2 2>/dev/null; then
    echo "ERROR: SDL2 development files are required for the interactive native runner." >&2
    echo "  brew install sdl2" >&2
    exit 2
  fi
  if ! command -v clang++ >/dev/null 2>&1; then
    echo "ERROR: clang++ not found. Run: xcode-select --install" >&2
    exit 2
  fi
else
  BASH4="$(command -v bash)"
  if ! "$BASH4" -c '(( BASH_VERSINFO[0] >= 4 ))' 2>/dev/null; then
    echo "ERROR: Bash 4+ is required." >&2
    exit 2
  fi
  if ! command -v clang++ >/dev/null 2>&1 && ! command -v c++ >/dev/null 2>&1; then
    echo "ERROR: a C++20 compiler is required." >&2
    exit 2
  fi
  if ! pkg-config --exists sdl2 2>/dev/null; then
    echo "ERROR: SDL2 development files are required for the interactive native runner." >&2
    exit 2
  fi
fi
# Upstream helper scripts use /usr/bin/env bash. Put Homebrew Bash ahead of the
# macOS system Bash 3.2 so child scripts get the same compatible interpreter.
PATH="$(dirname "$BASH4"):$PATH"
export PATH

mkdir -p "$WORKDIR"
if [[ ! -d "$UPSTREAM/.git" ]]; then
  echo "== Cloning PS2 Web Recomp into external work directory"
  git clone "$REPO_URL" "$UPSTREAM"
fi
if [[ -n "$(git -C "$UPSTREAM" status --porcelain)" ]]; then
  echo "ERROR: external runtime clone has local changes; refusing to overwrite them: $UPSTREAM" >&2
  exit 2
fi

echo "== Pinning PS2 Web Recomp to $REV"
git -C "$UPSTREAM" fetch --depth 1 origin "$REV"
git -C "$UPSTREAM" checkout --detach "$REV"

FRAMES="${TMB_PROBE_FRAMES:-3600}"
ROUNDS="${TMB_PROBE_ROUNDS:-4}"
[[ "$FRAMES" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: TMB_PROBE_FRAMES must be a positive integer." >&2; exit 2; }
[[ "$ROUNDS" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: TMB_PROBE_ROUNDS must be a positive integer." >&2; exit 2; }

echo "== Building/running native compatibility probe (frames=$FRAMES, max rounds=$ROUNDS)"
"$BASH4" "$UPSTREAM/scripts/port.sh" "$NAME" "$ISO" --native-only --frames "$FRAMES" --rounds "$ROUNDS"

LOG="$UPSTREAM/games/$NAME/explore.log"
echo
echo "== Probe outputs"
echo "Runtime checkout: $UPSTREAM"
echo "Log: $LOG"
echo "Captured frames: $UPSTREAM/games/$NAME/shots"
echo "Generated native runner: $UPSTREAM/build/native-$NAME/ps2run"
if [[ -f "$LOG" ]]; then
  echo
echo "== Potential compatibility blockers (last 120 matches)"
  grep -Ei '\[sys\].*(unhandled|missing)|\[iop\].*(missing|unhandled)|\[ee\].*(halted|exception)|fatal|segmentation|assertion|error:' "$LOG" | tail -n 120 || true
else
  echo "No explore.log was generated; inspect the terminal output and native build log." >&2
fi

echo
echo "== If the generated runner and window build succeeded, launch it manually with:"
printf '"%s" "%s" --disc "%s" --window --memcard "%s"\n' \
  "$UPSTREAM/build/native-$NAME/ps2run" \
  "$UPSTREAM/games/$NAME/disc/SCUS_971.01" \
  "$UPSTREAM/games/$NAME/disc.iso" \
  "$UPSTREAM/games/$NAME/tmb.ps2"
echo
echo "This is only a compatibility probe. A successful compile does not mean the game boots or is playable."
