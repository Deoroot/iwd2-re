#!/usr/bin/env python3
"""Short Frida trace for the iwd2-re intro dialogue/journal startup path."""
from __future__ import annotations

import argparse
import configparser
import ctypes
import ctypes.wintypes
import json
import os
import re
import subprocess
import struct
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path

import frida

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
gdi32 = ctypes.windll.gdi32
try:
    user32.SetProcessDPIAware()
except Exception:
    pass

VK_1 = 0x31
VK_4 = 0x34
VK_ESCAPE = 0x1B
VK_SPACE = 0x20
KEYEVENTF_KEYUP = 0x0002
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
SW_RESTORE = 9
WM_MOUSEMOVE = 0x0200
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
MK_LBUTTON = 0x0001
SRCCOPY = 0x00CC0020
BI_RGB = 0
DIB_RGB_COLORS = 0

NEW_GAME_BUTTON = (645, 263)
PARTY_ROWS = [
    (124, 150),
    (124, 212),
    (124, 273),
    (124, 337),
    (124, 400),
    (124, 462),
]
PARTY_DONE_BUTTON = (537, 562)
CHAPTER_DONE_BUTTON = (514, 549)
CHAPTER_VISIBLE_BEFORE_CAPTURE_SECONDS = 1.0
CHAPTER_AUDIO_GRACE_AFTER_CAPTURE_SECONDS = 2.0

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parent if SCRIPT_DIR.name.lower() == "scripts" else SCRIPT_DIR
GAME_DIR = Path(r"C:\GOG Games\Icewind Dale 2")
RE_EXE = REPO / "build" / "Debug" / "iwd2-re.exe"
ORIG_EXE = GAME_DIR / "IWD2.exe"
PARTY_INI = GAME_DIR / "Party.ini"
LOG = REPO / "tmp_frida_intro_trace.log"


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", ctypes.wintypes.DWORD),
        ("biWidth", ctypes.wintypes.LONG),
        ("biHeight", ctypes.wintypes.LONG),
        ("biPlanes", ctypes.wintypes.WORD),
        ("biBitCount", ctypes.wintypes.WORD),
        ("biCompression", ctypes.wintypes.DWORD),
        ("biSizeImage", ctypes.wintypes.DWORD),
        ("biXPelsPerMeter", ctypes.wintypes.LONG),
        ("biYPelsPerMeter", ctypes.wintypes.LONG),
        ("biClrUsed", ctypes.wintypes.DWORD),
        ("biClrImportant", ctypes.wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER)]
MAP_FILE = REPO / "build" / "Debug" / "iwd2-re.map"
LINK_IMAGE_BASE = 0x400000

RE_HOOKS = {
    "CGameAIBase::ExecuteAction": 0x0B2FC0,
    "CGameAIBase::EvaluateStatusTrigger": 0x0B1DA0,
    "CGameSprite::EvaluateStatusTrigger": 0x1948D0,
    "CAICondition::Hold": 0x04B570,
    "CAIResponseSet::Choose": 0x0572C0,
    "CAIScript::Find": 0x057D20,
    "CGameAIBase::InsertResponse": 0x0C1E90,
    "CGameAIBase::GetNextAction": 0x0C1B00,
    "CGameAIBase::StartCutScene": 0x0BBF00,
    "CGameDialogSprite::StartDialog": 0x140B00,
    "CGameSprite::Dialogue": 0x1BCC50,
    "CScreenWorld::StartDialog": 0x34A3D0,
    "CGameJournal::AddEntry2": 0x17FEE0,
    "CGameJournal::AddEntry4": 0x17FFA0,
    "CGameJournal::SetQuestDone": 0x180B90,
    "CGameJournal::DeleteEntry": 0x1810F0,
    "CInfGame::SetCurrentChapter": 0x203220,
    "CScreenChapter::EngineActivated": 0x273470,
    "CScreenChapter::TimerAsynchronousUpdate": 0x273E60,
    "CScreenChapter::OnDoneButtonClick": 0x275370,
    "CScreenChapter::StartChapter": 0x2756D0,
    "CScreenChapter::StartChapterMultiplayerHost": 0x2757F0,
    "CScreenChapter::StartText": 0x2749D0,
    "CScreenChapter::ResetMainPanel": 0x273600,
    "CSoundMixer::StartSong": 0x365F00,
    "CSound::Play": 0x3618E0,
    "CInfGame::NewGame": 0x1F0900,
    "CInfGame::SaveGame": 0x1EB580,
    "CInfGame::LoadGame": 0x1EFFC0,
    "CInfGame::Unmarshal": 0x1EC260,
    "CMessageSaveGame::Run": 0x23D7C0,
}

RE_MAP_SYMBOLS = {
    "CGameAIBase::ExecuteAction": "?ExecuteAction@CGameAIBase@@UAEFXZ",
    "CGameAIBase::EvaluateStatusTrigger": "?EvaluateStatusTrigger@CGameAIBase@@UAEHABVCAITrigger@@@Z",
    "CGameSprite::EvaluateStatusTrigger": "?EvaluateStatusTrigger@CGameSprite@@UAEHABVCAITrigger@@@Z",
    "CAICondition::Hold": "?Hold@CAICondition@@QAEEAAV?$CTypedPtrList@VCPtrList@@PAVCAITrigger@@@@PAVCGameAIBase@@@Z",
    "CAIResponseSet::Choose": "?Choose@CAIResponseSet@@QAEPAVCAIResponse@@XZ",
    "CAIScript::Find": "?Find@CAIScript@@QAEPAVCAIResponse@@AAV?$CTypedPtrList@VCPtrList@@PAVCAITrigger@@@@PAVCGameAIBase@@@Z",
    "CGameAIBase::InsertResponse": "?InsertResponse@CGameAIBase@@QAEXAAVCAIResponse@@HH@Z",
    "CGameAIBase::GetNextAction": "?GetNextAction@CGameAIBase@@QAEAAVCAIAction@@AAV2@@Z",
    "CGameAIBase::StartCutScene": "?StartCutScene@CGameAIBase@@QAEFXZ",
    "CGameDialogSprite::StartDialog": "?StartDialog@CGameDialogSprite@@QAEHPAVCGameSprite@@@Z",
    "CGameSprite::Dialogue": "?Dialogue@CGameSprite@@QAEFPAV1@@Z",
    "CScreenWorld::StartDialog": "?StartDialog@CScreenWorld@@QAEHPAVCGameSprite@@0EE@Z",
    "CGameJournal::AddEntry2": "?AddEntry@CGameJournal@@QAEHKG@Z",
    "CGameJournal::AddEntry4": "?AddEntry@CGameJournal@@QAEHKHJG@Z",
    "CGameJournal::SetQuestDone": "?SetQuestDone@CGameJournal@@QAEXK@Z",
    "CGameJournal::DeleteEntry": "?DeleteEntry@CGameJournal@@QAEXK@Z",
    "CInfGame::SetCurrentChapter": "?SetCurrentChapter@CInfGame@@QAEXH@Z",
    "CScreenChapter::EngineActivated": "?EngineActivated@CScreenChapter@@UAEXXZ",
    "CScreenChapter::TimerAsynchronousUpdate": "?TimerAsynchronousUpdate@CScreenChapter@@UAEXXZ",
    "CScreenChapter::OnDoneButtonClick": "?OnDoneButtonClick@CScreenChapter@@QAEXXZ",
    "CScreenChapter::StartChapter": "?StartChapter@CScreenChapter@@QAEXABVCResRef@@@Z",
    "CScreenChapter::StartChapterMultiplayerHost": "?StartChapterMultiplayerHost@CScreenChapter@@QAEXEPAE@Z",
    "CScreenChapter::StartText": "?StartText@CScreenChapter@@QAEHABVCResRef@@@Z",
    "CScreenChapter::ResetMainPanel": "?ResetMainPanel@CScreenChapter@@QAEXXZ",
    "CSoundMixer::StartSong": "?StartSong@CSoundMixer@@QAEXHK@Z",
    "CSound::Play": "?Play@CSound@@QAEHH@Z",
    "CInfGame::NewGame": "?NewGame@CInfGame@@QAEXEE@Z",
    "CInfGame::SaveGame": "?SaveGame@CInfGame@@QAEHEEE@Z",
    "CInfGame::LoadGame": "?LoadGame@CInfGame@@QAEXEE@Z",
    "CInfGame::Unmarshal": "?Unmarshal@CInfGame@@QAEHPAEJE@Z",
    "CMessageSaveGame::Run": "?Run@CMessageSaveGame@@UAEXXZ",
}

