#!/usr/bin/env python3
"""Frida trace for area music, ambient loops, and positioned sounds."""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import time
import uuid
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_intro_trace import (
    GAME_DIR,
    REPO,
    RE_EXE,
    VK_1,
    capture_game_surface,
    click_client,
    send_key_to_pid,
    window_metrics,
)

MAP_FILE = REPO / "build" / "Debug" / "iwd2-re.map"
LINK_IMAGE_BASE = 0x400000
OUT = REPO / "tmp_audio_trace.jsonl"

SYMBOLS = {
    "Area.OnActivation": "?OnActivation@CGameArea@@QAEXXZ",
    "Area.OnDeactivation": "?OnDeactivation@CGameArea@@QAEXXZ",
    "Area.SetTimeOfDay": "?SetTimeOfDay@CGameArea@@QAEXKE@Z",
    "Area.SetDay": "?SetDay@CGameArea@@QAEXXZ",
    "Area.SetNight": "?SetNight@CGameArea@@QAEXXZ",
    "Area.SetDawn": "?SetDawn@CGameArea@@QAEXEE@Z",
    "Area.SetDusk": "?SetDusk@CGameArea@@QAEXEE@Z",
    "Area.GetSong": "?GetSong@CGameArea@@QAEEF@Z",
    "Area.PlaySong": "?PlaySong@CGameArea@@QAEXFK@Z",
    "Mixer.SetListenPosition": "?SetListenPosition@CSoundMixer@@QAEXHHH@Z",
    "Mixer.SetChannelVolume": "?SetChannelVolume@CSoundMixer@@QAEXHH@Z",
    "Mixer.UpdateSoundList": "?UpdateSoundList@CSoundMixer@@QAEXXZ",
    "Mixer.StartSong2": "?StartSong@CSoundMixer@@QAEXHK@Z",
    "Mixer.StartSong4": "?StartSong@CSoundMixer@@QAEXHHHK@Z",
    "Mixer.StopMusic": "?StopMusic@CSoundMixer@@QAEXH@Z",
    "Mixer.GetChannelStatus": "?GetChannelStatus@CSoundMixer@@QAEHXZ",
    "Sound.SetChannel": "?SetChannel@CSound@@QAEHHK@Z",
    "Sound.Play": "?Play@CSound@@QAEHH@Z",
    "Sound.PlayPos": "?Play@CSound@@QAEHHHHH@Z",
    "Dimm.GetMemoryAmount": "?GetMemoryAmount@CDimm@@QAEHXZ",
    "Music.musicForceSection": "?musicForceSection@@YAHHHH@Z",
    "Music.forceSong": "?forceSong@@YAHHHH@Z",
    "Music.internalMusicPlay": "?internalMusicPlay@@YAHHHH@Z",
    "Music.musicSetSong": "?musicSetSong@@YAHHHH@Z",
    "Music.musicFade": "?musicFade@@YAXHHHH@Z",
    "Music.musicStop": "?musicStop@@YAHXZ",
    "Music.musicForceStop": "?musicForceStop@@YAHXZ",
    "Music.musicLoadSongList": "?musicLoadSongList@@YAHPAPADH@Z",
    "Music.musicSetPath": "?musicSetPath@@YAHPBD0@Z",
    "SoundBackend.soundLoad": "?soundLoad@@YAHPAUtag_sound@@PAD@Z",
    "SoundBackend.soundPlayFromPosition": "?soundPlayFromPosition@@YAHPAUtag_sound@@H@Z",
    "SoundBackend.soundDelete": "?soundDelete@@YAHPAUtag_sound@@@Z",
}


