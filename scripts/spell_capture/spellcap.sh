#!/usr/bin/env bash
# spellcap.sh - facade for the spell-cast capture rig (host recorder + VM marker).
#
#   spellcap.sh rec [recorder.py args...]   start the host recorder (Xvfb + SPICE
#                                           client + ffmpeg + UDP listener/cutter).
#                                           Start the GAME in the VM FIRST so the
#                                           resolution probe sees the in-game res.
#   spellcap.sh mark-orig                   ship+run the marker in the VM, attached
#                                           to the running ORIGINAL IWD2.exe (Frida).
#   spellcap.sh mark-ours                   ship+run the marker in the VM, tailing
#                                           our iwd2-re.exe debug log (no Frida).
#   spellcap.sh compare <Spell_Name>        hstack clips/<Spell>__orig.mkv and
#                                           __ours.mkv -> __cmp.mkv (both audio tracks).
#   spellcap.sh stop                        stop host recorder + VM marker.
#
# Port must match on both sides (default 48888). Override: PORT=NNNN spellcap.sh ...
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
VMSH="$REPO/scripts/vm.sh"
VM="win11vm"
PORT="${PORT:-48888}"
CLIPS="${CLIPS:-$HERE/clips}"

cmd="${1:-}"; shift || true
case "$cmd" in
  rec)
    exec python3 "$HERE/recorder.py" --port "$PORT" --clips "$CLIPS" "$@"
    ;;
  mark-orig)
    "$VMSH" frida "$HERE/cast_marker.py" --mode frida --port "$PORT" "$@"
    echo "marker (orig) launched in VM; clips -> $CLIPS/<spell>__orig.mkv"
    ;;
  mark-ours)
    "$VMSH" frida "$HERE/cast_marker.py" --mode tail --port "$PORT" "$@"
    echo "marker (ours) launched in VM; clips -> $CLIPS/<spell>__ours.mkv"
    echo "reminder: our build needs '.iwd2-re-debug.enabled' in the game CWD."
    ;;
  compare)
    spell="${1:?usage: spellcap.sh compare <Spell_Name>}"
    o="$CLIPS/${spell}__orig.mkv"; u="$CLIPS/${spell}__ours.mkv"
    [ -f "$o" ] || { echo "missing $o"; exit 1; }
    [ -f "$u" ] || { echo "missing $u"; exit 1; }
    out="$CLIPS/${spell}__cmp.mkv"
    ffmpeg -hide_banner -loglevel error -y -i "$o" -i "$u" \
      -filter_complex "[0:v][1:v]hstack=inputs=2[v]" \
      -map "[v]" -map 0:a? -map 1:a? \
      -metadata:s:a:0 title=orig -metadata:s:a:1 title=ours \
      -c:v av1_nvenc -preset p4 -cq 34 -c:a libopus -b:a 64k "$out"
    echo "side-by-side -> $out  (audio track 0=orig, 1=ours)"
    ;;
  stop)
    pkill -TERM -f "spell_capture/recorder.py" 2>/dev/null && echo "host recorder stopped" || echo "no host recorder running"
    ssh "$VM" 'powershell -NoProfile -Command "Stop-Process -Name python -Force -ErrorAction SilentlyContinue"' >/dev/null 2>&1 || true
    echo "VM marker (python) stopped"
    ;;
  *)
    sed -n '2,22p' "$0"
    ;;
esac
