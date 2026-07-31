#!/usr/bin/env python3
"""frida_probe.py - parametric Frida tracer: a probe is 15 lines of JSON, not 6K of python.

  scripts/frida_probe.py --hooks tmp_hooks.json            generate driver + launch via vm.sh frida
  scripts/frida_probe.py --hooks tmp_hooks.json --attach   attach instead of spawn
  scripts/frida_probe.py --hooks tmp_hooks.json --dry      only write the driver, print its path
  scripts/frida_probe.py --summary [tmp_hooks.json]        pull the VM log + per-tag counts/samples

Hook spec (JSON):
{
  "process": "IWD2.exe",              // default; or iwd2-re.exe
  "log": "tmp_frida_probe.log",       // VM-side, relative to C:\\iwd2-re
  "hooks": [
    { "addr": "0x4063e0", "name": "GST", "conv": "thiscall",
      "args": ["s32", "s32", "s32", "s16", "s32", "s32"],
      "this_dump": [ {"off": "0x06", "type": "s16", "label": "posx"} ],
      "ret": "s32",                   // optional: log retval onLeave
      "max": 200 }                    // optional: stop after N hits (default 500)
  ]
}
Arg types: u8 u16 u32 s16 s32 ptr f32 str wstr (str/wstr deref the arg pointer, guarded).
conv: thiscall (this=ecx, stack args -> args[0..]) | cdecl | stdcall (args[0..]).
Addresses absolute (IWD2.exe, no ASLR). Hook function ENTRIES only.

Complex probes (call-origin filters, list walks) still deserve a bespoke script --
see frida_formation_trace.py, and put the result in scripts/probes/ (one-off
investigations live there; scripts/ keeps only the reusable tooling).

------------------------------------------------------------------------------
The six gotchas of a `vm.sh frida` payload. frida_probe.py already handles all
of them; a bespoke probe has to, and every one of these has cost a session at
least once. The payload runs as a scheduled task launched by a fire-and-forget
VBS, which is where most of this comes from.

1. NO STDIN. `sys.stdin.read()` returns EOF immediately and the driver exits
   before a single hook fires. Keep it alive with `while True: time.sleep(0.5)`.
2. STDOUT REDIRECTION IS LOST once the VBS parent exits, so `> out.txt` silently
   drops writes. Write to a DEDICATED file and read THAT, not vm_s1_out.txt.
3. FLUSH AND FSYNC every line (`f.flush(); os.fsync(f.fileno())`). Buffered
   output is gone if the game crashes -- which is usually the interesting case.
4. A STUCK DRIVER BLOCKS THE NEXT `frida.attach` (silent no-fire, or a locked
   logfile). `taskkill` is unreliable here: use `Get-Process python` /
   `Stop-Process -Name python -Force` and confirm it is empty before re-shipping.
5. SILENT NO-FIRE IS USUALLY NOT THE PATH. ImageBase is 0x400000 with no ASLR,
   so `ptr(0xADDR)` absolute is correct -- dump 8 bytes at each hook and diff
   against `sym.py bytes ADDR` before suspecting anything else.
6. SESSION 0 NEVER RENDERS. GUI over ssh lands in session 0; anything needing
   render, input or Frida must go through vm_s1.cmd (session 1). A smoke
   timeout there means no desktop, not a crash.
------------------------------------------------------------------------------
"""
import argparse
import json
import os
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VMSH = REPO / "scripts" / "vm.sh"

READERS = {
    "u8":  "{p}.readU8()",
    "u16": "{p}.readU16()",
    "u32": "{p}.readU32()",
    "s16": "(({p}.readU32() << 16) >> 16)",
    "s32": "{p}.readS32()",
    "f32": "{p}.readFloat()",
    "ptr": "{p}.readPointer().toString()",
}
ARG_VALUE = {   # NativePointer arg -> logged value
    "u32": "{a}.toUInt32()",
    "s32": "{a}.toInt32()",
    "s16": "(({a}.toInt32() << 16) >> 16)",
    "u16": "({a}.toUInt32() & 0xffff)",
    "u8":  "({a}.toUInt32() & 0xff)",
    "ptr": "{a}.toString()",
    "f32": "{a}.toInt32()",          # raw bits; floats passed on stack: read via dump if needed
    "str": "rdstr({a})",
    "wstr": "rdwstr({a})",
}


