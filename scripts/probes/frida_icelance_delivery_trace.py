#!/usr/bin/env python3
"""Differential trace of ORIGINAL IWD2.exe Ice Lance (proj 0xFB) DELIVERY path.

Confirms our recovered IcewindCProjectileTravellingVFX launch+flight+delivery
matches the binary, field-for-field. Cast Ice Lance on the ORIGINAL under this
trace, cast the same on our build (Iwd2DebugLog TRAVEL* lines), diff the two.

IWD2.exe: ImageBase 0x400000, no ASLR -> runtime absolute addr == Ghidra addr.
Projectile is tracked by its `this` pointer captured at Fire, so AIUpdate /
OnArrival / DeliverEffects only log THAT projectile (ignores stray arrows etc.).

Hooks (all __thiscall, this = ecx, stack args a[0..]):
  0x5791D0  IcewindCProjectileTravellingVFX::Fire(pArea,src,tgt,tgtPt,nH,nType)
              onEnter  -> the launch inputs;  onLeave -> the computed launch state
  0x52B900  CProjectileTravelling::AIUpdate()   -> per-tick flight (pos/lifetime)
  0x529FB0  CProjectile::OnArrival()            -> arrival pos + target + payload
  0x52A1A0  CProjectile::DeliverEffects()       -> targetId + #effects delivered

Field offsets (CProjectile / CProjectileTravelling / CGameObject):
  pos.x +0x06  pos.y +0x0A  posZ +0x0E  projType +0x6E  velocity +0x70
  srcId +0x72  tgtId +0x76  effCount +0x92  deltaZ +0x16C  flightDist +0x170
  nTargetId +0x182  targetX +0xC8  targetY +0xCC  dirCount +0x1D8  facing +0x1DC
  distLifetime +0x29D  lifetime +0x29E

Run (original already loaded in VM session 1, party with Ice Lance ready):
  scripts/vm.sh frida scripts/frida_icelance_delivery_trace.py
Then cast Ice Lance on an enemy. Log VM-side:
  C:\\iwd2-re\\tmp_frida_icelance.jsonl   (pull with: scripts/vm.sh pull tmp_frida_icelance.jsonl)
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_icelance.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
function u16(p,o){return p.add(o).readU16();}
function s16(p,o){return (p.add(o).readU16()<<16)>>16;}
function u32(p,o){return p.add(o).readU32();}
function s32(p,o){return p.add(o).readS32();}

// live-tracked Ice Lance projectiles, keyed by `this` string
const tracked = {};

function launchState(p){
  return {
    pos:[s32(p,0x06),s32(p,0x0A)], posZ:s32(p,0x0E),
    projType:u16(p,0x6E), velocity:s16(p,0x70),
    srcId:s32(p,0x72), tgtId:s32(p,0x76), nTargetId:s32(p,0x182),
    effCount:s32(p,0x92),
    targetX:s32(p,0xC8), targetY:s32(p,0xCC),
    deltaZ:s16(p,0x16C), flightDist:s32(p,0x170),
    dirCount:s16(p,0x1D8), facing:s16(p,0x1DC),
    distLifetime:p.add(0x29D).readU8(), lifetime:s16(p,0x29E),
  };
}

// 0x5791D0  IcewindCProjectileTravellingVFX::Fire
Interceptor.attach(ptr(0x5791D0), {
  onEnter(a){
    this.thiz = this.context.ecx;
    send({t:'FIRE_in', proj:this.thiz.toString(),
          pArea:a[0].toString(), src:a[1].toInt32(), tgt:a[2].toInt32(),
          tgtPt:[a[3].toInt32(), a[4].toInt32()], nHeight:a[5].toInt32(),
          nType:(a[6].toInt32()<<16)>>16});
  },
  onLeave(ret){
    tracked[this.thiz.toString()] = 1;
    send({t:'FIRE_out', proj:this.thiz.toString(), st:launchState(this.thiz)});
  }
});

// 0x52B900  CProjectileTravelling::AIUpdate  (per-tick flight; throttle)
const tick = {};
Interceptor.attach(ptr(0x52B900), { onEnter(a){
  const p = this.context.ecx; const k = p.toString();
  if (!tracked[k]) return;
  tick[k] = (tick[k]||0) + 1;
  if (tick[k] > 60) return;           // cap ticks logged
  send({t:'TICK', proj:k, n:tick[k],
        pos:[s32(p,0x06),s32(p,0x0A)], posZ:s32(p,0x0E),
        lifetime:s16(p,0x29E), flightDist:s32(p,0x170),
        targetX:s32(p,0xC8), targetY:s32(p,0xCC)});
}});

// 0x529FB0  CProjectile::OnArrival
Interceptor.attach(ptr(0x529FB0), { onEnter(a){
  const p = this.context.ecx; const k = p.toString();
  if (!tracked[k]) return;
  send({t:'ARRIVE', proj:k,
        pos:[s32(p,0x06),s32(p,0x0A)], posZ:s32(p,0x0E),
        tgtId:s32(p,0x76), nTargetId:s32(p,0x182),
        callbackProj:s32(p,0x7A), effCount:s32(p,0x92)});
}});

// 0x52A1A0  CProjectile::DeliverEffects
Interceptor.attach(ptr(0x52A1A0), { onEnter(a){
  const p = this.context.ecx; const k = p.toString();
  if (!tracked[k]) { return; }
  send({t:'DELIVER', proj:k, srcId:s32(p,0x72), tgtId:s32(p,0x76),
        projType:u16(p,0x6E), effCount:s32(p,0x92)});
  delete tracked[k];                  // done with this projectile
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
