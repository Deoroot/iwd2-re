#!/usr/bin/env python3
"""Crash oracle for OUR build: arm a Frida exception handler on the live game so a
silent cast-time crash (window vanishes = unhandled access violation) is caught
with a symbolized EBP backtrace BEFORE the process dies.

This is the permanent promotion of the throwaway tmp_crash_catch.py that caught the
two Fireball crashes (abort-on-cast 11ef54f6, UAF 0xDDDDDDDD 1ac84b92). The lesson
those bugs taught: parity GREEN + a Frida trace on the *original* is not enough --
nothing exercised our recovered code on our own build, so cast-time faults shipped
as "done". `vm.sh smoke` arms this guard while you drive the recovered path.

Channel: print to stdout. Run as a host-side background ssh child
(`ssh win11vm python scripts/frida_crash_guard.py <pid>`); its stdout returns over
ssh reliably -- unlike the vm.sh-frida VBS payload, whose VM-side file writes are
dropped once the parent exits. Attach BY PID (from the running iwd2-re.exe), not by
spawning ptr(0x400000) hooks. Stays alive until the crash (or until killed).

  python scripts/frida_crash_guard.py <pid|name>     # default target: iwd2-re.exe

Exit / output contract (parsed by vm.sh smoke):
  "ARMED"                  -> handler installed, safe to drive the feature
  "########## EXCEPTION ..."-> a fault fired; the block that follows is the backtrace
  "########## DETACHED"    -> target went away (crash death or normal close)
"""
import frida, sys, os, time

CRASH_TAG = "########## EXCEPTION"
DETACH_TAG = "########## DETACHED"


def out(s):
    print(s, flush=True)   # -> host-side background-task stdout (the reliable channel over ssh)


# The exception-handler + EBP-walk JS is kept verbatim from tmp_crash_catch.py: it is
# the version proven to capture both Fireball faults. Do not "improve" without a repro.
JS = r"""
function sym(a){
  try { var d = DebugSymbol.fromAddress(a);
    var s = (d.moduleName||"?") + "!" + (d.name||"?");
    if (d.fileName) s += "  [" + d.fileName + ":" + d.lineNumber + "]";
    return a + "  " + s;
  } catch(e){ return a + "  <sym err>"; }
}
function ebpwalk(ctx){
  var out = [], ebp = ctx.ebp, sp = ctx.sp;
  out.push("  EIP  " + sym(ctx.pc));
  for (var i = 0; i < 80; i++){
    if (ebp.compare(sp) < 0 || ebp.compare(sp.add(0x80000)) > 0) break;
    var ret, nxt;
    try { ret = ptr(ebp.add(4).readU32()); nxt = ptr(ebp.readU32()); } catch(e){ break; }
    out.push("  ret  " + sym(ret));
    if (nxt.compare(ebp) <= 0) break;
    ebp = nxt;
  }
  return out.join("\n");
}
Process.setExceptionHandler(function(d){
  // Ignore benign, continuable exceptions Frida reports as 'system': chiefly
  // DBG_PRINTEXCEPTION_C raised by OutputDebugString (our Iwd2DebugLog) and the
  // MS_VC thread-name exception. They are NOT crashes -- reporting them aborts the
  // smoke at the first debug-log line (e.g. the CAST marker) before the real fault.
  // Real faults are access-violation / breakpoint / illegal-instruction / etc.
  if (d.type === 'system') { return false; }   // pass through, don't report
  var hdr = "%TAG% " + d.type + " @ " + d.address;
  if (d.memory) hdr += "  mem " + d.memory.operation + " " + d.memory.address;
  hdr += " ##########";
  send({tag:"CRASH", body: hdr + "\n" + ebpwalk(d.context)});
  Thread.sleep(2);   // let the host receive the backtrace before we let it die
  return false;      // do NOT swallow -> process still crashes (faithful)
});
""".replace("%TAG%", CRASH_TAG)


def on_message(msg, data):
    try:
        if msg.get("type") == "send" and isinstance(msg.get("payload"), dict):
            p = msg["payload"]
            if p.get("tag") == "CRASH":
                out(p["body"])
                out("")
        else:
            out("[msg] " + repr(msg))
    except Exception as e:
        out("[on_message err] " + repr(e))


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "iwd2-re.exe"
    out("=== attaching to %s, arming exception handler ===" % target)
    try:
        session = frida.attach(int(target) if target.isdigit() else target)
    except Exception as e:
        out("ATTACH_FAILED: " + repr(e))
        return 2
    # When the target dies (crash or normal close) the host loop needs an end marker.
    session.on("detached", lambda *_: (out(DETACH_TAG), os._exit(0)))
    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    out("ARMED; drive the recovered path now (cast the spell). Waiting for a crash...")
    while True:
        time.sleep(0.5)


if __name__ == "__main__":
    raise SystemExit(main())
