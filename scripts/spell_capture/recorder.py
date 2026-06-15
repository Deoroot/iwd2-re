#!/usr/bin/env python3
"""Host-side spell-cast capture daemon.

Records the IWD2 VM display+audio (delivered to the host over SPICE) and cuts a
short clip every time a spell is cast in the game. Cast events arrive as UDP
datagrams from a marker forwarder running inside the VM (see cast_marker.py):

    {"exe": "orig"|"ours", "spell": "<resref>", "ts": <guest epoch, optional>}

The resref (e.g. SPWI304) names the clip; the recorder resolves it to the spell's
display name (Fireball) via reagent_asset_names.py for the filename and overlay.

Pipeline (all encoding on the host GPU, the VM only serves its normal SPICE):

  1. pactl null-sink "iwd2cap"  <- a dedicated, isolated audio sink.
  2. virsh screenshot           <- probe the guest's current resolution.
  3. Xvfb :N                    <- offscreen X server at that resolution.
  4. SPICE client (remote-viewer/spicy) into Xvfb, audio -> iwd2cap.
  5. ffmpeg, continuous, segmented HEVC(NVENC)+Opus -> ring/seg_<wallclock>.mkv.
  6. ring cleaner               <- drop segments older than --keep seconds.
  7. UDP listener + cutter      <- on a cast, re-cut [ts-pre, ts+post] from the
                                   ring and OVERWRITE clips/<spell>__<exe>.mkv.

The host stamps each UDP datagram's arrival time, which shares the wall clock
used to name the segments, so no cross-machine clock sync is needed; the --pre
roll absorbs the (sub-ms over slirp) trigger latency.

Start the game in the VM first, then run this so the resolution probe sees the
in-game resolution. Ctrl-C tears everything down (clips are kept).
"""
import argparse
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
ASSET_NAMES = REPO / "scripts" / "reagent_asset_names.py"

# --- encoder presets -------------------------------------------------------
# Light defaults on purpose: this is for spotting visual/audio differences, not
# archival quality. High cq / fast preset / low fps = small files, low GPU load.
VENC = {
    # name        : (codec,        [extra ffmpeg -c:v opts])
    "hevc_nvenc": ("hevc_nvenc", ["-preset", "p4", "-rc", "vbr", "-cq", "30"]),
    "av1_nvenc":  ("av1_nvenc",  ["-preset", "p4", "-rc", "vbr", "-cq", "34"]),
    "libx265":    ("libx265",    ["-preset", "veryfast", "-crf", "28"]),
}


def log(msg):
    print(f"[recorder {time.strftime('%H:%M:%S')}] {msg}", flush=True)


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, **kw)


# --- spell resref -> canonical name ---------------------------------------
_NAME_CACHE = {}            # RESREF -> display name (e.g. "SPWI304" -> "Fireball")


def spell_display(resref):
    """Resolve a spell resref to its TLK display name, cached. Falls back to the
    resref itself if reagent_asset_names.py can't resolve it (unknown/non-SPL)."""
    rr = str(resref).strip().upper()
    if not rr:
        return "unknown"
    if rr in _NAME_CACHE:
        return _NAME_CACHE[rr]
    name = rr
    try:
        r = run([sys.executable, str(ASSET_NAMES), rr, "--json"])
        data = json.loads(r.stdout)
        if data and data[0].get("name"):
            name = data[0]["name"]
    except (OSError, ValueError, IndexError, KeyError, AttributeError):
        pass
    _NAME_CACHE[rr] = name
    return name


def spell_filename(resref):
    """filename-safe display name (underscored), e.g. SPWI304 -> 'Fireball'."""
    return re.sub(r"[^A-Za-z0-9]+", "_", spell_display(resref)).strip("_") \
        or str(resref).strip().upper() or "unknown"


