#!/usr/bin/env python3
r"""VM-side spell-cast marker forwarder (runs inside the Win11 VM).

Detects every PLAYER spell cast and forwards it to the host recorder as a UDP
datagram (guest -> host gateway 10.0.2.2 over QEMU user-mode networking):

    {"exe": "orig"|"ours", "spell": "<resref>", "ts": <guest epoch>}

The trigger is the player's spell-cast ACTION (CGameSprite::Spell /
SpellPointSequence), keyed by the spell resref, NOT the projectile factory: that
older hook (DecodeProjectile) fired per-projectile -- spamming on enemy/ambient
projectiles in combat and missing non-projectile spells (cones like Color Spray).
Enemies cast via ForceSpell (CGameAIBase::ForceSpellAction @0x461190), a
different path, so hooking the player cast actions is a clean player filter.

Two modes, one for each build under test:

  --mode frida   ORIGINAL IWD2.exe (no source): attach by name and hook
                 CGameSprite::ApplyCastingEffect @0x755A70 -- the visual cast-start
                 -- filtered by the call return address to the two PLAYER cast
                 actions CGameSprite::Spell (ret 0x741B22) and SpellPointSequence
                 (ret 0x743DBB). ApplyCastingEffect is also called from the enemy
                 ForceSpell paths (0x461190 / ForceSpellPointAction); excluding
                 those keeps the trigger to player casts AND fires it once per cast
                 at the same point our build logs, so orig/ours clips line up.
                 __thiscall, this=ecx=sprite; resref = this->m_curAction.m_string1
                 (CString char* at this+0x538; m_curAction @+0x476,
                 m_string1 @CAIAction+0xC2, verified vs the binary). We ATTACH
                 (never spawn) so the original instance / kill-policy is untouched.

  --mode tail    OUR iwd2-re.exe: tail iwd2-re-debug.log for the "CAST spell=<resref>"
                 line emitted once per cast by CGameSprite::Spell /
                 SpellPointSequence (gated by .\iwd2-re-debug.enabled). No Frida,
                 no addresses.

Designed to be shipped + run as a vm.sh session-1 payload: it never reads stdin
and keeps itself alive, so the fire-and-forget VBS parent exiting is harmless.
"""
import argparse
import json
import os
import re
import socket
import sys
import threading
import time

DEFAULT_LOG = r"C:\GOG Games\Icewind Dale 2\iwd2-re-debug.log"
CAST_RE = re.compile(r"CAST\s+spell=([A-Za-z0-9_]+)")

FRIDA_SCRIPT = r"""
// Hook CGameSprite::ApplyCastingEffect (0x755A70) = the visual cast-start, and
// keep ONLY the calls coming from the PLAYER cast actions, filtered by the call
// return address: CGameSprite::Spell (ret 0x741B22) and SpellPointSequence
// (ret 0x743DBB). ApplyCastingEffect is also reached from the enemy ForceSpell
// paths (0x461190 / ForceSpellPointAction) -- excluding those keeps the trigger
// to player casts and fires it exactly once per cast (the source gates the call
// on !m_bStartedCasting / m_castCounter==0), at the same point our debug build
// logs, so orig and ours clips line up. __thiscall: this=ecx=sprite; the spell
// resref is this->m_curAction.m_string1, an MFC CString whose char* is stored at
// this+0x538 (m_curAction @+0x476, m_string1 @CAIAction+0xC2). NUL-terminated.
var APPLY = ptr(0x755A70);
var M_STRING1 = 0x538;
var RET_SPELL   = ptr(0x741B22);   // call+5 of ApplyCastingEffect in Spell
var RET_SPELLPT = ptr(0x743DBB);   // call+5 of ApplyCastingEffect in SpellPointSequence

function readResRef(self) {
    if (self.isNull()) { return null; }
    try {
        var p = self.add(M_STRING1).readPointer();   // CString m_pchData
        if (p.isNull()) { return null; }
        // readCString() with NO size reads until the NUL terminator. (Passing a
        // size reads that many FIXED bytes and ignores the NUL, so a 7-char
        // resref came back as "SPWI304" + 1 garbage byte.) MFC CString is always
        // NUL-terminated; slice to the resref charset as a final guard.
        var s = p.readCString();
        if (!s) { return null; }
        var m = /^[A-Za-z0-9_]{1,8}/.exec(s);
        return m ? m[0] : null;
    } catch (e) {
        return null;
    }
}

Interceptor.attach(APPLY, {
    onEnter: function (args) {
        var ret = this.returnAddress;
        if (!ret.equals(RET_SPELL) && !ret.equals(RET_SPELLPT)) { return; }
        var rr = readResRef(this.context.ecx);
        if (rr && rr.length) { send({spell: rr}); }
    }
});
send({hooked: "CGameSprite::ApplyCastingEffect 0x755A70 (player casts only)"});
"""


