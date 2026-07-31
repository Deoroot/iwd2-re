"""Comprehensive action-bar trace on the ORIGINAL IWD2.exe.

Captures ground truth for the 3 remaining action-bar visual bugs:

  #2 quick-item gold UMD tint  -> CheckItemUsable (0x5BA080) retval (nUsable) +
                                  the STORTIN4 / STORTINT CVidCell::Render call
                                  (resref, dwFlags, nTransVal) from RenderButton
                                  quick-item branch (~ret 0x595667).
  #3 red selection persists    -> SetSelectedButton (0x452C50) lifecycle (value +
                                  caller) AND per-button m_bSelected / array
                                  m_nSelectedButton at each RenderButton dump.
  #4 green quick-weapon square -> the HIGHLGHT CVidCell::Render call (~ret
                                  0x59535a): real resref, frame (+0xC4), dwFlags
                                  (arg6), nTransVal (arg7). Border-vs-filled
                                  lives in these args + frame.

Attach (game already running, launched by the user); writes a dedicated file
with flush+fsync (vm.sh-frida payload gotchas). Ship + run:
  scp scripts/frida_actionbar_full_trace.py win11vm:C:/iwd2-re/scripts/
  ssh win11vm "python C:/iwd2-re/scripts/frida_actionbar_full_trace.py"   (bg)

CInfButtonArray layout (this = ecx at RenderButton 0x5950F0):
  settings[n]        = this + n*0x1e0
    field_0          + 0x000   m_bHasOverlay + 0x008  nIconNormalFrame + 0x00C
    nIconSelFrame    + 0x010   m_iconCell resref + 0x0C0   m_nIconSequence + 0x1C8
    m_bSelected      + 0x1CC   m_bActiveWeaponSet + 0x1D0  m_nCount + 0x1D8
    m_bGreyOut       + 0x1DC
  m_buttonTypes[n]   = this + 0x16b0 + n*4
  m_nSelectedButton  = this + 0x197e        m_nState = this + 0x1982
CVidCell:  resref + 0xAC   m_nCurrentFrame + 0xC4
"""
import time
import os
import frida

OUT = r"C:\iwd2-re\tmp_actionbar_full.txt"
_fh = open(OUT, "w", buffering=1)


def emit(line):
    ts = time.strftime("%H:%M:%S") + (".%03d" % (int(time.time() * 1000) % 1000))
    _fh.write(ts + "  " + line + "\n")
    _fh.flush()
    os.fsync(_fh.fileno())


SCRIPT = r"""
var RB        = ptr(0x5950F0);   // CInfButtonArray::RenderButton
var RB_LO     = ptr(0x5950F0), RB_HI = ptr(0x595A00);  // RenderButton(+Overlay) body
var RENDER    = ptr(0x7AEAD0);   // CVidCell::Render(nSurf,x,y,&clip,pPoly,nPolys,dwFlags,nTrans)
var CHECKUSE  = ptr(0x5BA080);   // CInfGame::CheckItemUsable
var SETSEL    = ptr(0x452C50);   // CInfButtonArray::SetSelectedButton

function resref(p) {
    var s = "";
    for (var i = 0; i < 8; i++) {
        var c = p.add(i).readU8();
        if (c === 0) break;
        s += String.fromCharCode(c);
    }
    return s.replace(/ +$/, "");
}

// ---- per-button settings dump, throttled to state/selection changes ----
var lastSig = "", lastTime = 0;
Interceptor.attach(RB, {
    onEnter: function (args) {
        var self = this.context.ecx;
        var state = self.add(0x1982).readS32();
        var sel   = self.add(0x197E).readS32();
        var sig = state + ":" + sel;
        var rows = [];
        for (var n = 0; n < 12; n++) {
            var s    = self.add(n * 0x1e0);
            var t    = self.add(0x16b0 + n * 4).readS32();
            var f0   = s.readS32();
            var nf   = s.add(0x00C).readS32();
            var sf   = s.add(0x010).readS32();
            var bsel = s.add(0x1CC).readS32();
            var aws  = s.add(0x1D0).readS32();
            var grey = s.add(0x1DC).readS32();
            var ic   = resref(s.add(0x0C0));
            sig += "|" + t + "," + bsel + "," + aws + "," + grey;
            rows.push("  btn[" + n + "] type=" + t + " f0=" + f0 +
                      " nF=" + nf + " sF=" + sf + " bSel=" + bsel +
                      " aws=" + aws + " grey=" + grey + " icon='" + ic + "'");
        }
        var now = Date.now();
        if (sig === lastSig && (now - lastTime) < 1500) return;
        lastSig = sig; lastTime = now;
        send("=== RenderButton state=" + state + " selBtn=" + sel + " ===\n" +
             rows.join("\n"));
    }
});

// ---- CVidCell::Render from the action-bar render path ----
var renderSig = {};
Interceptor.attach(RENDER, {
    onEnter: function (args) {
        var self = this.context.ecx;
        var res  = resref(self.add(0xAC));
        var ra   = this.returnAddress;
        var fromRB = (ra.compare(RB_LO) >= 0 && ra.compare(RB_HI) <= 0);
        var watch  = (res === "HIGHLGHT" || res === "STORTIN4" ||
                      res === "STORTINT" || res === "GUIBTACT" || res === "GUIBTBUT");
        if (!fromRB && !watch) return;
        var frame = self.add(0xC4).readS16();
        var dwF   = args[6].toInt32() >>> 0;
        var nTr   = args[7].toInt32();
        var line = "RENDER res='" + res + "' frame=" + frame +
                   " dwFlags=0x" + dwF.toString(16) + " nTrans=" + nTr +
                   " surf=" + args[0].toInt32() +
                   " x=" + args[1].toInt32() + " y=" + args[2].toInt32() +
                   " ra=" + ra;
        var k = res + "|" + frame + "|" + dwF + "|" + nTr + "|" + ra;
        var now = Date.now();
        if (renderSig[k] && (now - renderSig[k]) < 1500) return;
        renderSig[k] = now;
        send(line);
    }
});

// ---- CheckItemUsable retval (nUsable: 0=unusable,1=ok,2=UMD) ----
var lastUsable = {};
Interceptor.attach(CHECKUSE, {
    onLeave: function (ret) {
        var v = ret.toInt32();
        var now = Date.now();
        if (lastUsable.v === v && (now - (lastUsable.t || 0)) < 1500) return;
        lastUsable = { v: v, t: now };
        send("CHECKUSABLE -> " + v);
    }
});

// ---- SetSelectedButton lifecycle (when does it set / clear?) ----
Interceptor.attach(SETSEL, {
    onEnter: function (args) {
        send("SETSEL n=" + args[0].toInt32() + " from=" + this.returnAddress);
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
        emit("[trace starting; attaching to IWD2.exe]")
        session = frida.attach("IWD2.exe")
        emit("[attached; loading script]")
        script = session.create_script(SCRIPT)
        script.on("message", on_message)
        script.load()
        emit("[loaded; interact with the action bar -- hover/click quick item, cast, watch weapon squares]")
    except Exception:
        emit("[FATAL]\n" + traceback.format_exc())
        return
    n = 0
    while True:
        time.sleep(2.0)
        n += 1
        if n % 15 == 0:
            emit("[heartbeat %d]" % n)


if __name__ == "__main__":
    main()
