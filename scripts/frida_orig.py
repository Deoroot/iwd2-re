#!/usr/bin/env python3
"""frida_orig.py - unattended Frida trace of the ORIGINAL IWD2.exe, with a verdict.

`vm.sh smoke` does this for our build: launch it, load a save, watch for a
fault, count hits, print RESULT and exit with a code.  This is the counterpart
for the original binary -- the one we have no source for and therefore the only
place a runtime question can actually be answered.

  scripts/vm.sh trace --hooks scripts/probes/store_ownership.json --load-slot 3
  python scripts/frida_orig.py --hooks h.json --load-slot 3 --out C:\\iwd2-re\\t.jsonl

It eats the same hook table as frida_probe.py (schema in frida_hooks.py) and
adds: getting the game into a loaded save without a human, a crash handler, hit
counting, and a machine-readable verdict.

WHY IT SPAWNS RATHER THAN ATTACHES: the interesting questions are usually about
startup and load order (when does m_idLocalPlayer become 1?), and attaching to a
game that already reached the main menu has missed all of it.

HOW IT DRIVES THE MENU: by CALLING the engine, not by sending keystrokes.  Our
build's auto-load is two engine calls (CScreenConnection.cpp:816-827 ->
OnLoadGameButtonClick(FALSE), CScreenLoad.cpp:219-248 -> LoadGame(nSlot)); the
original has those same two functions, so we hook the enclosing update, then
call them through a NativeFunction.  Zero struct offsets, no window focus, no
timing races -- and it replaces the 172KB of synthetic Escape/Space keys in
frida_intro_trace.py.  Input-driving order per CLAUDE.md: engine call first.

--load-slot is a VISIBLE ROW of the load screen, the same number our build's
--slot takes (CScreenLoad.cpp:240-246), so the two sides stay comparable.

KNOWN LIMITATION -- the load driver does not land yet.  Everything up to the
main menu works: the movies are skipped, the service-provider enumeration
finishes, popup 19 is dismissed, m_bAllowInput reads 1 and m_nProtocol reads 0.
The Load click itself then blocks on the UI manager's critical section
(CScreenConnection.cpp:1142-1143): GetPanel(6) at :1145 never runs and a thread
parks in NtWaitForAlertByThreadId while every engine thread idles.  Issuing it
from onEnter, from onLeave, and from the engine's own mid-body call site (after
CUIManager::TimerAsynchronousUpdate) all block identically, so the section is
held by another thread rather than by our own call path.  Until that is
understood, --load-slot ends in RESULT: NOT-LOADED and exit 2 -- it reports the
failure instead of passing a run that proved nothing.  Traces that do not need a
save (startup, menu, network setup) work today: pass --load-slot -1.
NEXT LEAD: dispatch the click through the engine's own input path
(OnLButtonDown/Up at the control's centre, the way src/AutoUI.cpp does it in our
build) instead of calling the handler directly.

NOT for our build: its addresses are a different image entirely, and the debug
build's +0xA base shift breaks Frida member reads (53fe6dccc766).  Instrument
our side with Iwd2DebugLog and diff the two logs -- docs/frida-differential-tracing.md.

Output contract (what vm.sh trace parses, and what to read by hand):
  <out>          JSONL, one record per hook hit, fsync'd per line
  <out>.status   plain lines: `loaded: ...`, `HITS <name> <n>`, `RESULT: <verdict>`
Verdicts: CLEAN 0 | CRASH 1 | NOT-LOADED 2 | NOT-EXERCISED 2.
NOT-LOADED and NOT-EXERCISED both mean "ran, proved nothing" -- exit 2, never a
silent pass (see the --hit lesson behind vm.sh smoke).

The six gotchas of a session-1 payload all apply and are handled here; they are
enumerated in frida_probe.py's docstring.  Read them before editing this file.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_hooks import (  # noqa: E402
    PRELUDE,
    hit_counts_js,
    hooks_js,
    install_dump_js,
)

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
ORIG_EXE = os.path.join(GAME_DIR, "IWD2.exe")

# The load driver's call sites, all resolved from our source's // 0xADDR markers.
CONN_UPDATE     = 0x5FB3E0   # CScreenConnection::TimerAsynchronousUpdate
CONN_READY      = 0x601CB0   # CScreenConnection::AutoSelectServiceProvider
CONN_DISMISS    = 0x5FC850   # CScreenConnection::DismissPopup
UIMGR_UPDATE    = 0x4D3F00   # CUIManager::TimerAsynchronousUpdate
UIMGR_OFF       = 0x0030     # CBaldurEngine::m_cUIManager
CONN_PROTOCOL   = 0x0466     # CScreenConnection::m_nProtocol      (0 = single player)
CONN_ALLOWINPUT = 0x04B2     # CScreenConnection::m_bAllowInput
LOAD_BUTTON     = 0x5FCA00   # CScreenConnection::OnLoadGameButtonClick(BOOL bQuick)
LOAD_ACTIVATED  = 0x63B410   # CScreenLoad::EngineActivated
LOAD_GAME       = 0x63BE80   # CScreenLoad::LoadGame(INT nSlot)
WORLD_ACTIVATED = 0x6869C0   # CScreenWorld::EngineActivated -- the "loaded" signal
PLAY_MOVIE_INT  = 0x43F230   # CBaldurProjector::PlayMovieInternal(const CResRef&, BOOL)

# The intro movies are not cosmetic here, they are a hard block: the connection
# screen runs ONE update, then PlayMovieInternal does SelectEngine(projector)
# (CBaldurProjector.cpp:441-446) and the menu stops updating until the movie is
# over -- measured, 1 TimerAsynchronousUpdate in 32714 engine ticks.  Replacing
# that one chokepoint with a no-op means the projector is never selected at all,
# so the menu keeps ticking and the load driver gets its turn.  Every caller goes
# through it (PlayMovie only clears the queue first), and it is a cleaner skip
# than marking the movies played in the game's ini, which would change the user's
# install rather than this one run.
SKIP_MOVIES_JS = """
Interceptor.replace(ptr(%(play_movie)#x), new NativeCallback(function (thiz, resref, asynch) {
  send({ tag: 'drive', step: 'skipMovie' });
}, 'void', ['pointer', 'pointer', 'int'], 'thiscall'));
"""

# Hooking onLeave, never mid-function: Frida's 5-byte x86 trampoline corrupts a
# branch cluster and takes the game down with it (docs/frida-differential-tracing.md:101).
DRIVER_JS = """
const LOAD_SLOT = %(slot)d;
const SETTLE = %(settle)d;
const FALLBACK_TICKS = %(fallback)d;
let ticks = 0, settled = 0, inputReady = false, dismissed = false;
let clicked = false, loading = false, connThis = null;

