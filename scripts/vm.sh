#!/usr/bin/env bash
# vm.sh - one-command facade for the Win11 VM (kills the ssh/scp/iconv/taskkill ceremony).
#
#   scripts/vm.sh build [--run]        sync+build via remote_build.sh, print ONLY errors/warnings
#                                      (full log -> tmp_vm_build.log). --run: kill ours + launch s1.
#   scripts/vm.sh run [slot]           kill ours, launch game in session 1 (default save slot 2)
#   scripts/vm.sh log <regex> [-n 50] [-f <vm-path>]
#                                      Select-String server-side (UTF-16 safe, no iconv), last N
#   scripts/vm.sh tail [N] [-f <vm-path>]   last N raw lines of the debug log
#   scripts/vm.sh status               cat vm_s1_out.txt (launch/smoke status)
#   scripts/vm.sh ps                   iwd2/frida processes on the VM
#   scripts/vm.sh kill [ours|orig|all] taskkill: ours=iwd2-re.exe (default), orig=IWD2.exe.
#                                      NEVER called implicitly by `build`; `run` kills ours only
#                                      (relaunch implies replacing the instance; Frida sessions on
#                                      the original survive builds).
#   scripts/vm.sh pull <vm-path> [dst] scp from VM (default dst .)
#   scripts/vm.sh push <src> <vm-path> scp to VM
#   scripts/vm.sh frida <script.py> [args...]
#                                      scp script + run it as the session-1 payload, ready-to-log
set -euo pipefail

VM="win11vm"
VM_REPO='C:/iwd2-re'
DEFAULT_LOG='C:\GOG Games\Icewind Dale 2\iwd2-re-debug.log'
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(dirname "$HERE")"

ps_quote() { printf "%s" "${1//\'/\'\'}"; }   # double single-quotes for PS single-quoted strings

# Feed the PS script over stdin: the VM's default ssh shell is PowerShell itself,
# so any inline quoting gets a second round of parsing (and eats $_).
vm_ps() { printf '%s\n' "$1" | ssh "$VM" "powershell -NoProfile -Command -"; }

cmd="${1:-}"; shift || true
case "$cmd" in
  build)
    log="$REPO/tmp_vm_build.log"
    if "$HERE/remote_build.sh" "$@" >"$log" 2>&1; then ok=1; else ok=0; fi
    # surface: orchestrator phases, compiler/linker diagnostics, terminal status
    grep -E '^==>|^   (status|WARN)|(: |LINK : )(fatal )?(error|warning) [A-Z]*[0-9]+|LNK[0-9]+|^done\.' "$log" \
      | sed 's/^[[:space:]]*//' | awk '!seen[$0]++' | head -60 || true
    n_err=$(grep -cE ': (fatal )?error ' "$log" || true)
    if [ "$ok" = 1 ] && [ "${n_err:-0}" = "0" ]; then
      echo "BUILD OK  (full log: tmp_vm_build.log)"
    else
      echo "BUILD FAILED ($n_err errors)  full log: tmp_vm_build.log"
      grep -E ': (fatal )?error ' "$log" | grep -q 'LNK1104.*iwd2-re' \
        && echo "hint: iwd2-re.exe is running and locks the exe -> scripts/vm.sh kill ours"
      exit 1
    fi
    ;;
  run)
    slot="${1:-}"
    if [ -n "$slot" ]; then
      sed -i "s/--slot [0-9]*/--slot $slot/" "$HERE/vm_s1_payload.cmd"
      scp -q "$HERE/vm_s1_payload.cmd" "$VM:$VM_REPO/scripts/"
    fi
    ssh "$VM" 'cmd /c "taskkill /im iwd2-re.exe /f >nul 2>&1 & exit 0"' >/dev/null || true
    ssh "$VM" "cmd /c $VM_REPO/scripts/vm_s1.cmd"
    for i in $(seq 1 30); do
      out=$(ssh "$VM" 'cmd /c "type C:\iwd2-re\vm_s1_out.txt 2>nul"' 2>/dev/null || true)
      case "$out" in *loaded:*|*timeout*|*failed:*) echo "status: $out"; exit 0 ;; esac
      sleep 5
    done
    echo "WARN: no terminal status after 150s (last: '${out:-<empty>}')"
    ;;
  log)
    pat="${1:?usage: vm.sh log <regex> [-n N] [-f vm-path]}"; shift
    n=50; f="$DEFAULT_LOG"
    while [ $# -gt 0 ]; do case "$1" in -n) n="$2"; shift 2;; -f) f="$2"; shift 2;; *) shift;; esac; done
    vm_ps "Select-String -Path '$(ps_quote "$f")' -Pattern '$(ps_quote "$pat")' | Select-Object -Last $n | ForEach-Object { \$_.LineNumber.ToString() + ':' + \$_.Line }"
    ;;
  tail)
    n="${1:-30}"; f="$DEFAULT_LOG"
    [ "${2:-}" = "-f" ] && f="$3"
    vm_ps "Get-Content -Tail $n -Path '$(ps_quote "$f")'"
    ;;
  status)
    ssh "$VM" 'cmd /c "type C:\iwd2-re\vm_s1_out.txt 2>nul"' || echo "(no vm_s1_out.txt)"
    ;;
  ps)
    ssh "$VM" 'cmd /c "tasklist | findstr /i \"iwd2 IWD2 frida python\" & exit 0"'
    ;;
  kill)
    tgt="${1:-ours}"
    case "$tgt" in
      ours) imgs="iwd2-re.exe" ;;
      orig) imgs="IWD2.exe" ;;
      all)  imgs="iwd2-re.exe IWD2.exe" ;;
      *) echo "kill ours|orig|all"; exit 2 ;;
    esac
    for im in $imgs; do
      ssh "$VM" "cmd /c \"taskkill /im $im /f & exit 0\"" || true
    done
    ;;
  pull)
    src="${1:?vm.sh pull <vm-path> [dst]}"; dst="${2:-.}"
    scp -q "$VM:$src" "$dst" && echo "pulled -> $dst"
    ;;
  push)
    src="${1:?vm.sh push <src> <vm-path>}"; dst="${2:?vm.sh push <src> <vm-path>}"
    scp -q "$src" "$VM:$dst" && echo "pushed -> $dst"
    ;;
  frida)
    script="${1:?vm.sh frida <script.py> [args...]}"; shift
    base="$(basename "$script")"
    scp -q "$script" "$VM:$VM_REPO/scripts/"
    printf '@echo off\r\ncd /d C:\\iwd2-re\r\npython scripts\\%s %s > C:\\iwd2-re\\vm_s1_out.txt 2>&1\r\n' \
      "$base" "$*" > /tmp/vm_s1_payload.cmd
    scp -q /tmp/vm_s1_payload.cmd "$VM:$VM_REPO/scripts/"
    ssh "$VM" "cmd /c $VM_REPO/scripts/vm_s1.cmd"
    echo "frida payload launched in session 1 (watch: vm.sh status / vm.sh tail)"
    ;;
  *)
    sed -n '2,22p' "$0"
    ;;
esac