def _find_game_window_rect():
    """(x, y, w, h) of the iwd2 game window CLIENT area in screen coords, or None.

    Runs in-process in session 1, where EnumWindows sees the game's window (the
    host can't: MainWindowHandle is 0 across the session-0/1 boundary).
    """
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    # CRITICAL on 64-bit: declare arg/return types so HWND/HANDLE are passed at
    # pointer width. Without this ctypes truncates them to 32-bit and GetClientRect
    # silently returns garbage (we were getting the window rect, not the client).
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.GetClientRect.restype = wintypes.BOOL
    user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    user32.ClientToScreen.restype = wintypes.BOOL
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.QueryFullProcessImageNameW.argtypes = [
        wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    user32.GetWindowTextLengthW.restype = ctypes.c_int
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.restype = ctypes.c_int
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    targets = ("iwd2-re.exe", "iwd2.exe")

    def win_title(hwnd):
        n = user32.GetWindowTextLengthW(hwnd)
        if n <= 0:
            return ""
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        return buf.value

    def proc_name(pid):
        h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not h:
            return ""
        try:
            buf = ctypes.create_unicode_buffer(512)
            size = wintypes.DWORD(512)
            if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
                return buf.value.rsplit("\\", 1)[-1]
            return ""
        finally:
            kernel32.CloseHandle(h)

    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if proc_name(pid.value).lower() in targets:
            found.append(hwnd)
        return True       # collect ALL matching windows, don't stop at the first

    user32.EnumWindows(cb, 0)
    # Prefer the window TITLED "Icewind Dale" (the real game window, same title in
    # our build and the original); fall back to the largest. Avoids the background/
    # sub windows that made the geom flap.
    titled = [h for h in found if "icewind" in win_title(h).lower()]
    candidates = titled or found
    best, best_area = None, 0
    for hwnd in candidates:
        rect = wintypes.RECT()
        if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
            continue
        w, h = rect.right - rect.left, rect.bottom - rect.top
        if w * h > best_area:
            pt = wintypes.POINT(0, 0)
            if not user32.ClientToScreen(hwnd, ctypes.byref(pt)):
                continue
            best, best_area = (pt.x, pt.y, w, h), w * h
    return best if best_area > 0 else None


def geom_reporter(host, port, interval, stop):
    """Periodically send the game window rect so the host crops clips to it."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    last = None
    while not stop.is_set():
        try:
            rect = _find_game_window_rect()
        except Exception as e:                        # noqa: BLE001
            rect = None
            print(f"[marker] geom probe error: {e}", flush=True)
        if rect:
            try:
                sock.sendto(json.dumps({"geom": 1, "x": rect[0], "y": rect[1],
                                        "w": rect[2], "h": rect[3]}).encode(),
                            (host, port))
            except OSError:
                pass
            if rect != last:
                print(f"[marker] geom {rect} -> {host}:{port}", flush=True)
                last = rect
        stop.wait(interval)


def make_sender(host, port, exe):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(resref):
        resref = str(resref).strip().upper()
        if not resref:
            return
        payload = json.dumps({"exe": exe, "spell": resref,
                              "ts": time.time()}).encode("utf-8")
        try:
            sock.sendto(payload, (host, port))
        except OSError as e:
            print(f"[marker] UDP send failed: {e}", flush=True)
            return
        print(f"[marker] CAST exe={exe} spell={resref} -> {host}:{port}",
              flush=True)

    return send


def run_frida(args, send):
    import frida
    target = args.target
    print(f"[marker] frida mode: waiting for {target} ...", flush=True)
    session = None
    while session is None:
        try:
            session = frida.attach(target)
        except frida.ProcessNotFoundError:
            time.sleep(1.0)
        except Exception as e:                            # noqa: BLE001
            print(f"[marker] attach error: {e}; retrying", flush=True)
            time.sleep(1.0)
    print(f"[marker] attached to {target}", flush=True)
    script = session.create_script(FRIDA_SCRIPT)

    def on_message(message, data):
        if message.get("type") != "send":
            print(f"[marker] frida: {message}", flush=True)
            return
        p = message.get("payload", {})
        if "spell" in p:
            send(p["spell"])

    script.on("message", on_message)
    script.load()
    print("[marker] hooked ApplyCastingEffect (player casts only); cast a spell.",
          flush=True)
    while True:                                  # keep-alive (no stdin)
        time.sleep(0.5)


def run_tail(args, send):
    path = args.log
    print(f"[marker] tail mode: {path}", flush=True)
    # wait for the file, then follow from the end
    while not os.path.exists(path):
        print("[marker] waiting for debug log (is .iwd2-re-debug.enabled set?)",
              flush=True)
        time.sleep(2.0)
    with open(path, "r", encoding="latin-1", errors="replace") as f:
        f.seek(0, os.SEEK_END)
        while True:
            line = f.readline()
            if not line:
                time.sleep(0.2)
                continue
            m = CAST_RE.search(line)
            if m:
                send(m.group(1))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", required=True, choices=["frida", "tail"])
    ap.add_argument("--host", default="10.0.2.2", help="host gateway (user-mode net)")
    ap.add_argument("--port", type=int, default=48888)
    ap.add_argument("--exe", default="", help="orig|ours (default by mode)")
    ap.add_argument("--target", default="IWD2.exe", help="frida: process to attach")
    ap.add_argument("--log", default=DEFAULT_LOG, help="tail: debug log path")
    ap.add_argument("--no-geom", action="store_true",
                    help="don't report the game window rect (host won't auto-crop)")
    ap.add_argument("--geom-interval", type=float, default=2.0)
    args = ap.parse_args()

    if not args.exe:
        args.exe = "orig" if args.mode == "frida" else "ours"
    send = make_sender(args.host, args.port, args.exe)
    print(f"[marker] forwarding to {args.host}:{args.port} as exe={args.exe}",
          flush=True)

    if not args.no_geom:
        threading.Thread(target=geom_reporter,
                         args=(args.host, args.port, args.geom_interval,
                               threading.Event()), daemon=True).start()
        print("[marker] geom reporter started (auto-crop to game window)",
              flush=True)
    try:
        if args.mode == "frida":
            run_frida(args, send)
        else:
            run_tail(args, send)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
