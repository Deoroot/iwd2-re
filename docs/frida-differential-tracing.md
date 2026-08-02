# Frida differential tracing — ground truth from the original `IWD2.exe`

When static review (Ghidra + GhidraMCP) is exhausted and our build still diverges
from the binary — or the decompiler's output is ambiguous/misleading — hook the
**original** `IWD2.exe` at runtime, capture ground-truth args/outputs under an
in-game action, and diff them against our build's `Iwd2DebugLog` output under the
**same** action. This finds divergences the static read misses.

Template / working example: [`scripts/frida_formation_trace.py`](../scripts/frida_formation_trace.py).
Copy it per investigation.

## Prerequisites

- `pip install frida-tools` (have 17.x), Python 3.
- Original game at `C:\GOG Games\Icewind Dale 2\IWD2.exe`.

## Why hooking is trivial here

- `IWD2.exe` has **no ASLR** (DllCharacteristics `0x0`, ImageBase `0x400000`) →
  Ghidra addresses are **absolute**. `ptr(0xADDR)` points straight at the
  function/data; no rebasing.
- x86 (32-bit). Calling conventions:
  - `__thiscall` (most class methods): `this = this.context.ecx`; stack args are
    `args[0]`, `args[1]`, … (= `[esp+4]`, `[esp+8]`, …).
  - `__cdecl` / free functions: `args[0..]` are the stack args directly.
- Read FP/data **constants from the PE with `pefile`**, not a memory-read endpoint
  (same as the GhidraMCP note in `CLAUDE.md`).

## Workflow

1. **Instrument our build:** add `Iwd2DebugLog("TAG ...", ...)` at the suspect
   function (args, intermediates, outputs). Build, deploy.
2. **Capture OURS:** run `iwd2-re.exe`, perform the in-game action, read
   `iwd2-re-debug.log`.
3. **Capture GROUND TRUTH:** `python scripts/<trace>.py` spawns `IWD2.exe` under
   Frida hooking the **same** function(s); perform the **same** action; read the
   JSON log (`tmp_*.log`).
4. **Diff.** Localize the first divergence.
5. **If inputs match but outputs differ → hook the exact op/constant in the binary.**
   Do not theorize. See the case study.
6. **Revert all temp instrumentation before commit** — it is non-faithful and
   frame-layout sensitive (the corruption Heisenbug; see
   [`non-faithful-passages.md`](non-faithful-passages.md) / the pathfinding arc).

## Reusable hook patterns

### Function-entry hook, `__thiscall`, log args
```js
const Fn = ptr(0x4063e0);                 // no ASLR -> absolute
function s16(v){ return (v.toInt32() << 16) >> 16; }
Interceptor.attach(Fn, {
  onEnter(args) {
    const thiz = this.context.ecx;        // __thiscall `this`
    send({ tag:'Fn', this: thiz.toString(),
           a0: args[0].toInt32(), a1: s16(args[1]) });
  }
});
```

### Resolve an object id → live pointer → fields (returnAddress-filtered `GetDeny`)
Object-array ids are `(objectId | arrayIndex<<16)`, **not** pointers. To read a
live object's fields, hook `CGameObjectArray::GetDeny` (`0x599C70`) and filter by
the caller's address range, so you only see the objects the function under study
touches; then read fields at header offsets.
```js
const GetDeny = ptr(0x599c70);
const LO = ptr(0x4063e0), HI = ptr(0x407280);   // [start,end) of the fn you study
const M_POS = 0x06;                               // CGameObject::m_pos (x@+0x06, y@+0x0a)
// GetDeny(LONG id, BYTE thread, CGameObject** out, DWORD timeout) __thiscall; SUCCESS==0
Interceptor.attach(GetDeny, {
  onEnter(args) {
    const ra = this.returnAddress;
    this.in = ra.compare(LO) >= 0 && ra.compare(HI) < 0;
    if (this.in) { this.id = args[0].toInt32() >>> 0; this.out = args[2]; }
  },
  onLeave(rv) {
    if (!this.in || (rv.toInt32() & 0xff) !== 0) return;  // only SUCCESS
    const p = this.out.readPointer();
    if (p.isNull()) return;
    send({ tag:'POS', id:this.id,
           pos:[p.add(M_POS).readS32(), p.add(M_POS+4).readS32()] });
  }
});
```

