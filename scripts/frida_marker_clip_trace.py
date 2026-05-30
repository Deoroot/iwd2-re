#!/usr/bin/env python3
"""Differential Frida tracer for IWD2 ground/destination-marker clipping.

Launches the *original* ``IWD2.exe`` under Frida and hooks the marker render
path plus the viewport-clip / scissor calls.  Diff this live trace against our
rebuilt ``iwd2-re.exe`` to find why the green destination markers bleed out of
the world viewport and paint over the action bar on a right-click-drag party
move.

What it answers:
  * what clip ``CRect`` is handed to every ellipse/recticle draw,
  * whether ``EnableScissoring`` / ``Set3dClipRect`` actually wrap the marker
    pass (and with what rect),
  * the screen-space centre each marker is drawn at.

Usage (PowerShell)::

    pip install frida
    python scripts/frida_marker_clip_trace.py            # spawn the game
    python scripts/frida_marker_clip_trace.py --attach   # hook a running one

Then drag the party with a right-click-drag toward the bottom edge of the
viewport.  Output is JSON lines to stdout and ``tmp_frida_marker_clip.log``.
Stop with Ctrl-C / kill.

Addresses are absolute: IWD2.exe preferred base is 0x400000 and the game ships
without ASLR, so they are stable across runs.  Every address below was
re-confirmed against Ghidra (truth).  The software-path DrawRecticle/DrawEllipse
(0x798780 / 0x796050) are deliberately NOT hooked: they are undefined in our
Ghidra DB and the live game is 3d-accelerated, so it funnels through the *3d
variants.  Only function ENTRIES are hooked (mid-function hooks crash IWD2.exe;
see docs/frida-differential-tracing.md).
"""
from __future__ import annotations

import argparse
import json
import os
import sys

try:
    import frida
except ImportError:  # pragma: no cover - dependency hint
    sys.stderr.write("frida not installed - run: pip install frida\n")
    sys.exit(1)


GAME_DIR = r"C:\GOG Games\Icewind Dale 2"
EXE = os.path.join(GAME_DIR, "IWD2.exe")
LOG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "tmp_frida_marker_clip.log")

# Hook addresses (absolute, ImageBase 0x400000, no ASLR).  All __thiscall:
# `this` is in ECX, stack args start at args[0].
ADDR = {
    "RenderMarkers":     0x704770,  # CGameSprite::RenderMarkers(pVidMode, nSurface)
    "MarkerRenderDest":  0x766EF0,  # CMarker::Render(pVidMode,nSurface,pInfinity,&dest,nX,nY)
    "MarkerRenderSprite":0x766E40,  # CMarker::Render(pVidMode,nSurface,pSprite)
    "DrawRecticle2d":    0x798780,  # CVidMode::DrawRecticle(&rd,&rClip,color) dispatcher
    "DrawEllipse2d":     0x796050,  # CVidMode::DrawEllipse(&ptCenter,&axis,&rClip,color) dispatcher
    "DrawRecticle3d":    0x7BC7A0,  # CVidMode::DrawRecticle3d(&rd, &rClip, color)
    "DrawEllipse3d":     0x7BC580,  # CVidMode::DrawEllipse3d(&ptCenter, &axis, &rClip, color)
    "Set3dClipRect":     0x7BC110,  # CVidMode::Set3dClipRect(&rClip) -> glScissor
    "EnableScissoring":  0x7BC180,  # CVidMode::EnableScissoring()
    "DisableScissoring": 0x7BC1C0,  # CVidMode::DisableScissoring()
}


