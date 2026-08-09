#!/usr/bin/env python3
# Original-side trace of the saving-throw path for Emotion: Fear (SPWI420).
# Hooks the three functions in the effect save/admission chain and logs, at each
# entry, the effect opcode (this+0x0c = m_effectID) and save-type bitmask
# (this+0x3c = m_savingThrow), so we can see WHERE opcode 24 (State: Horror,
# save 0x10 will) is handled in the original -- and whether the base CheckSave
# (0x4A42F0) is even called for it.
#
#   0x4A3310  CGameEffect::CheckAdd        (this = ecx = effect)
#   0x4A9E30  FUN_004a9e30                 (the spell-delivery save caller)
#   0x4A42F0  CGameEffect::CheckSave       (base d20 save; + return value)
#
# Ship + run as the session-1 payload once the ORIGINAL IWD2.exe is loaded:
#   scripts/vm.sh frida scripts/frida_checksave_diff.py
# Writes to a dedicated file (flush + fsync; survives the VBS parent exit).

import frida, sys, time, os

OUT = r"C:\iwd2-re\checksave_orig.log"

JS = r"""
function eff(ecx) {
  try {
    const op = ecx.add(0x0c).readU32();
    const st = ecx.add(0x3c).readU32();
    return "op=" + op + " saveType=0x" + st.toString(16);
  } catch (e) { return "op=? (ecx=" + ecx + ")"; }
}

Interceptor.attach(ptr(0x4A3310), {          // CGameEffect::CheckAdd
  onEnter(a) { send("CheckAdd  " + eff(this.context.ecx)); }
});

Interceptor.attach(ptr(0x4A9E30), {          // FUN_004a9e30 (spell-save caller)
  onEnter(a) { send("FUN9E30   " + eff(this.context.ecx) + " ecx=" + this.context.ecx); }
});

Interceptor.attach(ptr(0x4A42F0), {          // CGameEffect::CheckSave (base)
  onEnter(a) { this.info = eff(this.context.ecx); },
  onLeave(r) { send("CheckSave " + this.info + " -> ret=" + r.toInt32()); }
});

send("__armed__ CheckAdd/FUN9E30/CheckSave");
"""

def main():
    fd = open(OUT, "a", buffering=1)
    def emit(line):
        fd.write(line + "\n"); fd.flush(); os.fsync(fd.fileno())

    try:
        session = frida.attach("IWD2.exe")
    except Exception as e:
        emit("ERROR attach IWD2.exe: %r" % e)
        while True: time.sleep(0.5)

    script = session.create_script(JS)
    script.on("message", lambda m, d: emit(m.get("payload", str(m))
                                           if m.get("type") == "send" else "MSG %r" % m))
    script.load()
    emit("__driver_loaded__")
    while True:
        time.sleep(0.5)

if __name__ == "__main__":
    main()
