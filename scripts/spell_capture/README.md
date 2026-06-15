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
| `cast_marker.py` | VM | detects player casts and UDP-forwards `{exe,spell,ts}` (spell = resref). `--mode frida` (original: hook `ApplyCastingEffect` `0x755A70`, return-address filtered to the player cast sites) / `--mode tail` (ours: tail the debug log) |
| `spellcap.sh` | host | facade: `rec` / `mark-orig` / `mark-ours` / `compare` / `stop` |

The spell **resref** (e.g. `SPWI304`) keys each clip; the recorder resolves it to
the TLK display name (`Fireball`) via `reagent_asset_names.py` for the filename and
overlay, falling back to the resref for spells absent from the export.

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

## Quick start

```bash
# 1. the game must already be running in the VM (the recorder probes its
#    resolution). Start the recorder -> an "iwd2-spellcap" Xephyr window opens.
#    THIS is your view: watch and cast in it. (Ctrl-C stops; clips are kept.)
scripts/spell_capture/spellcap.sh rec
#    options: --encoder hevc_nvenc|libx265 (default av1_nvenc) --post 15 --pre 1
#             --crop X,Y,W,H (manual rect)  --full (whole desktop)  --headless

# 2. start the marker that matches the running build
scripts/spell_capture/spellcap.sh mark-orig     # original IWD2.exe (Frida)
#   or
scripts/spell_capture/spellcap.sh mark-ours     # our iwd2-re.exe (tails debug log)

# 3. cast IN THE XEPHYR WINDOW -> clips/<Spell>__<exe>.mkv
# 4. compare / stop
scripts/spell_capture/spellcap.sh compare Fireball   # side-by-side, both audio tracks
scripts/spell_capture/spellcap.sh stop
```

## Guide: a full orig-vs-ours session

The recorder is **shared** — it tags every clip by build (`exe`) from the
datagram, so you record **both** builds against **one** running recorder. Only the
marker and the game swap; **do not Ctrl-C the recorder between builds**.

**A. Original first** (original `IWD2.exe` already running in the VM)

```bash
scripts/spell_capture/spellcap.sh rec          # recorder — leave it running
scripts/spell_capture/spellcap.sh mark-orig    # Frida attach to IWD2.exe
# cast your spells in the Xephyr window -> clips/<Spell>__orig.mkv
```

**B. Switch to our build** (the host recorder stays up)

```bash
# stop ONLY the VM marker (keeps the host recorder alive)
ssh win11vm 'powershell -NoProfile -Command "Stop-Process -Name python -Force"'

scripts/vm.sh kill orig        # close the original (explicit; never auto-killed)
scripts/vm.sh run              # launch our iwd2-re.exe in session 1

# enable our debug log: an "iwd2-re-debug.enabled" file (NO leading dot!) in the
# game CWD. (A leading-dot ".iwd2-re-debug.enabled" silently leaves logging OFF.)
ssh win11vm 'powershell -NoProfile -Command "New-Item -Force -ItemType File \"C:\GOG Games\Icewind Dale 2\iwd2-re-debug.enabled\""'

scripts/spell_capture/spellcap.sh mark-ours    # tails the debug log
# cast the SAME spells -> clips/<Spell>__ours.mkv
```

**C. Compare**

```bash
scripts/spell_capture/spellcap.sh compare Fireball   # -> clips/Fireball__cmp.mkv
scripts/spell_capture/spellcap.sh stop               # recorder + VM marker
```

Both builds trigger at the same cast-start point, so the two clips line up for a
frame-by-frame diff. `compare <Name>` takes the clip's **display name** (e.g.
`Fireball`, `Color_Spray`), not the resref.

Our build only emits the `CAST spell=<resref>` log line while the
`iwd2-re-debug.enabled` file (no leading dot) exists in the game CWD
(`C:\GOG Games\Icewind Dale 2`); `mark-ours` reminds you.

## Notes / tuning

- Trigger = the player's cast action at the visual cast-start, keyed by resref:
  our build logs `CAST spell=<resref>` once in `CGameSprite::Spell` /
  `SpellPointSequence`; the original is hooked at `ApplyCastingEffect` filtered to
  those two call sites. Enemies cast via `ForceSpell` (a different path), so combat
  enemy spells **don't** clip, and non-projectile spells (cones like Color Spray)
  clip too — both were broken by the old per-projectile factory trigger.
- The host stamps UDP arrival on the same wall clock as the ring segments; the 1 s
  pre-roll absorbs trigger latency — no clock sync needed.
- Ring keeps `--keep` seconds (default 120) of 5 s segments under `ring/`; only the
  cut clips persist. `ring/` can be deleted any time the recorder is stopped.
- Window-only capture: IWD2 runs in an 800x600 window, so the recorder queries
  the game window's client rect on the VM (over SSH) and `x11grab`s **only that
  region** (no desktop/borders). Xvfb still mirrors the full guest desktop so the
  offset maps 1:1. Start the game before `rec` so the window exists. Override with
  `--crop X,Y,W,H` (e.g. the window moved) or `--full` for the whole desktop.
