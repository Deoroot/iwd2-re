#!/usr/bin/env python3
"""
Frida trace for CScreenInventory::OnDoneButtonClick case 3 -- the paperdoll
colour apply (0x6285B0). OUR build stubs case 3 to DismissPopup, so this MUST
attach to the ORIGINAL IWD2.exe (same bytes, ImageBase 0x400000, no ASLR).

Captures, when the user clicks Done on the portrait-colour popup (popup 3):
  - field_11E (colour slot) / field_11F (colour value) at this+0x11E/+0x11F
  - the sprite colour-array write  (sprite+0x5C8+slot = value)
  - the 4 CGameAnimation SetColorRange calls (vtable+0xA4): arg1=range, arg2=value
  - the colour CMessage handed to CMessageHandler::AddMessage (0x14 bytes dumped)

Run as a session-1 payload (scripts/vm.sh frida). The ORIGINAL IWD2.exe must
already be running with a save loaded; then open inventory -> appearance/colour
-> pick a colour -> Done.
"""
import sys
import os
import time
import frida

OUT_PATH = r"C:\iwd2-re\tmp_colorapply_trace.log"
TARGET = "IWD2.exe"  # ORIGINAL game (NOT our iwd2-re.exe)

JS = r"""
'use strict';
function u8(p){ return p.readU8(); }
function u32(p){ return p.readU32(); }
function hx(n){ return '0x' + (n >>> 0).toString(16); }

function bytesAt(p, n){
    var out = [];
    for (var i = 0; i < n; i++) {
        out.push(('0' + p.add(i).readU8().toString(16)).slice(-2));
    }
    return out.join(' ');
}

// 1) colour slot / value read  (0x628746: mov al,[edi+0x11E]; edi = this)
Interceptor.attach(ptr(0x628746), function () {
    var thiz = this.context.edi;
    send('[case3] ENTER this=' + thiz +
         ' field_11E(slot)=' + u8(thiz.add(0x11e)) +
         ' field_11F(value)=' + u8(thiz.add(0x11f)));
});

// 2) sprite colour-array write  (0x6289CA: mov [edx+ecx+0x5C8], bl)
//    ecx = sprite, edx = slot&0xff, bl = value
Interceptor.attach(ptr(0x6289ca), function () {
    var sprite = this.context.ecx;
    var slot   = this.context.edx.toInt32() & 0xff;
    var value  = this.context.ebx.toInt32() & 0xff;
    send('[case3] write sprite=' + sprite +
         ' m_id=' + hx(u32(sprite.add(0x5c))) +
         ' colours[' + slot + ']=' + hx(value));
});

// 3) the four SetColorRange calls (call [reg+0xA4]); [esp]=arg1(range), [esp+4]=arg2(value)
[0x628a11, 0x628a96, 0x628b1c, 0x628b9c].forEach(function (addr) {
    Interceptor.attach(ptr(addr), function () {
        var esp = this.context.esp;
        send('[SetColorRange@' + addr.toString(16) + '] animType=' + this.context.ecx +
             ' arg1(range)=' + hx(u32(esp)) +
             ' arg2(value)=' + hx(u32(esp.add(4))));
    });
});

// 4) colour CMessage -> AddMessage (0x628BED). [esp] = msg ptr.
Interceptor.attach(ptr(0x628bed), function () {
    var msg = this.context.esp.readPointer();
    send('[colorMsg] ptr=' + msg +
         ' vtable=' + msg.readPointer() +
         ' bytes[0x14]=' + bytesAt(msg, 0x14));
});

send('[trace] hooks installed on ' + Process.enumerateModules()[0].name);
"""


def main():
    out = open(OUT_PATH, "a", buffering=1)

    def log(line):
        out.write(line + "\n")
        out.flush()
        os.fsync(out.fileno())

    def on_message(message, data):
        if message.get("type") == "send":
            log(message["payload"])
        elif message.get("type") == "error":
            log("[frida-error] " + str(message.get("description")))

    log("[trace] attaching to %s @ %s" % (TARGET, time.strftime("%H:%M:%S")))
    try:
        session = frida.attach(TARGET)
    except Exception as e:
        log("[trace] ATTACH FAILED: %s (is the ORIGINAL IWD2.exe running?)" % e)
        return
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    log("[trace] loaded; do the colour popup -> Done now")

    # vm.sh frida payload has no stdin (EOF -> exit). Stay alive.
    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    main()
