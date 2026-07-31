"""Differential probe: what character level does the original derive?

Hooks CDerivedStats::Reload (0x4440F0) in the stock IWD2.exe and logs the
creature's stored m_characterLevel (+0x82) plus the 11 per-class level bytes
(+0x83..0x8D) and the resulting derived m_nLevel (+0x46). This tells us whether
the engine recomputes the aggregate character level from the class levels at
runtime (cheated save stores classLevel=12 but characterLevel=0).

Spawn the stock game, load slot 2 (the combat save), and watch the log -- the
sorcerer's row will show sorcererLevel=12.
"""
import time
import frida

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = GAME_DIR + r"\IWD2.exe"

SCRIPT = r"""
var RELOAD = ptr(0x4440F0);
var seen = {};
Interceptor.attach(RELOAD, {
    onEnter: function (args) {
        // __thiscall: this=ecx, stack args -> args[0]=pSprite, args[1]=pCreature
        var pCre = args[1];
        this.pCre = pCre;
        if (pCre.isNull()) { return; }
        var charLevel = pCre.add(0x82).readU8();
        var classes = [];
        for (var i = 0; i < 11; i++) {
            classes.push(pCre.add(0x83 + i).readU8());
        }
        // dedupe on (charLevel, classes) so we do not spam every frame
        var key = charLevel + ":" + classes.join(",");
        if (seen[key]) { return; }
        seen[key] = 1;
        var names = ["barb","bard","cler","drui","figh","monk","pala","rang","rogu","sorc","wiza"];
        var nonzero = [];
        for (var j = 0; j < 11; j++) {
            if (classes[j] !== 0) { nonzero.push(names[j] + "=" + classes[j]); }
        }
        send("RELOAD pCreature=" + pCre + " charLevel(+0x82)=" + charLevel +
             "  classes: " + nonzero.join(" "));
    }
});
send("hooked CDerivedStats::Reload @ 0x4440F0 -- load slot 2 + cast");
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
