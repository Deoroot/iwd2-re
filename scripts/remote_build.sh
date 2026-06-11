#!/usr/bin/env bash
# Host -> VM build/run orchestrator for iwd2-re.
# Editing + Claude Code + re-agent live on the CachyOS host; the build (VS2019/MFC) and the
# game run on the Win11 VM, driven over SSH (alias `win11vm`, localhost:2222, key auth).
#
# Usage:
#   scripts/remote_build.sh            # sync + build only
#   scripts/remote_build.sh --run      # sync + build + deploy + launch in session 1 (renders),
#                                      #   then poll for world activation. Game stays up for Frida.
#
# Why .cmd batches instead of inline `ssh win11vm cmd /c "..."`:
#   - the VM's default SSH shell is PowerShell, which mangles `(x86)` and spaces;
#   - cmake/msbuild are NOT in the bare PATH -> must go through VsDevCmd (vm_build.cmd does this);
#   - GUI apps launched over SSH land in session 0 (Services, no desktop) -> they don't render
#     and never reach the world. vm_s1.cmd relaunches in the interactive console session (1).
set -euo pipefail

HOST_REPO="$HOME/iwd2-re"
VM="win11vm"
VM_REPO='C:/iwd2-re'                    # native Windows path for cmd + scp target

echo "==> sync source host -> VM (scp -rp keeps mtimes => incremental compile)"
scp -rpq "$HOST_REPO/src" "$HOST_REPO/CMakeLists.txt" "$VM:$VM_REPO/"
scp -q  "$HOST_REPO/scripts/vm_build.cmd" "$HOST_REPO/scripts/vm_s1.cmd" \
        "$HOST_REPO/scripts/vm_s1_payload.cmd" "$HOST_REPO/scripts/auto_start_game.py" \
        "$VM:$VM_REPO/scripts/"

echo "==> build on VM (VsDevCmd x86 + cmake --build, Debug)"
ssh "$VM" "cmd /c $VM_REPO/scripts/vm_build.cmd"

if [ "${1:-}" = "--run" ]; then
  echo "==> deploy + launch in session 1 (renders + input; required for Frida)"
  ssh "$VM" "cmd /c $VM_REPO/scripts/vm_s1.cmd"
  echo "==> wait for world activation (engine writes vm_s1_out.txt)"
  status=""
  for i in $(seq 1 30); do
    # NB: cmd's `type` rejects forward slashes -- use a backslash path.
    out=$(ssh "$VM" 'cmd /c "type C:\iwd2-re\vm_s1_out.txt 2>nul"' 2>/dev/null || true)
    case "$out" in
      *loaded:*|*timeout*|*failed:*) status="$out"; break ;;
    esac
    echo "   ... waiting ($((i * 5))s)${out:+ [last: $out]}"
    sleep 5
  done
  if [ -n "$status" ]; then
    echo "   status: $status"
  else
    echo "   WARN: no terminal status after 150s; vm_s1_out.txt: '${out:-<empty>}'"
    echo "   check: ssh $VM 'cmd /c type $VM_REPO\\vm_s1_out.txt' / tasklist | findstr iwd2"
  fi
  echo "   game stays up. Attach Frida: ssh $VM 'cmd /c python $VM_REPO/scripts/frida_attach_test.py'"
fi
echo "done."
