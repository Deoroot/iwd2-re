#!/usr/bin/env python3
"""Ground-truth trace for CProjectileTravelling::AIUpdate (0x52B900) on the
ORIGINAL IWD2.exe, to recover the flight logic with high confidence instead of
stubbing the low-confidence branches.

All travelling projectiles (arrows, magic missiles, darts, thrown weapons,
fireballs) share this AIUpdate (the leaves inherit vtable slot 3). So casting
Magic Missile (SPMAGMIS) exercises exactly the code we want.

Hooks (function ENTRIES only, absolute addresses, ImageBase 0x400000, no ASLR):
  0x52CA10  CProjectileMagicMissile ctor  -- a SPMAGMIS projectile is created
  0x52B900  CProjectileTravelling::AIUpdate -- per-tick flight  <== KEY
  0x52C050  CProjectileTravelling::Fire     -- launch (sets target/velocity?)

Per AIUpdate tick we read the fields the decompile uses, so we can confirm
which offset is target X/Y, velocity, lifetime, the pause-gate, and watch the
trajectory evolve:
  this+0x06 m_pos.x   this+0x0A m_pos.y   this+0x0E m_posZ
  this+0xC8 target?X  this+0xCC target?Y
  this+0x70 velocity? (s16)   this+0x29E lifetime? (s16)
  this+0x76 m_targetId   this+0x5C m_id
  this+0x170, this+0x16C(s16), this+0x16E(s16), this+0xE2, this+0x9A(s16)
  *this = vtable (SPMAGMIS=0x84DB54, ARARROW=0x84E58C, base Travelling=0x84D9C4)
  pause-gate: pInfGame = *(0x8CF6DC + 0x1C54); g+0x4B40, g+0x4B44

Run:  python scripts/frida_travelling_aiupdate_trace.py
Then: load a combat save, cast Magic Missile at an enemy ONCE.
Stop: Ctrl+C  (full JSON also written to tmp_aiupdate_trace.log)
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_intro_trace import GAME_DIR, ORIG_EXE  # noqa: E402

LOG_PATH = SCRIPT_DIR.parent / "tmp_aiupdate_trace.log"

JS = r"""
const AIUpdate = ptr(0x52b900);
const Fire     = ptr(0x52c050);
const Ctor     = ptr(0x52ca10);
const CHITIN   = ptr(0x8cf6dc);

function s16(p){ try { return (p.readU16() << 16) >> 16; } catch(e){ return null; } }
function s32(p){ try { return p.readS32(); } catch(e){ return null; } }
function u32(p){ try { return p.readU32() >>> 0; } catch(e){ return null; } }

function pauseGate() {
  try {
    const g = u32(CHITIN.add(0x1c54));
    if (!g) return null;
    const gp = ptr(g);
    return { g4b40: u32(gp.add(0x4b40)), g4b44: u32(gp.add(0x4b44)) };
  } catch(e){ return null; }
}

function snap(t) {
  return {
    this: t.toString(),
    vt: (u32(t) || 0).toString(16),
    px: s32(t.add(0x06)), py: s32(t.add(0x0a)), pz: s32(t.add(0x0e)),
    tx: s32(t.add(0xc8)), ty: s32(t.add(0xcc)),
    vel: s16(t.add(0x70)), life: s16(t.add(0x29e)),
    tid: s32(t.add(0x76)), id: s32(t.add(0x5c)),
    f170: s32(t.add(0x170)), f16c: s16(t.add(0x16c)), f16e: s16(t.add(0x16e)),
    e2: s32(t.add(0xe2)), x9a: s16(t.add(0x9a)),
    gate: pauseGate(),
  };
}

Interceptor.attach(Ctor, { onEnter(a){ const t=this.context.ecx;
  send(Object.assign({tag:'CTOR'}, {this:t.toString()})); }});

Interceptor.attach(Fire, { onEnter(a){ const t=this.context.ecx;
  send(Object.assign({tag:'FIRE', a0:a[0].toInt32(), a1:a[1].toInt32(),
    a2:a[2].toInt32(), a4:a[4].toInt32(), a5:(a[5].toInt32()<<16)>>16}, snap(t))); }});

Interceptor.attach(AIUpdate, { onEnter(a){ const t=this.context.ecx;
  send(Object.assign({tag:'AIU'}, snap(t))); }});

