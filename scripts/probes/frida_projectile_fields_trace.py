#!/usr/bin/env python3
"""Frida trace: dump unnamed CProjectileTravelling fields during a Magic Missile cast.

Hooks on the original IWD2.exe (no ASLR, ptr(0xADDR) absolute):
  0x52C050  CProjectileTravelling::Fire       (vtable 0x84D9C4 slot 27) -- onLeave: dump all fields
  0x52BD20  CProjectileTravelling::AimAtPoint  (vtable 0x84D9C4 slot 33) -- onEnter/onLeave: carry fields
  0x52B900  CProjectileTravelling::AIUpdate    (vtable 0x84D9C4 slot 3)  -- onEnter: flight fields

Usage:
  python scripts/frida_projectile_fields_trace.py
  # In-game: load combat save, cast Magic Missile on an enemy.
  # Ctrl-C to stop. Log: tmp_frida_projectile_fields.log
"""
import frida
import sys
import os
import json

GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_projectile_fields.log")

JS = r"""
'use strict';

const Fire       = ptr(0x52C050);   // CProjectileTravelling::Fire __thiscall
const AimAtPoint = ptr(0x52BD20);   // CProjectileTravelling::AimAtPoint __thiscall
const AIUpdate   = ptr(0x52B900);   // CProjectileTravelling::AIUpdate __thiscall

function s16(v) {
  var n = typeof v === 'number' ? v : v.toInt32();
  return (n << 16) >> 16;
}

function readFields(p) {
  return {
    // Context
    m_pos:            { x: p.add(0x06).readS32(), y: p.add(0x0a).readS32() },
    m_id:             p.add(0x00).readU32(),
    m_projectileType: p.add(0x6E).readU16(),
    m_sourceId:       p.add(0x72).readU32(),
    m_casterClass:    p.add(0x186).readU32(),

    // Motion
    m_velocity:       s16(p.add(0x70).readU16()),
    m_posAccumX:      p.add(0x9C).readS32(),
    m_posAccumY:      p.add(0xA0).readS32(),
    m_stepX:          p.add(0xA4).readS32(),
    m_stepY:          p.add(0xA8).readS32(),

    // UNNAMED: drift / spread
    field_AC:         p.add(0xAC).readS32(),
    field_B0:         p.add(0xB0).readS32(),
    field_B4:         p.add(0xB4).readS32(),
    field_B8:         p.add(0xB8).readS32(),
    field_BC:         p.add(0xBC).readS32(),
    field_C0:         p.add(0xC0).readS32(),
    field_C4:         p.add(0xC4).readS32(),
    field_E0:         p.add(0xE0).readU16(),

    // Target / flags
    m_renderFlags:    p.add(0xE6).readU32(),
    m_targetX:        p.add(0xC8).readS32(),
    m_targetY:        p.add(0xCC).readS32(),
    m_facing:         s16(p.add(0x1DC).readU16()),

    // UNNAMED: flight / mirror / render / lifetime
    field_170:        p.add(0x170).readS32(),
    field_1CA:        p.add(0x1CA).readS32(),
    field_1CE:        p.add(0x1CE).readS32(),
    field_1D2:        s16(p.add(0x1D2).readU16()),
    field_29D:        p.add(0x29D).readU8(),

    // Known fields for cross-check
    m_lifetime:       s16(p.add(0x29E).readU16()),
    m_dirCount:       s16(p.add(0x1D8).readU16()),
    m_direction:      s16(p.add(0x1DA).readU16()),
    m_mirror:         p.add(0x1C6).readS32(),
    m_visible:        p.add(0x1DE).readS32(),
    m_hasShadowCell:  p.add(0x1D4).readS32(),
    m_tinted:         p.add(0x1BE).readS32(),
    m_paletteSwap:    p.add(0x29C).readU8(),
  };
}

// Fire — onLeave: dump post-setup state.  __thiscall: ecx = this.
Interceptor.attach(Fire, {
  onEnter(args) { this.thiz = this.context.ecx; },
  onLeave() {
    send({ tag: 'FIRE', fields: readFields(this.thiz) });
  }
});

// AimAtPoint — onEnter/onLeave: carry + step evolution per tick.
// __thiscall: ecx = this; stack: x(int), y(int), ret 8.
Interceptor.attach(AimAtPoint, {
  onEnter(args) {
    var p = this.context.ecx;
    send({
      tag: 'AIM_in', x: args[0].toInt32(), y: args[1].toInt32(),
      AC: p.add(0xAC).readS32(), B0: p.add(0xB0).readS32(),
      E0: p.add(0xE0).readU16(),
      B4: p.add(0xB4).readS32(), B8: p.add(0xB8).readS32(),
      BC: p.add(0xBC).readS32(), C0: p.add(0xC0).readS32(),
      stepX: p.add(0xA4).readS32(), stepY: p.add(0xA8).readS32(),
      accumX: p.add(0x9C).readS32(), accumY: p.add(0xA0).readS32(),
    });
    this.thiz = p;
  },
  onLeave() {
    var p = this.thiz;
    send({
      tag: 'AIM_out',
      AC: p.add(0xAC).readS32(), B0: p.add(0xB0).readS32(),
      E0: p.add(0xE0).readU16(),
      stepX: p.add(0xA4).readS32(), stepY: p.add(0xA8).readS32(),
      accumX: p.add(0x9C).readS32(), accumY: p.add(0xA0).readS32(),
      pos_x: p.add(0x06).readS32(), pos_y: p.add(0x0a).readS32(),
    });
  }
});

// AIUpdate — onEnter: flight / arrival state.
Interceptor.attach(AIUpdate, {
  onEnter(args) {
    var p = this.context.ecx;
    send({
      tag: 'AIUPD',
      field_170:  p.add(0x170).readS32(),
      field_29D:  p.add(0x29D).readU8(),
      m_lifetime: s16(p.add(0x29E).readU16()),
      pos_x:      p.add(0x06).readS32(), pos_y: p.add(0x0a).readS32(),
      tgtX:       p.add(0xC8).readS32(), tgtY: p.add(0xCC).readS32(),
      velocity:   s16(p.add(0x70).readU16()),
      id:         p.add(0x00).readU32(),
    });
  }
});

send({ tag: 'ready' });
"""


def main():
    attach = "--attach" in sys.argv
    open(LOG, "w").close()

    def on_message(message, data):
        if message["type"] == "send":
            line = json.dumps(message["payload"])
        else:
            line = "ERROR " + json.dumps(message)
        print(line, flush=True)
        with open(LOG, "a") as f:
            f.write(line + "\n")

    if attach:
        session = frida.attach("IWD2.exe")
        pid = None
        print("[*] attached to running IWD2.exe", flush=True)
    else:
        pid = frida.spawn(EXE, cwd=GAME_DIR)
        session = frida.attach(pid)
        print(f"[*] spawned IWD2.exe pid={pid}", flush=True)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()
    if pid is not None:
        frida.resume(pid)

    print("[*] hooks live — cast Magic Missile on an enemy.", flush=True)
    print(f"[*] logging to {LOG}", flush=True)
    sys.stdout.flush()
    import threading
    threading.Event().wait()


if __name__ == "__main__":
    main()
