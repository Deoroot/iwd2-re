# Differential / diagnostic capture on RETAIL IWD2.exe.
#   1. dump 8 bytes @0x71C0C0 (must match our reference 6a ff 68 8b aa 83 00 ..)
#   2. count ProcessEffectList (0x72DE60) calls -- sanity that the hot path fires
#   3. count + read sub_71C0C0 (0x71C0C0) exits, one stat line per unique m_id
# Retail = release binary, ImageBase 0x400000, no ASLR -> absolute addrs exact.
import frida, time, os, sys

OUT = r"C:\iwd2-re\diff_orig.log"

JS = r"""
var b = ptr(0x71c0c0).readByteArray(8);
send({t: "bytes", v: Array.prototype.map.call(new Uint8Array(b),
        function (x) { return ("0" + x.toString(16)).slice(-2); }).join(" ")});

var pel = 0, der = 0, seen = {};
Interceptor.attach(ptr(0x72de60), {           // ProcessEffectList
    onEnter: function (a) {
        pel++;
        if (pel % 200 === 1) send({t: "pel", v: pel});
    }
});
Interceptor.attach(ptr(0x71c0c0), {           // sub_71C0C0
    onEnter: function (a) { this.self = this.context.ecx; der++; },
    onLeave: function (r) {
        if (der === 1) send({t: "der1", v: der});
        var s = this.self;
        if (s.isNull()) return;
        var id = s.add(0x5c).readS32();
        if (seen[id]) return;
        seen[id] = 1;
        send({t: "stat", v: "ORIG id=" + id
            + " maxhp=" + s.add(0x924).readS16()
            + " fort=" + s.add(0x93c).readS16()
            + " ref=" + s.add(0x93e).readS16()
            + " will=" + s.add(0x940).readS16()
            + " hpconold=" + s.add(0x7234).readS32()});
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
            log("%s: %s" % (p.get("t"), p.get("v")))
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
log("ATTACHED 0x71c0c0 + 0x72de60")
while True:
    time.sleep(0.5)
