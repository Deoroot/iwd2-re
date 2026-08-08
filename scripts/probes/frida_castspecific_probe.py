"""Differential probe: how does the original feed the spell-cast level?

Hooks the shared spell action handler FUN_00461190 in the stock IWD2.exe and
reads, at entry, the caster's m_curAction.m_specificID (+0x52C), the derived
m_nLevel (+0x966) and the action id (+0x476). Also hooks DecodeProjectile
(0x51EAF0) and logs the missileType it is handed. Casting Magic Missile from the
action bar on a level-12 sorcerer tells us whether the original passes
specificID = casterLevel (our queue wrongly passes 0) or relies on m_nLevel
(our handler wrongly reads specificID).

Spawn the stock game, load slot 2, cast Magic Missile at an enemy.
"""
import time
import frida

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = GAME_DIR + r"\IWD2.exe"

SCRIPT = r"""
var HANDLER = ptr(0x461190);
var DECODE  = ptr(0x51EAF0);

Interceptor.attach(HANDLER, {
    onEnter: function (args) {
        var self = this.context.ecx;       // __thiscall this = sprite
        if (self.isNull()) { return; }
        var specificID = self.add(0x52C).readS32();
        var nLevel     = self.add(0x966).readU8();
        var actionID   = self.add(0x476).readU16();
        send("HANDLER 0x461190  actionID=0x" + actionID.toString(16) +
             " specificID=" + specificID + " m_nLevel=" + nLevel);
    }
});

Interceptor.attach(DECODE, {
    onEnter: function (args) {
        // static __cdecl DecodeProjectile(USHORT type, caster, BYTE) -> stack arg0
        var t = args[0].toInt32() & 0xffff;
        send("DECODE 0x51EAF0  missileType=0x" + t.toString(16));
    }
});
send("hooked spell handler + DecodeProjectile -- load slot 2, cast Magic Missile");
"""


def main():
    pid = frida.spawn([EXE], cwd=GAME_DIR)
    session = frida.attach(pid)
    script = session.create_script(SCRIPT)
    script.on("message", lambda m, d: print(m.get("payload", m), flush=True))
    script.load()
    frida.resume(pid)
    print(f"spawned pid={pid}; load slot 2 + cast Magic Missile.", flush=True)
    time.sleep(240)
    print("probe window elapsed", flush=True)


if __name__ == "__main__":
    main()