const OnLoadGameButtonClick = new NativeFunction(ptr(%(load_button)#x), 'void',
                                                ['pointer', 'int'], 'thiscall');
const LoadGame = new NativeFunction(ptr(%(load_game)#x), 'void',
                                    ['pointer', 'int'], 'thiscall');
const DismissPopup = new NativeFunction(ptr(%(conn_dismiss)#x), 'void',
                                        ['pointer'], 'thiscall');

// The connection screen comes up with popup 19 ("Finding the network devices on
// this host...") and m_bAllowInput FALSE, and stays that way until the service
// provider enumeration counts down (CScreenConnection.cpp:701-746).  Clicking
// before that FREEZES the game -- measured: the engine tick stopped dead on the
// click, twice, identically.  AutoSelectServiceProvider is called on the very
// line that sets m_bAllowInput = TRUE, so it is the "input is live" event.
// (DismissPopup is NOT: the original skips it when byte_8F376C is set, and it
// never fired in any measured run.)
Interceptor.attach(ptr(%(conn_ready)#x), {
  onEnter() { if (!inputReady) { inputReady = true; send({ tag: 'drive', step: 'inputReady', ticks: ticks }); } }
});

Interceptor.attach(ptr(%(conn_update)#x), {
  onEnter() { connThis = this.context.ecx; ticks++; }
});

// WHERE the click is issued matters as much as when. OnLoadGameButtonClick opens
// by taking the UI manager's critical section (CScreenConnection.cpp:1142-1143);
// issued from either boundary of TimerAsynchronousUpdate that acquire never
// returns -- measured from both onEnter and onLeave: the click record is the last
// line of the trace, GetPanel(6) at :1145 never runs, and a thread sits in
// NtWaitForAlertByThreadId while every engine thread idles.
// The engine's own call site is mid-body (:816), right after
// m_cUIManager.TimerAsynchronousUpdate() at :813 -- a position no hook on the
// enclosing function's boundaries can reach. So we hook that inner call instead
// and drive from ITS onLeave, which lands exactly where the engine would have
// called, in the lock state it expects. m_cUIManager is CBaldurEngine+0x30, which
// is how we tell the connection screen's manager from every other screen's.
Interceptor.attach(ptr(%(uimgr_update)#x), {
  onEnter() { this.mgr = this.context.ecx; },
  onLeave() {
    if (clicked || LOAD_SLOT < 0 || connThis === null) return;
    if (!this.mgr.equals(connThis.add(%(uimgr_off)#x))) return;
    // FALLBACK_TICKS keeps a config that never reaches AutoSelectServiceProvider
    // from hanging here forever; it is a backstop, not the normal path.
    if (!inputReady && ticks < FALLBACK_TICKS) return;

    // Popup 19 has to go before the click. The engine dismisses it itself only
    // when byte_8F376C is clear, and that flag is set the moment the intro
    // movies are QUEUED (CScreenConnection.cpp:688) -- not when they finish. So
    // skipping the movies leaves the flag set and the engine skips its own
    // DismissPopup (:704), leaving the popup owning the screen while the Load
    // handler reaches for panel 6. Calling it is the branch the no-movie path
    // would have taken.
    if (!dismissed) {
      dismissed = true;
      send({ tag: 'drive', step: 'DismissPopup', ticks: ticks });
      DismissPopup(connThis);
      return;
    }
    if (++settled < SETTLE) return;
    clicked = true;
    send({ tag: 'drive', step: 'OnLoadGameButtonClick', ticks: ticks,
           allowInput: guard(() => connThis.add(%(allow_input)#x).readU32()),
           protocol: guard(() => connThis.add(%(protocol)#x).readS32()),
           pGame: guard(() => ptr(0x8CF6DC).readPointer().add(0x1C54).readPointer().toString()) });
    OnLoadGameButtonClick(connThis, 0);
  }
});

Interceptor.attach(ptr(%(load_activated)#x), {
  onEnter() { this.thiz = this.context.ecx; },
  onLeave() {
    if (loading || LOAD_SLOT < 0) return;
    loading = true;
    send({ tag: 'drive', step: 'LoadGame', slot: LOAD_SLOT });
    LoadGame(this.thiz, LOAD_SLOT);
  }
});

Interceptor.attach(ptr(%(world_activated)#x), {
  onEnter() { send({ tag: 'loaded', detail: 'world engine activated' }); }
});
"""

# Lifted from frida_crash_guard.py: same EBP walk, same 'system' pass-through so
# an OutputDebugString exception does not read as a fault, same `return false`
# so the crash stays faithful.  The original has no PDB, so frames go out raw
# and vm.sh symbolizes them host-side through sym.py addr2fn.
CRASH_JS = """
Process.setExceptionHandler(function (d) {
  if (d.type === 'system') return false;
  const ctx = d.context;
  send({ tag: 'crash', type: d.type, address: d.address.toString(),
         memory: d.memory ? { operation: d.memory.operation,
                              address: d.memory.address.toString() } : null,
         eip: ctx.eip.toString(), frames: guard(() => ebpwalk(ctx.ebp, 24)) });
  return false;
});
"""


class Trace:
    def __init__(self, out: Path, status: Path):
        self.out = open(out, "w", encoding="utf-8", errors="replace")
        self.status = open(status, "w", encoding="utf-8", errors="replace")
        self.loaded = False
        self.loaded_detail = ""
        self.crash = None
        self.counts: dict[str, int] = {}

    def _write(self, fp, line: str) -> None:
        fp.write(line + "\n")
        fp.flush()
        os.fsync(fp.fileno())   # gotcha 3: a crash is the interesting case

    def record(self, payload) -> None:
        self._write(self.out, json.dumps(payload))

    def say(self, line: str) -> None:
        """A status line -- this is what the host polls, so it goes to both."""
        self._write(self.status, line)
        print(line, flush=True)

    def on_message(self, message, data) -> None:
        if message["type"] != "send":
            self.record({"tag": "ERROR", "message": message})
            return
        payload = message["payload"]
        tag = payload.get("tag")
        if tag == "HITS":
            self.counts = payload.get("counts", {})
            return          # the timer fires every 2s; only the totals matter
        self.record(payload)
        if tag == "loaded" and not self.loaded:
            self.loaded = True
            self.loaded_detail = payload.get("detail", "")
            self.say(f"loaded: {self.loaded_detail}")
        elif tag == "crash" and self.crash is None:
            self.crash = payload

    def finish(self, drove: bool, hit: str | None, hit_min: int) -> int:
        for name, n in sorted(self.counts.items()):
            self.say(f"HITS {name} {n}")

        if self.crash is not None:
            c = self.crash
            self.say(f"EXCEPTION {c.get('type')} @ {c.get('address')} eip={c.get('eip')}")
            for frame in c.get("frames") or []:
                self.say(f"  frame {frame}")
            verdict, rc = "CRASH", 1
        elif drove and not self.loaded:
            verdict, rc = "NOT-LOADED", 2
        elif hit and self.counts.get(hit, 0) < hit_min:
            verdict, rc = "NOT-EXERCISED", 2
        else:
            verdict, rc = "CLEAN", 0

        note = ""
        if hit:
            note = f"  ({hit} hit x{self.counts.get(hit, 0)})"
        elif verdict == "CLEAN" and not self.counts:
            note = "  (no hits -- nothing proved the path ran)"
        self.say(f"RESULT: {verdict}{note}")
        self.out.close()
        self.status.close()
        return rc


def build_js(spec: dict, slot: int, settle: int, skip_movies: bool = True) -> str:
    hooks = spec["hooks"]
    blocks = ["'use strict';", PRELUDE, CRASH_JS]
    if skip_movies:
        blocks.append(SKIP_MOVIES_JS % {"play_movie": PLAY_MOVIE_INT})
    # Always installed: with --load-slot -1 the driving halves self-gate, but the
    # "loaded" signal is still worth having.
    blocks.append(DRIVER_JS % {
        "slot": slot, "settle": settle, "fallback": max(600, settle * 30),
        "conn_update": CONN_UPDATE, "conn_ready": CONN_READY,
        "conn_dismiss": CONN_DISMISS,
        "uimgr_update": UIMGR_UPDATE, "uimgr_off": UIMGR_OFF,
        "allow_input": CONN_ALLOWINPUT, "protocol": CONN_PROTOCOL,
        "load_button": LOAD_BUTTON,
        "load_activated": LOAD_ACTIVATED, "load_game": LOAD_GAME,
        "world_activated": WORLD_ACTIVATED,
    })
    blocks.append(hooks_js(hooks))
    blocks.append(install_dump_js(hooks))
    blocks.append(hit_counts_js(hooks))
    blocks.append("send({ tag: 'ready', hooks: %d });" % len(hooks))
    return "\n".join(blocks)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--hooks", required=True, help="hook spec JSON (schema: frida_hooks.py)")
    ap.add_argument("--out", default=r"C:\iwd2-re\tmp_orig_trace.jsonl")
    ap.add_argument("--status", help="verdict file (default: <out>.status)")
    ap.add_argument("--load-slot", type=int, default=3,
                    help="visible load-screen row to load; -1 leaves the game at the menu")
    ap.add_argument("--settle-ticks", type=int, default=10,
                    help="menu update ticks to wait AFTER the enumeration popup clears")
    ap.add_argument("--post-load", type=float, default=20.0,
                    help="seconds to keep tracing once the save is up")
    ap.add_argument("--timeout", type=float, default=180.0, help="hard cap, from spawn")
    ap.add_argument("--hit", help="hook name that must fire, else NOT-EXERCISED")
    ap.add_argument("--hit-min", type=int, default=1)
    ap.add_argument("--no-skip-movies", dest="skip_movies", action="store_false",
                    help="let the intro movies play (they block the menu -- see SKIP_MOVIES_JS)")
    ap.add_argument("--exe", default=ORIG_EXE)
    ns = ap.parse_args()

    spec = json.loads(Path(ns.hooks).read_text())
    out = Path(ns.out)
    status = Path(ns.status) if ns.status else Path(str(out) + ".status")
    trace = Trace(out, status)
    trace.say(f"=== spawning {ns.exe} (slot {ns.load_slot}, {len(spec['hooks'])} hooks) ===")

    pid = frida.spawn(ns.exe, cwd=GAME_DIR)
    session = frida.attach(pid)
    script = session.create_script(
        build_js(spec, ns.load_slot, ns.settle_ticks, ns.skip_movies))
    script.on("message", trace.on_message)
    script.load()
    frida.resume(pid)
    trace.say(f"ARMED pid={pid}")

    deadline = time.monotonic() + ns.timeout
    load_deadline = None
    while time.monotonic() < deadline:
        if trace.crash is not None:
            break
        if trace.loaded and load_deadline is None:
            load_deadline = time.monotonic() + ns.post_load
        if load_deadline is not None and time.monotonic() >= load_deadline:
            break
        time.sleep(0.5)     # gotcha 1: there is no stdin to block on

    rc = trace.finish(drove=ns.load_slot >= 0, hit=ns.hit, hit_min=ns.hit_min)

    # Kill policy: the original is fair to kill once the trace is done -- leaving
    # it idle burns VM CPU for nothing.
    try:
        frida.kill(pid)
    except Exception:
        pass
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
