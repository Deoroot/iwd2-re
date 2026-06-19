#!/usr/bin/env bash
# ghidra_restore.sh - fetch the published Ghidra project snapshot (host side).
#
# Downloads the rolling GitHub Release asset produced by ghidra_snapshot.sh and
# extracts it into the local Ghidra projects dir. The snapshot is NOT in git
# (this repo is a public fork, so it lives as a Release asset, not an LFS blob).
#
# Usage:
#   scripts/ghidra_restore.sh            # download + extract to ~/ghidra_projects/IWD2/IWD2
#   GHIDRA_PROJECT_PARENT=/path scripts/ghidra_restore.sh
#   scripts/ghidra_restore.sh --force    # overwrite an existing project
set -euo pipefail

RELEASE_TAG="ghidra-snapshot"
ASSET_NAME="IWD2_rep.zip"
REPO_DEFAULT="WillScarlettOhara/iwd2-re"
# Active project lives at <parent>/IWD2 (the nested IWD2/IWD2 layout, per CLAUDE.md).
PARENT="${GHIDRA_PROJECT_PARENT:-$HOME/ghidra_projects/IWD2}"
FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

err() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null \
    | sed -E 's#.*github.com[:/]([^/]+/[^/]+)$#\1#; s#\.git$##')"
REPO="${REPO:-$REPO_DEFAULT}"

DEST="$PARENT/IWD2"
if [ -e "$DEST/IWD2.rep" ] && [ "$FORCE" = 0 ]; then
    err "$DEST/IWD2.rep already exists. Re-run with --force to overwrite (you will lose local Ghidra edits)."
fi
mkdir -p "$DEST"

URL="https://github.com/$REPO/releases/download/$RELEASE_TAG/$ASSET_NAME"
TMP="$(mktemp "${TMPDIR:-/tmp}/iwd2_rep.XXXXXX.zip")"
trap 'rm -f "$TMP"' EXIT

echo "Downloading $URL ..."
if command -v gh >/dev/null 2>&1; then
    gh release download "$RELEASE_TAG" -R "$REPO" -p "$ASSET_NAME" -O "$TMP" --clobber
else
    curl -fL --retry 3 -o "$TMP" "$URL" || err "download failed (need gh or a reachable public release)"
fi

command -v unzip >/dev/null || err "unzip not installed"
unzip -t "$TMP" >/dev/null 2>&1 || err "downloaded asset is not a valid zip"

echo "Extracting into $DEST ..."
[ "$FORCE" = 1 ] && rm -rf "$DEST/IWD2.rep" "$DEST/IWD2.gpr"
unzip -q -o "$TMP" -d "$DEST"
echo "Done. Open the project at: $DEST (IWD2.gpr)"