### Read a virtual method's result without hooking mid-function
A `CALL [vtable+off]` site **cannot** be hooked directly (see hazards). Hook the
helper that hands you the pointer (`GetDeny` above), then read the field the
virtual would return — e.g. `GetPos` = `vtable+0x1c` returns `&m_pos`, so just read
`+0x06` off the resolved pointer.

### Walk an embedded `CPtrList` from `this`
`CAIGroup` member list = embedded `CPtrList` @ `+0x06` → `m_pNodeHead` @ **`+0x0a`**;
node `{ next@0, prev@4, data@8 }`, `data` = object id.
```js
let node = thiz.add(0x0a).readPointer();
for (let g = 0; !node.isNull() && g < 64; g++, node = node.readPointer())
  send({ tag:'member', id: node.add(8).readU32() });
```

## Hazards (learned the hard way)

- **Never `Interceptor.attach` a jump target or mid-function instruction.** Frida's
  x86 inline trampoline overwrites ~5 bytes and corrupts a nearby branch cluster →
  the game crashes on the traced action (hooking `0x40686f` and `0x40685d` both
  crashed `IWD2.exe`). Hook **function entries** only; get everything else by
  walking `this`/args in `onEnter` or via returnAddress-filtered helper hooks.
- **Don't trust the decompiler's `__thiscall` arg recovery.** GhidraMCP rendered
  `FUN_00405370`'s args inconsistently (a call showed `param_3=0` while the callee
  received `param_3=240`). When decompiler and runtime disagree, **runtime wins** —
  confirm with the disassembly and the actual constants.
- **`send()` is async** — log line order is approximate. Correlate by tag/ids, not
  by line position.
- `send()` payloads must be JSON-serializable; wrap pointer reads in `try/catch`
  (bad ids throw).

## Case study — §C formation 60° mis-orientation (commit `82035cf5`)

- Symptom: party formation rotated wrong on a leader→click move; static review of
  the whole formation pipeline found nothing.
- Rig hooked `GroupSetTarget` (`0x4063e0`) + `RotateOffsets` (`0x4058e0`), plus a
  `GetDeny` hook to dump each member's `m_pos`.
- Finding: all 6 member positions were **byte-identical** to ours and the target
  matched — but the binary's `RotateOffsets` received **240** where ours computed
  **−60**. (A false lead — member[0]'s object id differed, idx1 vs idx109 — was
  killed by the position match: same positions, so the leader was not the bug.)
- Drilled into the only thing between matching inputs and divergent output: the
  radian→degree conversion. Binary uses `baseAngle * -360.0 / 2π`
  (`FMUL [0x847b88]`=−360.0, `FDIV [0x847b60]`=2π); our code used `* 180.0 / PI`
  (positive) → wrong sign → 300° instead of 240°.
- Confirmed `-360.0` by reading `0x847b88` with `pefile`; fixed the sign in all
  three callers.
- **Lesson embodied:** identical inputs + divergent output ⇒ hook the exact
  constant/op, don't theorize.

## Running the template

```bash
python scripts/frida_formation_trace.py           # spawn IWD2.exe, hook, log JSON to tmp_*.log
python scripts/frida_formation_trace.py --attach   # attach to a running IWD2.exe
```

Perform the in-game action, then Ctrl-C / kill. Logs are `tmp_*` (untracked RE
noise — delete freely).

## Unattended on the original — `vm.sh trace`

The template above assumes a human at the keyboard: spawn, click, Ctrl-C, read.
`scripts/vm.sh trace` is the unattended form — `vm.sh smoke`'s counterpart for a
binary we have no source for.

```bash
scripts/vm.sh trace --hooks scripts/probes/store_ownership.json --load-slot -1 \
                    --hit CGameObjectArray::Add --timeout 120
```

