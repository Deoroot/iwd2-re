#!/usr/bin/env python3
"""Locate opcode-402 (IcewindCGameEffectApplyEffectsList::ApplyEffect, 0x5675E0)
field offsets on the ORIGINAL IWD2.exe, by casting Sunscorch (SPPR113).

WHY: recovering 0x5675E0 needs two offsets the static read can't pin:
  - the Type/spell-protection selector (decompile+asm agree: this+0x1c) -- CONFIRM live.
  - the SUB-SPELL RESREF passed to BuildSubSpellEffects: decompile says this+0xc4,
    raw asm says this+0x2c, our header has m_res@0x28 -- all three disagree, and
    this+0xc4 is also read as the BYTE level. Wrong guess = wrong sub-spell applied
    (Sunscorch silently broken, parity stays GREEN). Runtime = final word.

HOW: Sunscorch (SPPR113) chains 4x op402, Type 0x49/0x4b/0x1f/0x02, res EffS1/EffS2/EffS3.
  1. 0x5675E0 entry: scan this[0..0x140] for an ascii "Eff..." -> the resref offset(s);
     also dump durationType(0x1c), the 3 candidate resref fields, casterLevel, byte[0xc4].
  2. 0x586220 BuildSubSpellEffects entry, GATED to the op402 caller: read the real
     spellRes pointer arg -> offset = spellRes - thisOf402 (the decisive answer) + its
     8-char string + the level arg.

IWD2.exe: ImageBase 0x400000, no ASLR -> ptr(0xADDR) absolute == Ghidra addr.

Run (original already loaded in VM session 1):
  scripts/vm.sh frida scripts/frida_sunscorch_op402_probe.py
Then in-game cast Sunscorch (SPPR113) on an enemy creature. A few casts is fine.
Log VM-side: C:\\iwd2-re\\tmp_frida_sunscorch_op402.jsonl  (pull with vm.sh pull).
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_sunscorch_op402.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
function u32(p,o){ try { return p.add(o).readU32(); } catch(e){ return -1; } }
function s32(p,o){ try { return p.add(o).readS32(); } catch(e){ return -1; } }
function u8(p,o){ try { return p.add(o).readU8(); } catch(e){ return -1; } }
// fixed 8-byte resref (NUL-trimmed, non-printables shown)
function rr(p,o){
  try {
    let s='';
    for (let i=0;i<8;i++){ const c=p.add(o+i).readU8(); if(c===0) break;
      s += (c>=32 && c<127) ? String.fromCharCode(c) : '.'; }
    return s;
  } catch(e){ return '?'; }
}
function hex(p,o,n){
  try { let s=''; for(let i=0;i<n;i++){ const b=p.add(o+i).readU8();
    s += ('0'+b.toString(16)).slice(-2); } return s; } catch(e){ return '?'; }
}

let lastThis = null;
let n402 = 0;

// 0x5675E0  op402 ApplyEffect(pSprite)  (ecx=this, a[0]=target sprite)
Interceptor.attach(ptr(0x5675E0), { onEnter(a){
  const e = this.context.ecx;
  lastThis = e;
  // scan whole effect object for an "Eff" ascii run -> resref field offset(s)
  let hits = [];
  for (let off=0; off<0x140; off++){
    if (u8(e,off)===0x45 /*E*/ && u8(e,off+1)===0x66 /*f*/ && u8(e,off+2)===0x66 /*f*/){
      hits.push({o:'0x'+off.toString(16), s:rr(e,off)});
    }
  }
  if (n402 < 50){
    send({t:'OP402', n:n402,
      eff:e.toString(),
      effectID:u32(e,0x08),
      durationType_1c:u32(e,0x1c),   // suspected Type selector
      dwFlags_18:u32(e,0x18),
      casterLevel_c0:u32(e,0xc0),
      byte_c4:u8(e,0xc4),
      res_28:rr(e,0x28), res_2c:rr(e,0x2c), res_c4:rr(e,0xc4),
      effHits:hits,
      tgtId:s32(a[0],0x5C),
      hex_14_34:hex(e,0x14,0x20),
      hex_c0_cc:hex(e,0xc0,0x0c)});
  }
  n402++;
}});

// 0x586220  BuildSubSpellEffects(resultBuf, caster, spellRes, level)  __cdecl static
// GATE to the op402 caller (0x5675E0 body) so we only see Sunscorch's apply.
let nBuild = 0;
Interceptor.attach(ptr(0x586220), { onEnter(a){
  const ret = this.returnAddress;
  const r = ret.toInt32() >>> 0;
  if (r < 0x567000 || r > 0x569000) return;   // not the op402 call site
  const sp = this.context.esp;
  const resultBuf = u32(sp,0x04);
  const caster    = u32(sp,0x08);
  const spellRes  = ptr(u32(sp,0x0c) >>> 0);
  const level     = u8(sp,0x10);
  let offFromThis = null;
  if (lastThis) { try { offFromThis = '0x'+(spellRes.sub(lastThis)).toInt32().toString(16); } catch(_){} }
  if (nBuild < 50){
    send({t:'BUILD', n:nBuild, ret:'0x'+r.toString(16),
      caster:'0x'+caster.toString(16),
      spellRes:spellRes.toString(),
      spellResStr:rr(spellRes,0),
      offFrom402This:offFromThis,
      level:level});
  }
  nBuild++;
}});

send({t:'READY', note:'cast Sunscorch SPPR113 on an enemy'});
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
