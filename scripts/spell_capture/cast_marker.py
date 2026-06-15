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

  --mode frida   ORIGINAL IWD2.exe (no source): attach by name and hook the cast
                 action entries CGameSprite::Spell @0x740270 and
                 SpellPointSequence @0x742840 (__thiscall, this=ecx=sprite),
                 reading the resref from this->m_curAction.m_string1 (CString
                 char* at this+0x538; m_curAction @+0x476, m_string1 @CAIAction+0xC2,
                 verified vs the binary). These are per-tick state machines so the
                 entry fires every casting tick; the host recorder debounces a
                 cast's ticks into one clip. We ATTACH (never spawn) so the
                 original instance / kill-policy is untouched.

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
// CGameSprite::Spell (target) + SpellPointSequence (point): the two player
// cast-action handlers. __thiscall, this = ecx = the casting sprite. The spell
// resref is this->m_curAction.m_string1, an MFC CString whose char* is stored at
// this+0x538 (m_curAction @+0x476, m_string1 @CAIAction+0xC2 -- 0x41E1B0 reads it
// at ecx+0xC2). Resref is <=8 chars, NUL-terminated.
var SPELL   = ptr(0x740270);
var SPELLPT = ptr(0x742840);
var M_STRING1 = 0x538;

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

[SPELL, SPELLPT].forEach(function (addr) {
    Interceptor.attach(addr, {
        onEnter: function (args) {
            var rr = readResRef(this.context.ecx);
            if (rr && rr.length) { send({spell: rr}); }
        }
    });
});
send({hooked: "CGameSprite::Spell 0x740270 + SpellPointSequence 0x742840"});
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
    print("[marker] hooked CGameSprite::Spell + SpellPointSequence; cast a spell.",
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