It spawns `IWD2.exe` in session 1 under Frida, installs a hook table, waits, and
prints a verdict with an exit code: `CLEAN` 0, `CRASH` 1, `NOT-LOADED` /
`NOT-EXERCISED` / `NO-VERDICT` 2. The last three all mean *ran, proved nothing* —
the same discipline as smoke's `--hit`, so a green run cannot be a false green.

- **Hook table**, not a bespoke script: the schema lives in `scripts/frida_hooks.py`
  and is shared with `frida_probe.py`. Beyond args and `this_dump` it takes
  `globals` (a deref chain — `g_pChitin(0x8CF6D8)->cNetwork.m_idLocalPlayer(+0x104C)`
  is `{"chain": ["0x8CF6D8", "0x104C"], "type": "u32"}`) and `bt`.
- **Every hook self-verifies**: the driver dumps 8 bytes at each site on install,
  so a silent no-fire is diffed against `sym.py bytes ADDR 8` rather than guessed at.
- **Crash handling** is the crash guard's: `Process.setExceptionHandler` + EBP
  walk, `system` exceptions passed through. The original has no PDB, so frames come
  back raw and `vm.sh` symbolizes them host-side through `sym.py addr2fn`.
- **It spawns rather than attaches**, because the questions worth asking are
  usually about startup and load order, and a game already at the main menu has
  missed all of it.
- **Every click is a real click.** The cursor is moved onto the control and the
  left button pressed, then the engine's own poll (`CChitin::AsynchronousUpdate:1537`)
  dispatches `OnLButtonDown/Up`. Calling the handlers directly does not work:
  `OnLoadGameButtonClick` opens by taking the UI manager's critical section
  (`CScreenConnection.cpp:1142-1143`) and never returns from it — from `onEnter`, from
  `onLeave`, and from the engine's own mid-body call site alike. The engine polls
  `m_ptPointer`, which comes from the real cursor and reads `(-1,-1)` when the pointer
  is off the window, so unattended it never dispatches anything on its own.
  `SetCursorPos` speaks desktop pixels while the engine works in 800×600 game space,
  so the driver measures the linear map with two probe points instead of assuming it —
  aiming blind put `(645,295)` at game `(402,205)` and hit no control.
- **Getting into a save takes four steps, none of them guessable statically:**
  - The intro movies are a hard block, not decoration. The connection screen runs
    one update, then `PlayMovieInternal` does `SelectEngine(projector)` and the menu
    stops (1 update in 32714 engine ticks). The driver no-ops that one chokepoint.
  - Skipping them then leaves `byte_8F376C` set — the flag is raised when the
    movies are *queued* (`CScreenConnection.cpp:688`), not when they finish — so the
    engine skips its own `DismissPopup` (`:704`) and popup 19 ("Finding the network
    devices...") stays on the stack forever. The driver calls `DismissPopup` itself.
  - Input only goes live when `AutoSelectServiceProvider` runs, which is the line that
    sets `m_bAllowInput = TRUE` (`:741-743`). Clicking before that freezes the game.
  - Loading through the connection screen is the multiplayer HOST path — it ends in
    `SetArbitrationLockStatus` (`:1285-1287`) — so the game parks on Character
    Arbitration until Done (GUIMP panel 0 / control 28) is pressed.
- **Our side of a differential stays `Iwd2DebugLog`.** This tool is original-only
  on purpose: our build is a different image, and the debug build's +0xA base shift
  breaks Frida member reads (`53fe6dccc766`).

First result off it: the store-ownership question (memlite `5c1c8585edfa`) had been
stuck on a hypothesis that `m_idLocalPlayer` was still 0 during load in the original.
It is not — it reads 1 at all 177 `CGameObjectArray::Add` calls, exactly as ours does.
The real divergence is one level down: the original's `ChangeControlOnLoadGame` claims
each object with `ChangeControl(old=(0,id), new=(1,id), bLocalControl=0)`, which returns
1 and makes **zero** calls to `CGameRemoteObjectArray::Add`. Ours routes that same claim
through `Add`, which refuses because the entry already exists. Two days of static
argument, one unattended run.
