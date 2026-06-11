#!/usr/bin/env python3
r"""Observe-only Frida trace of the ORIGINAL IWD2.exe Whirlwind projectile.

IWD2.exe has no ASLR (ImageBase 0x400000), so Ghidra addresses are absolute.
The script WAITS for IWD2.exe to appear (launch the game yourself on the VM
desktop), attaches, and logs the whole whirlwind lifecycle as JSON lines:
ctor/dtor, Fire (wander seed + tracker config), wander leg picks, leg
arrivals, sampled AIUpdate state (lifetime/leg budget/finishing), tracker
strikes and immunity verdicts.

Validates the recover at commits b8102ea7 (CProjectileWhirlwind) and
3a3a067d (IcewindCProjectileTargetMap) against the original binary.

Hooks (all IWD2.exe absolute, == Ghidra; function ENTRIES only):
  0x57F640  CProjectileWhirlwind::CProjectileWhirlwind()           __thiscall
  0x57F760  CProjectileWhirlwind::~CProjectileWhirlwind()          __thiscall
  0x57FF80  CProjectileWhirlwind::Fire(...)                        __thiscall
  0x57F8D0  CProjectileWhirlwind::AIUpdate()                       __thiscall
  0x580270  CProjectileWhirlwind::OnArrival()                      __thiscall
  0x5800E0  CProjectileWhirlwind::PickWanderPoint(POINT*, BOOL)    __thiscall
  0x55AB80  IcewindCProjectileTargetMap::DeliverStrike(LONG)       __thiscall
  0x536FC0  CProjectile::IsTargetImmune(CGameSprite*)              __thiscall

Usage (run ON THE VM, e.g. from the host):
  ssh win11vm 'cmd /c python C:\iwd2-re\scripts\frida_whirlwind_trace.py'

Then on the VM desktop: launch the ORIGINAL IWD2.exe, load the druid save,
cast Whirlwind (SPPR613). Log: C:\iwd2-re\tmp_frida_whirlwind.log.
Ctrl-C / kill to stop; the script exits on its own when the game quits.
"""
import frida
import json
import os
import sys
import time

LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_whirlwind.log")

