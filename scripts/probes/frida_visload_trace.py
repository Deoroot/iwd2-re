#!/usr/bin/env python3
"""Trace ORIGINAL IWD2.exe party visibility-map registration on save load.

Question: in single-player, WHICH path registers each party member in the
CVisibilityMap (calls CVisibilityMap::AddCharacter)?  Our build only wires
CGameSprite::AddToArea (0x6f40f0), which runs at load while the late party
members still have portrait==-1, so they never register and never reveal
terrain/enemies/the Call Lightning bolt as they move.  The original must
register them via another path -- candidates:
  - CGameSprite::AddToArea           (0x6f40f0)        [we have this]
  - CGameSprite::AddReplacementToArea(0x6f3b80)        [NOT recovered]
  - CMessageVisibilityMapMove::Run   (0x5111f0)        [recovered, never posted in SP?]

This hooks AddCharacter and buckets its return address to name the caller,
so we learn which path actually fires per party member.  Also hooks the two
add-to-area entries to see order/portrait.

IWD2.exe: ImageBase 0x400000, no ASLR -> runtime addr == Ghidra addr.

Run (ORIGINAL IWD2.exe already at the MAIN MENU in VM session 1):
  scripts/vm.sh frida scripts/frida_visload_trace.py
Then LOAD save slot 3 (the combat save).  Optionally walk the druid + barbarian.
Tell me when done; pull C:\\iwd2-re\\tmp_frida_visload.jsonl.
"""
import frida, os, time, json

OUT = r"C:\iwd2-re\tmp_frida_visload.jsonl"
PROC = "IWD2.exe"

JS = r"""
'use strict';
const ADDCHAR  = ptr(0x551840);   // CVisibilityMap::AddCharacter(this, &pos, charId, &table)
const ADDAREA  = ptr(0x6f40f0);   // CGameSprite::AddToArea (caller of AddCharacter)
const ADDREPL  = ptr(0x6f3b80);   // CGameSprite::AddReplacementToArea (caller, NOT recovered in ours)
const MSGRUN   = ptr(0x5111f0);   // CMessageVisibilityMapMove::Run (caller)

function s32(p,o){ try { return p.add(o).readS32(); } catch(e){ return 0x7fffffff; } }

// Name the AddCharacter caller by which function the return address lands in.
function callerName(ret){
  if (ret.compare(ptr(0x6f3b80)) >= 0 && ret.compare(ptr(0x6f40f0)) < 0) return 'AddReplacementToArea';
  if (ret.compare(ptr(0x6f40f0)) >= 0 && ret.compare(ptr(0x6f47f0)) < 0) return 'AddToArea';
  if (ret.compare(ptr(0x5111f0)) >= 0 && ret.compare(ptr(0x511300)) < 0) return 'MsgVisMapMove::Run';
  return ret.toString();
}

// CVisibilityMap::AddCharacter -- __thiscall: ecx=vismap, a[0]=&pos, a[1]=charId, a[2]=&table
Interceptor.attach(ADDCHAR, { onEnter(a){
  send({t:'ADDCHAR', charId:a[1].toInt32(), via:callerName(this.returnAddress),
        ret:this.returnAddress.toString()});
}});

// CGameSprite::AddToArea -- ecx=sprite, m_id@0x5C. (entry => sprite being added)
Interceptor.attach(ADDAREA, { onEnter(a){
  send({t:'AddToArea', id:s32(this.context.ecx,0x5c)});
}});

// CGameSprite::AddReplacementToArea -- ecx=sprite, m_id@0x5C.
Interceptor.attach(ADDREPL, { onEnter(a){
  send({t:'AddReplacementToArea', id:s32(this.context.ecx,0x5c)});
}});

// CMessageVisibilityMapMove::Run -- ecx=message; m_targetId/m_moveOntoList layout unknown,
// just mark that it ran.
Interceptor.attach(MSGRUN, { onEnter(a){
  send({t:'MsgVisMapMove::Run'});
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
