#!/usr/bin/env python3
"""Trace ORIGINAL IWD2.exe Call Lightning BAM render (CALLLIH, 3 cycles stacked).

Hypothesis: CProjectileCallLightning overrides Render (vtable slot 19 = 0x534dd0)
to draw the CALLLIH BAM as 3 vertically-stacked cycles (cycle 0 bottom, 1 middle,
2 top). Our build never recovered that override, so it inherits the single-cell
CProjectileBAM::Render (0x57CFB0) and draws only one cycle -> the "zoomed" look.

This trace CONFIRMS, per render frame:
  - that 0x534dd0 fires (the override runs in the original),
  - how many CVidCell::SequenceSet calls it makes (== cycle count, expect 3) and
    which sequence indices,
  - the per-cycle FXRender draw args (expect the Y to step by ~one frame height
    each cycle == the vertical stacking).

IWD2.exe: ImageBase 0x400000, no ASLR -> runtime absolute addr == Ghidra addr.

Run (ORIGINAL IWD2.exe already loaded in VM session 1, outdoor save, Call
Lightning memorised, an enemy in reach):
  scripts/vm.sh frida scripts/frida_calllight_render_trace.py
Then cast Call Lightning and let one bolt render.
Log VM-side: C:\\iwd2-re\\tmp_frida_cl_render.jsonl  (pull with vm.sh pull).
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_cl_render.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
const R0 = ptr(0x534dd0);   // CProjectileCallLightning::Render (slot 19 override)
const R1 = ptr(0x5350f1);   // its exit -> caller-range filter for shared callees
function inRender(ret){ return ret.compare(R0) >= 0 && ret.compare(R1) <= 0; }
function s32(p,o){ try { return p.add(o).readS32(); } catch(e){ return 0x7fffffff; } }

let renderN = 0;

// 0x534DD0  CProjectileCallLightning::Render(pArea, pVidMode, nSurface)  ecx=this
Interceptor.attach(R0, { onEnter(a){
  renderN++;
  const t = this.context.ecx;
  send({t:'RENDER', n:renderN, this:t.toString(),
        posX:s32(t,0x6), posY:s32(t,0xa), posZ:s32(t,0xe),
        vfxFlag:s32(t,0x290)});
}});

// 0x7AE290  CVidCell::SequenceSet(seq)  ecx=vidcell, a[0]=sequence index
// Filter to calls coming from inside Render -> counts cycles + their indices.
Interceptor.attach(ptr(0x7AE290), { onEnter(a){
  if (!inRender(this.returnAddress)) return;
  send({t:'SEQSET', seq:a[0].toInt32(), vidcell:this.context.ecx.toString()});
}});

// 0x5CE280  CInfinity::FXRender(...)  ecx=infinity
// Filter to Render's call site -> per-cycle draw args (one is the stacked Y).
Interceptor.attach(ptr(0x5CE280), { onEnter(a){
  if (!inRender(this.returnAddress)) return;
  send({t:'FXRENDER', a0:a[0].toInt32(), a1:a[1].toInt32(),
        a2:a[2].toInt32(), a3:a[3].toInt32(), a4:a[4].toInt32()});
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
