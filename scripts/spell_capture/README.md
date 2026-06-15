# spell_capture — auto video+audio clips per spell cast

Records the IWD2 VM (display + sound, delivered to the host over SPICE) and cuts
a short clip every time a spell is cast, so the **original `IWD2.exe`** and **our
`iwd2-re.exe`** can be compared side-by-side instead of replayed live.

All encoding runs on the **host GPU** (RTX 4070, NVENC AV1 + Opus by default; HEVC
or libx265 selectable); the VM only serves its normal SPICE stream. Clips are
small `.mkv` and **overwrite by
(spell, exe)** — one file per spell per build, recasting replaces it.

## Pieces

| File | Runs on | Role |
|------|---------|------|
| `recorder.py` | host | null-sink → Xephyr (visible nested X) → SPICE client → continuous segmented ffmpeg → ring buffer + UDP cast listener + cutter |
| `cast_marker.py` | VM | detects casts and UDP-forwards `{exe,projType,ts}` to the host. `--mode frida` (original, hook `0x51EAF0`) / `--mode tail` (ours, tail debug log) |
| `spellcap.sh` | host | facade: `rec` / `mark-orig` / `mark-ours` / `compare` / `stop` |

`projType` → `data/near_infinity_export/SRC/MISSILE.SRC` (1-based line) →
canonical spell name used in the filename.

## Dependencies

Host: `ffmpeg` (NVENC), `Xephyr` (`xorg-server-xephyr`; `Xvfb` for `--headless`),
a SPICE client (`virt-viewer` for `remote-viewer`, else `spicy`), `pactl`, `virsh`.
VM: Python + `frida` (already used by the other `frida_*` scripts).

**Single SPICE client.** This VM's SPICE effectively allows one viewer at a time,
so a second client kicks the first. The recorder therefore runs its SPICE client
in a **Xephyr** window that *you* watch and drive (cast spells in it) — it replaces
virt-manager. Do **not** also open virt-manager/another viewer while recording, or
it will disconnect the recorder. (`--headless` uses offscreen Xvfb instead, only
useful when you don't need to see/drive the game.)

## Workflow

```bash
# 1. start the recorder -> an "iwd2-spellcap" Xephyr window opens. THIS is your
#    view: watch and drive the game in it. (Ctrl-C to stop; keeps clips.)
scripts/spell_capture/spellcap.sh rec
#    options: --encoder hevc_nvenc|libx265  (default av1_nvenc)  --post 15 --pre 1
#             --fps 20  --abr 64k  (light defaults)
#             --crop X,Y,W,H (manual rect)  --full (whole desktop)  --headless

# 2. launch the game (renders now that the recorder's SPICE client is connected)
scripts/vm.sh run 3          # our build, slot 3   (or launch the original yourself)
#    it appears in the Xephyr window

# 3. start the matching marker in the VM
scripts/spell_capture/spellcap.sh mark-ours     # our iwd2-re.exe (tails debug log)
#   or
scripts/spell_capture/spellcap.sh mark-orig     # original IWD2.exe (Frida)

# 4. drive the game IN THE XEPHYR WINDOW and cast. Clips appear in
#    scripts/spell_capture/clips/<Spell>__<exe>.mkv

# 5. (optional) side-by-side, both audio tracks (0=orig, 1=ours)
scripts/spell_capture/spellcap.sh compare Fireball

# 6. teardown
scripts/spell_capture/spellcap.sh stop
```

Our build only emits the `CAST type=N` log line when
`.\iwd2-re-debug.enabled` exists in the game CWD
(`C:\GOG Games\Icewind Dale 2`); `mark-ours` reminds you.

## Notes / tuning

- Trigger point = `CProjectile::DecodeProjectile` (the projectile/VFX factory), so
  every *visual* spell clips; pure no-VFX spells don't (nothing to compare).
- The host stamps UDP arrival on the same wall clock as the ring segments; the 1 s
  pre-roll absorbs trigger latency — no clock sync needed.
- Ring keeps `--keep` seconds (default 120) of 5 s segments under `ring/`; only the
  cut clips persist. `ring/` can be deleted any time the recorder is stopped.
- Window-only capture: IWD2 runs in an 800x600 window, so the recorder queries
  the game window's client rect on the VM (over SSH) and `x11grab`s **only that
  region** (no desktop/borders). Xvfb still mirrors the full guest desktop so the
  offset maps 1:1. Start the game before `rec` so the window exists. Override with
  `--crop X,Y,W,H` (e.g. the window moved) or `--full` for the whole desktop.
