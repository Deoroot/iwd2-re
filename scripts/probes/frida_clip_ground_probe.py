#!/usr/bin/env python3
"""Confirm the Call-Lightning / Sunscorch GROUND-clip on the ORIGINAL IWD2.exe.

Hypothesis: CInfinity::FXRenderClippingPolys (0x5CE350) builds its clip rect with nPosZ
  CRect rClip(left, top + nPosZ, right, bottom + nPosZ)   (src/CInfinity.cpp:1845)
while the fill point uses nPosY (1846).  For ground effects nPosZ==0 -> rClip not shifted
to the foot Y -> the beam's bottom is clipped at the GC top, not the ground.  The canopy
notch fix (50c5d9d8) changed nPosZ->nPosY in the height TEST (1805/1809) but maybe not here.

This traces the ORIGINAL only and is decisive: read the rClip actually passed to
FillPoly/FillConvexPoly and compare rClip.top - rGCBounds.top against nPosY vs nPosZ.
If it equals nPosY -> our nPosZ at 1845 is the bug (1-line fix nPosZ->nPosY).
If it equals nPosZ -> our code is faithful, look elsewhere.

IWD2.exe: ImageBase 0x400000, no ASLR -> ptr(0xADDR) absolute == Ghidra addr.
FXRenderClippingPolys __thiscall(this=ecx, a0=nPosX, a1=nPosY, a2=nPosZ, a3=&ptRef, a4=&rGCBounds).
Fill{Convex}Poly(__thiscall this=poly): from FX asm, rClip ptr @ [esp+0xc], ptFill ptr @ [esp+0x18].

Run (original already loaded in VM session 1):
  scripts/vm.sh frida scripts/frida_clip_ground_probe.py
Then cast Call Lightning on an enemy (a couple of bolts).
Log VM-side: C:\\iwd2-re\\tmp_frida_clip_ground.jsonl
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_clip_ground.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
function s32(p,o){ try { return p.add(o).readS32(); } catch(e){ return 0x7fffffff; } }
function rectOf(p){ try { return {l:s32(p,0),t:s32(p,4),r:s32(p,8),b:s32(p,0xc)}; } catch(e){ return null; } }

let fx = null;       // last FXRenderClippingPolys inputs
let nFx = 0, nFill = 0;

// 0x5CE350  CInfinity::FXRenderClippingPolys(nPosX, nPosY, nPosZ, &ptRef, &rGCBounds, ...)
// GATE to CProjectileCallLightning::Render (0x534DD0) so only the bolt's clip is traced
// (FX has 28 callers -- ground anims everywhere flood it otherwise).
Interceptor.attach(ptr(0x5CE350), { onEnter(a){
  const ret = this.returnAddress.toInt32() >>> 0;
  if (ret < 0x534DD0 || ret > 0x535200) { fx = null; return; }
  const nPosX = a[0].toInt32(), nPosY = a[1].toInt32(), nPosZ = a[2].toInt32();
  const gc = rectOf(a[4]);
  let ref = null; try { ref = {x:s32(a[3],0), y:s32(a[3],4)}; } catch(e){}
  fx = { nPosX, nPosY, nPosZ, gc, ref };
  if (nFx < 40) send({ t:'FX', n:nFx, nPosX, nPosY, nPosZ, gc, ref });
  nFx++;
}});

// rClip @ [esp+0xc], ptFill @ [esp+0x18] (both pointers) for both fill primitives.
function onFill(name, sp){
  if (!fx) return;
  let rClip = null, ptFill = null;
  try { rClip = rectOf(ptr(sp.add(0xc).readU32())); } catch(e){}
  try { const pf = ptr(sp.add(0x18).readU32()); ptFill = {x:s32(pf,0), y:s32(pf,4)}; } catch(e){}
  if (nFill < 60 && rClip) {
    const dTop = rClip.t - (fx.gc ? fx.gc.t : 0);
    const dBot = rClip.b - (fx.gc ? fx.gc.b : 0);
    send({ t:'FILL', n:nFill, via:name,
      rClip, ptFill,
      gcTop: fx.gc ? fx.gc.t : null, gcBot: fx.gc ? fx.gc.b : null,
      nPosY: fx.nPosY, nPosZ: fx.nPosZ,
      dTop, dBot,
      topEqY: dTop === fx.nPosY, topEqZ: dTop === fx.nPosZ,
      // raw stack window for sanity if offsets are off
      raw: [s32(sp,4),s32(sp,8),s32(sp,0xc),s32(sp,0x10),s32(sp,0x14),s32(sp,0x18)] });
  }
  nFill++;
}

// 0x7c0f40 CVidPoly::FillConvexPoly ; 0x7c13a0 CVidPoly::FillPoly -- gate to the FX caller
Interceptor.attach(ptr(0x7c0f40), { onEnter(a){
  const r = this.returnAddress.toInt32() >>> 0;
  if (r >= 0x5ce350 && r <= 0x5ce8a0) onFill('FillConvexPoly', this.context.esp);
}});
Interceptor.attach(ptr(0x7c13a0), { onEnter(a){
  const r = this.returnAddress.toInt32() >>> 0;
  if (r >= 0x5ce350 && r <= 0x5ce8a0) onFill('FillPoly', this.context.esp);
}});

send({ t:'READY', note:'cast Call Lightning on an enemy' });
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