ORIG_HOOKS = {
    "CBaldurChitin::CBaldurChitin": 0x421E40,
    "CBaldurChitin::Init": 0x423800,
    "CChitin::InitApplication": 0x790FE0,
    "CChitin::InitGraphics": 0x791150,
    "CChitin::InitInstance": 0x790080,
    "CChitin::SelectEngine": 0x790860,
    "CChitin::AsynchronousUpdate": 0x78F0E0,
    "CChitin::WinMain": 0x7926B0,
    "CGameAIBase::ExecuteAction": 0x44DC10,
    "CGameAIBase::EvaluateStatusTrigger": 0x453840,
    "CGameSprite::EvaluateStatusTrigger": 0x731B30,
    "CAICondition::Hold": 0x404150,
    "CGameAIBase::InsertResponse": 0x45C300,
    "CGameAIBase::GetNextAction": 0x45B970,
    "CBaldurProjector::PlayMovieInternal": 0x43F230,
    "CBaldurProjector::TimerAsynchronousUpdate": 0x43F4C0,
    "CGameDialogSprite::StartDialog": 0x4839F0,
    "CGameSprite::Dialogue": 0x752DD0,
    "CScreenConnection::EngineActivated": 0x5FA9B0,
    "CScreenConnection::StartConnection": 0x600770,
    "CScreenConnection::TimerAsynchronousUpdate": 0x5FB3E0,
    "CScreenConnection::OnNewGameButtonClick": 0x5FD0A0,
    "CScreenChapter::EngineActivated": 0x5D3180,
    "CScreenSinglePlayer::EngineActivated": 0x660850,
    "CScreenSinglePlayer::TimerAsynchronousUpdate": 0x660D00,
    "CScreenSinglePlayer::OnDoneButtonClick": 0x6619C0,
    "CScreenSinglePlayer::OnMainDoneButtonClick": 0x6629E0,
    "CScreenSinglePlayer::OnPartySelectionDoneButtonClick": 0x6642C0,
    "CScreenChapter::TimerAsynchronousUpdate": 0x5D3600,
    "CScreenChapter::OnDoneButtonClick": 0x5D4190,
    "CScreenChapter::StartChapter": 0x5D4380,
    "CScreenChapter::StartChapterMultiplayerHost": 0x5D4450,
    "CScreenChapter::StartText": 0x5D4650,
    "CScreenChapter::ResetMainPanel": 0x5D3A80,
    "CSoundMixer::StartSong": 0x7AC4F0,
    "CSound::Play": 0x7A9B10,
    "CScreenWorld::StartDialog": 0x68EA00,
    "CGameJournal::AddEntry4": 0x4C63B0,
    "CGameJournal::SetQuestDone": 0x4C7220,
    "CGameJournal::DeleteEntry": 0x4C7560,
    "CInfGame::SetCurrentChapter": 0x435110,
    "CInfGame::NewGame": 0x5ABA20,
    "CInfGame::LoadGame": 0x5AB190,
    "CInfGame::Unmarshal": 0x5A7E40,
    "CInfGame::SaveGame": 0x5AC430,
}


def resolve_party(value: str) -> int:
    try:
        return int(value)
    except ValueError:
        pass

    parser = configparser.RawConfigParser()
    parser.read(PARTY_INI)

    wanted = value.strip().lower()
    for section in parser.sections():
        if not section.lower().startswith("party "):
            continue
        name = parser.get(section, "Name", fallback="").strip().lower()
        if name == wanted:
            return int(section.split()[1])

    raise SystemExit(f"party not found in {PARTY_INI}: {value!r}")


def read_result(path: Path) -> dict[str, str]:
    values = {}
    if not path.exists():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value
    return values


def resolve_re_hooks_from_map() -> dict[str, int]:
    hooks = dict(RE_HOOKS)
    if not MAP_FILE.exists():
        return hooks

    wanted = {decorated: name for name, decorated in RE_MAP_SYMBOLS.items()}
    found: set[str] = set()
    for line in MAP_FILE.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        decorated = parts[1]
        name = wanted.get(decorated)
        if name is None:
            continue
        if not re.fullmatch(r"[0-9A-Fa-f]{8}", parts[2]):
            continue
        hooks[name] = int(parts[2], 16) - LINK_IMAGE_BASE
        found.add(name)

    missing = sorted(set(RE_MAP_SYMBOLS) - found)
    if missing:
        raise SystemExit(f"missing symbols in {MAP_FILE}: {', '.join(missing)}")
    return hooks


def find_window_for_pid(pid: int) -> int:
    windows: list[tuple[int, int]] = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    def enum_proc(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        proc_id = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(proc_id))
        if proc_id.value == pid:
            rect = ctypes.wintypes.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            area = max(0, rect.right - rect.left) * max(0, rect.bottom - rect.top)
            windows.append((area, int(hwnd)))
        return True

    user32.EnumWindows(enum_proc, 0)
    if not windows:
        return 0
    windows.sort(reverse=True)
    return windows[0][1]


def game_surface_origin(hwnd: int) -> tuple[int, int]:
    window = ctypes.wintypes.RECT()
    client = ctypes.wintypes.RECT()
    origin = ctypes.wintypes.POINT(0, 0)
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    user32.GetClientRect(hwnd, ctypes.byref(client))
    user32.ClientToScreen(hwnd, ctypes.byref(origin))

    window_w = window.right - window.left
    window_h = window.bottom - window.top
    client_w = client.right - client.left
    client_h = client.bottom - client.top

    if 800 <= window_w <= 840 and 600 <= window_h <= 660:
        border_x = max((window_w - 800) // 2, 0)
        top_border = max((window_h - 600) - border_x, 0)
        return window.left + border_x, window.top + top_border

    if 800 <= client_w <= 840 and 600 <= client_h <= 660:
        border_x = max((client_w - 800) // 2, 0)
        top_border = max((client_h - 600) - border_x, 0)
        return origin.x + border_x, origin.y + top_border

    return origin.x, origin.y


def candidate_capture_origins(hwnd: int) -> list[tuple[str, int, int]]:
    window = ctypes.wintypes.RECT()
    origin = ctypes.wintypes.POINT(0, 0)
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    user32.ClientToScreen(hwnd, ctypes.byref(origin))

    candidates = [
        ("surface", *game_surface_origin(hwnd)),
        ("client", origin.x, origin.y),
        ("window", window.left, window.top),
    ]

    seen: set[tuple[int, int]] = set()
    unique: list[tuple[str, int, int]] = []
    for name, x, y in candidates:
        key = (x, y)
        if key not in seen:
            seen.add(key)
            unique.append((name, x, y))
    return unique


def make_lparam(x: int, y: int) -> int:
    return (y & 0xFFFF) << 16 | (x & 0xFFFF)


def post_mouse_click(hwnd: int, screen_x: int, screen_y: int) -> None:
    origin = ctypes.wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    client_x = screen_x - origin.x
    client_y = screen_y - origin.y
    user32.PostMessageW(hwnd, WM_MOUSEMOVE, 0, make_lparam(client_x, client_y))
    time.sleep(0.02)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, make_lparam(client_x, client_y))
    time.sleep(0.04)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, make_lparam(client_x, client_y))


def focus_window(hwnd: int, click: bool = False) -> bool:
    if hwnd == 0:
        return False

    foreground = user32.GetForegroundWindow()
    current_thread = kernel32.GetCurrentThreadId()
    target_thread = user32.GetWindowThreadProcessId(hwnd, None)
    foreground_thread = user32.GetWindowThreadProcessId(foreground, None) if foreground else 0

    if target_thread:
        user32.AttachThreadInput(current_thread, target_thread, True)
    if foreground_thread and foreground_thread != target_thread:
        user32.AttachThreadInput(current_thread, foreground_thread, True)

    user32.ShowWindow(hwnd, SW_RESTORE)
    user32.BringWindowToTop(hwnd)
    user32.SetActiveWindow(hwnd)
    user32.SetFocus(hwnd)
    ok = bool(user32.SetForegroundWindow(hwnd))

    if click:
        pt = ctypes.wintypes.POINT(20, 20)
        user32.ClientToScreen(hwnd, ctypes.byref(pt))
        user32.SetCursorPos(pt.x, pt.y)
        time.sleep(0.03)
        user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.03)
        user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        ok = True

    if foreground_thread and foreground_thread != target_thread:
        user32.AttachThreadInput(current_thread, foreground_thread, False)
    if target_thread:
        user32.AttachThreadInput(current_thread, target_thread, False)

    return ok


