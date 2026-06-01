# Recovery and Validation Process

This is the general workflow for source recovery work in this repo. It is meant
to keep fixes faithful to `IWD2.exe` and to make later debugging reproducible.

## Ground Rules

- Treat Ghidra and runtime traces from the original game as truth.
- Do not invent source behavior to make a test pass. If a function cannot be
  fully recovered yet, leave the missing part explicit with `TODO INCOMPLETE`.
- For `src/` and `refs/gemrb/`, use the code-review graph tools before text
  search or file reads. Start with `semantic_search_nodes`, then inspect callers,
  callees, or file summaries.
- Hook function entries only with Frida. Do not hook mid-function branch targets.
- Run the same player action in the original game and in the RE build before
  calling a runtime difference fixed.

## Static Recovery

Use GhidraMCP for the original binary and keep addresses tied to Ghidra, not to
comments in source files.

```powershell
curl -s "http://127.0.0.1:8089/analyze_function_complete?name=0x005AC430"
curl -s "http://127.0.0.1:8089/get_xrefs_to?address=0x0084c44c&limit=20"
```

Read raw PE bytes with `pefile`, not with a memory endpoint:

```powershell
@'
import pefile
pe = pefile.PE(r"C:\GOG Games\Icewind Dale 2\IWD2.exe", fast_load=True)
ib = pe.OPTIONAL_HEADER.ImageBase
print(pe.get_data(0x8ABCA4 - ib, 16))
'@ | python -
```

When a layout is suspected, check both the original offsets in Ghidra and the
generated RE binary:

```powershell
python scripts\vtable_audit.py CGameSprite
llvm-pdbutil --version
llvm-objdump -d --start-address=0x00606de0 --stop-address=0x00606e20 build\Debug\iwd2-re.exe
```

If PDB member dumps are needed, generate a temporary dump and remove it before
commit:

```powershell
llvm-pdbutil dump -types build\Debug\iwd2-re.pdb > tmp_iwd2_re_pdb_types.txt
```

## Build and Deploy

Build Win32 Debug and copy the executable into the game directory before runtime
validation:

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32
cmake --build build --config Debug
Copy-Item build\Debug\iwd2-re.exe "C:\GOG Games\Icewind Dale 2\iwd2-re.exe" -Force
```

Before automation runs, clear stale logs when the investigation depends on them:

```powershell
Remove-Item "C:\GOG Games\Icewind Dale 2\iwd2-re-crash.log","C:\GOG Games\Icewind Dale 2\iwd2-re-debug.log" -ErrorAction SilentlyContinue
```

## Runtime Automation

Use New Game for intro/dialog/chapter validation. Loading an existing save can
skip the very state being tested.

```powershell
python scripts\auto_start_game.py --new-game --timeout 60
python scripts\auto_start_game.py --slot 0 --timeout 60
python scripts\auto_start_game.py --save-name "000000000-Autosave - Prologue" --timeout 60
python scripts\frida_intro_trace.py --mode original --timeout 60 --auto-dialog
python scripts\frida_intro_trace.py --mode re --timeout 60 --auto-dialog
```

Use `--save-name` when a specific save directory must be validated. Visible
slot numbers depend on load-screen scroll position and can point at another
save when the directory contains many saves.

`scripts\frida_intro_trace.py` should keep the game window focused, skip original
intro `.mve` videos, wait briefly on the chapter screen, click Done, and advance
dialogue Continue/reply buttons when `--auto-dialog` is set.

For differential checks, compare at least:

- chapter screen visibility and Done handling;
- dialogue start, replies, Continue buttons, and dialogue exit;
- `DisplayText*` log entries such as pause, unpause, autosave, chapter text, and
  journal update messages;
- journal update calls;
- party gold message dispatch and `CScreenWorld::UpdatePartyGoldStatus`;
- `SaveGame` return value and the autosave directory contents;
- reload of the generated autosave with `python scripts\auto_start_game.py`;
- relevant audio events such as narration, speech, music, and ambience when the
  current script traces them.

## Crash Capture

If the game opens an error dialog instead of exiting silently, capture the screen
before killing the process:

```powershell
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $bounds.Width,$bounds.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($bounds.Location,[System.Drawing.Point]::Empty,$bounds.Size)
$bmp.Save("C:\iwd2-re\tmp_error.png",[System.Drawing.Imaging.ImageFormat]::Png)
```

Temporary logs, screenshots, PDB dumps, and trace files should stay untracked
unless they are intentionally promoted into documentation.