def build_script() -> str:
    return r"""
'use strict';

// CRect = 4 x LONG: left@0, top@4, right@8, bottom@0xC.
function rect(p) {
  if (p.isNull()) return null;
  return [p.add(0).readS32(), p.add(4).readS32(),
          p.add(8).readS32(), p.add(12).readS32()];
}
// CPoint = x@0, y@4.  CSize = cx@0, cy@4.
function pt(p) {
  if (p.isNull()) return null;
  return [p.add(0).readS32(), p.add(4).readS32()];
}

const A = %(ADDR_JSON)s;

// CGameSprite::RenderMarkers - per-sprite marker pass entry.
Interceptor.attach(ptr(A.RenderMarkers), {
  onEnter(args) {
    send({ tag: 'RenderMarkers', this: this.context.ecx.toString(),
           surface: args[1].toInt32() });
  }
});

// CMarker::Render (destination 6-arg) - the green dest reticle/ellipse.
// args: [0]pVidMode [1]nSurface [2]pInfinity [3]&dest [4]nXSize [5]nYSize
Interceptor.attach(ptr(A.MarkerRenderDest), {
  onEnter(args) {
    send({ tag: 'MarkerRenderDest', dest: pt(args[3]),
           sizeX: args[4].toInt32(), sizeY: args[5].toInt32(),
           pInfinity: args[2].toString() });
  }
});

// CMarker::Render (sprite 3-arg) - the selection / talk ellipse under a sprite.
Interceptor.attach(ptr(A.MarkerRenderSprite), {
  onEnter(args) {
    send({ tag: 'MarkerRenderSprite', pSprite: args[2].toString() });
  }
});

// 2d dispatchers.  These branch to the *3d variant when m_bIs3dAccelerated,
// else fall to the software DrawRecticle16/DrawEllipse16 (which clip via
// IntersectRect).  Logging them tells us which clip path the binary actually
// takes for the dest markers.  args[1] (recticle) / args[2] (ellipse) = &rClip.
Interceptor.attach(ptr(A.DrawRecticle2d), {
  onEnter(args) { send({ tag: 'DrawRecticle2d', clip: rect(args[1]) }); }
});
Interceptor.attach(ptr(A.DrawEllipse2d), {
  onEnter(args) {
    send({ tag: 'DrawEllipse2d', center: pt(args[0]), axis: pt(args[1]),
           clip: rect(args[2]) });
  }
});

// CMarker::Render (sprite 3-arg) - the selection / talk ellipse under a sprite.
Interceptor.attach(ptr(A.MarkerRenderSprite), {
  onEnter(args) {
    send({ tag: 'MarkerRenderSprite', pSprite: args[2].toString() });
  }
});

// 2d dispatchers.  These branch to the *3d variant when m_bIs3dAccelerated,
// else fall to the software DrawRecticle16/DrawEllipse16 (which clip via
// IntersectRect).  Logging them tells us which clip path the binary actually
// takes for the dest markers.  args[1] (recticle) / args[2] (ellipse) = &rClip.
Interceptor.attach(ptr(A.DrawRecticle2d), {
  onEnter(args) { send({ tag: 'DrawRecticle2d', clip: rect(args[1]) }); }
});
Interceptor.attach(ptr(A.DrawEllipse2d), {
  onEnter(args) {
    send({ tag: 'DrawEllipse2d', center: pt(args[0]), axis: pt(args[1]),
           clip: rect(args[2]) });
  }
});

// 3d-accelerated draws.  rClip == the viewport rect handed down from CMarker.
// CVidMode::DrawRecticle3d(&rd, &rClip, color)
Interceptor.attach(ptr(A.DrawRecticle3d), {
  onEnter(args) {
    send({ tag: 'DrawRecticle3d', clip: rect(args[1]),
           color: args[2].toUInt32() });
  }
});
// CVidMode::DrawEllipse3d(&ptCenter, &axis, &rClip, color)
Interceptor.attach(ptr(A.DrawEllipse3d), {
  onEnter(args) {
    send({ tag: 'DrawEllipse3d', center: pt(args[0]), axis: pt(args[1]),
           clip: rect(args[2]), color: args[3].toUInt32() });
  }
});

// CVidMode::Set3dClipRect(&rClip) -> glScissor.  Suspected culprit: in our
// rebuild this only fires when m_bResizedViewPort is set.
Interceptor.attach(ptr(A.Set3dClipRect), {
  onEnter(args) { send({ tag: 'Set3dClipRect', rect: rect(args[0]) }); }
});

// Scissor enable/disable bracket the world render in CScreenWorld.
Interceptor.attach(ptr(A.EnableScissoring),  { onEnter() { send({ tag: 'EnableScissoring' }); } });
Interceptor.attach(ptr(A.DisableScissoring), { onEnter() { send({ tag: 'DisableScissoring' }); } });

send({ tag: 'ready' });
""" % {"ADDR_JSON": json.dumps(ADDR)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attach", action="store_true",
                        help="attach to a running IWD2 instead of spawning")
    args = parser.parse_args()

    open(LOG, "w").close()

    def on_message(message, data):
        if message["type"] == "send":
            line = json.dumps(message["payload"])
        else:
            line = "ERROR " + json.dumps(message)
        print(line, flush=True)
        with open(LOG, "a") as f:
            f.write(line + "\n")

    if args.attach:
        session = frida.attach("IWD2.exe")
        pid = None
        print("[*] attached to running IWD2.exe", flush=True)
    else:
        # cwd MUST be the game dir or IWD2.exe asserts "bad key file"
        # (Chitin.cpp:1738 - chitin.key is looked up relative to cwd).
        pid = frida.spawn(EXE, cwd=GAME_DIR)
        session = frida.attach(pid)
        print(f"[*] spawned IWD2.exe pid={pid}", flush=True)

    script = session.create_script(build_script())
    script.on("message", on_message)
    script.load()
    if pid is not None:
        frida.resume(pid)

    print("[*] hooks live. Right-click-drag the party toward the bottom edge.",
          flush=True)
    print(f"[*] logging to {LOG}", flush=True)
    sys.stdout.flush()
    import threading
    threading.Event().wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
