# Differential capture on RETAIL IWD2.exe for CGameSprite::BuildAbilityButtonData (0x718390).
# Mirrors the Iwd2DebugLog instrumentation in our build so the two logs diff line-for-line.
#   BABD-IN  res=<resref> nClass=<n> nSpec=<n>
#   BABD-OUT res=<resref> casterLvl=<n> targetType=<n> name=<strref> icon=<resref> result=<0/1>
# __thiscall: this=ecx, stack args = args[0..3] (CResRef*, BYTE nClass, DWORD nSpec, CButtonData*).
# CButtonData layout (pack 2): m_icon@0, m_name@8, m_abilityId@0x1A, m_targetType@0x28.
# Retail = release binary, ImageBase 0x400000, no ASLR -> absolute addrs exact.
import frida, time, os, sys

OUT = r"C:\iwd2-re\diff_orig.log"

JS = r"""
function rr(p){
    try {
        var b = new Uint8Array(p.readByteArray(8));
        var s = '';
        for (var i = 0; i < 8; i++) { if (b[i] === 0) break; s += String.fromCharCode(b[i]); }
        return s;
    } catch (e) { return '?'; }
}
function s16(v){ return (v << 16) >> 16; }

var gDepth = 0, gCaster = -999;

// CGameSprite::GetCasterLevel -- capture the caster level used inside BABD.
Interceptor.attach(ptr(0x75e940), {
    onLeave: function (r) { if (gDepth > 0) gCaster = s16(r.toInt32() & 0xffff); }
});

Interceptor.attach(ptr(0x718390), {           // BuildAbilityButtonData
    onEnter: function (a) {
        gDepth++;
        gCaster = -999;
        this.res = rr(a[0]);
        this.nClass = a[1].toInt32() & 0xff;
        this.nSpec = a[2].toUInt32();
        this.pButton = a[3];
        send({t: "in", v: "res=" + this.res + " nClass=" + this.nClass + " nSpec=" + this.nSpec});
    },
    onLeave: function (r) {
        var result = r.toInt32();
        var icon = "-", name = -1, tt = -1;
        if (result !== 0 && !this.pButton.isNull()) {
            icon = rr(this.pButton);                       // m_icon @0
            name = this.pButton.add(0x8).readS32();        // m_name @8
            tt   = this.pButton.add(0x28).readS16();       // m_abilityId.m_targetType @0x28
        }
        send({t: "out", v: "res=" + this.res + " casterLvl=" + gCaster
            + " targetType=" + tt + " name=" + name + " icon=" + icon
            + " result=" + result});
        if (gDepth > 0) gDepth--;
    }
});
"""

f = open(OUT, "w")
def log(line):
    f.write(line + "\n"); f.flush(); os.fsync(f.fileno())

def on_message(msg, data):
    if msg.get("type") == "send":
        p = msg["payload"]
        if isinstance(p, dict):
            log("BABD-%s %s" % (p.get("t").upper(), p.get("v")))
        else:
            log(str(p))
    elif msg.get("type") == "error":
        log("ERR " + str(msg.get("stack") or msg))

try:
    session = frida.attach("IWD2.exe")
except Exception as e:
    log("ATTACH-FAIL " + str(e)); sys.exit(1)

script = session.create_script(JS)
script.on("message", on_message)
script.load()
log("ATTACHED 0x718390 (+0x75e940 GetCasterLevel)")
while True:
    time.sleep(0.5)