def read_result(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value
    return values


def map_offsets() -> dict[str, int]:
    wanted = {decorated: name for name, decorated in SYMBOLS.items()}
    found: dict[str, int] = {}
    for line in MAP_FILE.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        decorated = parts[1]
        name = wanted.get(decorated)
        if name is None:
            continue
        if re.fullmatch(r"[0-9A-Fa-f]{8}", parts[2]):
            found[name] = int(parts[2], 16) - LINK_IMAGE_BASE
    missing = sorted(set(SYMBOLS) - set(found))
    if missing:
        raise SystemExit(f"missing map symbols: {missing}")
    return found


def make_js(hooks: dict[str, int]) -> str:
    return f"""
'use strict';
const hooks = {json.dumps(hooks)};
const base = Process.getModuleByName('iwd2-re.exe').base;
let playCount = 0;
let playPosCount = 0;
let setChannelCount = 0;
let dimmCount = 0;

function safe(fn, fallback) {{
  try {{ return fn(); }} catch (e) {{ return fallback; }}
}}

function s32(p) {{ return safe(() => Memory.readS32(p), 0); }}
function u32(p) {{ return safe(() => Memory.readU32(p), 0); }}

function resref(p) {{
  return safe(() => {{
    let s = '';
    for (let i = 0; i < 8; i++) {{
      const c = Memory.readU8(p.add(i));
      if (c === 0) break;
      s += String.fromCharCode(c);
    }}
    return s;
  }}, '');
}}

function cstr(p) {{
  if (p.isNull()) return '';
  return safe(() => Memory.readCString(p), '');
}}

function soundInfo(p) {{
  return {{
    ptr: p.toString(),
    resref: resref(p.add(0x0c)),
    range: s32(p.add(0x20)),
    rangeVolume: s32(p.add(0x24)),
    pos: [s32(p.add(0x28)), s32(p.add(0x2c)), s32(p.add(0x30))],
    volume: s32(p.add(0x38)),
    channel: s32(p.add(0x40)),
    priority: s32(p.add(0x44)),
    looping: s32(p.add(0x48)),
    area: '0x' + u32(p.add(0x60)).toString(16),
  }};
}}

function areaInfo(p) {{
  const h = p.add(0x5c);
  return {{
    ptr: p.toString(),
    resref: resref(p.add(0x1f0)),
    dayMusic: u32(h),
    nightMusic: u32(h.add(0x04)),
    dayAmbient: resref(h.add(0x24)),
    dayAmbientExt: resref(h.add(0x2c)),
    dayAmbientVolume: u32(h.add(0x34)),
    nightAmbient: resref(h.add(0x38)),
    nightAmbientExt: resref(h.add(0x40)),
    nightAmbientVolume: u32(h.add(0x48)),
    currentSlot: s32(p.add(0x1626)),
  }};
}}

function mixerInfo(p) {{
  return {{
    ptr: p.toString(),
    fieldC0: s32(p.add(0xc0)),
    activeArea: '0x' + u32(p.add(0xf0)).toString(16),
    musicInitialized: s32(p.add(0x118)),
    currentSong: s32(p.add(0x11c)),
    numSongs: s32(p.add(0x120)),
    lastSong: s32(p.add(0x124)),
  }};
}}

function emit(tag, data) {{
  data.tag = tag;
  send(data);
}}

function attach(name, callbacks) {{
  try {{
    Interceptor.attach(base.add(hooks[name]), callbacks);
    emit('hooked', {{ name, addr: base.add(hooks[name]).toString() }});
  }} catch (e) {{
    emit('hook-error', {{ name, error: String(e) }});
  }}
}}

attach('Area.OnActivation', {{
  onEnter(args) {{ this.area = this.context.ecx; emit('Area.OnActivation.in', {{ area: areaInfo(this.area) }}); }},
  onLeave(rv) {{ emit('Area.OnActivation.out', {{ area: areaInfo(this.area) }}); }},
}});
attach('Area.OnDeactivation', {{
  onEnter(args) {{ emit('Area.OnDeactivation', {{ area: areaInfo(this.context.ecx) }}); }},
}});
attach('Area.SetTimeOfDay', {{
  onEnter(args) {{ emit('Area.SetTimeOfDay', {{ area: areaInfo(this.context.ecx), timeOfDay: args[0].toUInt32(), movie: args[1].toInt32() }}); }},
}});
for (const name of ['Area.SetDay', 'Area.SetNight']) {{
  attach(name, {{
    onEnter(args) {{ this.area = this.context.ecx; emit(name + '.in', {{ area: areaInfo(this.area) }}); }},
    onLeave(rv) {{ emit(name + '.out', {{ area: areaInfo(this.area) }}); }},
  }});
}}
for (const name of ['Area.SetDawn', 'Area.SetDusk']) {{
  attach(name, {{
    onEnter(args) {{ this.area = this.context.ecx; emit(name + '.in', {{ area: areaInfo(this.area), intensity: args[0].toInt32(), movie: args[1].toInt32() }}); }},
    onLeave(rv) {{ emit(name + '.out', {{ area: areaInfo(this.area) }}); }},
  }});
}}
attach('Area.GetSong', {{
  onEnter(args) {{ this.area = this.context.ecx; this.slot = args[0].toInt32(); }},
  onLeave(rv) {{ emit('Area.GetSong.ret', {{ area: areaInfo(this.area), slot: this.slot, song: rv.toInt32() & 0xff }}); }},
}});
attach('Area.PlaySong', {{
  onEnter(args) {{ emit('Area.PlaySong', {{ area: areaInfo(this.context.ecx), slot: args[0].toInt32(), flags: args[1].toUInt32() }}); }},
}});
attach('Mixer.SetListenPosition', {{
  onEnter(args) {{ emit('Mixer.SetListenPosition', {{ mixer: mixerInfo(this.context.ecx), pos: [args[0].toInt32(), args[1].toInt32(), args[2].toInt32()] }}); }},
}});
attach('Mixer.SetChannelVolume', {{
  onEnter(args) {{
    const ch = args[0].toInt32();
    if (ch >= 16 && ch <= 20) emit('Mixer.SetChannelVolume', {{ mixer: mixerInfo(this.context.ecx), channel: ch, volume: args[1].toInt32() }});
  }},
}});
attach('Mixer.UpdateSoundList', {{
  onEnter(args) {{ emit('Mixer.UpdateSoundList.in', {{ mixer: mixerInfo(this.context.ecx) }}); }},
  onLeave(rv) {{ emit('Mixer.UpdateSoundList.out', {{ mixer: mixerInfo(this.context.ecx) }}); }},
}});
attach('Mixer.StartSong2', {{
  onEnter(args) {{ this.m = this.context.ecx; emit('Mixer.StartSong2.in', {{ mixer: mixerInfo(this.m), song: args[0].toInt32(), flags: args[1].toUInt32(), caller: this.returnAddress.toString() }}); }},
  onLeave(rv) {{ emit('Mixer.StartSong2.out', {{ mixer: mixerInfo(this.m) }}); }},
}});
attach('Mixer.StartSong4', {{
  onEnter(args) {{ this.m = this.context.ecx; emit('Mixer.StartSong4.in', {{ mixer: mixerInfo(this.m), song: args[0].toInt32(), section: args[1].toInt32(), position: args[2].toInt32(), flags: args[3].toUInt32(), caller: this.returnAddress.toString() }}); }},
  onLeave(rv) {{ emit('Mixer.StartSong4.out', {{ mixer: mixerInfo(this.m) }}); }},
}});
attach('Mixer.StopMusic', {{
  onEnter(args) {{ this.m = this.context.ecx; emit('Mixer.StopMusic.in', {{ mixer: mixerInfo(this.m), force: args[0].toInt32(), caller: this.returnAddress.toString() }}); }},
  onLeave(rv) {{ emit('Mixer.StopMusic.out', {{ mixer: mixerInfo(this.m) }}); }},
}});
attach('Mixer.GetChannelStatus', {{
  onEnter(args) {{ this.m = this.context.ecx; }},
  onLeave(rv) {{ emit('Mixer.GetChannelStatus.ret', {{ mixer: mixerInfo(this.m), ret: rv.toInt32() }}); }},
}});
attach('Sound.SetChannel', {{
  onEnter(args) {{
    const ch = args[0].toInt32();
    const area = args[1].toUInt32();
    if ((ch >= 16 && ch <= 20) || area !== 0) {{
      setChannelCount++;
      if (setChannelCount <= 300) emit('Sound.SetChannel', {{ sound: soundInfo(this.context.ecx), newChannel: ch, newArea: '0x' + area.toString(16) }});
    }}
  }},
}});
attach('Sound.Play', {{
  onEnter(args) {{
    const info = soundInfo(this.context.ecx);
    const interesting = info.looping || (info.channel >= 16 && info.channel <= 20) || info.area !== '0x0';
    if (interesting && playCount++ < 300) {{
      this.trace = true;
      emit('Sound.Play.in', {{ sound: info, replay: args[0].toInt32() }});
    }}
  }},
  onLeave(rv) {{ if (this.trace) emit('Sound.Play.out', {{ ret: rv.toInt32(), sound: soundInfo(this.context.ecx) }}); }},
}});
attach('Sound.PlayPos', {{
  onEnter(args) {{
    const info = soundInfo(this.context.ecx);
    const interesting = info.channel >= 16 && info.channel <= 20 || info.area !== '0x0';
    if (interesting && playPosCount++ < 300) {{
      this.trace = true;
      emit('Sound.PlayPos.in', {{ sound: info, pos: [args[0].toInt32(), args[1].toInt32(), args[2].toInt32()], replay: args[3].toInt32() }});
    }}
  }},
  onLeave(rv) {{ if (this.trace) emit('Sound.PlayPos.out', {{ ret: rv.toInt32(), sound: soundInfo(this.context.ecx) }}); }},
}});
attach('Dimm.GetMemoryAmount', {{
  onLeave(rv) {{ if (dimmCount++ < 40) emit('Dimm.GetMemoryAmount.ret', {{ ret: rv.toInt32() }}); }},
}});
attach('Music.musicLoadSongList', {{
  onEnter(args) {{ this.num = args[1].toInt32(); emit('Music.musicLoadSongList.in', {{ num: this.num }}); }},
  onLeave(rv) {{ emit('Music.musicLoadSongList.out', {{ ret: rv.toInt32(), num: this.num }}); }},
}});
attach('Music.musicSetPath', {{
  onEnter(args) {{ emit('Music.musicSetPath.in', {{ path: cstr(args[0]), ext: cstr(args[1]) }}); }},
  onLeave(rv) {{ emit('Music.musicSetPath.out', {{ ret: rv.toInt32() }}); }},
}});
for (const name of ['Music.musicForceSection', 'Music.forceSong', 'Music.internalMusicPlay', 'Music.musicSetSong']) {{
  attach(name, {{
    onEnter(args) {{
      this.args = [args[0].toInt32(), args[1].toInt32(), args[2].toInt32()];
      emit(name + '.in', {{ song: this.args[0], section: this.args[1], position: this.args[2], caller: this.returnAddress.toString() }});
    }},
    onLeave(rv) {{ emit(name + '.out', {{ ret: rv.toInt32(), song: this.args[0], section: this.args[1], position: this.args[2] }}); }},
  }});
}}
attach('Music.musicFade', {{
  onEnter(args) {{
    emit('Music.musicFade.in', {{ song: args[0].toInt32(), section: args[1].toInt32(), position: args[2].toInt32(), fadeTime: args[3].toInt32(), caller: this.returnAddress.toString() }});
  }},
}});
for (const name of ['Music.musicStop', 'Music.musicForceStop']) {{
  attach(name, {{
    onEnter(args) {{ emit(name + '.in', {{ caller: this.returnAddress.toString() }}); }},
    onLeave(rv) {{ emit(name + '.out', {{ ret: rv.toInt32() }}); }},
  }});
}}
attach('SoundBackend.soundLoad', {{
  onEnter(args) {{ this.sound = args[0]; this.name = cstr(args[1]); emit('SoundBackend.soundLoad.in', {{ sound: this.sound.toString(), name: this.name }}); }},
  onLeave(rv) {{ emit('SoundBackend.soundLoad.out', {{ ret: rv.toInt32(), sound: this.sound.toString(), name: this.name }}); }},
}});
attach('SoundBackend.soundPlayFromPosition', {{
  onEnter(args) {{ this.sound = args[0]; this.position = args[1].toInt32(); emit('SoundBackend.soundPlayFromPosition.in', {{ sound: this.sound.toString(), position: this.position }}); }},
  onLeave(rv) {{ emit('SoundBackend.soundPlayFromPosition.out', {{ ret: rv.toInt32(), sound: this.sound.toString(), position: this.position }}); }},
}});
attach('SoundBackend.soundDelete', {{
  onEnter(args) {{ emit('SoundBackend.soundDelete', {{ sound: args[0].toString(), caller: this.returnAddress.toString() }}); }},
}});
"""


def emit(payload: dict[str, object]) -> None:
    line = json.dumps(payload, ensure_ascii=True)
    print(line, flush=True)
    with OUT.open("a", encoding="utf-8") as f:
        f.write(line + "\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--slot", type=int, default=1, help="visible load-screen slot to load")
    ap.add_argument("--save-name", help="exact MPSave directory name to load")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--post-load-seconds", type=float, default=18.0)
    ap.add_argument("--output", type=Path, help="jsonl output path; defaults to tmp_audio_trace_<save>.jsonl")
    ns = ap.parse_args()

    global OUT
    label = f"slot{ns.slot}" if not ns.save_name else re.sub(r"[^a-zA-Z0-9_.-]+", "_", ns.save_name).strip("_")
    OUT = ns.output or (REPO / f"tmp_audio_trace_{label}.jsonl")
    OUT.write_text("", encoding="utf-8")

    result_path = Path(tempfile.gettempdir()) / f"iwd2-re-audio-{uuid.uuid4().hex}.txt"
    env = os.environ.copy()
    env["IWD2_RE_AUTO_RESULT"] = str(result_path)
    env["IWD2_RE_AUTO_ACTION"] = "load"
    if ns.save_name:
        env["IWD2_RE_AUTO_SAVE_NAME"] = ns.save_name
        target = {"saveName": ns.save_name}
    else:
        env["IWD2_RE_AUTO_SLOT"] = str(ns.slot)
        target = {"slot": ns.slot}

    pid = frida.spawn(str(RE_EXE), env=env, cwd=str(GAME_DIR))
    session = frida.attach(pid)
    script = session.create_script(make_js(map_offsets()))
    script.on("message", lambda m, d: emit(m["payload"] if m["type"] == "send" else {"tag": "ERROR", "message": m}))
    script.load()
    frida.resume(pid)
    emit({"tag": "Driver.spawned", "pid": pid, "result": str(result_path), "target": target, "output": str(OUT)})

    loaded_at = 0.0
    moved = False
    deadline = time.time() + ns.timeout
    try:
        while time.time() < deadline:
            if result_path.exists() and loaded_at == 0.0:
                result = read_result(result_path)
                emit({"tag": "Driver.loaded-result", "result": result, "window": window_metrics(pid)})
                if result.get("status") == "loaded":
                    loaded_at = time.time()
                    try:
                        shot_label = f"audio_{label}_loaded"
                        path, capture = capture_game_surface(pid, shot_label)
                        emit({"tag": "Driver.screenshot", "label": shot_label, "path": str(path), "capture": capture})
                    except Exception as e:
                        emit({"tag": "Driver.screenshot-error", "err": str(e)})
                else:
                    return 1

            if loaded_at and not moved and time.time() - loaded_at > 2.0:
                emit({"tag": "Driver.key", "vk": VK_1, "window": window_metrics(pid)})
                send_key_to_pid(pid, VK_1, activation_click=True)
                time.sleep(0.25)
                for pos in [(410, 305), (510, 350), (330, 360)]:
                    emit({"tag": "Driver.click", "pos": pos, "window": window_metrics(pid)})
                    click_client(pid, *pos, activation_click=False, hover_seconds=0.08)
                    time.sleep(2.0)
                moved = True

            if loaded_at and moved and time.time() - loaded_at > ns.post_load_seconds:
                try:
                    shot_label = f"audio_{label}_after_move"
                    path, capture = capture_game_surface(pid, shot_label)
                    emit({"tag": "Driver.screenshot", "label": shot_label, "path": str(path), "capture": capture})
                except Exception as e:
                    emit({"tag": "Driver.screenshot-error", "err": str(e)})
                return 0
            time.sleep(0.25)
        emit({"tag": "Driver.timeout"})
        return 1
    finally:
        try:
            frida.kill(pid)
        except Exception:
            pass
        try:
            session.detach()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
