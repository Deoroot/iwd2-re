#!/usr/bin/env python3
"""Wide Frida trace of original IWD2.exe Call Lightning — extended hooks."""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_calllightning.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
function u32(p,o){return p.add(o).readU32();}
function s32(p,o){return p.add(o).readS32();}

// DecodeProjectile @ 0x51EAF0
Interceptor.attach(ptr(0x51EAF0), {
  onEnter(a){send({t:'DP',type:a[0].toInt32(),idx:(a[0].toInt32()>0x1000?a[0].toInt32()-0x1001:-1)});}
});

// DecodeSpellHitProjectile @ 0x560310
Interceptor.attach(ptr(0x560310), {
  onEnter(a){send({t:'DSH',idx:a[0].toInt32()});}
});

// CInfinity::CallLightning @ 0x5D1340
Interceptor.attach(ptr(0x5D1340), {
  onEnter(a){send({t:'CL',x:a[0].toInt32(),y:a[1].toInt32()});}
});

// CGameEffect::FireSpell @ 0x4A3FF0  (this=ecx)
Interceptor.attach(ptr(0x4A3FF0), {
  onEnter(a){var e=this.context.ecx; send({t:'FS',eid:u32(e,0x0C),flg:u32(e,0x98)});}
});

// ExecuteAction: hook at the CallLightning call site (0x44E7EB)
// This is inside case 0x1A — hooks the actual CInfinity::CallLightning call
// We already hook CallLightning above.

// CGameEffect::ApplyEffect base @ 0x48C3D0 (CGameEffectBase::ApplyEffect? Let me check the vtable)
// Actually, hook CGameEffectVisualSpellHitIWD::ApplyEffect @ its code addr
// Not sure of address. Use DecodeSpellHitProjectile instead.

// CGameSprite::Spell — where DecodeProjectile is called
// At 0x742530 there's a DecodeProjectile call. Hook just that instruction?
// Already covered by DecodeProjectile hook.

send({t:'READY'});
""";

def main():
    out = open(OUT, "w", buffering=1)
    def on_message(msg, data):
        if msg.get("type") == "send":
            out.write(json.dumps(msg["payload"]) + "\n")
        else:
            out.write(json.dumps({"t":"ERR","msg":str(msg)}) + "\n")
        out.flush(); os.fsync(out.fileno())
    for _ in range(120):
        try: session = frida.attach(PROC); break
        except frida.ProcessNotFoundError: time.sleep(1)
    else: out.write('{"t":"ERR","msg":"not found"}\n'); return
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    while True: time.sleep(0.5)

if __name__ == "__main__": main()
