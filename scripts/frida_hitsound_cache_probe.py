"""
Frida probe: does FUN_004A8DF0's resource-handle "cache" comparison read a real
persistent value or uninitialized stack garbage?

Hooks the ORIGINAL IWD2.exe at:
  - 0x4A8DF0 (function entry) -- logs param_1 (damage type) and ecx-analog target ptr
  - 0x4A9926 (the comparison site) -- dumps the exact stack slots involved:
      [esp+0x20]/[esp+0x24] = the freshly-built resref (ptr,len-ish word)
      [esp+0x34]/[esp+0x38] = the "cached" resref being compared against
      [esp+0x30]            = the "cached" CRes* handle (iStack_68 equivalent)
      [esp+0x2c]            = the "request pending" flag (iStack_6c equivalent)

No ASLR (base 0x400000), so these are absolute addresses.
"""
import frida
import sys
import time

EXE = r"C:\Juegos\Icewind Dale 2\IWD2.exe"

ENTRY = 0x4A8DF0
CMP_SITE = 0x4A9926

SCRIPT = """
const ENTRY = ptr("0x4A8DF0");
const CMP_SITE = ptr("0x4A9926");

let hitCount = 0;

Interceptor.attach(ENTRY, {
    onEnter(args) {
        hitCount++;
        send({tag: "entry", n: hitCount, param1: this.context.ecx ? args[0].toInt32() : null,
              esi_will_be: "see param2 arg", raw_arg0: args[0].toInt32()});
    }
});

Interceptor.attach(CMP_SITE, {
    onEnter(args) {
        const esp = this.context.esp;
        function rd(off) {
            try { return esp.add(off).readU32(); } catch (e) { return "ERR:" + e.message; }
        }
        const newPtr = rd(0x20);
        const newLen = rd(0x24);
        const cachedPtr = rd(0x34);
        const cachedLen = rd(0x38);
        const cachedHandle = rd(0x30);
        const reqFlag = rd(0x2c);
        let newStr = "";
        try { newStr = ptr(newPtr).readCString(8); } catch (e) {}
        let cachedStr = "";
        try { cachedStr = ptr(cachedPtr).readCString(8); } catch (e) {}
        send({
            tag: "cmp",
            newPtr: newPtr, newLen: newLen, newStr: newStr,
            cachedPtr: cachedPtr, cachedLen: cachedLen, cachedStr: cachedStr,
            cachedHandle: cachedHandle, reqFlag: reqFlag
        });
    }
});

send({tag: "ready"});
"""

def on_message(message, data):
    if message["type"] == "send":
        print(message["payload"], flush=True)
    else:
        print("MSG:", message, flush=True)

def main():
    import os
    pid = frida.spawn(EXE, cwd=os.path.dirname(EXE))
    session = frida.attach(pid)
    script = session.create_script(SCRIPT)
    script.on("message", on_message)
    script.load()
    frida.resume(pid)
    print(f"Spawned IWD2.exe pid={pid}, hooks live. Play and land hits...", flush=True)
    with open(r"C:\Users\User\Documents\Visual Studio Projects\Icewind-dale-reverse-ee\tmp_hitsound_probe.pid", "w") as f:
        f.write(str(pid))
    while True:
        time.sleep(1)

if __name__ == "__main__":
    main()
