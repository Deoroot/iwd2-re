"""Dump CInfButtonArray cell resrefs on the ORIGINAL IWD2.exe.

Goal: resolve which CVidCell holds the action-button BAM (GUIBTACT) in the
original.  Hooks CInfButtonArray::RenderButton (0x5950F0) entry, reads ecx=this
(the array) and dumps, once per ~1s:
  - the 3 array-level overlay cells field_16E8 / field_17C2 / field_189C resrefs
  - the 12 per-button m_iconCell resrefs + m_buttonTypes + field_0/m_bHasOverlay
A CVidCell's CResRef sits at cell+0xAC (CResHelper<CResCell>).

Layout (CInfButtonArray):
  settings[n]        = this + n*0x1e0
    field_0          + 0x000
    m_bHasOverlay    + 0x008
    m_nIconNormalFrm + 0x00C
    m_iconCell       + 0x014   -> resref at +0x014+0xAC = +0x0C0
  m_buttonTypes[n]   = this + 0x16b0 + n*4
  field_16E8 resref  = this + 0x16e8 + 0xAC = this + 0x1794
  field_17C2 resref  = this + 0x17c2 + 0xAC = this + 0x186E
  field_189C resref  = this + 0x189c + 0xAC = this + 0x1948

vm.sh frida payload: no stdin, write to a dedicated file with fsync, keep alive.
"""
import time
import os
import frida

OUT = r"C:\iwd2-re\tmp_actionbar_cells.txt"
RENDERBUTTON = 0x5950F0

_fh = open(OUT, "w", buffering=1)


def emit(line):
    _fh.write(line + "\n")
    _fh.flush()
    os.fsync(_fh.fileno())


SCRIPT = r"""
var RB = ptr(0x5950F0);
var lastSig = "";
var lastTime = 0;

function resref(p) {
    // 8-byte CResRef, ASCII, NUL/space padded.
    var s = "";
    for (var i = 0; i < 8; i++) {
        var c = p.add(i).readU8();
        if (c === 0) break;
        s += String.fromCharCode(c);
    }
    return s.replace(/ +$/, "");
}

Interceptor.attach(RB, {
    onEnter: function (args) {
        var self = this.context.ecx;
        // Build a signature so we only emit when the bar content/state changes
        // (or at most once per 2s) -- keeps the log readable across states.
        var state = self.add(0x1982).readS32();
        var sel = self.add(0x197E).readS32();
        var sig = state + ":" + sel;
        var rows = [];
        for (var n = 0; n < 12; n++) {
            var s = self.add(n * 0x1e0);
            var t = self.add(0x16b0 + n * 4).readS32();
            var f0 = s.readS32();
            var ov = s.add(0x008).readS32();
            var nf = s.add(0x00C).readS32();
            var sf = s.add(0x010).readS32();
            var seq = s.add(0x1C8).readS32();
            var bsel = s.add(0x1CC).readS32();
            var aws = s.add(0x1D0).readS32();
            var cnt = s.add(0x1D8).readS32();
            var grey = s.add(0x1DC).readS32();
            var ic = resref(s.add(0x0C0));
            var cc = resref(s.add(0x19A));
            sig += "|" + t + "," + f0 + "," + nf + "," + bsel + "," + ic;
            rows.push("  btn[" + n + "] type=" + t + " f0=" + f0 +
                      " ov=" + ov + " seq=" + seq + " nF=" + nf + " sF=" + sf +
                      " bSel=" + bsel + " aws=" + aws + " cnt=" + cnt +
                      " grey=" + grey + " icon='" + ic + "' cnt2='" + cc + "'");
        }
        var now = Date.now();
        if (sig === lastSig && (now - lastTime) < 2000) return;
        lastSig = sig;
        lastTime = now;
        var lines = [];
        lines.push("=== RenderButton this=" + self + " state=" + state +
                   " selBtn=" + sel + " ===");
        lines.push("  field_16E8='" + resref(self.add(0x1794)) +
                   "' field_17C2='" + resref(self.add(0x186E)) +
                   "' field_189C='" + resref(self.add(0x1948)) + "'");
        send(lines.concat(rows).join("\n"));
    }
});
"""


def on_message(msg, data):
    if msg.get("type") == "send":
        emit(msg["payload"])
    elif msg.get("type") == "error":
        emit("[error] " + str(msg))


def main():
    import traceback
    try:
        emit("[probe starting; attaching to IWD2.exe]")
        session = frida.attach("IWD2.exe")
        emit("[attached; loading script]")
        script = session.create_script(SCRIPT)
        script.on("message", on_message)
        script.load()
        emit("[script loaded; open the action bar in-game]")
    except Exception:
        emit("[FATAL]\n" + traceback.format_exc())
        return
    n = 0
    while True:
        time.sleep(2.0)
        n += 1
        if n % 5 == 0:
            emit("[heartbeat %d -- waiting for RenderButton]" % n)


if __name__ == "__main__":
    main()
