#!/usr/bin/env python3
"""Frida trace for area music, ambient loops, and positioned sounds."""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path

import frida

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from frida_intro_trace import (
    GAME_DIR,
    ORIG_EXE,
    REPO,
    RE_EXE,
    VK_1,
    capture_game_surface,
    click_client,
    send_key_to_pid,
    skip_original_startup_movies,
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
    "Area.GetSong": "?GetSong@CGameArea@@QAEDF@Z",
    "Area.PlaySong": "?PlaySong@CGameArea@@QAEXFK@Z",
    "Sprite.InitializeWalkingSound": "?InitializeWalkingSound@CGameSprite@@QAEXXZ",
    "Animation.GetSndWalk": "?GetSndWalk@CGameAnimation@@QAEPADF@Z",
    "Animation.GetSndWalkFreq": "?GetSndWalkFreq@CGameAnimation@@QAEKXZ",
    "Mixer.SetListenPosition": "?SetListenPosition@CSoundMixer@@QAEXHHH@Z",
    "Mixer.SetChannelVolume": "?SetChannelVolume@CSoundMixer@@QAEXHH@Z",
    "Mixer.UpdateSoundList": "?UpdateSoundList@CSoundMixer@@QAEXXZ",
    "Mixer.StartSong2": "?StartSong@CSoundMixer@@QAEXHK@Z",
    "Mixer.StartSong4": "?StartSong@CSoundMixer@@QAEXHHHK@Z",
    "Mixer.StopMusic": "?StopMusic@CSoundMixer@@QAEXH@Z",
    "Mixer.GetChannelStatus": "?GetChannelStatus@CSoundMixer@@QAEHXZ",
    "Sound.SetResRefWave": "?SetResRef@?$CResHelper@VCResWave@@$03@@QAEXABVCResRef@@HH@Z",
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

ORIG_HOOKS = {
    "Area.OnActivation": 0x4750E0,
    "Area.OnDeactivation": 0x475330,
    "Area.SetTimeOfDay": 0x479110,
    "Area.SetDay": 0x477EE0,
    "Area.SetNight": 0x4781B0,
    "Area.SetDawn": 0x478490,
    "Area.SetDusk": 0x478AC0,
    "Area.GetSong": 0x479DB0,
    "Area.PlaySong": 0x479E80,
    "Sprite.InitializeWalkingSound": 0x71DD20,
    "Mixer.SetListenPosition": 0x7ABA90,
    "Mixer.SetChannelVolume": 0x7AB990,
    "Mixer.UpdateSoundList": 0x7ABBA0,
    "Mixer.StartSong2": 0x7AC4F0,
    "Mixer.StartSong4": 0x7AC510,
    "Mixer.StopMusic": 0x7AC8E0,
    "Mixer.GetChannelStatus": 0x7ACA30,
    "Sound.SetChannel": 0x7AA4B0,
    "Sound.Play": 0x7A9B10,
    "Sound.PlayPos": 0x7A9DB0,
    "Music.musicForceSection": 0x7D60E0,
    "Music.forceSong": 0x7D5D00,
    "Music.musicSetSong": 0x7D58B0,
    "Music.musicFade": 0x7D5860,
    "Music.musicStop": 0x7D5A30,
    "Music.musicForceStop": 0x7D5BB0,
    "Music.musicLoadSongList": 0x7D45E0,
    "Music.musicSetPath": 0x7D5C80,
    "SoundBackend.soundLoad": 0x7D1E30,
    "SoundBackend.soundPlayFromPosition": 0x7D2370,
    "SoundBackend.soundDelete": 0x7D2620,
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


def make_js(hooks: dict[str, int], module_name: str, mode: str) -> str:
    return f"""
'use strict';
const hooks = {json.dumps(hooks)};
const isRe = {json.dumps(mode == "re")};
const base = Process.getModuleByName({json.dumps(module_name)}).base;
let playCount = 0;
let playPosCount = 0;
let setChannelCount = 0;
let dimmCount = 0;
let areaScanCount = 0;
let setResRefWaveCount = 0;
let sndWalkCount = 0;
let sndWalkFreqCount = 0;

function safe(fn, fallback) {{
  try {{ return fn(); }} catch (e) {{ return fallback; }}
}}

function s32(p) {{ return safe(() => Memory.readS32(p), 0); }}
function u32(p) {{ return safe(() => Memory.readU32(p), 0); }}
function u8(p) {{ return safe(() => Memory.readU8(p), 0); }}

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

function rangeInfo(p) {{
  return safe(() => {{
    const r = Process.findRangeByAddress(p);
    if (r === null) return null;
    return {{ base: r.base.toString(), size: r.size, protection: r.protection }};
  }}, null);
}}

function cstr(p) {{
  if (p.isNull()) return '';
  return safe(() => Memory.readCString(p), '');
}}

function bytes(p, n) {{
  if (p.isNull()) return [];
  return safe(() => {{
    const out = [];
    for (let i = 0; i < n; i++) out.push(Memory.readU8(p.add(i)));
    return out;
  }}, []);
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
    dayMusicByte: u8(h),
    dayMusic: u32(h),
    nightMusicByte: u8(h.add(0x04)),
    nightMusic: u32(h.add(0x04)),
    dayAmbient: resref(h.add(0x24)),
    dayAmbientExt: resref(h.add(0x2c)),
    dayAmbientVolume: u32(h.add(0x34)),
    nightAmbient: resref(h.add(0x38)),
    nightAmbientExt: resref(h.add(0x40)),
    nightAmbientVolume: u32(h.add(0x48)),
    ambientVolume: s32(p.add(0x984)),
    ambientDayVolume: s32(p.add(0x986)),
    ambientNightVolume: s32(p.add(0x988)),
    currentSlot: s32(p.add(0xae8)),
  }};
}}

function areaAmbientInfo(p) {{
  return {{
    day: soundInfo(p.add(0x8bc)),
    night: soundInfo(p.add(0x920)),
  }};
}}

function areaScan(p) {{
  const smallWords = [];
  for (let off = 0; off < 0x1000; off += 4) {{
    const v = u32(p.add(off));
    if (v > 0 && v <= 40) smallWords.push(['0x' + off.toString(16), v]);
    if (smallWords.length >= 80) break;
  }}
  const resrefs = [];
  for (let off = 0; off < 0x1000; off++) {{
    const s = resref(p.add(off));
    if (/^[A-Z0-9_][A-Z0-9_][A-Z0-9_]*$/.test(s) && s.length <= 8) {{
      resrefs.push(['0x' + off.toString(16), s]);
    }}
    if (resrefs.length >= 80) break;
  }}
  return {{ smallWords, resrefs }};
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

function addr(name) {{
  return isRe ? base.add(hooks[name]) : ptr(hooks[name]);
}}

function attach(name, callbacks) {{
  if (!(name in hooks)) {{
    emit('hook-skip', {{ name }});
    return;
  }}
  try {{
    Interceptor.attach(addr(name), callbacks);
    emit('hooked', {{ name, addr: addr(name).toString() }});
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
    onEnter(args) {{
      this.area = this.context.ecx;
      emit(name + '.in', {{ area: areaInfo(this.area), ambient: areaAmbientInfo(this.area) }});
    }},
    onLeave(rv) {{ emit(name + '.out', {{ area: areaInfo(this.area), ambient: areaAmbientInfo(this.area) }}); }},
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
  onLeave(rv) {{
    const rawSong = rv.toInt32() & 0xff;
    emit('Area.GetSong.ret', {{ area: areaInfo(this.area), slot: this.slot, song: rawSong >= 0x80 ? rawSong - 0x100 : rawSong, rawSong: rawSong }});
    if (this.slot === 0 && areaScanCount++ < 2) emit('Area.scan', {{ area: this.area.toString(), range: rangeInfo(this.area), scan: areaScan(this.area) }});
  }},
}});
attach('Area.PlaySong', {{
  onEnter(args) {{ emit('Area.PlaySong', {{ area: areaInfo(this.context.ecx), slot: args[0].toInt32(), flags: args[1].toUInt32() }}); }},
}});
attach('Sprite.InitializeWalkingSound', {{
  onEnter(args) {{ this.sprite = this.context.ecx; emit('Sprite.InitializeWalkingSound.in', {{ sprite: this.sprite.toString() }}); }},
  onLeave(rv) {{ emit('Sprite.InitializeWalkingSound.out', {{ sprite: this.sprite.toString() }}); }},
}});
attach('Animation.GetSndWalk', {{
  onEnter(args) {{ this.tableIndex = args[0].toInt32(); this.anim = this.context.ecx; }},
  onLeave(rv) {{
    if (sndWalkCount++ < 200) emit('Animation.GetSndWalk.ret', {{ anim: this.anim.toString(), tableIndex: this.tableIndex, ret: rv.toString(), sound: cstr(rv), resref: resref(rv), bytes: bytes(rv, 9) }});
  }},
}});
attach('Animation.GetSndWalkFreq', {{
  onEnter(args) {{ this.anim = this.context.ecx; }},
  onLeave(rv) {{
    if (sndWalkFreqCount++ < 200) emit('Animation.GetSndWalkFreq.ret', {{ anim: this.anim.toString(), freq: rv.toUInt32() }});
  }},
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
      this.trace = setChannelCount <= 300;
      this.sound = this.context.ecx;
      if (this.trace) emit('Sound.SetChannel.in', {{ sound: soundInfo(this.sound), newChannel: ch, newArea: '0x' + area.toString(16) }});
    }}
  }},
  onLeave(rv) {{ if (this.trace) emit('Sound.SetChannel.out', {{ sound: soundInfo(this.sound), ret: rv.toInt32() }}); }},
}});
attach('Sound.SetResRefWave', {{
  onEnter(args) {{
    const helper = this.context.ecx;
    const sound = helper.sub(4);
    const rr = resref(args[0]);
    if (rr === '' && setResRefWaveCount++ >= 40) return;
    emit('Sound.SetResRefWave', {{
      helper: helper.toString(),
      sound: soundInfo(sound),
      resref: rr,
      bSetAutoRequest: args[1].toInt32(),
      bWarningIfMissing: args[2].toInt32(),
      caller: this.returnAddress.toString(),
    }});
  }},
}});
attach('Sound.Play', {{
  onEnter(args) {{
    const info = soundInfo(this.context.ecx);
    const interesting = info.looping || (info.channel >= 16 && info.channel <= 20) || info.area !== '0x0';
    if ((interesting || playCount < 80) && playCount++ < 300) {{
      this.trace = true;
      this.sound = this.context.ecx;
      emit('Sound.Play.in', {{ sound: info, replay: args[0].toInt32() }});
    }}
  }},
  onLeave(rv) {{ if (this.trace) emit('Sound.Play.out', {{ ret: rv.toInt32(), sound: soundInfo(this.sound) }}); }},
}});
attach('Sound.PlayPos', {{
  onEnter(args) {{
    const info = soundInfo(this.context.ecx);
    if (playPosCount++ < 300) {{
      this.trace = true;
      this.sound = this.context.ecx;
      emit('Sound.PlayPos.in', {{ sound: info, pos: [args[0].toInt32(), args[1].toInt32(), args[2].toInt32()], replay: args[3].toInt32() }});
    }}
  }},
  onLeave(rv) {{ if (this.trace) emit('Sound.PlayPos.out', {{ ret: rv.toInt32(), sound: soundInfo(this.sound) }}); }},
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
    ap.add_argument("--mode", choices=["re", "original"], default="re")
    ap.add_argument("--pid", type=int, help="attach to an existing process instead of spawning")
    ap.add_argument("--slot", type=int, default=1, help="visible load-screen slot to load")
    ap.add_argument("--save-name", help="exact MPSave directory name to load")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--post-load-seconds", type=float, default=18.0)
    ap.add_argument("--output", type=Path, help="jsonl output path; defaults to tmp_audio_trace_<save>.jsonl")
    ns = ap.parse_args()

    global OUT
    label = f"slot{ns.slot}" if not ns.save_name else re.sub(r"[^a-zA-Z0-9_.-]+", "_", ns.save_name).strip("_")
    label = f"{ns.mode}_{label}"
    OUT = ns.output or (REPO / f"tmp_audio_trace_{label}.jsonl")
    OUT.write_text("", encoding="utf-8")

    result_path = Path(tempfile.gettempdir()) / f"iwd2-re-audio-{uuid.uuid4().hex}.txt"
    spawned = False
    target: dict[str, object]
    if ns.pid:
        pid = ns.pid
        target = {"pid": pid}
    elif ns.mode == "re":
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
        spawned = True
    else:
        pid = frida.spawn(str(ORIG_EXE), cwd=str(GAME_DIR))
        spawned = True
        target = {"manualLoadSlot": ns.slot}

    session = frida.attach(pid)
    hooks = map_offsets() if ns.mode == "re" else ORIG_HOOKS
    module_name = RE_EXE.name if ns.mode == "re" else ORIG_EXE.name
    state = {"area_active_at": 0.0}

    def on_message(message, data):
        payload = message["payload"] if message["type"] == "send" else {"tag": "ERROR", "message": message}
        if payload.get("tag") == "Area.OnActivation.out" and not state["area_active_at"]:
            state["area_active_at"] = time.time()
        emit(payload)

    script = session.create_script(make_js(hooks, module_name, ns.mode))
    script.on("message", on_message)
    script.load()
    if spawned:
        frida.resume(pid)
    stop_movie_keys = threading.Event()
    if spawned and ns.mode == "original":
        threading.Thread(
            target=skip_original_startup_movies,
            args=(pid, stop_movie_keys),
            daemon=True,
        ).start()
    emit({"tag": "Driver.spawned", "pid": pid, "result": str(result_path), "target": target, "output": str(OUT)})

    loaded_at = 0.0
    moved = False
    deadline = time.time() + ns.timeout
    try:
        while time.time() < deadline:
            if ns.mode == "re" and result_path.exists() and loaded_at == 0.0:
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
            elif ns.mode == "original" and state["area_active_at"] and loaded_at == 0.0:
                loaded_at = state["area_active_at"]
                emit({"tag": "Driver.loaded-result", "result": {"status": "loaded", "detail": "Area.OnActivation"}, "window": window_metrics(pid)})
                try:
                    shot_label = f"audio_{label}_loaded"
                    path, capture = capture_game_surface(pid, shot_label)
                    emit({"tag": "Driver.screenshot", "label": shot_label, "path": str(path), "capture": capture})
                except Exception as e:
                    emit({"tag": "Driver.screenshot-error", "err": str(e)})

            if loaded_at and not moved and time.time() - loaded_at > 2.0:
                emit({"tag": "Driver.key", "vk": VK_1, "window": window_metrics(pid)})
                send_key_to_pid(pid, VK_1, activation_click=True)
                time.sleep(0.25)
                for pos in [(250, 250), (610, 260), (230, 335)]:
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
        stop_movie_keys.set()
        try:
            if spawned:
                frida.kill(pid)
        except Exception:
            pass
        try:
            session.detach()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
