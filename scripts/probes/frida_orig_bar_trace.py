"""Attach to the ORIGINAL iwd2.exe and trace CachingRequirements internals.

Differential against our build: prove WHY the original's CachingRequirements
returns 0 for AR2000 while ours returns GetCacheSize=12.76M.

Hooks (absolute, ImageBase 0x400000, no ASLR):
  0x5A04B0  CInfGame::CachingRequirements  -> areaName (in), return (out), sets inCR
  0x789A20  CResFile::GetCacheSize         -> while inCR: m_nRefCount(this+8),
                                              m_nIndex(this+0x10) (in), return (out)
"""
import frida
import os
import sys
import time

OUT = r"C:\iwd2-re\tmp_orig_bar.log"

JS = r"""
let inCR = 0;

Interceptor.attach(ptr(0x5A04B0), {
  onEnter: function (args) {
    inCR++;
    let name = "?";
    try { name = this.context.esp.add(4).readPointer().readPointer().readCString(); } catch (e) {}
    send("[CR] enter area=" + name);
  },
  onLeave: function (retval) {
    send("[CR] leave ret=" + retval.toInt32());
    if (inCR > 0) inCR--;
  }
});

Interceptor.attach(ptr(0x789A20), {
  onEnter: function (args) {
    this._inCR = inCR;
    if (inCR > 0) {
      const self = this.context.ecx;
      const ref = self.add(8).readS32();
      const idx = self.add(0x10).readS32();
      send("[GCS] enter refCount=" + ref + " index=" + idx);
    }
  },
  onLeave: function (retval) {
    if (this._inCR > 0) {
      send("[GCS] leave ret=" + retval.toInt32());
    }
  }
});

// CDimm::GetResFileName(nIndex, &sResFileName, &nDrive, a5)
Interceptor.attach(ptr(0x786520), {
  onEnter: function (args) {
    this._inCR = inCR;
    this._idx = args[0].toInt32();
    this._pnDrive = args[2];
  },
  onLeave: function (retval) {
    if (this._inCR > 0) {
      let nd = -1;
      try { nd = this._pnDrive.readU16(); } catch (e) {}
      send("[GRFN] index=" + this._idx + " ret=" + retval.toInt32() + " nDrive=0x" + nd.toString(16));
    }
  }
});
send("hooks installed");
"""


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "iwd2.exe"
    try:
        session = frida.attach(target)
    except Exception as e:
        print("ATTACH_FAILED: %s" % e, flush=True)
        return 1

    fd = open(OUT, "w", buffering=1)

    def on_message(message, data):
        if message.get("type") == "send":
            fd.write(message["payload"] + "\n")
            fd.flush()
            os.fsync(fd.fileno())
        elif message.get("type") == "error":
            fd.write("ERROR: " + str(message) + "\n")
            fd.flush()
            os.fsync(fd.fileno())

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    print("ARMED (attached to %s, writing %s)" % (target, OUT), flush=True)

    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    sys.exit(main())