def send_key_to_pid(pid: int, vk: int) -> bool:
    hwnd = find_window_for_pid(pid)
    if hwnd == 0:
        return False
    focus_window(hwnd)
    time.sleep(0.05)
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.05)
    user32.keybd_event(vk, 0, KEYEVENTF_KEYUP, 0)
    return True


def click_client(pid: int, x: int, y: int) -> bool:
    hwnd = find_window_for_pid(pid)
    if hwnd == 0:
        return False
    focus_window(hwnd, click=True)
    time.sleep(0.08)
    origin_x, origin_y = game_surface_origin(hwnd)
    screen_x = origin_x + x
    screen_y = origin_y + y
    user32.SetCursorPos(screen_x, screen_y)
    time.sleep(0.04)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.04)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
    post_mouse_click(hwnd, screen_x, screen_y)
    return True


def window_metrics(pid: int) -> dict[str, object]:
    hwnd = find_window_for_pid(pid)
    if hwnd == 0:
        return {"hwnd": 0}
    window = ctypes.wintypes.RECT()
    client = ctypes.wintypes.RECT()
    origin = ctypes.wintypes.POINT(0, 0)
    user32.GetWindowRect(hwnd, ctypes.byref(window))
    user32.GetClientRect(hwnd, ctypes.byref(client))
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return {
        "hwnd": f"0x{hwnd:x}",
        "window": [window.left, window.top, window.right, window.bottom],
        "client": [client.left, client.top, client.right, client.bottom],
        "clientOrigin": [origin.x, origin.y],
        "surfaceOrigin": list(game_surface_origin(hwnd)),
        "foreground": hwnd == user32.GetForegroundWindow(),
    }


def capture_score(pixels: bytes) -> int:
    score = 0
    step = 4 * 16
    for i in range(0, len(pixels) - 2, step):
        b = pixels[i]
        g = pixels[i + 1]
        r = pixels[i + 2]
        lum = r + g + b
        if lum > 24:
            score += lum
    return score


def write_bmp(path: Path, width: int, height: int, pixels: bytes) -> None:
    file_header_size = 14
    dib_header_size = 40
    image_size = len(pixels)
    data_offset = file_header_size + dib_header_size
    file_size = data_offset + image_size

    with path.open("wb") as f:
        f.write(struct.pack("<2sIHHI", b"BM", file_size, 0, 0, data_offset))
        f.write(
            struct.pack(
                "<IiiHHIIiiII",
                dib_header_size,
                width,
                -height,
                1,
                32,
                BI_RGB,
                image_size,
                0,
                0,
                0,
                0,
            )
        )
        f.write(pixels)


def capture_game_surface(pid: int, label: str) -> tuple[Path, dict[str, object]]:
    hwnd = find_window_for_pid(pid)
    if hwnd == 0:
        raise RuntimeError("window not found")

    focus_window(hwnd, click=False)
    time.sleep(0.05)

    width = 800
    height = 600

    screen_dc = user32.GetDC(0)
    if not screen_dc:
        raise RuntimeError("GetDC failed")

    try:
        best: tuple[str, int, int, bytes, int] | None = None
        errors: list[str] = []
        for origin_name, origin_x, origin_y in candidate_capture_origins(hwnd):
            mem_dc = gdi32.CreateCompatibleDC(screen_dc)
            if not mem_dc:
                errors.append(f"{origin_name}: CreateCompatibleDC failed")
                continue

            bitmap = gdi32.CreateCompatibleBitmap(screen_dc, width, height)
            if not bitmap:
                gdi32.DeleteDC(mem_dc)
                errors.append(f"{origin_name}: CreateCompatibleBitmap failed")
                continue

            old_obj = gdi32.SelectObject(mem_dc, bitmap)
            try:
                if not gdi32.BitBlt(mem_dc, 0, 0, width, height, screen_dc, origin_x, origin_y, SRCCOPY):
                    errors.append(f"{origin_name}: BitBlt failed")
                    continue

                info = BITMAPINFO()
                info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
                info.bmiHeader.biWidth = width
                info.bmiHeader.biHeight = -height
                info.bmiHeader.biPlanes = 1
                info.bmiHeader.biBitCount = 32
                info.bmiHeader.biCompression = BI_RGB
                info.bmiHeader.biSizeImage = width * height * 4

                pixels = ctypes.create_string_buffer(info.bmiHeader.biSizeImage)
                rows = gdi32.GetDIBits(mem_dc, bitmap, 0, height, pixels, ctypes.byref(info), DIB_RGB_COLORS)
                if rows != height:
                    errors.append(f"{origin_name}: GetDIBits copied {rows} rows")
                    continue

                raw = bytes(pixels.raw)
                score = capture_score(raw)
                if best is None or score > best[4]:
                    best = (origin_name, origin_x, origin_y, raw, score)
            finally:
                gdi32.SelectObject(mem_dc, old_obj)
                gdi32.DeleteObject(bitmap)
                gdi32.DeleteDC(mem_dc)

        if best is None:
            raise RuntimeError("; ".join(errors) if errors else "no capture candidates")

        safe_label = re.sub(r"[^a-zA-Z0-9_.-]+", "_", label).strip("_") or "capture"
        path = REPO / f"tmp_frida_{safe_label}.bmp"
        origin_name, origin_x, origin_y, raw, score = best
        write_bmp(path, width, height, raw)

        return path, {"origin": origin_name, "x": origin_x, "y": origin_y, "score": score}
    finally:
        user32.ReleaseDC(0, screen_dc)


def emit_screenshot(pid: int, label: str, emit) -> None:
    try:
        path, capture = capture_game_surface(pid, label)
        emit({"tag": "Driver.python.screenshot", "label": label, "path": str(path), "capture": capture})
    except Exception as e:
        emit({"tag": "Driver.python.error", "stage": f"screenshot-{label}", "err": str(e)})


def send_intro_dialog_replies(pid: int) -> None:
    # 10HEDRON path: 1 -> 4 -> 1 adds journal #739, then exits via 1 -> 1.
    for vk in [VK_1, VK_4, VK_1, VK_1, VK_1, VK_1]:
        send_key_to_pid(pid, vk)
        time.sleep(1.25)


def keep_game_focused(pid: int, duration: float) -> None:
    deadline = time.time() + duration
    logged = False
    while time.time() < deadline:
        hwnd = find_window_for_pid(pid)
        if hwnd != 0:
            focus_window(hwnd, click=not logged)
            if not logged:
                print(f"focused hwnd=0x{hwnd:x}", flush=True)
                logged = True
        time.sleep(0.5)


def skip_original_startup_movies(pid: int, stop_event: threading.Event, duration: float = 8.0) -> None:
    deadline = time.time() + duration
    keys = [VK_ESCAPE, VK_SPACE]
    index = 0
    while time.time() < deadline and not stop_event.is_set():
        send_key_to_pid(pid, keys[index % len(keys)])
        index += 1
        time.sleep(0.35)