// Render (slot 19) -- draw config fields, once per frame.
const Render   = ptr(0x52b190);
const RFlags   = ptr(0x5297d0);   // slot 32 (+0x80) -- base blit flags
function u8(p){ try { return p.readU8(); } catch(e){ return null; } }
Interceptor.attach(Render, { onEnter(a){ const t=this.context.ecx;
  send({tag:'RENDER', this:t.toString(), vt:(u32(t)||0).toString(16),
    pArea:a[0].toInt32(), pVidMode:a[1].toInt32(), surf:a[2].toInt32(),
    px:s32(t.add(6)), py:s32(t.add(0xa)), pz:s32(t.add(0xe)),
    dir:s16(t.add(0x1da)),
    f1be:s32(t.add(0x1be)), f1c2:s32(t.add(0x1c2)), f1c6:s32(t.add(0x1c6)),
    f1d4:s32(t.add(0x1d4)), cnt:s16(t.add(0x1d8)), f29c:u8(t.add(0x29c)),
    f1de:s32(t.add(0x1de)), f1a:u8(t.add(0x1a)),
    cell192:u32(t.add(0x192)), cell196:u32(t.add(0x196)) });
}});
Interceptor.attach(RFlags, { onLeave(r){ send({tag:'RFLAGS', flags:(this.context.eax>>>0)}); }});
"""


def main() -> int:
    pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
    session = frida.attach(pid)
    log_f = open(LOG_PATH, "w", encoding="utf-8")
    # per-projectile previous snapshot, to show deltas
    last: dict[str, dict] = {}
    seen_render: dict[str, int] = {}

    def on_message(message, data):
        if message.get("type") != "send":
            if message.get("type") == "error":
                print("[JS ERROR]", message.get("description"))
            return
        p = message["payload"]
        log_f.write(json.dumps(p) + "\n")
        log_f.flush()
        tag = p.get("tag")
        if tag == "CTOR":
            print(f"[CTOR] travelling projectile created  this={p['this']}")
        elif tag == "FIRE":
            print(f"[FIRE] this={p['this']} vt={p['vt']} pos=({p['px']},{p['py']},{p['pz']}) "
                  f"target=({p['tx']},{p['ty']}) vel={p['vel']} life={p['life']} tid={p['tid']} "
                  f"args[a0={p['a0']} a1={p['a1']} a2={p['a2']} a4={p['a4']} a5={p['a5']}]")
        elif tag == "AIU":
            t = p["this"]
            prev = last.get(t)
            dpos = ""
            if prev:
                dpos = f" d=({p['px']-prev['px']},{p['py']-prev['py']},{p['pz']-prev['pz']})"
            last[t] = p
            print(f"[AIU] {t[-6:]} vt={p['vt']} pos=({p['px']},{p['py']},{p['pz']}){dpos} "
                  f"target=({p['tx']},{p['ty']}) vel={p['vel']} life={p['life']} "
                  f"tid={p['tid']} f170={p['f170']} f16c={p['f16c']} f16e={p['f16e']} "
                  f"e2={p['e2']} 9a={p['x9a']} gate={p['gate']}")
        elif tag == "RENDER":
            seen_render[p["this"]] = seen_render.get(p["this"], 0) + 1
            if seen_render[p["this"]] <= 3:  # first few frames per projectile
                print(f"[RENDER] {p['this'][-6:]} vt={p['vt']} pos=({p['px']},{p['py']},{p['pz']}) "
                      f"dir={p['dir']} 1be={p['f1be']} 1c2={p['f1c2']} 1c6={p['f1c6']} "
                      f"1d4={p['f1d4']} cnt={p['cnt']} 29c={p['f29c']} 1de={p['f1de']} "
                      f"1a={p['f1a']} cell192={p['cell192']} cell196={p['cell196']} "
                      f"args[pArea={p['pArea']} pVidMode={p['pVidMode']} surf={p['surf']}]")
        elif tag == "RFLAGS":
            print(f"[RFLAGS] base blit flags = 0x{p['flags']:x}")

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    print("[*] IWD2.exe spawned under Frida.")
    print("[*] 1) Load a combat save.  2) Cast Magic Missile at an enemy ONCE.")
    print("[*] Watch [FIRE] (launch: target/velocity) then [AIU] ticks (flight: pos delta,")
    print("[*] lifetime countdown). Full JSON -> tmp_aiupdate_trace.log")
    print("[*] Ctrl+C to stop.\n")
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n[*] stopping")
    finally:
        log_f.close()
        try:
            session.detach()
        except Exception:
            pass
        try:
            frida.kill(pid)
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
