#!/usr/bin/env python3
"""Frida trace of ORIGINAL IWD2.exe CGameEffect::CheckAdd (0x4A3310) tail.

Pins the main-admission tail (0x4A3BD3): the mislabelled lower_bound at
0x4C5AE0 (find/dedup on the target effect list), the m_saveMod counter-effect
fold, and which branches fire (GetSpellAbilityValue / CreateEffectImmunitySpell
/ CImmunitiesEffect::OnList). CheckAdd is UNWIRED in our build, so this can only
be observed on the original.

Member offsets (CGameEffect, vptr+4 shifted): m_effectID +0xC, m_savingThrow
+0x3C, m_saveMod +0x40, m_sourceRes +0x90 (8-byte resref), m_sourceID +0x10C.
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_checkadd.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
function u32(p,o){return p.add(o).readU32();}
function s32(p,o){return p.add(o).readS32();}
function res(p,o){                       // 8-byte resref -> ascii
  var s=''; for(var i=0;i<8;i++){var c=p.add(o+i).readU8(); if(c===0)break; s+=String.fromCharCode(c);} return s;
}

var g_seq = 0;          // sequence id per CheckAdd invocation
var g_in  = 0;          // >0 while inside a CheckAdd (gate the lower_bound log)

// CGameEffect::CheckAdd @ 0x4A3310  (this=ecx=the incoming effect, a[0]=pSprite)
Interceptor.attach(ptr(0x4A3310), {
  onEnter(a){
    var e=this.context.ecx;
    this.e=e; this.id=++g_seq; g_in++;
    this.savemod_in=s32(e,0x40);
    send({t:'CA', id:this.id, e:e.toInt32()>>>0, spr:a[0].toInt32()>>>0,
          eid:u32(e,0x0C), sav:s32(e,0x3C), smod:this.savemod_in,
          srcid:s32(e,0x10C), sres:res(e,0x90)});
  },
  onLeave(r){
    g_in--;
    var out=s32(this.e,0x40);
    send({t:'CA_ret', id:this.id, rc:r.toInt32(),
          smod_out:out, smod_delta:(out-this.savemod_in)});
  }
});

// mislabelled "CGameEffect::CheckAdd" @ 0x4C5AE0 -- the inlined lower_bound on
// the target effect list. Log ecx + the two stack dwords + return (convention
// unknown; capture all). Gated to CheckAdd so we skip the FUN_00764880 caller.
Interceptor.attach(ptr(0x4C5AE0), {
  onEnter(a){
    if(g_in<=0) return;
    var sp=this.context.esp;
    this.hit=1;
    this.a0=sp.add(4).readU32(); this.a1=sp.add(8).readU32();
    var key=-1; try{key=ptr(this.a1).readS32();}catch(e){}
    send({t:'LB', ecx:this.context.ecx.toInt32()>>>0, a0:this.a0>>>0, a1:this.a1>>>0, key:key});
  },
  onLeave(r){
    if(!this.hit) return;
    var rk=-1; try{rk=ptr(r.toInt32()).add(0x0C).readS32();}catch(e){}
    send({t:'LB_ret', ret:r.toInt32()>>>0, ret_key:rk});
  }
});

// counter-effect branch: CRuleTables::GetSpellAbilityValue @ 0x547040
Interceptor.attach(ptr(0x547040), {onEnter(a){ if(g_in>0) send({t:'GSAV'}); }});
// FUN_00547620 (secondary fold) @ 0x547620
Interceptor.attach(ptr(0x547620), {onEnter(a){ if(g_in>0) send({t:'F547620'}); }});
// conflict/dispel branch: IcewindMisc::CreateEffectImmunitySpell @ 0x585990
Interceptor.attach(ptr(0x585990), {onEnter(a){ if(g_in>0) send({t:'CREATE_IMM'}); }});
// CImmunitiesEffect::OnList @ 0x4E6EB0 (this=ecx=effect)
Interceptor.attach(ptr(0x4E6EB0), {
  onEnter(a){ this.g=g_in; },
  onLeave(r){ if(this.g>0) send({t:'ONLIST', rc:r.toInt32()}); }
});

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
    for _ in range(180):
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
