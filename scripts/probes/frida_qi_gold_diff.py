"""Quick-item GOLD (UMD) tint differential on the ORIGINAL IWD2.exe.

Disasm already proved the static args of CInfButtonArray::RenderButton's
quick-item branch (0x5950F0):
  - icon  CIcon::RenderIcon (0x4E66E0): rgbTint = (nUsable==2) ? *(0x84ebb0) : 0,
    and *(0x84ebb0) == 0x0000FFFF (yellow).
  - overlay CVidCell::Render (0x7AEAD0): STORTIN4 (gold) / STORTINT (red),
    dwFlags=2 (CVIDIMG_TRANSLUCENT), nTransVal=0xC0 for BOTH.

This confirms them at RUNTIME and captures whether the tint actually fires:
  - RenderIcon  -> icon resref + rgbTint actually passed for the scroll.
  - SetTintColor (CVidImage::SetTintColor 0x7ACD60) -> the color the icon cell
    is tinted with, AND whether it is called at all from the icon path.
  - CVidCell::Render -> STORTIN4/STORTINT dwFlags/nTrans + return value.

Attach (game already running, launched by the user). Dedicated out file with
flush+fsync (vm.sh-frida payload gotchas). No stdin; keep-alive loop.
"""
import time
import os
import frida

OUT = r"C:\iwd2-re\tmp_qi_gold.txt"
_fh = open(OUT, "w", buffering=1)


def emit(line):
    ts = time.strftime("%H:%M:%S") + (".%03d" % (int(time.time() * 1000) % 1000))
    _fh.write(ts + "  " + line + "\n")
    _fh.flush()
    os.fsync(_fh.fileno())


SCRIPT = r"""
var RB_LO    = ptr(0x5950F0), RB_HI = ptr(0x595A00);  // RenderButton body
var RI_LO    = ptr(0x4E66E0), RI_HI = ptr(0x4E6A00);  // RenderIcon body
var RENDERICON = ptr(0x4E66E0);   // CIcon::RenderIcon(a1,pos,size,clip,resref,dbl,flags,cnt,fc,sc,fsc,rgbTint)
var SETTINT    = ptr(0x7ACD60);   // CVidImage::SetTintColor(COLORREF)
var RENDER     = ptr(0x7AEAD0);   // CVidCell::Render(nSurf,x,y,&clip,pPoly,nPolys,dwFlags,nTrans)
var CHECKUSE   = ptr(0x5BA080);   // CInfGame::CheckItemUsable -> 0/1/2

function resref(p) {
    var s = "";
    for (var i = 0; i < 8; i++) {
        var c = p.add(i).readU8();
        if (c === 0) break;
        s += String.fromCharCode(c);
    }
    return s.replace(/ +$/, "");
}

// ---- CIcon::RenderIcon: capture icon resref + rgbTint, only from RenderButton ----
var riSig = {};
Interceptor.attach(RENDERICON, {
    onEnter: function (args) {
        var ra = this.returnAddress;
        if (ra.compare(RB_LO) < 0 || ra.compare(RB_HI) > 0) return;  // quick-item branch only
        var res  = resref(args[4]);            // cResIcon (CResRef&)
        var dwF  = args[6].toInt32() >>> 0;     // dwFlags (greyout)
        var tint = args[11].toInt32() >>> 0;    // rgbTint (12th stack arg)
        var line = "RENDERICON res='" + res + "' dwFlags=0x" + dwF.toString(16) +
                   " rgbTint=0x" + tint.toString(16) + " ra=" + ra;
        var k = res + "|" + dwF + "|" + tint;
        var now = Date.now();
        if (riSig[k] && (now - riSig[k]) < 1500) return;
        riSig[k] = now;
        send(line);
    }
});

// ---- CVidImage::SetTintColor: only the calls coming from RenderIcon ----
var stSig = {};
Interceptor.attach(SETTINT, {
    onEnter: function (args) {
        var ra = this.returnAddress;
        if (ra.compare(RI_LO) < 0 || ra.compare(RI_HI) > 0) return;  // icon path only
        var col = args[0].toInt32() >>> 0;
        var k = col + "|" + ra;
        var now = Date.now();
        if (stSig[k] && (now - stSig[k]) < 1500) return;
        stSig[k] = now;
        send("SETTINT color=0x" + col.toString(16) + " ra=" + ra);
    }
});

// ---- CVidCell::Render: STORTIN4/STORTINT overlay, with return value ----
var rSig = {};
Interceptor.attach(RENDER, {
    onEnter: function (args) {
        var self = this.context.ecx;
        var res  = resref(self.add(0xAC));
        if (res !== "STORTIN4" && res !== "STORTINT") return;
        this._res = res;
        this._dwF = args[6].toInt32() >>> 0;
        this._nTr = args[7].toInt32();
        this._frame = self.add(0xC4).readS16();
        this._ra = this.returnAddress;
    },
    onLeave: function (ret) {
        if (!this._res) return;
        var k = this._res + "|" + this._dwF + "|" + this._nTr + "|" + ret.toInt32();
        var now = Date.now();
        if (rSig[k] && (now - rSig[k]) < 1500) return;
        rSig[k] = now;
        send("RENDER res='" + this._res + "' frame=" + this._frame +
             " dwFlags=0x" + this._dwF.toString(16) + " nTrans=" + this._nTr +
             " -> ret=" + ret.toInt32() + " ra=" + this._ra);
    }
});

// ---- CheckItemUsable retval ----
var lu = {};
Interceptor.attach(CHECKUSE, {
    onLeave: function (ret) {
        var v = ret.toInt32();
        var now = Date.now();
        if (lu.v === v && (now - (lu.t || 0)) < 1500) return;
        lu = { v: v, t: now };
        send("CHECKUSABLE -> " + v);
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
        emit("[attaching to IWD2.exe]")
        session = frida.attach("IWD2.exe")
        emit("[attached; loading script]")
        script = session.create_script(SCRIPT)
        script.on("message", on_message)
        script.load()
        emit("[loaded; select the rogue, hover the quick-item scrolls]")
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
