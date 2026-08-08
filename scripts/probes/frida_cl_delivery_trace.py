#!/usr/bin/env python3
"""Differential trace of ORIGINAL IWD2.exe Call Lightning (SPPR302) DELIVERY path.

Goal: find HOW opcode 449 (Static Charge, res EffCL) gets applied to a sprite and
whether its periodic engine ticks -- our build never resolves it (ResolveEffect(449)
== 0 in iwd2-re-debug.log). The plain CProjectile (missileType=1) Fire is an empty
stub (faithful, 0x78E740), so the effect rides a projectile that never arrives.

IWD2.exe: ImageBase 0x400000, no ASLR -> runtime absolute addr == Ghidra addr.
Backtraces are logged as absolute (== Ghidra) addresses; resolve with sym.py addr2fn.

Hooks (all __thiscall unless noted; this = ecx, stack args = a[0..]):
  0x49EC50  IcewindCGameEffectCallLightning::ctor   -> who BUILDS the 449 effect
  0x4A3030  CGameEffect::ResolveEffect(pSprite)     -> eid=[ecx+0xC]; filter 449/288
                                                        + backtrace = the DELIVERY path
  0x564F80  IcewindCGameEffectCallLightning::ApplyEffect(pSprite) -> engine tick + cadence
  0x52A1A0  CProjectile::DeliverEffects()           -> projectile delivery
  0x529FB0  CProjectile::OnArrival()                -> did a projectile arrive
  0x5D1340  CInfinity::CallLightning(x,y)  __thiscall(x@a0,y@a1) -> the sky-strike render

Run (original already loaded in VM session 1):
  scripts/vm.sh frida scripts/frida_cl_delivery_trace.py
Then in-game cast Call Lightning on an enemy and wait a few rounds.
Log VM-side: C:\\iwd2-re\\tmp_frida_cl_delivery.jsonl  (pull with vm.sh pull).
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_cl_delivery.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
const BASE = ptr(0x400000);
function u32(p,o){return p.add(o).readU32();}
function s32(p,o){return p.add(o).readS32();}
function id(p){ if(p.isNull()) return -1; try { return s32(p,0x5C); } catch(e){ return -2; } }
// backtrace as absolute (== Ghidra) addresses, top N frames
function bt(ctx,n){
  try {
    return Thread.backtrace(ctx, Backtracer.ACCURATE)
      .slice(0,n).map(a => a.toString());
  } catch(e){ return ['ERR:'+e]; }
}

// 0x49EC50  IcewindCGameEffectCallLightning::ctor  (ecx=this)
Interceptor.attach(ptr(0x49EC50), { onEnter(a){
  send({t:'CTOR', this:this.context.ecx.toString(), bt:bt(this.context,10)});
}});

// 0x4A3030  CGameEffect::ResolveEffect(pSprite)  eid=[ecx+0xC]
let resolveBtCount = 0;
Interceptor.attach(ptr(0x4A3030), { onEnter(a){
  const e = this.context.ecx;
  let eid; try { eid = u32(e,0x0C); } catch(_){ return; }
  if (eid !== 449 && eid !== 288) return;
  const o = {t:'RESOLVE', eid:eid, eff:e.toString(),
             srcId:s32(e,0x10C), spriteId:id(a[0]),
             durType:u32(e,0xA0)};   // m_durationType (verify offset post-hoc)
  if (eid === 449 && resolveBtCount < 6) { o.bt = bt(this.context,12); resolveBtCount++; }
  send(o);
}});

// 0x564F80  IcewindCGameEffectCallLightning::ApplyEffect(pSprite)  (ecx=this)
let applyCount = 0;
Interceptor.attach(ptr(0x564F80), { onEnter(a){
  const e = this.context.ecx;
  const o = {t:'APPLY', eff:e.toString(), spriteId:id(a[0]), charges:s32(e,0x18)};
  if (applyCount < 4) { o.bt = bt(this.context,12); applyCount++; }
  send(o);
}});

// 0x52A1A0  CProjectile::DeliverEffects()  (ecx=proj)
let deliverCount = 0;
Interceptor.attach(ptr(0x52A1A0), { onEnter(a){
  const o = {t:'DELIVER', proj:this.context.ecx.toString()};
  if (deliverCount < 10) { o.bt = bt(this.context,8); deliverCount++; }
  send(o);
}});

// 0x529FB0  CProjectile::OnArrival()  (ecx=proj)
let arriveCount = 0;
Interceptor.attach(ptr(0x529FB0), { onEnter(a){
  const o = {t:'ARRIVE', proj:this.context.ecx.toString()};
  if (arriveCount < 10) { o.bt = bt(this.context,8); arriveCount++; }
  send(o);
}});

// 0x5D1340  CInfinity::CallLightning(INT x, INT y)  __thiscall (x@a0, y@a1)
Interceptor.attach(ptr(0x5D1340), { onEnter(a){
  send({t:'BOLT', x:a[0].toInt32(), y:a[1].toInt32()});
}});

send({t:'READY'});
""";

def main():
    out = open(OUT, "w", buffering=1)
    def on_message(msg, data):
        if msg.get("type") == "send":
            out.write(json.dumps(msg["payload"]) + "\n")
        else:
            out.write(json.dumps({"t": "ERR", "msg": str(msg)}) + "\n")
        out.flush(); os.fsync(out.fileno())
    for _ in range(120):
        try:
            session = frida.attach(PROC); break
        except frida.ProcessNotFoundError:
            time.sleep(1)
    else:
        out.write('{"t":"ERR","msg":"IWD2.exe not found"}\n'); out.flush(); return
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    while True:
        time.sleep(0.5)

if __name__ == "__main__":
    main()