def original_ui_driver(
    pid: int,
    party_index: int,
    timeout: float,
    auto_chapter: bool,
    state: dict[str, object],
    state_lock: threading.Lock,
    stop_movie_keys: threading.Event,
    emit,
) -> None:
    driver_started_at = time.time()
    deadline = time.time() + timeout

    def snapshot() -> dict[str, object]:
        with state_lock:
            return dict(state)

    def wait_for(name: str, predicate, max_seconds: float | None = None) -> bool:
        local_deadline = min(deadline, time.time() + max_seconds) if max_seconds is not None else deadline
        while time.time() < local_deadline:
            if predicate(snapshot()):
                return True
            time.sleep(0.1)
        emit({"tag": "Driver.python.timeout", "stage": name})
        return False

    emit({"tag": "Driver.python.start", "party": party_index, "autoChapter": auto_chapter})

    if not wait_for(
        "connection",
        lambda s: s.get("active_screen") == "connection"
        and (
            s.get("intro_movie_seen")
            or (not s.get("movie_seen") and time.time() - driver_started_at > 3.0)
        ),
        max_seconds=min(12.0, timeout),
    ):
        return

    stop_movie_keys.set()
    time.sleep(0.25)
    emit({"tag": "Driver.python.click", "target": "new-game", "pos": NEW_GAME_BUTTON, "window": window_metrics(pid)})
    if not click_client(pid, *NEW_GAME_BUTTON):
        emit({"tag": "Driver.python.error", "stage": "new-game-click", "err": "window not found"})
        return
    time.sleep(1.0)
    first_click_state = snapshot()
    if first_click_state.get("active_screen") != "singleplayer" and not first_click_state.get("new_game_seen"):
        emit({"tag": "Driver.python.retry", "target": "new-game", "pos": NEW_GAME_BUTTON, "window": window_metrics(pid)})
        if not click_client(pid, *NEW_GAME_BUTTON):
            emit({"tag": "Driver.python.error", "stage": "new-game-retry", "err": "window not found"})
            return

    if not wait_for(
        "singleplayer",
        lambda s: s.get("active_screen") == "singleplayer" or s.get("singleplayer_seen"),
        max_seconds=10.0,
    ):
        return

    if party_index < 0 or party_index >= len(PARTY_ROWS):
        emit({"tag": "Driver.python.error", "stage": "party-index", "party": party_index, "visible": len(PARTY_ROWS)})
        return

    time.sleep(0.35)
    emit({"tag": "Driver.python.click", "target": "party-row", "party": party_index, "pos": PARTY_ROWS[party_index], "window": window_metrics(pid)})
    if not click_client(pid, *PARTY_ROWS[party_index]):
        emit({"tag": "Driver.python.error", "stage": "party-row-click", "err": "window not found"})
        return

    time.sleep(0.2)
    emit({"tag": "Driver.python.click", "target": "party-done", "pos": PARTY_DONE_BUTTON, "window": window_metrics(pid)})
    if not click_client(pid, *PARTY_DONE_BUTTON):
        emit({"tag": "Driver.python.error", "stage": "party-done-click", "err": "window not found"})
        return

    if not auto_chapter:
        return

    if not wait_for(
        "chapter",
        lambda s: s.get("active_screen") == "chapter" or s.get("chapter_seen"),
        max_seconds=12.0,
    ):
        return

    emit({"tag": "Driver.python.chapter-visible-wait", "delayMs": int(CHAPTER_VISIBLE_BEFORE_CAPTURE_SECONDS * 1000)})
    time.sleep(CHAPTER_VISIBLE_BEFORE_CAPTURE_SECONDS)
    emit_screenshot(pid, "original_chapter_before_done", emit)
    emit({"tag": "Driver.python.chapter-audio-grace", "delayMs": int(CHAPTER_AUDIO_GRACE_AFTER_CAPTURE_SECONDS * 1000)})
    time.sleep(CHAPTER_AUDIO_GRACE_AFTER_CAPTURE_SECONDS)
    emit({"tag": "Driver.python.click", "target": "chapter-done", "pos": CHAPTER_DONE_BUTTON, "window": window_metrics(pid)})
    if not click_client(pid, *CHAPTER_DONE_BUTTON):
        emit({"tag": "Driver.python.error", "stage": "chapter-done-click", "err": "window not found"})


def re_chapter_driver(
    pid: int,
    timeout: float,
    state: dict[str, object],
    state_lock: threading.Lock,
    emit,
) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with state_lock:
            chapter_active_seen = bool(state.get("chapter_active_seen"))
            chapter_done_seen = bool(state.get("chapter_done_seen"))
        if chapter_active_seen and not chapter_done_seen:
            emit({"tag": "Driver.python.chapter-visible-wait", "delayMs": int(CHAPTER_VISIBLE_BEFORE_CAPTURE_SECONDS * 1000)})
            time.sleep(CHAPTER_VISIBLE_BEFORE_CAPTURE_SECONDS)
            emit_screenshot(pid, "re_chapter_before_done", emit)
            emit({"tag": "Driver.python.chapter-audio-grace", "delayMs": int(CHAPTER_AUDIO_GRACE_AFTER_CAPTURE_SECONDS * 1000)})
            time.sleep(CHAPTER_AUDIO_GRACE_AFTER_CAPTURE_SECONDS)
            emit({"tag": "Driver.python.click", "target": "chapter-done", "pos": CHAPTER_DONE_BUTTON, "window": window_metrics(pid)})
            if not click_client(pid, *CHAPTER_DONE_BUTTON):
                emit({"tag": "Driver.python.error", "stage": "re-chapter-done-click", "err": "window not found"})
            time.sleep(0.5)
            with state_lock:
                chapter_done_seen = bool(state.get("chapter_done_seen"))
            if not chapter_done_seen:
                emit({"tag": "Driver.python.key", "target": "chapter-done", "vk": VK_RETURN, "window": window_metrics(pid)})
                if not send_key_to_pid(pid, VK_RETURN):
                    emit({"tag": "Driver.python.error", "stage": "re-chapter-done-enter", "err": "window not found"})
            return
        time.sleep(0.1)


