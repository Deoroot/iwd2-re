#!/usr/bin/env bash
# ghidra_snapshot.sh - refresh the published Ghidra project snapshot.
#
# Re-zips the local IWD2 Ghidra project and uploads it to a rolling GitHub
# Release (tag: ghidra-snapshot) so the repo ships current function names
# without bloating git history. The zip is NOT committed (gitignored): this
# repo is a public fork, so git-lfs uploads are blocked by GitHub, and a 30 MB
# blob per snapshot would balloon .git. A Release asset is free, works on a
# fork, and is fetched on demand.
#
# Run this BEFORE a push, NOT from the export loop:
#   - `gb export create-functions` runs many times per recovery session; the
#     snapshot is a ~30 MB artifact and belongs on a push cadence.
#   - Ghidra must be CLOSED: a locked .rep can be zipped mid-write and corrupt.
#
# Usage:
#   scripts/ghidra_snapshot.sh            # zip + upload to the Release
#   scripts/ghidra_snapshot.sh --no-upload  # only write the local zip
#   scripts/ghidra_snapshot.sh --check    # only verify it's safe to snapshot
#   GHIDRA_PROJECT_DIR=/path scripts/ghidra_snapshot.sh
set -euo pipefail

RELEASE_TAG="ghidra-snapshot"
ASSET_NAME="IWD2_rep.zip"

# Active project is the NESTED IWD2/IWD2 (per CLAUDE.md). The outer
# ~/ghidra_projects/IWD2/IWD2.rep is a stale May copy - do NOT zip that one.
PROJECT_DIR="${GHIDRA_PROJECT_DIR:-$HOME/ghidra_projects/IWD2/IWD2}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO_ROOT/data/ghidra/IWD2_rep.zip"
CHECK_ONLY=0
UPLOAD=1
case "${1:-}" in
    --check)     CHECK_ONLY=1 ;;
    --no-upload) UPLOAD=0 ;;
esac

err() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
human() { numfmt --to=iec --suffix=B "${1:-0}" 2>/dev/null || echo "${1:-0}B"; }

[ -d "$PROJECT_DIR/IWD2.rep" ] || err "no IWD2.rep under $PROJECT_DIR (set GHIDRA_PROJECT_DIR)"
command -v zip >/dev/null || err "zip not installed"

# Safety 1: Ghidra lock files inside the LIVE .rep = project is open. Scope to
# IWD2.rep only - sibling .rep.bak_*/.rep.corrupted_* dirs are irrelevant.
LOCKS="$(find "$PROJECT_DIR/IWD2.rep" -maxdepth 3 \( -iname '*.lock' -o -iname '.lock' -o -iname '*~lock*' \) 2>/dev/null || true)"
[ -z "$LOCKS" ] || err $'project is locked (Ghidra open?). Close it first:\n'"$LOCKS"

# Safety 2: a live Ghidra JVM holding this project. Match java processes only
# (the MCP bridge is python, and grepping all 'ghidra' lines self-matches the
# grep's own argv).
if pgrep -af 'java' 2>/dev/null | grep -iq 'ghidra'; then
    err "a Ghidra JVM appears to be running — close the GUI/headless session first"
fi

if [ "$CHECK_ONLY" = 1 ]; then
    echo "OK: $PROJECT_DIR is unlocked and safe to snapshot."
    exit 0
fi

# Zip IWD2.gpr + IWD2.rep at archive root (matches the existing zip layout that
# restore.ps1 expands into C:\ghidra_projects\IWD2). Exclude Ghidra transaction
# temporaries (tmp*.ps) and any stray lock - they are rebuilt on next open and
# only bloat the archive (~100 MB each).
TMP="$(mktemp "${TMPDIR:-/tmp}/iwd2_rep.XXXXXX.zip")"
trap 'rm -f "$TMP"' EXIT

OLD_SIZE=0
[ -f "$OUT" ] && OLD_SIZE="$(stat -c%s "$OUT" 2>/dev/null || echo 0)"

echo "Zipping $PROJECT_DIR -> $OUT ..."
rm -f "$TMP"   # zip treats an existing 0-byte file as a corrupt archive; create fresh
(
    cd "$PROJECT_DIR"
    GPR=()
    [ -f IWD2.gpr ] && GPR=(IWD2.gpr)
    zip -r -q -X "$TMP" "${GPR[@]}" IWD2.rep \
        -x '*/tmp*.ps' -x 'IWD2.rep/*lock*' -x '*.lock'
)

NEW_SIZE="$(stat -c%s "$TMP")"
mv -f "$TMP" "$OUT"
trap - EXIT

printf 'Snapshot written: %s\n' "$OUT"
printf '  old: %s   new: %s   delta: %+d bytes\n' \
    "$(human "$OLD_SIZE")" "$(human "$NEW_SIZE")" "$((NEW_SIZE - OLD_SIZE))"
echo

if [ "$UPLOAD" = 0 ]; then
    echo "Skipped upload (--no-upload). Local zip only."
    exit 0
fi
command -v gh >/dev/null || err "gh not installed - rerun with --no-upload, or install GitHub CLI"

REPO="$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null \
    | sed -E 's#.*github.com[:/]([^/]+/[^/]+)$#\1#; s#\.git$##')"
[ -n "$REPO" ] || err "could not derive GitHub repo from origin remote"

# Rolling release: create once, then clobber the asset each run.
if ! gh release view "$RELEASE_TAG" -R "$REPO" >/dev/null 2>&1; then
    echo "Creating rolling release '$RELEASE_TAG' on $REPO ..."
    gh release create "$RELEASE_TAG" -R "$REPO" --latest=false \
        --title "Ghidra project snapshot" \
        --notes "Rolling snapshot of the IWD2 Ghidra project ($ASSET_NAME), auto-updated by scripts/ghidra_snapshot.sh. Fetch with scripts/ghidra_restore.sh."
fi
echo "Uploading $ASSET_NAME to $REPO ($RELEASE_TAG) ..."
gh release upload "$RELEASE_TAG" "$OUT#$ASSET_NAME" -R "$REPO" --clobber
echo "Done. Asset live at:"
echo "  https://github.com/$REPO/releases/download/$RELEASE_TAG/$ASSET_NAME"