def js_for_hook(h, idx):
    name = h["name"]
    addr = h["addr"]
    conv = h.get("conv", "thiscall")
    args = h.get("args", [])
    maxn = int(h.get("max", 500))
    parts = []
    parts.append(f"let cnt{idx} = 0;")
    parts.append(f"Interceptor.attach(ptr({addr}), {{")
    parts.append("  onEnter(args) {")
    parts.append(f"    if (++cnt{idx} > {maxn}) return;")
    fields = [f"tag: '{name}'"]
    if conv == "thiscall":
        parts.append("    const thiz = this.context.ecx;")
        fields.append("this: thiz.toString()")
    for i, t in enumerate(args):
        expr = ARG_VALUE.get(t, ARG_VALUE["u32"]).format(a=f"args[{i}]")
        fields.append(f"a{i}_{t}: {expr}")
    for d in h.get("this_dump", []):
        off = d["off"] if isinstance(d["off"], str) else hex(d["off"])
        rd = READERS.get(d.get("type", "u32"), READERS["u32"]).format(p=f"thiz.add({off})")
        fields.append(f"{d.get('label', 'f_' + off)}: guard(() => {rd})")
    parts.append("    send({ " + ", ".join(fields) + " });")
    if h.get("ret"):
        parts.append("  },")
        parts.append("  onLeave(retval) {")
        parts.append(f"    if (cnt{idx} > {maxn}) return;")
        rexpr = ARG_VALUE.get(h["ret"], ARG_VALUE["u32"]).format(a="retval")
        parts.append(f"    send({{ tag: '{name}_ret', ret: {rexpr} }});")
    parts.append("  }")
    parts.append("});")
    return "\n".join(parts)


DRIVER = '''#!/usr/bin/env python3
# GENERATED by frida_probe.py -- do not hand-edit; edit the hooks JSON instead.
import frida, sys, json, threading, os

GAME_DIR = r"C:\\GOG Games\\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, {process!r})
LOG = r"C:\\iwd2-re\\{log}"

JS = r"""
'use strict';
function guard(f) {{ try {{ return f(); }} catch (e) {{ return 'ERR:' + e; }} }}
function rdstr(p)  {{ try {{ return p.readUtf8String(64); }} catch (e) {{ return 'ERR'; }} }}
function rdwstr(p) {{ try {{ return p.readUtf16String(64); }} catch (e) {{ return 'ERR'; }} }}
{hooks_js}
send({{ tag: 'ready', hooks: {n_hooks} }});
"""

def on_message(message, data):
    line = json.dumps(message["payload"]) if message["type"] == "send" else "ERROR " + json.dumps(message)
    print(line, flush=True)
    with open(LOG, "a") as f:
        f.write(line + "\\n")

open(LOG, "w").close()
if "--attach" in sys.argv:
    session = frida.attach({process!r})
    pid = None
else:
    pid = frida.spawn(EXE, cwd=GAME_DIR)
    session = frida.attach(pid)
script = session.create_script(JS)
script.on("message", on_message)
script.load()
if pid is not None:
    frida.resume(pid)
print("[*] hooks live, logging to " + LOG, flush=True)
threading.Event().wait()
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hooks", help="hook spec JSON")
    ap.add_argument("--attach", action="store_true")
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--summary", nargs="?", const="", metavar="HOOKS_JSON")
    args = ap.parse_args()

    if args.summary is not None:
        spec = json.loads(Path(args.summary or args.hooks or "tmp_hooks.json").read_text()) \
            if (args.summary or args.hooks) else {}
        log_name = spec.get("log", "tmp_frida_probe.log")
        local = REPO / log_name
        subprocess.run([str(VMSH), "pull", f"C:/iwd2-re/{log_name}", str(local)], check=True)
        tags = Counter()
        first, last = {}, {}
        for line in open(local, errors="replace"):
            try:
                tag = json.loads(line).get("tag", "?")
            except Exception:
                tag = "(unparsed)"
            tags[tag] += 1
            first.setdefault(tag, line.strip())
            last[tag] = line.strip()
        if not tags:
            print(f"log empty: {local}")
            return 1
        for tag, n in tags.most_common():
            print(f"{n:6d}  {tag}")
            print(f"        first: {first[tag][:160]}")
            if n > 1:
                print(f"        last:  {last[tag][:160]}")
        print(f"(full log: {local})")
        return 0

    if not args.hooks:
        print(__doc__.strip())
        return 2
    spec = json.loads(Path(args.hooks).read_text())
    hooks_js = "\n".join(js_for_hook(h, i) for i, h in enumerate(spec["hooks"]))
    driver = DRIVER.format(process=spec.get("process", "IWD2.exe"),
                           log=spec.get("log", "tmp_frida_probe.log"),
                           hooks_js=hooks_js, n_hooks=len(spec["hooks"]))
    out = REPO / "tmp_frida_probe_driver.py"
    out.write_text(driver)
    print(f"driver: {out}")
    if args.dry:
        return 0
    cmd = [str(VMSH), "frida", str(out)] + (["--attach"] if args.attach else [])
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    raise SystemExit(main())