def make_js(mode: str, party_index: int, auto_chapter: bool, hooks: dict[str, int]) -> str:
    hooks_json = json.dumps(hooks)
    is_re = "true" if mode == "re" else "false"
    auto_chapter_js = "true" if auto_chapter else "false"
    return f"""
'use strict';

const hooks = {hooks_json};
const isRe = {is_re};
const requestedParty = {party_index};
const autoChapter = {auto_chapter_js};
const base = isRe ? Process.getModuleByName('iwd2-re.exe').base : ptr(0);
const gChitinPtr = isRe ? base.add(0x854cf4) : ptr(0x008cf6d8);
const sleepMs = new NativeFunction(Process.getModuleByName('kernel32.dll').getExportByName('Sleep'), 'void', ['uint']);
const O = isRe
  ? {{
      objId: 0x64,
      curAction: 0x4c0,
      actionString1: 0xd0,
      triggerSpecific: 0x04,
      triggerSpecific2: 0x4c,
      triggerSpecific3: 0x50,
      triggerString1: 0x54,
      triggerString2: 0x58,
    }}
  : {{
      objId: 0x5c,
      curAction: 0x476,
      actionString1: 0xc2,
      triggerSpecific: 0x02,
      triggerSpecific2: 0x46,
      triggerSpecific3: 0x4a,
      triggerString1: 0x4e,
      triggerString2: 0x52,
    }};
const CHITIN_ORIG = {{
  engineActive: 0x0048,
  reinitializing: 0x00e0,
  activeEngine: 0x03c4,
  objectGame: 0x1c54,
  engineConnection: 0x1c98,
  engineSinglePlayer: 0x1c94,
  engineChapter: 0x1ca0,
  engineWorld: 0x1c88,
  engineProjector: 0x1cac,
}};
const CONN_ORIG = {{
  bLoadGame: 0x0106,
  protocol: 0x0466,
  enumCountdown: 0x048e,
  allowInput: 0x04b2,
  showIntro: 0x0fa8,
  selectedGameMode: 0x0fb4,
}};
const SP_ORIG = {{
  popupStack: 0x043c,
  lobbyMode: 0x045c,
  partyCount: 0x1392,
  topParty: 0x1396,
  party: 0x139a,
  selectedPopup: 0x139e,
}};
const CHAPTER_ORIG = {{
  started: 0x01b4,
}};
const CHAPTER = isRe
  ? {{ textListCandidates: [0x0144, 0x0148, 0x014c, 0x0150, 0x0154], started: 0x01b8 }}
  : {{ textListCandidates: [0x0144], started: 0x01b4 }};
const PROJECTOR_ORIG = {{
  deactivate: 0x0112,
  field144: 0x0144,
  field145: 0x0145,
}};

function addr(name) {{
  const value = hooks[name];
  return isRe ? base.add(value) : ptr(value);
}}

function s16(v) {{
  return (v << 16) >> 16;
}}

function safeCString(p) {{
  try {{
    const s = p.readPointer();
    if (s.isNull()) return '';
    return s.readAnsiString(80) || '';
  }} catch (e) {{
    return '<bad-cstring>';
  }}
}}

function resRefString(p) {{
  try {{
    const bytes = p.readByteArray(8);
    const view = new Uint8Array(bytes);
    let out = '';
    for (let i = 0; i < view.length; i++) {{
      if (view[i] === 0) break;
      out += String.fromCharCode(view[i]);
    }}
    return out;
  }} catch (e) {{
    return '<bad-resref>';
  }}
}}

function chapterTextState(chapter) {{
  for (const textListOff of CHAPTER.textListCandidates) {{
    try {{
      const list = chapter.add(textListOff).readPointer();
    if (list.isNull()) {{
        continue;
    }}

    const count = list.add(0x0c).readS32();
      if (count <= 0 || count > 8) {{
        continue;
      }}

    const out = [];
    let node = list.add(0x04).readPointer();
    for (let i = 0; i < count && i < 4 && !node.isNull(); i++) {{
        const strref = node.add(0x08).readS32();
        if (strref < 0 || strref > 1000000) {{
          throw new Error('implausible strref ' + strref);
        }}
        out.push(strref);
      node = node.readPointer();
    }}
      if (out.length > 0) {{
        return {{ offset: textListOff, count, strrefs: out }};
      }}
  }} catch (e) {{
      // Try the next candidate; RE and original layouts differ under VS2019.
    }}
  }}
  return {{ err: 'no plausible CList at chapter text offsets' }};
}}

function readBool32(p, off) {{
  return p.add(off).readS32() !== 0;
}}

function writeBool32(p, off, value) {{
  p.add(off).writeS32(value ? 1 : 0);
}}

function popupStackHasTail(p, off) {{
  try {{
    return !p.add(off + 4).readPointer().isNull();
  }} catch (e) {{
    return false;
  }}
}}

const nativeCalls = {{}};
function callThis(name, thiz) {{
  if (!(name in hooks)) {{
    throw new Error('missing hook address for ' + name);
  }}
  if (!(name in nativeCalls)) {{
    nativeCalls[name] = new NativeFunction(addr(name), 'void', ['pointer'], 'thiscall');
  }}
  nativeCalls[name](thiz);
}}

function actionId(thiz) {{
  return s16(thiz.add(O.curAction).readS16());
}}

function actionString1(thiz) {{
  return safeCString(thiz.add(O.curAction + O.actionString1));
}}

function actionIdAt(p) {{
  try {{ return s16(p.readS16()); }} catch (e) {{ return 0; }}
}}

function objectId(thiz) {{
  try {{ return thiz.add(O.objId).readS32(); }} catch (e) {{ return 0; }}
}}

function networkInfo() {{
  try {{
    const chitin = gChitinPtr.readPointer();
    const net = chitin.add(0x952);
    return {{
      service: net.add(0x1c).readS32(),
      open: net.add(0x6e0).readU8(),
      host: net.add(0x6e1).readU8(),
    }};
  }} catch (e) {{
    return {{ err: '' + e }};
  }}
}}

function trigId(p) {{
  try {{ return s16(p.readS16()); }} catch (e) {{ return 0; }}
}}

function trigInfo(p) {{
  try {{
    return {{
      id: trigId(p),
      specific: p.add(O.triggerSpecific).readS32(),
      specific2: p.add(O.triggerSpecific2).readS32(),
      specific3: p.add(O.triggerSpecific3).readS32(),
      string1: safeCString(p.add(O.triggerString1)),
      string2: safeCString(p.add(O.triggerString2)),
    }};
  }} catch (e) {{
    return {{ id: 0, err: '' + e }};
  }}
}}

function responseActions(resp) {{
  try {{
    const out = [];
    let node = resp.add(0x0c).readPointer();
    for (let i = 0; i < 16 && !node.isNull(); i++) {{
      const action = node.add(0x08).readPointer();
      out.push(actionIdAt(action));
      node = node.readPointer();
    }}
    return out;
  }} catch (e) {{
    return ['err:' + e];
  }}
}}

function responseMeta(resp) {{
  try {{
    return {{
      weight: s16(resp.readS16()),
      responseNum: s16(resp.add(0x02).readS16()),
      responseSetNum: s16(resp.add(0x04).readS16()),
      scriptNum: s16(resp.add(0x06).readS16()),
      actions: responseActions(resp),
    }};
  }} catch (e) {{
    return {{ err: '' + e }};
  }}
}}

const interestingActions = new Set([8, 30, 83, 109, 120, 121, 122, 123, 127, 161, 183, 229, 256, 272, 275]);
const interestingTriggers = new Set([0x0036, 0x400f, 0x4023, 0x4030, 0x4034, 0x4035, 0x40d1, 0x40ef]);
let execCount = 0;
let holdCount = 0;
let triggerCount = 0;
let actionQueueTraceCount = 0;
let activeChapter = ptr(0);
let originalDriver = {{
  connTicks: 0,
  connReadyAt: 0,
  newGameClicked: false,
  spTicks: 0,
  partyDone: false,
  movieSkipLogged: false,
  chapterStarted: false,
  chapterDoneAt: 0,
  chapterDone: false,
  lastActiveEngine: '',
}};

function shouldTraceTrigger(info) {{
  const s = info.string1 || '';
  if (info.id === 0x400f || info.id === 0x4034 || info.id === 0x4035) {{
    return s.indexOf('CHAPTER') >= 0 || s.indexOf('AR1000') >= 0;
  }}
  return info.id === 0x40d1 || info.id === 0x40ef;
}}

function tryOriginalNewGame(conn, reason) {{
  if (isRe || originalDriver.newGameClicked) return;

  let allowInput = false;
  let enumCountdown = 0;
  try {{
    allowInput = readBool32(conn, CONN_ORIG.allowInput);
    enumCountdown = conn.add(CONN_ORIG.enumCountdown).readS32();
  }} catch (e) {{
    send({{ tag: 'Driver.original.error', stage: 'connection-state', err: '' + e }});
    return;
  }}

  if (!allowInput || enumCountdown > 0) {{
    return;
  }}

  if (originalDriver.connReadyAt === 0) {{
    send({{ tag: 'Driver.original.newgame-wait', reason, delayMs: 1000 }});
    sleepMs(1000);
    originalDriver.connReadyAt = Date.now();
  }}

  try {{
    conn.add(CONN_ORIG.protocol).writeS32(0);
    writeBool32(conn, CONN_ORIG.bLoadGame, false);
    writeBool32(conn, CONN_ORIG.showIntro, false);
    writeBool32(conn, CONN_ORIG.allowInput, true);
    conn.add(CONN_ORIG.selectedGameMode).writeS32(1);

    originalDriver.newGameClicked = true;
    send({{
      tag: 'Driver.original.newgame-click',
      reason,
      this: conn.toString(),
      allowInput,
      enumCountdown,
    }});
    send({{ tag: 'Driver.original.newgame-call', target: 'CScreenConnection::OnNewGameButtonClick' }});
    callThis('CScreenConnection::OnNewGameButtonClick', conn);
    send({{ tag: 'Driver.original.newgame-call.ret', target: 'CScreenConnection::OnNewGameButtonClick' }});
  }} catch (e) {{
    originalDriver.newGameClicked = false;
    send({{ tag: 'Driver.original.error', stage: 'newgame-click', err: '' + e }});
  }}
}}

function tryOriginalPartySelection(sp, reason) {{
  if (isRe || originalDriver.partyDone) return;

  let lobbyMode = 0;
  let partyCount = 0;
  let hasPopup = false;
  try {{
    lobbyMode = sp.add(SP_ORIG.lobbyMode).readS32();
    partyCount = sp.add(SP_ORIG.partyCount).readS32();
    hasPopup = popupStackHasTail(sp, SP_ORIG.popupStack);
  }} catch (e) {{
    send({{ tag: 'Driver.original.error', stage: 'singleplayer-state', err: '' + e }});
    return;
  }}

  if (lobbyMode !== 1 || partyCount <= 0) {{
    return;
  }}

  if (requestedParty < 0 || requestedParty >= partyCount) {{
    originalDriver.partyDone = true;
    send({{
      tag: 'Driver.original.error',
      stage: 'party-index',
      requestedParty,
      partyCount,
    }});
    return;
  }}

  if (!hasPopup && originalDriver.spTicks < 20) {{
    return;
  }}

  const top = Math.max(Math.min(requestedParty, partyCount - 6), 0);
  try {{
    sp.add(SP_ORIG.topParty).writeS32(top);
    sp.add(SP_ORIG.party).writeS32(requestedParty - top);
    sp.add(SP_ORIG.selectedPopup).writeS32(10);

    originalDriver.partyDone = true;
    send({{
      tag: 'Driver.original.party-selected',
      reason,
      this: sp.toString(),
      requestedParty,
      partyCount,
      hasPopup,
    }});
    send({{ tag: 'Driver.original.party-call', target: 'CScreenSinglePlayer::OnPartySelectionDoneButtonClick' }});
    callThis('CScreenSinglePlayer::OnPartySelectionDoneButtonClick', sp);
    send({{ tag: 'Driver.original.party-call.ret', target: 'CScreenSinglePlayer::OnPartySelectionDoneButtonClick' }});
  }} catch (e) {{
    originalDriver.partyDone = false;
    send({{ tag: 'Driver.original.error', stage: 'party-select', err: '' + e }});
  }}
}}

function tryOriginalChapterDone(chapter, reason) {{
  if (isRe || !autoChapter || originalDriver.chapterDone || !originalDriver.chapterStarted) return;
  try {{
    if (!readBool32(chapter, CHAPTER_ORIG.started)) return;
    if (originalDriver.chapterDoneAt === 0) {{
      send({{ tag: 'Driver.original.chapter-done-wait', reason, this: chapter.toString(), delayMs: 1000 }});
      sleepMs(1000);
      originalDriver.chapterDoneAt = Date.now();
    }}
    originalDriver.chapterDone = true;
    send({{ tag: 'Driver.original.chapter-done', reason, this: chapter.toString() }});
    callThis('CScreenChapter::OnDoneButtonClick', chapter);
  }} catch (e) {{
    send({{ tag: 'Driver.original.error', stage: 'chapter-done', err: '' + e }});
  }}
}}

function tryOriginalDriverFromChitin(chitin, reason) {{
  if (isRe) return;
  try {{
    if (!readBool32(chitin, CHITIN_ORIG.engineActive)) return;
    if (chitin.add(CHITIN_ORIG.reinitializing).readU8() !== 0) return;

    const active = chitin.add(CHITIN_ORIG.activeEngine).readPointer();
    if (active.isNull()) return;
    const activeText = active.toString();

    const conn = chitin.add(CHITIN_ORIG.engineConnection).readPointer();
    const singlePlayer = chitin.add(CHITIN_ORIG.engineSinglePlayer).readPointer();
    const chapter = chitin.add(CHITIN_ORIG.engineChapter).readPointer();
    let screen = 'unknown';

    if (active.equals(conn)) {{
      screen = 'connection';
    }} else if (active.equals(singlePlayer)) {{
      screen = 'singleplayer';
    }} else if (active.equals(chapter)) {{
      originalDriver.chapterStarted = true;
      screen = 'chapter';
    }} else {{
      const world = chitin.add(CHITIN_ORIG.engineWorld).readPointer();
      const projector = chitin.add(CHITIN_ORIG.engineProjector).readPointer();
      if (active.equals(world)) {{
        screen = 'world';
      }} else if (active.equals(projector)) {{
        screen = 'projector';
      }}
    }}

    if (activeText !== originalDriver.lastActiveEngine) {{
      originalDriver.lastActiveEngine = activeText;
      send({{ tag: 'Driver.original.active-screen', reason, screen, active: activeText }});
    }}
  }} catch (e) {{
    send({{ tag: 'Driver.original.error', stage: 'chitin-driver', err: '' + e }});
  }}
}}

function hook(name, callbacks) {{
  if (!(name in hooks)) return;
  try {{
    Interceptor.attach(addr(name), callbacks);
    send({{ tag: 'hooked', name, addr: addr(name).toString() }});
  }} catch (e) {{
    send({{ tag: 'hook-error', name, err: '' + e }});
  }}
}}

for (const name of [
  'CBaldurChitin::CBaldurChitin',
  'CBaldurChitin::Init',
  'CChitin::WinMain',
  'CChitin::InitApplication',
  'CChitin::InitGraphics',
  'CChitin::InitInstance',
  'CScreenConnection::StartConnection',
  'CScreenConnection::EngineActivated',
  'CScreenChapter::EngineActivated',
]) {{
  hook(name, {{
    onEnter(args) {{
      if (!isRe && name === 'CScreenChapter::EngineActivated') {{
        originalDriver.chapterStarted = true;
      }}
      if (name === 'CScreenChapter::EngineActivated') {{
        activeChapter = this.context.ecx;
      }}
      send({{ tag: name, this: this.context.ecx.toString() }});
    }}
  }});
}}

hook('CChitin::SelectEngine', {{
  onEnter(args) {{
    send({{ tag: 'CChitin::SelectEngine', this: this.context.ecx.toString(), engine: args[0].toString() }});
  }}
}});

hook('CChitin::AsynchronousUpdate', {{
  onEnter(args) {{
    this.thiz = this.context.ecx;
  }},
  onLeave(rv) {{
    tryOriginalDriverFromChitin(this.thiz, 'chitin-async');
  }}
}});

hook('CGameAIBase::ExecuteAction', {{
  onEnter(args) {{
    const thiz = this.context.ecx;
    this.aid = actionId(thiz);
    if (interestingActions.has(this.aid)) {{
      send({{
        tag: 'ExecuteAction',
        aid: this.aid,
        obj: objectId(thiz),
        this: thiz.toString(),
        s1: actionString1(thiz),
        net: this.aid === 275 ? networkInfo() : undefined,
      }});
    }}
    execCount++;
  }},
  onLeave(rv) {{
    if (interestingActions.has(this.aid)) {{
      send({{ tag: 'ExecuteAction.ret', aid: this.aid, ret: s16(rv.toInt32()) }});
    }}
  }}
}});

for (const name of ['CGameAIBase::EvaluateStatusTrigger', 'CGameSprite::EvaluateStatusTrigger']) {{
  hook(name, {{
    onEnter(args) {{
      this.info = trigInfo(args[0]);
      if (shouldTraceTrigger(this.info)) {{
        send({{ tag: name, trigger: this.info }});
      }}
      triggerCount++;
    }},
    onLeave(rv) {{
      if (this.info && shouldTraceTrigger(this.info)) {{
        send({{ tag: name + '.ret', trigger: this.info, ret: rv.toInt32() }});
      }}
    }}
  }});
}}

if (false) hook('CAICondition::Hold', {{
  onEnter(args) {{
    this.thiz = this.context.ecx;
    if (holdCount < 80) {{
      send({{ tag: 'CAICondition::Hold', this: this.thiz.toString(), caller: args[0].toString() }});
    }}
    holdCount++;
  }},
  onLeave(rv) {{
    if (holdCount < 120) {{
      send({{ tag: 'CAICondition::Hold.ret', ret: rv.toInt32() }});
    }}
  }}
}});

if (false) hook('CAIResponseSet::Choose', {{
  onEnter(args) {{
    send({{ tag: 'CAIResponseSet::Choose', this: this.context.ecx.toString() }});
  }}
}});

if (false) hook('CAIScript::Find', {{
  onEnter(args) {{
    send({{ tag: 'CAIScript::Find', this: this.context.ecx.toString(), caller: args[0].toString() }});
  }}
}});

hook('CGameAIBase::InsertResponse', {{
  onEnter(args) {{
    if (actionQueueTraceCount >= 120) return;
    const meta = responseMeta(args[0]);
    if (Array.isArray(meta.actions) && meta.actions.some(a => interestingActions.has(a))) {{
      actionQueueTraceCount++;
      send({{
        tag: 'AI.InsertResponse',
        obj: objectId(this.context.ecx),
        this: this.context.ecx.toString(),
        check: args[1].toInt32(),
        clear: args[2].toInt32(),
        response: meta,
      }});
    }}
  }}
}});

hook('CGameAIBase::GetNextAction', {{
  onEnter(args) {{
    this.thiz = this.context.ecx;
  }},
  onLeave(rv) {{
    if (actionQueueTraceCount >= 160) return;
    const aid = actionIdAt(rv);
    if (interestingActions.has(aid)) {{
      actionQueueTraceCount++;
      send({{ tag: 'AI.GetNextAction.ret', obj: objectId(this.thiz), this: this.thiz.toString(), aid }});
    }}
  }}
}});

hook('CBaldurProjector::PlayMovieInternal', {{
  onEnter(args) {{
    send({{ tag: 'Movie.PlayMovieInternal', this: this.context.ecx.toString(), resref: resRefString(args[0]), async: args[1].toInt32() }});
  }}
}});

hook('CBaldurProjector::TimerAsynchronousUpdate', {{
  onEnter(args) {{
    if (isRe) return;
    const thiz = this.context.ecx;
    try {{
      thiz.add(PROJECTOR_ORIG.deactivate).writeS32(1);
      thiz.add(PROJECTOR_ORIG.field144).writeU8(1);
      thiz.add(PROJECTOR_ORIG.field145).writeU8(1);
      if (!originalDriver.movieSkipLogged) {{
        originalDriver.movieSkipLogged = true;
        send({{ tag: 'Driver.original.skip-mve', this: thiz.toString() }});
      }}
    }} catch (e) {{
      send({{ tag: 'Driver.original.error', stage: 'skip-mve', err: '' + e }});
    }}
  }}
}});

hook('CScreenConnection::TimerAsynchronousUpdate', {{
  onEnter(args) {{
    this.thiz = this.context.ecx;
  }},
  onLeave(rv) {{
    if (isRe) return;
    originalDriver.connTicks++;
  }}
}});

hook('CScreenConnection::OnNewGameButtonClick', {{
  onEnter(args) {{
    send({{ tag: 'Connection.OnNewGameButtonClick', this: this.context.ecx.toString() }});
  }}
}});

hook('CScreenSinglePlayer::EngineActivated', {{
  onEnter(args) {{
    this.thiz = this.context.ecx;
    send({{ tag: 'SinglePlayer.EngineActivated', this: this.thiz.toString() }});
  }}
}});

hook('CScreenSinglePlayer::TimerAsynchronousUpdate', {{
  onEnter(args) {{
    this.thiz = this.context.ecx;
  }},
  onLeave(rv) {{
    if (isRe) return;
    originalDriver.spTicks++;
  }}
}});

hook('CScreenSinglePlayer::OnDoneButtonClick', {{
  onEnter(args) {{
    send({{ tag: 'SinglePlayer.OnDoneButtonClick', this: this.context.ecx.toString() }});
  }}
}});

hook('CScreenSinglePlayer::OnPartySelectionDoneButtonClick', {{
  onEnter(args) {{
    send({{ tag: 'SinglePlayer.OnPartySelectionDoneButtonClick', this: this.context.ecx.toString() }});
  }}
}});

hook('CScreenSinglePlayer::OnMainDoneButtonClick', {{
  onEnter(args) {{
    send({{ tag: 'SinglePlayer.OnMainDoneButtonClick', this: this.context.ecx.toString() }});
  }}
}});

hook('CScreenChapter::StartChapter', {{
  onEnter(args) {{
    activeChapter = this.context.ecx;
    originalDriver.chapterStarted = true;
    send({{ tag: 'Chapter.StartChapter', this: this.context.ecx.toString(), resref: resRefString(args[0]) }});
  }}
}});

hook('CScreenChapter::StartChapterMultiplayerHost', {{
  onEnter(args) {{
    activeChapter = this.context.ecx;
    originalDriver.chapterStarted = true;
    send({{ tag: 'Chapter.StartChapterMultiplayerHost', this: this.context.ecx.toString(), chapter: args[0].toInt32(), resref: resRefString(args[1]) }});
  }}
}});

hook('CScreenChapter::StartText', {{
  onEnter(args) {{
    this.thiz = this.context.ecx;
    activeChapter = this.thiz;
    send({{ tag: 'Chapter.StartText', this: this.thiz.toString(), resref: resRefString(args[0]) }});
  }},
  onLeave(rv) {{
    send({{ tag: 'Chapter.StartText.ret', this: this.thiz.toString(), ret: rv.toInt32(), text: chapterTextState(this.thiz) }});
  }}
}});

hook('CScreenChapter::ResetMainPanel', {{
  onEnter(args) {{
    const thiz = this.context.ecx;
    activeChapter = thiz;
    send({{ tag: 'Chapter.ResetMainPanel', this: thiz.toString(), text: chapterTextState(thiz) }});
  }}
}});

hook('CScreenChapter::TimerAsynchronousUpdate', {{
  onEnter(args) {{
    this.thiz = this.context.ecx;
  }}
}});

hook('CScreenChapter::OnDoneButtonClick', {{
  onEnter(args) {{
    send({{ tag: 'Chapter.OnDoneButtonClick', this: this.context.ecx.toString() }});
  }}
}});

hook('CSoundMixer::StartSong', {{
  onEnter(args) {{
    const song = args[0].toInt32();
    if (song === -1 || song === 41 || song === 42) {{
      send({{ tag: 'SoundMixer.StartSong', song, flags: args[1].toInt32() }});
    }}
  }}
}});

hook('CSound::Play', {{
  onEnter(args) {{
    const thiz = this.context.ecx;
    if (!activeChapter.isNull()) {{
      const delta = thiz.toUInt32() - activeChapter.toUInt32();
      if (delta >= 0x140 && delta < 0x1d0) {{
        send({{ tag: 'Chapter.VoiceSound.Play', this: thiz.toString(), delta, resref: resRefString(thiz.add(0x0c)), replay: args[0].toInt32() }});
      }}
    }}
  }}
}});

hook('CGameAIBase::StartCutScene', {{
  onEnter(args) {{
    const thiz = this.context.ecx;
    send({{ tag: 'StartCutScene', obj: objectId(thiz), script: actionString1(thiz) }});
  }},
  onLeave(rv) {{
    send({{ tag: 'StartCutScene.ret', ret: s16(rv.toInt32()) }});
  }}
}});

for (const name of ['CGameDialogSprite::StartDialog', 'CGameSprite::Dialogue', 'CScreenWorld::StartDialog']) {{
  hook(name, {{
    onEnter(args) {{
      send({{ tag: name, this: this.context.ecx.toString(), a0: args[0].toString(), a1: args[1].toString() }});
    }}
  }});
}}

hook('CGameJournal::AddEntry2', {{
  onEnter(args) {{
    send({{ tag: 'Journal.AddEntry2', strref: args[0].toInt32(), type: args[1].toInt32() }});
  }}
}});

hook('CGameJournal::AddEntry4', {{
  onEnter(args) {{
    send({{ tag: 'Journal.AddEntry4', strref: args[0].toInt32(), time: args[1].toInt32(), type: args[2].toInt32(), character: args[3].toInt32() }});
  }}
}});

hook('CGameJournal::SetQuestDone', {{
  onEnter(args) {{
    send({{ tag: 'Journal.SetQuestDone', strref: args[0].toInt32(), character: args[1].toInt32() }});
  }}
}});

hook('CGameJournal::DeleteEntry', {{
  onEnter(args) {{
    send({{ tag: 'Journal.DeleteEntry', strref: args[0].toInt32(), character: args[1].toInt32() }});
  }}
}});

hook('CInfGame::SetCurrentChapter', {{
  onEnter(args) {{
    send({{ tag: 'SetCurrentChapter', chapter: args[0].toInt32() }});
  }}
}});

hook('CInfGame::NewGame', {{
  onEnter(args) {{
    send({{ tag: 'NewGame', progressRequired: args[0].toInt32(), progressInPlace: args[1].toInt32() }});
  }}
}});

hook('CInfGame::SaveGame', {{
  onEnter(args) {{
    send({{ tag: 'SaveGame', a0: args[0].toInt32(), a1: args[1].toInt32(), a2: args[2].toInt32() }});
  }},
  onLeave(rv) {{
    send({{ tag: 'SaveGame.ret', ret: rv.toInt32() }});
  }}
}});

hook('CMessageSaveGame::Run', {{
  onEnter(args) {{
    send({{ tag: 'CMessageSaveGame::Run', this: this.context.ecx.toString() }});
  }}
}});

hook('CInfGame::LoadGame', {{
  onEnter(args) {{
    send({{ tag: 'LoadGame', a0: args[0].toInt32(), a1: args[1].toInt32() }});
  }}
}});

hook('CInfGame::Unmarshal', {{
  onEnter(args) {{
    send({{ tag: 'UnmarshalGame', size: args[1].toInt32(), a2: args[2].toInt32() }});
  }},
  onLeave(rv) {{
    send({{ tag: 'UnmarshalGame.ret', ret: rv.toInt32() }});
  }}
}});

send({{ tag: 'ready', mode: isRe ? 're' : 'original', base: base.toString() }});
"""


