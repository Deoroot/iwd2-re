#!/usr/bin/env python3
"""Differential / runtime trace of the Alt-held container highlight path.

Symptom: holding Alt highlights ground loot piles but NOT chests. Piles draw via
CInfinity::FXRender (shadow), chests draw via CInfinity::OutlinePoly -> the
CVidMode polygon rasterizer. We hook the whole dispatch chain and emit a
once-per-second snapshot plus menu-state transitions:

    CScreenWorld::SetSystemKeyMenu(bValue)   Alt detected -> m_bMenuKeyDown
    CScreenWorld::GetMenuKey()               read by container Render
    CGameContainer::Render(this,..)          split chest vs pile (m_containerType@0x5CA)
    CInfinity::OutlinePoly(.., rgbColor)      per-outline attempt + colour
    CVidMode::OutlinePoly(..) -> BOOL         the virtual dispatcher (slot 0xB0)
    CVidMode::OutlinePoly3d(..)              the accelerated line drawer

Reading the snapshot:
  * outline GREEN present + sw3d fires      -> 3D path works; look elsewhere
  * outline GREEN present + sw FALSE, no 3d -> software mode, stub not patched
  * chest renders == 0                      -> chest branch never reached

Usage:
    python scripts/frida_alt_highlight_probe.py            # spawn ORIGINAL IWD2.exe
    python scripts/frida_alt_highlight_probe.py --ours     # spawn our iwd2-re.exe
    python scripts/frida_alt_highlight_probe.py --ours --attach  # attach instead

In-game: load a save with a chest + a loot pile, then HOLD Alt ~3 s.
Log: tmp_frida_alt_highlight.log (repo root). Ctrl-C / kill to stop.
"""
import frida
import sys
import os
import json
import time

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "tmp_frida_alt_highlight.log")

# (module, {fn: rva})  -- rva = absolute_addr - 0x400000, resolved against the
# live module base so it survives ASLR on the Debug build.
TARGETS = {
    "orig": (
        "IWD2.exe",
        {"setMenu": 0x686650, "getMenu": 0x686640, "render": 0x47F580,
         "outline": 0x5CECD0, "outlineSw": 0x799540, "outline3d": 0x7BD090},
    ),
    "ours": (
        "iwd2-re.exe",
        {"setMenu": 0x76BF40, "getMenu": 0x76BFB0, "render": 0x54A550,
         "outline": 0x626040, "outlineSw": 0x7F6640, "outline3d": 0x7E0F00},
    ),
}

JS_TEMPLATE = r"""
'use strict';

const MODULE = '%MODULE%';
const RVA = %RVA_JSON%;
// Frida 17 removed Module.findBaseAddress; use the Process module API.
const mod = Process.getModuleByName(MODULE);
const base = mod.base;
send({tag: 'INIT', module: MODULE, base: base.toString()});

function A(name) { return base.add(RVA[name]); }

let lastMenu = -1;
let lastGet  = -1;
let renderChest = 0, renderPile = 0;
const outlineColors = {};   // CInfinity::OutlinePoly colour -> count
let swCalls = 0, swFalse = 0; // CVidMode::OutlinePoly entries / FALSE returns
let d3Calls = 0;             // OutlinePoly3d entries

function colorName(c) {
  switch (c >>> 0) {
    case 0x0000FA00: return 'GREEN(alt-chest)';
    case 0x00A04020: return 'BLUE(hover)';
    case 0x000000FF: return 'RED(drawpoly)';
    default: return '0x' + (c >>> 0).toString(16);
  }
}

// CScreenWorld::SetSystemKeyMenu(this, BOOL bValue) __thiscall
Interceptor.attach(A('setMenu'), {
  onEnter(args) {
    const v = args[0].toInt32();
    if (v !== lastMenu) { lastMenu = v; send({tag: 'SETMENU', v: v}); }
  }
});

// CScreenWorld::GetMenuKey(this) -> BOOL __thiscall
Interceptor.attach(A('getMenu'), {
  onLeave(ret) {
    const r = ret.toInt32();
    if (r !== lastGet) { lastGet = r; send({tag: 'GETMENU', r: r}); }
  }
});

// CGameContainer::Render(this, pArea, pVidMode, nSurface) __thiscall
Interceptor.attach(A('render'), {
  onEnter(args) {
    const ctype = this.context.ecx.add(0x5CA).readU16();  // m_containerType
    if (ctype === 4) renderPile++; else renderChest++;
  }
});

// CInfinity::OutlinePoly(this, pPoly, nVertices, &rClip, rgbColor) __thiscall
Interceptor.attach(A('outline'), {
  onEnter(args) {
    const c = args[3].toUInt32() >>> 0;
    outlineColors[c] = (outlineColors[c] || 0) + 1;
  }
});

// CVidMode::OutlinePoly(this, ..) -> BOOL  (virtual slot 0xB0, the dispatcher)
Interceptor.attach(A('outlineSw'), {
  onEnter(args) { swCalls++; },
  onLeave(ret) { if (ret.toInt32() === 0) swFalse++; }
});

// CVidMode::OutlinePoly3d(this, ..)  (accelerated line drawer)
Interceptor.attach(A('outline3d'), {
  onEnter(args) { d3Calls++; }
});

send({tag: 'READY', hooks: Object.keys(RVA).length});

// Unconditional per-second heartbeat so "no output" cannot be ambiguous: a
// SNAP with all-zero counters proves the timer fires but the hooks do not.
setInterval(function () {
  const colors = {};
  for (const k in outlineColors) colors[colorName(k)] = outlineColors[k];
  send({tag: 'SNAP', menu: lastMenu, get: lastGet,
        renderChest: renderChest, renderPile: renderPile,
        outline: colors, swCalls: swCalls, swFalse: swFalse, d3Calls: d3Calls});
  renderChest = 0; renderPile = 0; swCalls = 0; swFalse = 0; d3Calls = 0;
  for (const k in outlineColors) delete outlineColors[k];
}, 1000);
"""


def build_js(target_key):
    module, addrs = TARGETS[target_key]
    # TARGETS holds absolute addresses at preferred base 0x400000 (Ghidra / .map).
    # Convert to true RVAs so base.add() works regardless of ASLR relocation.
    rva = {k: v - 0x400000 for k, v in addrs.items()}
    return (JS_TEMPLATE
            .replace("%MODULE%", module)
            .replace("%RVA_JSON%", json.dumps(rva)))


def main():
    attach = "--attach" in sys.argv
    target_key = "ours" if "--ours" in sys.argv else "orig"
    module, _ = TARGETS[target_key]
    exe = os.path.join(GAME_DIR, module)

    logf = open(LOG, "w", encoding="utf-8")

    def on_message(message, data):
        if message.get("type") == "send":
            line = json.dumps(message["payload"])
            print(line, flush=True)
            logf.write(line + "\n")
            logf.flush()
        else:
            print("[frida]", message, flush=True)

    print(f"[*] target={target_key} module={module} attach={attach}")
    if attach:
        session = frida.attach(module)
        pid = None
    else:
        pid = frida.spawn([exe], cwd=GAME_DIR)
        session = frida.attach(pid)

    script = session.create_script(build_js(target_key))
    script.on("message", on_message)
    script.load()

    if pid is not None:
        frida.resume(pid)

    print("[*] hooked. Load a save with a chest + loot pile, then HOLD Alt ~3s.")
    print(f"[*] log -> {LOG}   (Ctrl-C to stop)")
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n[*] stopping")
        try:
            session.detach()
        except Exception:
            pass
        logf.close()


if __name__ == "__main__":
    main()