JS = r"""
'use strict';

// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
const CTOR     = ptr(0x57F640);   // CProjectileWhirlwind::CProjectileWhirlwind
const DTOR     = ptr(0x57F760);   // CProjectileWhirlwind::~CProjectileWhirlwind
const FIRE     = ptr(0x57FF80);   // CProjectileWhirlwind::Fire
const AIUPDATE = ptr(0x57F8D0);   // CProjectileWhirlwind::AIUpdate
const ARRIVE   = ptr(0x580270);   // CProjectileWhirlwind::OnArrival
const PICK     = ptr(0x5800E0);   // CProjectileWhirlwind::PickWanderPoint
const STRIKE   = ptr(0x55AB80);   // IcewindCProjectileTargetMap::DeliverStrike
const IMMUNE   = ptr(0x536FC0);   // CProjectile::IsTargetImmune

// CGameObject / CProjectileWhirlwind binary offsets (src/CProjectile.h annotations).
const M_POS     = 0x06;    // CGameObject::m_pos (x@+0x06, y@+0x0a)
const LIFETIME  = 0x2AE;   // LONG m_nLifetime
const LEG       = 0x2B2;   // LONG m_nLegBudget
const TRACKER   = 0x2BA;   // IcewindCProjectileTargetMap m_targetMap (by value)
const FINISHING = 0x354;   // BYTE m_bFinishing
const SEED      = 0x356;   // LONG m_wanderSeed

// IcewindCProjectileTargetMap binary offsets (vptr@0, VC6 std::map 0x10 bytes @+0x08).
const T_OWNER    = 0x04;   // CGameObject* m_pOwner
const T_PERIOD   = 0x18;   // LONG m_servicePeriod
const T_INTERVAL = 0x20;   // LONG m_strikeInterval
const T_MAXPER   = 0x24;   // LONG m_maxStrikesPerTarget
const T_MAXTOT   = 0x28;   // LONG m_maxStrikesTotal
const T_TOTAL    = 0x2C;   // LONG m_nStrikes
const T_DONE     = 0x30;   // BOOLEAN m_bDone
const T_RANGE    = 0x32;   // WORD m_nRange

const whirls = {};   // this -> {tick, fin} (live CProjectileWhirlwind instances)

function pos(p) { return [p.add(M_POS).readS32(), p.add(M_POS + 4).readS32()]; }

Interceptor.attach(CTOR, {
  onEnter() {
    const t = this.context.ecx;
    whirls[t] = { tick: 0, fin: 0 };
    send({ tag: 'CTOR', whirl: t.toString() });
  }
});

Interceptor.attach(DTOR, {
  onEnter() {
    const t = this.context.ecx;
    if (whirls[t]) {
      send({ tag: 'DTOR', whirl: t.toString(), ticks: whirls[t].tick });
      delete whirls[t];
    }
  }
});

Interceptor.attach(FIRE, {
  onEnter() {
    const t = this.context.ecx;
    if (!whirls[t]) return;
    const tr = t.add(TRACKER);
    send({
      tag: 'FIRE',
      whirl: t.toString(),
      pos: pos(t),
      seed: t.add(SEED).readS32(),
      lifetime: t.add(LIFETIME).readS32(),
      leg: t.add(LEG).readS32(),
      cfg: {
        period: tr.add(T_PERIOD).readS32(),
        interval: tr.add(T_INTERVAL).readS32(),
        maxPerTarget: tr.add(T_MAXPER).readS32(),
        maxTotal: tr.add(T_MAXTOT).readS32(),
        range: tr.add(T_RANGE).readU16(),
      },
    });
  }
});

// Every tick; log on m_bFinishing change or 1-in-30 sample (~1/s).
Interceptor.attach(AIUPDATE, {
  onEnter() {
    const t = this.context.ecx;
    const w = whirls[t];
    if (!w) return;
    w.tick++;
    const fin = t.add(FINISHING).readU8();
    if (fin !== w.fin || w.tick % 30 === 1) {
      w.fin = fin;
      const tr = t.add(TRACKER);
      send({
        tag: 'TICK', n: w.tick, pos: pos(t),
        lifetime: t.add(LIFETIME).readS32(),
        leg: t.add(LEG).readS32(),
        finishing: fin,
        strikes: tr.add(T_TOTAL).readS32(),
        done: tr.add(T_DONE).readU8(),
      });
    }
  }
});

Interceptor.attach(ARRIVE, {
  onEnter() {
    const t = this.context.ecx;
    if (!whirls[t]) return;
    send({ tag: 'ARRIVE', pos: pos(t), leg: t.add(LEG).readS32() });
  }
});

// PickWanderPoint(POINT* pResult, BOOL bReverseFacing) -> pResult
Interceptor.attach(PICK, {
  onEnter(args) {
    const t = this.context.ecx;
    this.skip = !whirls[t];
    if (this.skip) return;
    this.out = args[0];
    this.rev = args[1].toInt32();
    this.from = pos(t);
  },
  onLeave() {
    if (this.skip) return;
    send({
      tag: 'WANDER', from: this.from,
      to: [this.out.readS32(), this.out.add(4).readS32()],
      reverse: this.rev,
    });
  }
});

// DeliverStrike(LONG targetId); ecx = the embedded tracker.
Interceptor.attach(STRIKE, {
  onEnter(args) {
    const tr = this.context.ecx;
    const owner = tr.add(T_OWNER).readPointer();
    if (!whirls[owner]) return;
    send({ tag: 'STRIKE', target: args[0].toInt32(), total: tr.add(T_TOTAL).readS32() });
  }
});

// IsTargetImmune(CGameSprite*) -> BOOL; only for our whirl instances.
Interceptor.attach(IMMUNE, {
  onEnter(args) {
    const t = this.context.ecx;
    this.skip = !whirls[t];
    if (this.skip) return;
    this.sprite = args[0];
  },
  onLeave(retval) {
    if (this.skip) return;
    send({ tag: 'IMMUNE_CHECK', sprite: this.sprite.toString(), immune: retval.toInt32() & 1 });
  }
});

send({ tag: 'ready' });
"""


def main():
    open(LOG, "w").close()

    def on_message(message, data):
        if message["type"] == "send":
            line = json.dumps(message["payload"])
        else:
            line = "ERROR " + json.dumps(message)
        print(line, flush=True)
        with open(LOG, "a") as f:
            f.write(line + "\n")

    print("[*] waiting for IWD2.exe (launch the ORIGINAL game on the VM desktop now)",
          flush=True)
    while True:
        try:
            session = frida.attach("IWD2.exe")
            break
        except frida.ProcessNotFoundError:
            time.sleep(2)

    def on_detached(reason, *args_):
        print(f"[*] detached: {reason}", flush=True)
        os._exit(0)

    session.on("detached", on_detached)
    print("[*] attached to IWD2.exe", flush=True)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()

    print("[*] hooks live. Load the druid save and cast Whirlwind (SPPR613).", flush=True)
    print(f"[*] logging to {LOG}", flush=True)
    sys.stdout.flush()
    # Stay alive so the hooks persist while the user plays.
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