def find_font():
    r = run(["fc-match", "-f", "%{file}", "sans"])
    if r.stdout.strip() and Path(r.stdout.strip()).exists():
        return r.stdout.strip()
    for p in ("/usr/share/fonts/TTF/DejaVuSans.ttf",
              "/usr/share/fonts/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        if Path(p).exists():
            return p
    return ""


# --- guest resolution probe ------------------------------------------------
def probe_resolution(domain):
    tmp = "/tmp/iwd2_resprobe.png"
    r = run(["virsh", "screenshot", domain, tmp])
    if r.returncode != 0:
        log(f"virsh screenshot failed: {r.stderr.strip()}")
        return None
    p = run(["ffprobe", "-v", "error", "-select_streams", "v:0",
             "-show_entries", "stream=width,height", "-of", "csv=p=0", tmp])
    try:
        w, h = (int(x) for x in p.stdout.strip().split(",")[:2])
        return w, h
    except (ValueError, IndexError):
        log(f"could not parse resolution from ffprobe: {p.stdout!r}")
        return None


def pick_display():
    for n in range(99, 130):
        if not Path(f"/tmp/.X11-unix/X{n}").exists():
            return n
    return 99


# Game-window crop: IWD2 runs in an 800x600 window on a larger desktop, so we
# capture the full desktop (SPICE shows session 1 1:1 in Xvfb) and crop each clip
# to the game window. The host cannot read guest window geometry across the
# session-0 (SSH) / session-1 (game) boundary -- MainWindowHandle is 0 there --
# so the in-VM marker (cast_marker.py, session 1) reports the rect via "geom"
# UDP datagrams and the recorder crops to the latest one. --crop / --full override.


# --- segment bookkeeping ---------------------------------------------------
SEG_RE = re.compile(r"seg_(\d{8}_\d{6})\.mkv$")


def seg_start_epoch(path):
    m = SEG_RE.search(str(path))
    if not m:
        return None
    return time.mktime(time.strptime(m.group(1), "%Y%m%d_%H%M%S"))


class Recorder:
    def __init__(self, args):
        self.a = args
        self.ring = Path(args.ring); self.ring.mkdir(parents=True, exist_ok=True)
        self.clips = Path(args.clips); self.clips.mkdir(parents=True, exist_ok=True)
        self.stop = threading.Event()
        self.procs = {}            # name -> Popen
        self.sink_module = None    # pactl null-sink module id (str) to unload
        self.loopback_module = None  # pactl loopback (capture -> speakers) module id
        self.events = []           # queue of (arrival_epoch, exe, resref)
        self.ev_lock = threading.Lock()
        self._last_cast = {}       # exe -> last accepted cast time (debounce bursts)
        self.w = self.h = None            # Xvfb / guest desktop size (1:1 map)
        self.cx = self.cy = 0             # capture offset (full desktop -> 0,0)
        self.cw = self.ch = None          # capture size (full desktop)
        self.crop = None                  # (x,y,w,h) clip crop: --crop or marker geom
        self.crop_from_geom = False       # geom = guest coords (needs toolbar offset)
        self.crop_lock = threading.Lock()
        self.dh = None                    # Xephyr/capture height = guest h + toolbar_h

    # -- external resource setup --
    def load_null_sink(self):
        # Dedicated sink so ffmpeg captures ONLY the game audio (clean isolation).
        have = run(["pactl", "list", "short", "sinks"])
        if "iwd2cap" in have.stdout:
            log("null-sink iwd2cap already present, reusing")
        else:
            r = run(["pactl", "load-module", "module-null-sink",
                     "sink_name=iwd2cap",
                     "sink_properties=device.description=iwd2cap"])
            if r.returncode != 0:
                raise RuntimeError(f"pactl null-sink failed: {r.stderr.strip()}")
            self.sink_module = r.stdout.strip()
            log(f"null-sink iwd2cap loaded (module {self.sink_module})")
        # The null-sink is silent on the host, so loop its monitor back to the real
        # speakers -- otherwise YOU can't hear the game while it's being captured.
        if not self.a.mute:
            defsink = run(["pactl", "get-default-sink"]).stdout.strip()
            r = run(["pactl", "load-module", "module-loopback",
                     "source=iwd2cap.monitor", f"sink={defsink}", "latency_msec=60"])
            if r.returncode == 0:
                self.loopback_module = r.stdout.strip()
                log(f"audio loopback iwd2cap.monitor -> {defsink} (you hear the game)")
            else:
                log(f"WARN: audio loopback failed (game will be silent to you): "
                    f"{r.stderr.strip()}")

    def start_display(self, disp):
        # Default = Xephyr: a nested X server shown as a WINDOW on the host. You
        # watch and DRIVE the game in that window (it replaces virt-manager), so
        # only ONE SPICE client exists and nothing kicks the recorder. ffmpeg
        # captures the same :disp 1:1. --headless uses Xvfb (offscreen) instead,
        # only useful when you don't need to see/drive the game.
        if self.a.headless:
            cmd = ["Xvfb", f":{disp}", "-screen", "0", f"{self.w}x{self.dh}x24",
                   "-nolisten", "tcp"]
            kind = "Xvfb headless"
        else:
            cmd = ["Xephyr", f":{disp}", "-screen", f"{self.w}x{self.dh}",
                   "-title", "iwd2-spellcap  (drive the game in THIS window)",
                   "-no-host-grab"]
            kind = "Xephyr (drive the game in this window)"
        env = dict(os.environ)
        env.setdefault("DISPLAY", ":0")       # Xephyr draws onto the host X/XWayland
        self.procs["display"] = subprocess.Popen(cmd, env=env,
                                                stdout=subprocess.DEVNULL,
                                                stderr=subprocess.DEVNULL)
        time.sleep(2.0)
        if self.procs["display"].poll() is not None:
            raise RuntimeError(f"{cmd[0]} failed to start (is it installed?)")
        log(f"{kind} :{disp} {self.w}x{self.dh}")

    def start_spice_client(self, disp):
        # CRITICAL host-safety: confine the GTK client to the offscreen Xvfb.
        # GTK3 prefers the WAYLAND backend whenever WAYLAND_DISPLAY is set, which
        # would put the client on the HOST display instead of :disp -- and a
        # fullscreen/kiosk client there hijacks the real desktop. Force the X11
        # backend and drop the Wayland handle so it can ONLY reach Xvfb :disp.
        env = dict(os.environ)
        env.pop("WAYLAND_DISPLAY", None)
        env["GDK_BACKEND"] = "x11"
        env["XDG_SESSION_TYPE"] = "x11"
        env["DISPLAY"] = f":{disp}"
        env["PULSE_SINK"] = "iwd2cap"
        uri = self.a.spice
        client = self.a.client
        if client == "auto":
            client = "remote-viewer" if shutil.which("remote-viewer") else "spicy"
        if not shutil.which(client):
            raise RuntimeError(f"SPICE client '{client}' not installed "
                               "(install virt-viewer or use spicy)")
        # --kiosk: fullscreen with NO toolbar/menu, so the guest fills :disp at
        # (0,0) and the crop aligns (the --full-screen toolbar pushed the guest
        # down and clipped its bottom). Safe here: confined to the nested display,
        # not the host. (On Xephyr the user still drives by focusing its window.)
        if client == "remote-viewer":
            # WINDOWED (no --kiosk): shows the guest 1:1 with a thin menubar at the
            # top instead of the kiosk toolbar, so the guest's bottom taskbar stays
            # visible. --auto-resize=never so it never resizes/freezes the guest.
            cmd = ["remote-viewer", "--auto-resize=never", uri]
        else:
            cmd = ["spicy", f"--uri={uri}"]
        self.procs["spice"] = subprocess.Popen(cmd, env=env,
                                              stdout=subprocess.DEVNULL,
                                              stderr=subprocess.DEVNULL)
        time.sleep(3.0)
        self._route_audio()
        log(f"SPICE client '{client}' -> {uri} on :{disp}")

    def _route_audio(self):
        """Belt-and-suspenders: move the client's playback stream to iwd2cap."""
        pid = self.procs["spice"].pid
        r = run(["pactl", "list", "sink-inputs"])
        cur_id = None
        for line in r.stdout.splitlines():
            line = line.strip()
            m = re.match(r"Sink Input #(\d+)", line)
            if m:
                cur_id = m.group(1)
            if "application.process.id" in line and f'"{pid}"' in line and cur_id:
                run(["pactl", "move-sink-input", cur_id, "iwd2cap"])
                log(f"routed client audio (sink-input {cur_id}) -> iwd2cap")
                return

    def start_ffmpeg(self, disp):
        codec, opts = VENC[self.a.encoder]
        seg_tmpl = str(self.ring / "seg_%Y%m%d_%H%M%S.mkv")
        cmd = [
            "ffmpeg", "-hide_banner", "-loglevel", "warning", "-y",
            "-f", "x11grab", "-framerate", str(self.a.fps),
            "-video_size", f"{self.cw}x{self.ch}", "-i", f":{disp}.0+{self.cx},{self.cy}",
            "-f", "pulse", "-i", "iwd2cap.monitor",
            "-c:v", codec, *opts, "-g", str(self.a.fps), "-pix_fmt", "yuv420p",
            # PCM in the scratch segments: it concatenates GAPLESSLY at cut time
            # (independently-encoded Opus segments leave priming gaps = audio
            # glitches every segment). Re-encoded to Opus only in the final clip.
            "-c:a", "pcm_s16le",
            "-f", "segment", "-segment_time", str(self.a.seg),
            "-reset_timestamps", "1", "-strftime", "1", seg_tmpl,
        ]
        self.procs["ffmpeg"] = subprocess.Popen(cmd)
        log(f"ffmpeg recording {self.cw}x{self.ch}+{self.cx},{self.cy} -> "
            f"{self.ring}/seg_*.mkv ({self.a.encoder}, {self.a.fps}fps, "
            f"{self.a.seg}s segments)")

    # -- worker threads --
    def ring_cleaner(self):
        while not self.stop.wait(self.a.seg):
            cutoff = time.time() - self.a.keep
            for f in self.ring.glob("seg_*.mkv"):
                try:
                    if f.stat().st_mtime < cutoff:
                        f.unlink()
                except OSError:
                    pass

    def udp_listener(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", self.a.port))
        sock.settimeout(0.5)
        log(f"UDP listener on 0.0.0.0:{self.a.port}")
        while not self.stop.is_set():
            try:
                data, _ = sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                m = json.loads(data.decode("utf-8", "replace"))
            except ValueError:
                log(f"bad datagram: {data!r}")
                continue
            if m.get("geom"):
                self._update_geom(m)
                continue
            exe = str(m.get("exe", "unknown"))
            resref = str(m.get("spell", "")).strip().upper()
            if not re.fullmatch(r"[A-Z0-9_]{1,8}", resref):
                log(f"bad cast datagram: {m}")
                continue
            now = time.time()
            with self.ev_lock:
                # The cast action is a per-tick state machine -> the marker fires
                # every casting tick (and on the orig hook, every projectile too):
                # one clip per cast, keep the FIRST, debounce the rest.
                last = self._last_cast.get(exe, 0.0)
                if now - last < self.a.debounce:
                    log(f"  debounced {resref} (+{now - last:.1f}s, same cast)")
                    continue
                self._last_cast[exe] = now
                self.events.append((now, exe, resref))
            log(f"CAST exe={exe} spell={resref} ({spell_display(resref)})")
        sock.close()

    def _update_geom(self, m):
        if self.a.full or self.a.crop:
            return                       # crop is fixed/disabled; ignore marker geom
        try:
            rect = self._clamp_rect(m["x"], m["y"], m["w"], m["h"])
        except (KeyError, ValueError, TypeError):
            return
        with self.crop_lock:
            changed = rect != self.crop
            self.crop = rect
            self.crop_from_geom = True
        if changed:
            log(f"game window geom (guest) {rect}; render crop "
                f"+{self.a.win_border},{self.a.top_offset} {self.a.render_w}x{self.a.render_h}")

    def cutter(self):
        while not self.stop.is_set():
            ev = None
            with self.ev_lock:
                if self.events:
                    # ready once the post-roll segment is finalised
                    t0 = self.events[0][0]
                    if time.time() >= t0 + self.a.post + self.a.seg + 1.0:
                        ev = self.events.pop(0)
            if ev is None:
                self.stop.wait(0.5)
                continue
            try:
                self.make_clip(*ev)
            except Exception as e:                       # noqa: BLE001
                log(f"clip failed: {e}")

    def make_clip(self, arrival, exe, resref):
        lo, hi = arrival - self.a.pre, arrival + self.a.post
        segs = []
        for f in sorted(self.ring.glob("seg_*.mkv")):
            s = seg_start_epoch(f)
            if s is None:
                continue
            if s < hi and (s + self.a.seg) > lo:   # overlap
                segs.append((s, f))
        if not segs:
            log(f"no ring segments cover cast @{time.strftime('%H:%M:%S', time.localtime(arrival))}")
            return
        segs.sort()
        first_start = segs[0][0]
        ss = max(0.0, lo - first_start)
        dur = self.a.pre + self.a.post
        listfile = self.ring / f".cut_{int(arrival)}.txt"
        listfile.write_text("".join(f"file '{f.resolve()}'\n" for _, f in segs))
        out = self.clips / f"{spell_filename(resref)}__{exe}.mkv"
        tmp = self.clips / f".{out.name}.tmp.mkv"
        codec, opts = VENC[self.a.encoder]
        with self.crop_lock:
            cr, from_geom = self.crop, self.crop_from_geom
        vf = []
        if cr:
            x, y, w, h = cr
            # Marker geom is in GUEST coords; the guest sits toolbar_h px below the
            # top of the nested display (SPICE client toolbar), so shift down to map
            # guest->display. (--crop is already in display coords: no shift.)
            if from_geom:
                # geom = game WINDOW in guest coords. The render is the game's FIXED
                # resolution at a constant offset inside the window (side border +
                # title bar + the client menubar above the guest). Position follows
                # geom (adapts if the window moves); size is fixed. All tunable.
                x += self.a.win_border
                y += self.a.top_offset
                w = self.a.render_w
                h = self.a.render_h
            cap_h = self.dh or self.h
            h = (min(h, cap_h - y)) & ~1
            w &= ~1
            vf.append(f"crop={w}:{h}:{x}:{y}")
        if self.a.font:
            label = f"{spell_display(resref)}   {'RE' if exe == 'ours' else 'original'}"
            label = re.sub(r"[^A-Za-z0-9 ]", " ", label).strip()
            # bottom-RIGHT, right-anchored (x = w-text_w-margin) so long spell
            # names grow leftward and never overflow the right edge. No background
            # box; a thin black outline keeps white text legible over the game.
            vf.append(
                f"drawtext=fontfile={self.a.font}:text='{label}':fontcolor=white:"
                f"fontsize=28:x=w-text_w-20:y=h-text_h-16:"
                f"borderw=2:bordercolor=black@0.8"
            )
        vfilter = ["-vf", ",".join(vf)] if vf else []
        afilter = ["-af", f"volume={self.a.gain}dB"] if self.a.gain else []
        cmd = [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "concat", "-safe", "0", "-i", str(listfile),
            "-ss", f"{ss:.3f}", "-t", f"{dur:.3f}",
            *vfilter, *afilter, "-c:v", codec, *opts, "-pix_fmt", "yuv420p",
            "-c:a", "libopus", "-b:a", self.a.abr, str(tmp),
        ]
        r = run(cmd)
        try:
            listfile.unlink()
        except OSError:
            pass
        if r.returncode != 0:
            log(f"ffmpeg cut error: {r.stderr.strip()[:400]}")
            tmp.unlink(missing_ok=True)
            return
        os.replace(tmp, out)
        log(f"clip -> {out.name}  ({dur:.0f}s, overwritten)")

    # -- lifecycle --
    def _clamp_rect(self, x, y, w, h):
        H = self.dh or self.h
        x = max(0, min(int(x), self.w - 2))
        y = max(0, min(int(y), H - 2))
        w = max(2, min(int(w), self.w - x) & ~1)
        h = max(2, min(int(h), H - y) & ~1)
        return (x, y, w, h)

    def _init_capture(self):
        # Capture the FULL nested display. It is taller than the guest by toolbar_h
        # because the SPICE client's top toolbar pushes the guest down -- the extra
        # room keeps the guest's bottom (Windows taskbar) on screen. Clips are
        # cropped to the game window afterwards. --crop fixes a direct :disp rect;
        # --full disables cropping.
        # Windowed client = [menubar][guest 1:1]. Make the display taller than the
        # guest so the menubar fits on top AND the guest bottom (taskbar) stays on
        # screen. auto-resize=never keeps the guest at its own res (no ratchet/freeze).
        self.dh = (self.h + 90) & ~1
        self.cx, self.cy = 0, 0
        self.cw, self.ch = self.w & ~1, self.dh
        if self.a.crop:
            parts = self.a.crop.replace("x", ",").split(",")
            if len(parts) != 4:
                raise RuntimeError("--crop must be X,Y,W,H")
            self.crop = self._clamp_rect(*parts)
            self.crop_from_geom = False
            log(f"clip crop fixed at {self.crop} (--crop, direct :disp coords)")
        elif self.a.full:
            log("clip crop disabled (--full): clips = whole display")
        else:
            log("clip crop = game window (awaiting geom from the VM marker)")

    def setup(self):
        # Xvfb / capture surface = the guest DESKTOP size, so guest pixels map
        # 1:1 into the offscreen X server and window offsets are exact.
        if self.a.res:
            self.w, self.h = (int(x) for x in self.a.res.lower().split("x"))
        else:
            res = probe_resolution(self.a.domain)
            if not res:
                raise RuntimeError("resolution probe failed; pass --res WxH")
            self.w, self.h = res
        log(f"guest desktop {self.w}x{self.h}")
        self._init_capture()
        self.load_null_sink()
        disp = pick_display()
        self.disp = disp
        self.start_display(disp)
        self.start_spice_client(disp)
        self.start_ffmpeg(disp)

    def run(self):
        self.setup()
        threads = [
            threading.Thread(target=self.ring_cleaner, daemon=True),
            threading.Thread(target=self.udp_listener, daemon=True),
            threading.Thread(target=self.cutter, daemon=True),
        ]
        for t in threads:
            t.start()
        log("READY. Cast spells in the VM; clips land in "
            f"{self.clips}/<spell>__<exe>.mkv. Ctrl-C to stop.")
        try:
            while not self.stop.is_set():
                # bail out if a core process dies (SPICE client or ffmpeg)
                for name in ("ffmpeg", "spice"):
                    p = self.procs.get(name)
                    if p and p.poll() is not None:
                        log(f"{name} exited (code {p.returncode}); shutting down")
                        self.stop.set()
                        break
                time.sleep(1.0)
        except KeyboardInterrupt:
            pass
        self.teardown()

    def teardown(self):
        self.stop.set()
        for name in ("ffmpeg", "spice", "display"):
            p = self.procs.get(name)
            if p and p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
        if self.loopback_module:
            run(["pactl", "unload-module", self.loopback_module])
        if self.sink_module:
            run(["pactl", "unload-module", self.sink_module])
        # Purge the scratch ring: the raw full-desktop segments are far bigger
        # than the cropped clips, so keeping them defeats the point of encoding
        # small clips. Clips are the keep. (--keep-ring to retain for debugging.)
        if not self.a.keep_ring:
            n = 0
            for f in list(self.ring.glob("seg_*.mkv")) + list(self.ring.glob(".cut_*.txt")):
                try:
                    f.unlink()
                    n += 1
                except OSError:
                    pass
            log(f"purged {n} scratch ring files")
        log(f"stopped (clips kept in {self.clips})")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--domain", default="win11", help="libvirt domain (res probe)")
    ap.add_argument("--crop", default="", help="manual clip crop rect X,Y,W,H (:disp coords)")
    ap.add_argument("--win-border", type=int, default=12,
                    help="X offset from window left to render left (px)")
    ap.add_argument("--top-offset", type=int, default=86,
                    help="Y offset from window top to render top (menubar+title, px)")
    ap.add_argument("--render-w", type=int, default=800, help="game render width")
    ap.add_argument("--render-h", type=int, default=600, help="game render height")
    ap.add_argument("--full", action="store_true",
                    help="capture the whole desktop (default: just the game window)")
    ap.add_argument("--spice", default="spice://127.0.0.1:5900")
    ap.add_argument("--client", default="auto",
                    choices=["auto", "remote-viewer", "spicy"])
    ap.add_argument("--headless", action="store_true",
                    help="use offscreen Xvfb instead of the visible Xephyr "
                         "(only if you don't need to watch/drive the game)")
    ap.add_argument("--res", default="",
                    help="display WxH (default: probe the guest's current res)")
    ap.add_argument("--port", type=int, default=48888, help="UDP cast-event port")
    ap.add_argument("--encoder", default="av1_nvenc", choices=list(VENC))
    ap.add_argument("--fps", type=int, default=30, help="capture fps (game runs 30)")
    ap.add_argument("--abr", default="64k", help="audio bitrate (Opus)")
    ap.add_argument("--gain", type=float, default=18.0,
                    help="audio gain dB on clips (game audio is quiet; 0=off)")
    ap.add_argument("--debounce", type=float, default=3.0,
                    help="seconds; collapse a cast's projectile burst into one clip")
    ap.add_argument("--font", default=find_font(),
                    help="ttf for the spell/RE-vs-original overlay ('' = no overlay)")
    ap.add_argument("--mute", action="store_true",
                    help="don't loop captured audio to your speakers (capture only)")
    ap.add_argument("--seg", type=int, default=5, help="segment seconds")
    ap.add_argument("--pre", type=float, default=1.0, help="seconds before cast")
    ap.add_argument("--post", type=float, default=15.0, help="seconds after cast")
    ap.add_argument("--keep", type=int, default=120, help="ring retention seconds")
    ap.add_argument("--keep-ring", action="store_true",
                    help="don't purge the scratch ring on exit (debugging)")
    ap.add_argument("--ring", default=str(HERE / "ring"))
    ap.add_argument("--clips", default=str(HERE / "clips"))
    args = ap.parse_args()

    rec = Recorder(args)
    signal.signal(signal.SIGTERM, lambda *_: rec.teardown() or sys.exit(0))
    rec.run()


if __name__ == "__main__":
    main()
