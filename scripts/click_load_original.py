#!/usr/bin/env python3
"""Experimental UI driver: launch iwd2-re and click Load Game + first save slot.

This uses real Win32 mouse input (SetCursorPos + mouse_event), not PostMessage.
Coordinates are client-area pixels for the 800x600 window.
"""
from __future__ import annotations

import argparse
import ctypes
import subprocess
import time
from pathlib import Path
from ctypes import wintypes

REPO = Path(__file__).resolve().parents[1]
GAME_DIR = Path(r"C:\GOG Games\Icewind Dale 2")
EXE = REPO / "build" / "Debug" / "iwd2-re.exe"

user32 = ctypes.WinDLL("user32", use_last_error=True)

EnumWindows = user32.EnumWindows
EnumWindows.argtypes = [ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM), wintypes.LPARAM]
EnumWindows.restype = wintypes.BOOL

GetWindowThreadProcessId = user32.GetWindowThreadProcessId
GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
GetWindowThreadProcessId.restype = wintypes.DWORD

IsWindowVisible = user32.IsWindowVisible
IsWindowVisible.argtypes = [wintypes.HWND]
IsWindowVisible.restype = wintypes.BOOL

SetForegroundWindow = user32.SetForegroundWindow
SetForegroundWindow.argtypes = [wintypes.HWND]
SetForegroundWindow.restype = wintypes.BOOL

ShowWindow = user32.ShowWindow
ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
ShowWindow.restype = wintypes.BOOL

ClientToScreen = user32.ClientToScreen
ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
ClientToScreen.restype = wintypes.BOOL

SetCursorPos = user32.SetCursorPos
SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
SetCursorPos.restype = wintypes.BOOL

mouse_event = user32.mouse_event
mouse_event.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
mouse_event.restype = None

MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
SW_RESTORE = 9


def find_window_for_pid(pid: int, timeout: float = 15.0) -> int:
    deadline = time.time() + timeout
    hwnd_result = 0

    CALLBACK = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def enum_proc(hwnd, _):
        nonlocal hwnd_result
        if not IsWindowVisible(hwnd):
            return True
        win_pid = wintypes.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(win_pid))
        if win_pid.value == pid:
            hwnd_result = hwnd
            return False
        return True

    cb = CALLBACK(enum_proc)
    while time.time() < deadline:
        EnumWindows(cb, 0)
        if hwnd_result:
            return hwnd_result
        time.sleep(0.2)
    raise RuntimeError(f"window for pid {pid} not found")


def click_client(hwnd: int, x: int, y: int, pause: float = 0.08) -> None:
    pt = wintypes.POINT(x, y)
    if not ClientToScreen(hwnd, ctypes.byref(pt)):
        raise ctypes.WinError(ctypes.get_last_error())
    SetCursorPos(pt.x, pt.y)
    time.sleep(0.05)
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, None)
    time.sleep(pause)
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, None)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-launch", action="store_true", help="attach to an already launched process is not implemented")
    ap.add_argument("--menu-wait", type=float, default=2.0)
    ap.add_argument("--load-wait", type=float, default=1.0)
    ap.add_argument("--load-menu", default="620,287", help="client x,y for Load Game on main menu")
    ap.add_argument("--slot", default="720,211", help="client x,y for first load slot")
    args = ap.parse_args()

    if args.no_launch:
        raise SystemExit("--no-launch not implemented yet")

    proc = subprocess.Popen([str(EXE)], cwd=str(GAME_DIR))
    hwnd = find_window_for_pid(proc.pid)
    ShowWindow(hwnd, SW_RESTORE)
    SetForegroundWindow(hwnd)

    print(f"pid={proc.pid} hwnd=0x{hwnd:X}; waiting {args.menu_wait}s")
    time.sleep(args.menu_wait)

    x, y = map(int, args.load_menu.split(","))
    print(f"click Load Game @ {x},{y}")
    click_client(hwnd, x, y)

    time.sleep(args.load_wait)
    x, y = map(int, args.slot.split(","))
    print(f"click first save slot @ {x},{y}")
    click_client(hwnd, x, y)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
