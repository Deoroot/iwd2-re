"""Inventory item-state tint differential on the ORIGINAL IWD2.exe.

Settles two things for recovering CUIControlButtonInventorySlot::Render (0x62DDE0):
  1. WHICH STORTIN* overlay renders for a usable vs unusable scroll (and from the
     slot-render path), i.e. what the red/gold inventory tint actually is.
  2. The RUNTIME signatures of the usability helpers (Ghidra/decompile disagree
     with our src recovery):
       - IsItemUsableByClass (0x5b9c60): this (ecx) + first stack args + retval.
       - CanUseItem          (0x5b9d20): this (ecx) + first stack args + retval.
         -> if its `this` equals IsItemUsableByClass's `this`, our
            CGameSprite::CanUseItem(2-arg) recovery is wrong (really CInfGame/4-arg).
       - CheckItemUsable     (0x5ba080): retval (0/1/2).

Attach (game already running). Dedicated out file w/ flush+fsync. No stdin;
keep-alive loop. Set up: put scrolls on a char who CAN use them and one who
CANNOT, open each inventory / hover the slots, then read C:\iwd2-re\tmp_inv_tint.txt.
"""
import time
import os
import frida

OUT = r"C:\iwd2-re\tmp_inv_tint.txt"
_fh = open(OUT, "w", buffering=1)


def emit(line):
    ts = time.strftime("%H:%M:%S") + (".%03d" % (int(time.time() * 1000) % 1000))
    _fh.write(ts + "  " + line + "\n")
    _fh.flush()
    os.fsync(_fh.fileno())


SCRIPT = r"""
var SLOT_LO  = ptr(0x62DDE0), SLOT_HI = ptr(0x62E700);  // slot Render body
var SLOTREND = ptr(0x62DDE0);   // CUIControlButtonInventorySlot::Render
var RENDER   = ptr(0x7AEAD0);   // CVidCell::Render(nSurf,x,y,&clip,pPoly,nPolys,dwFlags,nTrans)
var ISUSABLE = ptr(0x5B9C60);   // IsItemUsableByClass? (this=ecx)
var CANUSE   = ptr(0x5B9D20);   // CanUseItem (this=ecx)
var CHECKUSE = ptr(0x5BA080);   // CInfGame::CheckItemUsable

function resref(p) {
    var s = "";
    for (var i = 0; i < 8; i++) {
        var c = p.add(i).readU8();
        if (c === 0) break;
        s += String.fromCharCode(c);
    }
    return s.replace(/ +$/, "");
}

// ---- which STORTIN* overlay renders, from the slot-render path ----
var rSig = {};
Interceptor.attach(RENDER, {
    onEnter: function (args) {
        var res = resref(this.context.ecx.add(0xAC));
        if (res.indexOf("STORTIN") !== 0 && res.indexOf("STONSLOT") !== 0) return;
        var ra = this.returnAddress;
        var fromSlot = (ra.compare(SLOT_LO) >= 0 && ra.compare(SLOT_HI) <= 0);
        var dwF = args[6].toInt32() >>> 0;
        var nTr = args[7].toInt32();
        var k = res + "|" + dwF + "|" + nTr + "|" + fromSlot;
        var now = Date.now();
        if (rSig[k] && (now - rSig[k]) < 2000) return;
        rSig[k] = now;
        send("OVERLAY res='" + res + "' dwFlags=0x" + dwF.toString(16) +
             " nTrans=" + nTr + " fromSlotRender=" + fromSlot + " ra=" + ra);
    }
});

// ---- slot Render entry: which slot id + item flags ----
var sSig = {};
Interceptor.attach(SLOTREND, {
    onEnter: function (args) {
        var self = this.context.ecx;
        var id = self.add(0xa).readS32();      // m_nID (decompile *(param_1+10))
        var k = "" + id;
        var now = Date.now();
        if (sSig[k] && (now - sSig[k]) < 3000) return;
        sSig[k] = now;
        send("SLOTRENDER m_nID=" + id);
    }
});

// ---- IsItemUsableByClass (0x5b9c60): this + args + retval ----
var iSig = {};
Interceptor.attach(ISUSABLE, {
    onEnter: function (args) {
        this._this = this.context.ecx;
        this._a = [args[0].toInt32(), args[1].toInt32() >>> 0,
                   args[2].toInt32() >>> 0, args[3].toInt32() >>> 0];
    },
    onLeave: function (ret) {
        var k = this._this + "|" + this._a.join(",") + "|" + ret.toInt32();
        var now = Date.now();
        if (iSig[k] && (now - iSig[k]) < 2000) return;
        iSig[k] = now;
        send("ISUSABLE this=" + this._this + " a0=" + this._a[0] +
             " a1=0x" + this._a[1].toString(16) + " a2=0x" + this._a[2].toString(16) +
             " a3=0x" + this._a[3].toString(16) + " -> ret=" + ret.toInt32());
    }
});

// ---- CanUseItem (0x5b9d20): this + args + retval (resolve signature) ----
var cSig = {};
Interceptor.attach(CANUSE, {
    onEnter: function (args) {
        this._this = this.context.ecx;
        this._a = [args[0].toInt32() >>> 0, args[1].toInt32() >>> 0,
                   args[2].toInt32() >>> 0, args[3].toInt32() >>> 0];
    },
    onLeave: function (ret) {
        var k = this._this + "|" + this._a.join(",") + "|" + ret.toInt32();
        var now = Date.now();
        if (cSig[k] && (now - cSig[k]) < 2000) return;
        cSig[k] = now;
        send("CANUSE this=" + this._this + " a0=0x" + this._a[0].toString(16) +
             " a1=0x" + this._a[1].toString(16) + " a2=0x" + this._a[2].toString(16) +
             " a3=0x" + this._a[3].toString(16) + " -> ret=" + ret.toInt32());
    }
});

// ---- CheckItemUsable retval ----
var lu = {};
Interceptor.attach(CHECKUSE, {
    onLeave: function (ret) {
        var v = ret.toInt32();
        var now = Date.now();
        if (lu.v === v && (now - (lu.t || 0)) < 2000) return;
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
        emit("[loaded; open the inventory of a usable + an unusable char, hover the scroll slots]")
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
