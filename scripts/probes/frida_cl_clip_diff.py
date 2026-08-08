#!/usr/bin/env python3
"""Trace ORIGINAL IWD2.exe Call Lightning canopy occlusion (FXRenderClippingPolys).

Goal: settle whether the bolt-vs-canopy depth in our build matches the original.
Our CProjectileCallLightning::Render (0x534DD0) is byte-faithful and m_posZ==0
everywhere, so static analysis says occlusion is identical.  This trace captures
the ORIGINAL's actual per-strike clip-call args so we can diff them against our
build (our build logs the same via Iwd2DebugLog; Frida addrs only fit the original).

Per bolt it logs:
  - RENDER: a fresh strike id + the strike foot (m_pos.x/y, m_posZ) so you can
    tell me WHICH bolt was the one that visually went behind the canopy.
  - CLIP : the 7 FXRenderClippingPolys args from inside Render
    (nPosX, nPosY, nPosZ, ptRef, rGCBounds[l,t,r,b], bool, flags) per cycle.

IWD2.exe: ImageBase 0x400000, no ASLR -> runtime absolute addr == Ghidra addr.

Run (ORIGINAL IWD2.exe loaded in VM session 1, AR2000, Call Lightning memorised):
  scripts/vm.sh frida scripts/frida_cl_clip_diff.py
Then cast Call Lightning; enemies move, so fire until one strikes the SAME enemy
under the canopy as the screenshot, then tell me which strike id that was.
Log VM-side: C:\\iwd2-re\\tmp_frida_cl_clip.jsonl   (pull with vm.sh pull).
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_cl_clip.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
const R0 = ptr(0x534dd0);   // CProjectileCallLightning::Render (slot 19 override)
const R1 = ptr(0x5350f1);   // its exit -> caller-range filter for shared callees
function inRender(ret){ return ret.compare(R0) >= 0 && ret.compare(R1) <= 0; }
function s32(p,o){ try { return p.add(o).readS32(); } catch(e){ return 0x7fffffff; } }
function rd(p){ try { return p.readS32(); } catch(e){ return 0x7fffffff; } }

let boltId = 0;
let cur = -1;
let lastThis = '';
let logClip = false;

// 0x534DD0  CProjectileCallLightning::Render(pArea, pVidMode, nSurface)  ecx=this
// Render fires once PER FRAME per live bolt -> bump the bolt counter only when
// the `this` pointer changes (a new bolt), so the id maps to "my Nth cast".
// onLeave (function-start hook, safe) gates clip logging to the bolt's FIRST
// frame only.  We do NOT attach mid-function (R1 0x5350f1 = a branch target;
// Frida relocating it corrupted control flow and crashed the strike path).
Interceptor.attach(R0, {
  onEnter(a){
    const t = this.context.ecx;
    const ts = t.toString();
    if (ts !== lastThis) {
      boltId++; lastThis = ts; logClip = true;
      send({t:'RENDER', id:boltId, this:ts,
            posX:s32(t,0x6), posY:s32(t,0xa), posZ:s32(t,0xe)});
    }
    cur = boltId;
  },
  onLeave(r){ logClip = false; }
});

// 0x5CE350  CInfinity::FXRenderClippingPolys(nPosX,nPosY,nPosZ,&ptRef,&rGC,b,flags)
// __thiscall: ecx=infinity, a[0..6] = stack args. Filter to Render's call site.
let inClip = false;
Interceptor.attach(ptr(0x5CE350), {
  onEnter(a){
    if (!logClip || !inRender(this.returnAddress)) return;
    inClip = true;
    const pt = a[3];   // const CPoint&
    const gc = a[4];   // const CRect&
    send({t:'CLIP', id:cur,
          nPosX:a[0].toInt32(), nPosY:a[1].toInt32(), nPosZ:a[2].toInt32(),
          refX:rd(pt), refY:rd(pt.add(4)),
          gcL:rd(gc), gcT:rd(gc.add(4)), gcR:rd(gc.add(8)), gcB:rd(gc.add(12)),
          b:a[5].toInt32(), flags:a[6].toUInt32()});
  },
  onLeave(r){ inClip = false; }
});

// 0x7C13A0 CVidPoly::FillPoly / 0x7C0F40 CVidPoly::FillConvexPoly --- the actual
// cover-poly draw.  Each call while inside the bolt's first-frame Render means the
// original IS occluding the bolt with a cover poly (the "notch"); ZERO calls means
// the original never occludes and ours has spurious fills.  rClipRect = a[2].
function fillHook(name){ return { onEnter(a){
  if (!inClip) return;
  const rc = a[2];
  send({t:'FILL', id:cur, fn:name,
        clL:rd(rc), clT:rd(rc.add(4)), clR:rd(rc.add(8)), clB:rd(rc.add(12))});
}};}
Interceptor.attach(ptr(0x7C13A0), fillHook('FillPoly'));
Interceptor.attach(ptr(0x7C0F40), fillHook('FillConvexPoly'));

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
