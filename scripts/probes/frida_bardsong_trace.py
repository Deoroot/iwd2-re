"""Differential bard-song trace on the ORIGINAL IWD2.exe.

Hooks the per-round bard-song chain to see what the original applies, to compare
against our recovered sub_72FD20 / CMessage105 path:
  - sub_72FD20 (0x72FD20)      : per-round modal update; log when modal==1 (song)
  - CMessage105 ctor (0x5152C0): the composite effect-list message; log caller/target
  - CMessageAddEffect::Run (0x4F8CA0): the actual per-effect apply; read m_effect
        and log the song effects (id 54/73/238/142/206) with their timing fields
        (+0x20 m_duration, +0x24) and target/source -- this is the ground truth
        for the effect timing our tweak produces (0x1000 / gametime+100).

Run:  scripts/vm.sh frida frida_bardsong_trace.py
      (spawns the original; in-game: load the bard save (slot 2) and sing)
Log:  tmp_frida_bardsong.log (repo root).
"""
import frida
import sys
import os
import json

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_bardsong.log")

JS = r"""
'use strict';
// IWD2.exe, no ASLR -> Ghidra addresses are absolute.
const CMsg105ctor  = ptr(0x5152C0);   // CMessage105::CMessage105(list, caller, target)
const CMsg105Run   = ptr(0x5157F0);   // CMessage105::Run (dispatch loop)
const AddEffectRun = ptr(0x4F8CA0);   // CMessageAddEffect::Run

Interceptor.attach(CMsg105ctor, {
  onEnter(args) {
    // __thiscall: ecx=this (new msg); stack args = [sourceList, caller, target].
    send({ tag: 'CMSG105_CTOR', caller: args[1].toInt32(), target: args[2].toInt32() });
  }
});

Interceptor.attach(CMsg105Run, {
  onEnter(args) {
    const thiz = this.context.ecx;            // CMessage105*
    let n = -1, tgt = 0;
    try { n = thiz.add(0x14).readU32(); tgt = thiz.add(0x4).readU32(); } catch (e) {}
    send({ tag: 'CMSG105_RUN', n: n, tgt: tgt });
  }
});

Interceptor.attach(AddEffectRun, {
  onEnter(args) {
    const thiz = this.context.ecx;            // CMessageAddEffect*
    try {
      const eff = thiz.add(0xc).readPointer(); // m_effect
      const id = eff.isNull() ? -1 : eff.add(0x8).readU32();   // m_effectID
      send({
        tag: 'ADDEFF',
        id: id,
        tgt: thiz.add(0x4).readU32(),          // m_targetId
        src: thiz.add(0x8).readU32()           // m_sourceId
      });
    } catch (e) { send({ tag: 'ERR', e: '' + e }); }
  }
});
"""


def main():
    attach = "--attach" in sys.argv
    open(LOG, "w").close()

    def on_message(message, data):
        if message["type"] == "send":
            line = json.dumps(message["payload"])
        else:
            line = "ERROR " + json.dumps(message)
        with open(LOG, "a") as f:
            f.write(line + "\n")
            f.flush()
            os.fsync(f.fileno())

    if attach:
        session = frida.attach("IWD2.exe")
        pid = None
        print("[*] attached to running IWD2.exe", flush=True)
    else:
        pid = frida.spawn(EXE, cwd=GAME_DIR)
        session = frida.attach(pid)
        print(f"[*] spawned IWD2.exe pid={pid}", flush=True)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    if pid is not None:
        frida.resume(pid)

    print(f"[*] hooks live. In-game: load the bard save (slot 2) and sing. Log: {LOG}", flush=True)
    sys.stdout.flush()
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
