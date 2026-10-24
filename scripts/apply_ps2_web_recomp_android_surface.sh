#!/usr/bin/env bash
set -euo pipefail

REV="333999564ad0fe84f24b504416869dc182404a61"
URL="https://github.com/snuri00/ps2-web-recomp.git"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}"
if [[ "$(uname -s)" == "Darwin" ]]; then CACHE_ROOT="$HOME/Library/Caches"; fi
WORKDIR="${TMB_RECOMP_ANDROID_WORKDIR:-$CACHE_ROOT/tmb-ps2-web-recomp-android}"
UPSTREAM="$WORKDIR/upstream"
PATCH="$ROOT/patches/ps2-web-recomp-android-surface.patch"

command -v git >/dev/null 2>&1 || { echo "ERROR: git is required." >&2; exit 2; }
if [[ ! -f "$PATCH" ]]; then echo "ERROR: patch not found: $PATCH" >&2; exit 2; fi
mkdir -p "$WORKDIR"
if [[ ! -d "$UPSTREAM/.git" ]]; then
  echo "== Cloning a separate upstream checkout (keeps the probe checkout untouched)"
  git clone "$URL" "$UPSTREAM"
fi
if [[ "$(git -C "$UPSTREAM" remote get-url origin)" != "$URL" ]]; then
  echo "ERROR: $UPSTREAM is not the expected upstream checkout." >&2; exit 2
fi
if [[ -n "$(git -C "$UPSTREAM" status --porcelain)" ]]; then
  echo "ERROR: upstream checkout has local changes; refusing to overwrite: $UPSTREAM" >&2; exit 2
fi
CURRENT="$(git -C "$UPSTREAM" rev-parse HEAD)"
if [[ "$CURRENT" != "$REV" ]]; then
  echo "== Pinning upstream checkout to $REV"
  git -C "$UPSTREAM" fetch --depth 1 origin "$REV"
  git -C "$UPSTREAM" checkout --detach "$REV"
fi
if [[ -n "$(git -C "$UPSTREAM" status --porcelain)" ]]; then
  echo "ERROR: checkout is not clean after pinning." >&2; exit 2
fi

echo "== Checking patch against pinned upstream"
git -C "$UPSTREAM" apply --check "$PATCH"
git -C "$UPSTREAM" apply "$PATCH"
git -C "$UPSTREAM" diff --check
printf '\nAndroid surface patch applied to:\n  %s\n\n' "$UPSTREAM"
echo "This is only the surface hand-off. It is not a complete Android app or a playable TMB build."
