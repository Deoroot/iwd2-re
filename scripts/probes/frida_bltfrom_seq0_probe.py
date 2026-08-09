#!/usr/bin/env python3
"""ORIGINAL-side capture of CProjectileCallLightning::Render's per-cell blit geometry.

Decisive our-vs-original differential for the "beam bottom clipped at ground" report.
Our build's Iwd2DebugLog trace already proved the Render LOOP draws seq0 (the foot cell)
FULL within the viewport; the FX callees do not ground-clip.  Open question: does the
ORIGINAL beam extend LOWER below the foot than ours, or is the screenshot difference just
animation-frame timing?

Metric (camera-independent): per Render call, the cell with the MAX nBaseY is seq0 (the
foot).  Its extension below the foot anchor is  ext = cellH - refY  (== rDraw.bottom - foot
when posZ==0).  Our build: ext ranges 16..44, MAX 44.  Capture the original's max ext over
the full animation and compare.  original_max > 44  => real bottom-extent diff to localize.
original_max ~= 44 => identical render => the screenshot diff is timing, not a clip.

Mechanics (IWD2.exe, ImageBase 0x400000, no ASLR -> ptr(0xADDR) absolute == Ghidra addr):
  0x534DD0 CProjectileCallLightning::Render  __thiscall(this=ecx)
           this+6=m_pos.x  this+0xa=m_pos.y  this+0xe=m_posZ
  0x5CE0A0 CInfinity::FXBltFrom(nDestSurface, &rFXRect, x=nBaseX, y=nBaseY, refX, refY, flags)
           __thiscall: a[0]=nDestSurface a[1]=&rFXRect a[2]=nBaseX a[3]=nBaseY a[4]=refX a[5]=refY
           rFXRect == CRect(0,0,fsz.cx,fsz.cy) -> cellH = [rect+0xc]-[rect+4]
  Gate FXBltFrom to callers inside Render [0x534DD0,0x535200] so only the bolt's cells log
  (FXBltFrom blits every sprite otherwise).

Run (original IWD2.exe already launched in VM session 1):
  scripts/vm.sh frida scripts/frida_bltfrom_seq0_probe.py
Then load slot 3 and cast Sunscorch (SPPR113) on open ground -- same spot as our build.
Log VM-side: C:\\iwd2-re\\tmp_frida_bltfrom.jsonl
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_bltfrom.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
function s32(p,o){ try { return p.add(o).readS32(); } catch(e){ return 0x7fffffff; } }

let nR = 0, nB = 0, nT = 0, n3 = 0;
let inBolt = false;   // true only while inside the bolt's FXBltFrom (set below, read by FXBltToBack)

// 0x534DD0  CProjectileCallLightning::Render -- full entry state per render burst.
//   this+6=pos.x this+0xa=pos.y this+0xe=posZ this+0x290=renderFlag this+0xea=area(==pInfinity)
//   area+0x55c=nCurrentX +0x560=nCurrentY +0x514/518/51c/520=rViewPort LTRB +0x550=nAreaY
Interceptor.attach(ptr(0x534DD0), { onEnter(a){
  const t = this.context.ecx;
  const posx = s32(t,6), posy = s32(t,0xa), posZ = s32(t,0xe), rFlag = s32(t,0x290);
  let vp = null, areaY = null;
  try {
    const area = t.add(0xea).readPointer();
    vp = { curX:s32(area,0x55c), curY:s32(area,0x560),
           vpl:s32(area,0x514), vpt:s32(area,0x518), vpr:s32(area,0x51c), vpb:s32(area,0x520) };
    areaY = s32(area,0x550);
  } catch(e){}
  if (nR < 200) send({ t:'R', n:nR, posx, posy, posZ, rFlag, vp, areaY });
  nR++;
}});

// 0x5CE0A0  CInfinity::FXBltFrom(nDestSurface, &rFXRect, x=nBaseX, y=nBaseY, refX, refY, flags)
//   -- one call per drawn cell. Gate to Render's call site. [loop geometry]
Interceptor.attach(ptr(0x5CE0A0), {
  onEnter(a){
    const ret = this.returnAddress.toInt32() >>> 0;
    this.bolt = (ret >= 0x534DD0 && ret <= 0x535200);
    if (!this.bolt) return;
    inBolt = true;                          // gates FXBltToBack/RenderTexture to the bolt only
    const rect = a[1];
    const cellW = s32(rect,8) - s32(rect,0);
    const cellH = s32(rect,0xc) - s32(rect,4);
    const nBaseX = a[2].toInt32(), nBaseY = a[3].toInt32();
    const refX = a[4].toInt32(), refY = a[5].toInt32();
    const ext = cellH - refY;               // extension below the foot anchor (posZ==0)
    const cellTop = nBaseY - refY, cellBottom = cellTop + cellH;
    if (nB < 800) send({ t:'B', n:nB, nBaseX, nBaseY, refX, refY, cellW, cellH, ext, cellTop, cellBottom });
    nB++;
  },
  onLeave(r){ if (this.bolt) inBolt = false; }
});

// 0x79CC90  CVidInf::FXBltToBack(&rFXRect, x, y, refX, refY, &rClip, flags) -- final SCREEN blit.
//   Gate to FXBltFrom [0x5CE0A0,0x5CE130). Captures the screen-space inputs the real DDraw clip
//   uses; offline replicate 1108-1129 to see if the bottom clamps.  [screen truth]
Interceptor.attach(ptr(0x79CC90), { onEnter(a){
  if (!inBolt) return;
  const rect = a[0];
  const cellW = s32(rect,8) - s32(rect,0);
  const cellH = s32(rect,0xc) - s32(rect,4);
  const x = a[1].toInt32(), y = a[2].toInt32(), refX = a[3].toInt32(), refY = a[4].toInt32();
  const rc = a[5];
  const clip = { l:s32(rc,0), t:s32(rc,4), r:s32(rc,8), b:s32(rc,0xc) };
  const flags = a[6].toInt32() >>> 0;
  const v2 = (flags & 0x20) ? (cellH - refY) : refY;
  const ptY = y - v2;                       // screen cell-top
  const scrBottom = ptY + cellH;            // screen cell-bottom (pre-clip)
  const clipBottom = (cellH + ptY - 1) >= clip.b;   // src 1114/1126: bottom would clamp
  const clipTop = ptY < clip.t;
  if (nT < 800) send({ t:'T', n:nT, x, y, refX, refY, cellW, cellH, clip, flags:'0x'+flags.toString(16),
    ptY, scrBottom, clipBottom, clipTop });
  nT++;
}});

// 0x7C4240  CVidCell::RenderTexture -- the 3D path inside FXBltToBack. If this fires during a
//   bolt blit, the original runs 3D-ACCELERATED (RenderTexture clip), not the software DDraw path.
Interceptor.attach(ptr(0x7C4240), { onEnter(a){
  if (!inBolt) return;
  if (n3 < 20) send({ t:'M3D', n:n3, note:'RenderTexture fired -> bolt uses 3D path' });
  n3++;
}});

send({ t:'READY', note:'load slot 3, cast Sunscorch on open ground (same spot as our build)' });
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