def spawn_re(args: argparse.Namespace) -> tuple[int, Path]:
    result_path = Path(tempfile.gettempdir()) / f"iwd2-re-auto-{uuid.uuid4().hex}.txt"
    env = os.environ.copy()
    env["IWD2_RE_AUTO_RESULT"] = str(result_path)
    env["IWD2_RE_AUTO_ACTION"] = "newgame"
    env["IWD2_RE_AUTO_PARTY"] = str(resolve_party(args.party))
    pid = frida.spawn(str(RE_EXE), env=env, cwd=str(GAME_DIR))
    return pid, result_path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["re", "original"], default="re")
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--party", default="Lady's Lament")
    ap.add_argument("--auto-dialog", action="store_true")
    ap.add_argument("--no-auto-chapter", dest="auto_chapter", action="store_false")
    ap.set_defaults(auto_chapter=True)
    ns = ap.parse_args()

    LOG.write_text("", encoding="utf-8")
    party_index = resolve_party(ns.party)
    hooks = resolve_re_hooks_from_map() if ns.mode == "re" else ORIG_HOOKS

    proc = None
    result_path = None
    pid = None
    spawned = False
    if ns.mode == "re":
        pid, result_path = spawn_re(ns)
        spawned = True
        session = frida.attach(pid)
        print(f"pid={pid}; mode=re", flush=True)
    else:
        pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
        spawned = True
        session = frida.attach(pid)
        print(f"pid={pid}; mode=original", flush=True)

    state = {
        "dialog_seen": False,
        "keys_sent": False,
        "new_game_seen": False,
        "load_game_seen": False,
        "chapter_seen": False,
        "chapter_active_seen": False,
        "connection_seen": False,
        "singleplayer_seen": False,
        "chapter_done_seen": False,
        "movie_seen": False,
        "intro_movie_seen": False,
        "active_screen": "",
    }
    state_lock = threading.Lock()

    def set_state(**values):
        with state_lock:
            state.update(values)

    def state_value(name: str) -> object:
        with state_lock:
            return state.get(name)

    def emit_driver(payload: dict[str, object]) -> None:
        line = json.dumps(payload, ensure_ascii=True)
        print(line, flush=True)
        with LOG.open("a", encoding="utf-8") as f:
            f.write(line + "\n")

    def on_message(message, data):
        if message["type"] == "send":
            payload = message["payload"]
            line = json.dumps(payload, ensure_ascii=True)
        else:
            payload = {"tag": "ERROR"}
            line = "ERROR " + json.dumps(message, ensure_ascii=True)
        tag = payload.get("tag", "")
        if "Dialog" in tag:
            set_state(dialog_seen=True)
        if tag == "Driver.original.active-screen":
            set_state(active_screen=payload.get("screen", ""))
        if tag == "Movie.PlayMovieInternal":
            resref = str(payload.get("resref", "")).upper()
            set_state(movie_seen=True, intro_movie_seen=state_value("intro_movie_seen") or resref == "INTRO")
        if tag in {"CScreenConnection::EngineActivated", "CScreenConnection::StartConnection"}:
            set_state(connection_seen=True)
        if tag == "SinglePlayer.EngineActivated":
            set_state(singleplayer_seen=True)
        if tag == "Chapter.OnDoneButtonClick":
            set_state(chapter_done_seen=True)
        if tag == "NewGame":
            set_state(new_game_seen=True)
        if tag == "LoadGame":
            set_state(load_game_seen=True)
        if tag == "CScreenChapter::EngineActivated":
            set_state(chapter_seen=True, chapter_active_seen=True)
        if tag in {"Chapter.StartChapter", "Chapter.StartChapterMultiplayerHost"}:
            set_state(chapter_seen=True)
        should_print = (
            tag in {"ready", "StartCutScene", "StartCutScene.ret", "NewGame", "Movie.PlayMovieInternal"}
            or tag.startswith("Driver.")
            or tag.startswith("Connection.")
            or tag.startswith("SinglePlayer.")
            or tag.startswith("Chapter.")
            or tag.startswith("AI.")
            or tag.startswith("CScreenChapter::")
            or tag.startswith("CScreenConnection::")
            or tag.startswith("SoundMixer.")
            or tag in {"SaveGame", "SaveGame.ret", "LoadGame", "UnmarshalGame", "UnmarshalGame.ret", "SetCurrentChapter", "CMessageSaveGame::Run", "AddMessage.SaveGame"}
            or "Dialog" in tag
            or tag.startswith("Journal.")
            or (tag == "ExecuteAction" and payload.get("aid") in {8, 120, 121, 122, 123, 127, 161, 183, 229, 256, 272, 275})
            or tag == "ERROR"
        )
        if should_print:
            print(line, flush=True)
        with LOG.open("a", encoding="utf-8") as f:
            f.write(line + "\n")

    if ns.mode == "re":
        print(f"resolved_re_hooks={len(hooks)} map={MAP_FILE}", flush=True)
    script = session.create_script(make_js(ns.mode, party_index, ns.auto_chapter, hooks))
    script.on("message", on_message)
    script.load()
    if spawned:
        frida.resume(pid)
    if ns.mode == "original":
        threading.Thread(
            target=keep_game_focused,
            args=(pid, ns.timeout),
            daemon=True,
        ).start()
        stop_movie_keys = threading.Event()
        threading.Thread(
            target=skip_original_startup_movies,
            args=(pid, stop_movie_keys),
            daemon=True,
        ).start()
        threading.Thread(
            target=original_ui_driver,
            args=(pid, party_index, ns.timeout, ns.auto_chapter, state, state_lock, stop_movie_keys, emit_driver),
            daemon=True,
        ).start()
    else:
        threading.Thread(
            target=keep_game_focused,
            args=(pid, ns.timeout),
            daemon=True,
        ).start()
        if ns.auto_chapter:
            threading.Thread(
                target=re_chapter_driver,
                args=(pid, ns.timeout, state, state_lock, emit_driver),
                daemon=True,
            ).start()

    deadline = time.time() + ns.timeout
    status = 1
    loaded_reported = False
    try:
        while time.time() < deadline:
            if proc is not None and proc.poll() is not None:
                print(f"process exited: {proc.returncode}", flush=True)
                break
            if result_path is not None and result_path.exists() and not loaded_reported:
                result = read_result(result_path)
                print(f"{result.get('status', 'unknown')}: {result.get('detail', '')}", flush=True)
                status = 0 if result.get("status") == "loaded" else 1
                loaded_reported = True
            if ns.mode == "original" and state_value("new_game_seen") and state_value("chapter_seen"):
                if state_value("dialog_seen") or not ns.auto_chapter:
                    status = 0
            if ns.auto_dialog and state_value("dialog_seen") and not state_value("keys_sent"):
                set_state(keys_sent=True)
                send_intro_dialog_replies(pid)
            if status == 0 and (not ns.auto_dialog or state_value("keys_sent")):
                break
            time.sleep(0.25)
    finally:
        if spawned:
            try:
                frida.kill(pid)
            except Exception:
                pass
        try:
            session.detach()
        except Exception:
            pass
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    with state_lock:
        final_state = dict(state)
    print(f"state={json.dumps(final_state, sort_keys=True)}", flush=True)
    print(f"log={LOG}", flush=True)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
