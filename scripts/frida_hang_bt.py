#!/usr/bin/env python3
"""Hang oracle for OUR build: the counterpart to frida_crash_guard.py.

A crash kills the process and trips the guard's exception handler. A HANG does not:
the window stays up, the audio thread keeps playing, and nothing faults -- so the
guard, WER and a minidump all stay silent. This attaches to the live (frozen)
process and samples every thread's instruction pointer plus an EBP-chain backtrace,
repeatedly, so a spin loop shows up as the same few addresses in every sample.

Frames inside our own module are printed as `iwd2-re.exe+0xOFFSET`; resolve them
against the linker map the build already emits (`/MAP`, CMakeLists.txt) with
scripts/mapsym.py.

Channel: print to stdout. Run as a host-side ssh child, exactly like the crash
guard -- `ssh win11vm python scripts/frida_hang_bt.py <pid>` -- not as a vm.sh frida
payload (whose VM-side writes are dropped once the VBS parent exits).

  python scripts/frida_hang_bt.py [pid|name] [samples] [interval_s]
"""
import frida, sys, time


def out(s):
    print(s, flush=True)


JS = r"""
function sym(a){
  try {
    var m = Process.findModuleByAddress(a);
    if (m && m.name.toLowerCase().indexOf("iwd2-re") === 0)
      return m.name + "+0x" + a.sub(m.base).toString(16);
    var d = DebugSymbol.fromAddress(a);
    if (d && d.name) return (d.moduleName || "?") + "!" + d.name;
    if (m) return m.name + "+0x" + a.sub(m.base).toString(16);
  } catch (e) {}
  return a.toString();
}

function walk(ebp, pc){
  var frames = [sym(pc)];
  var fp = ebp;
  for (var i = 0; i < 24; i++) {
    if (fp.isNull() || fp.and(3).toInt32() !== 0) break;
    var ret, next;
    try { ret = fp.add(4).readPointer(); next = fp.readPointer(); } catch (e) { break; }
    if (ret.isNull() || !Process.findRangeByAddress(ret)) break;
    frames.push(sym(ret));
    if (next.compare(fp) <= 0) break;
    fp = next;
  }
  return frames;
}

rpc.exports.sample = function () {
  var mods = Process.enumerateModules().filter(function (m) {
    return m.name.toLowerCase().indexOf("iwd2-re") === 0;
  });
  var rows = Process.enumerateThreads().map(function (t) {
    return {
      id: t.id,
      state: t.state,
      pc: sym(t.context.pc),
      stack: walk(t.context.ebp, t.context.pc),
    };
  });
  return { base: mods.length ? mods[0].base.toString() : "?", threads: rows };
};
"""


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "iwd2-re.exe"
    samples = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    interval = float(sys.argv[3]) if len(sys.argv) > 3 else 0.7

    out("=== attaching to %s ===" % target)
    try:
        session = frida.attach(int(target) if target.isdigit() else target)
    except Exception as e:
        out("ATTACH_FAILED: %r" % (e,))
        return 1
    script = session.create_script(JS)
    script.load()

    for n in range(samples):
        try:
            s = script.exports_sync.sample()
        except Exception as e:
            out("SAMPLE_FAILED: %r" % (e,))
            return 1
        out("")
        out("########## SAMPLE %d/%d   module base %s" % (n + 1, samples, s["base"]))
        for t in s["threads"]:
            out("  tid %-6d %-10s pc=%s" % (t["id"], t["state"], t["pc"]))
            for i, f in enumerate(t["stack"][1:], 1):
                out("        #%-2d %s" % (i, f))
        if n + 1 < samples:
            time.sleep(interval)

    session.detach()
    out("########## DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
