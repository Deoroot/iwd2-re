# Contributing to Icewind Dale 2 Reverse Engineering

Thank you for your interest! This project is reverse-engineered almost entirely
under AI guidance, and the workflow is **designed to be reproducible by people
who are not reverse-engineering experts.** You do not need to read x86 assembly,
operate Ghidra, write Frida scripts, or hand-write C++ — the tooling and the AI
agent do that. What you bring is **time**, an **AI subscription**, and the
discipline to keep every function **byte-for-byte faithful** to `IWD2.exe`
before moving on.

The golden rule never changes: **code must match `IWD2.exe`; missing is better
than wrong.**

## What you need (and what you don't)

**You do *not* need to:**

- read x86 assembly,
- operate Ghidra,
- write Frida trace scripts,
- be a C++ expert.

The AI agent reads the disassembly and the decompile, drives Ghidra and Frida
through the repo's tools, and writes the C++. A beginner's grasp of C++ helps you
steer and read diffs but is not required — the from-scratch tutorial
[`docs/recover-tutorial.md`](docs/recover-tutorial.md) assumes no Ghidra, barely
any assembly, and beginner C++.

**You *do* need:**

- **A top-tier reasoning model.** Opus / GPT-5.5-class, via subscription (the
  author runs Opus on two $20 Claude Pro subscriptions — a single $20 one is
  enough; two just goes faster). This is the one hard requirement; see *Use a top-tier model* below.
- **Patience.** The real bottleneck is waiting for message-limit resets, not the
  technique.
- **Discipline.** Faithful-or-nothing. One function at a time, proven against the
  binary before the next.

## Prerequisites

You install these once. You don't need to *understand* them — the AI drives
Ghidra, Frida, and the disassembler for you.

**To build, run, and test** (all a testing / bug-report contributor needs):

- **Visual Studio 2019** with **"Desktop development with C++"** and the **MFC**
  component — the build targets **Win32/x86** (the binary is 32-bit). The CMake
  generator is pinned to `Visual Studio 16 2019`.
- **Git**, plus a legitimate copy of Icewind Dale II
  ([GOG](https://www.gog.com/game/icewind_dale_2) or the original CD) for its
  assets — IWD2 was never sold on Steam.
- Step-by-step clone / cmake / run and the DirectX / VC-redist fixes are in
  **[BUILD_WINDOWS.md](BUILD_WINDOWS.md)**.

**To do recovery work** (the full loop):

- **An AI coding agent on a top-tier model** — e.g. Claude Code with a Claude Pro
  (Opus) subscription. It does the reading and writing; see *Use a top-tier model*
  below.
- **Python 3.10+** in a virtualenv with the `scripts/` dependencies (`pefile`,
  `capstone`); the repo's tools run through it.
- **[Ghidra](https://ghidra-sre.org/)** (free) for decompile / disassembly, plus
  the vendored **[`ghidra-ai-bridge`](vendor/ghidra-ai-bridge/)** for its headless
  export ([getting-started](vendor/ghidra-ai-bridge/docs/getting-started.md)).
  Fetch the prebuilt IWD2 Ghidra project with `scripts/ghidra_restore.sh` rather
  than analyzing the binary yourself.
- **[`re-agent`](vendor/auto-re-agent/)** (vendored —
  `pip install -e vendor/auto-re-agent`), the context assembler + parity oracle
  ([getting-started](vendor/auto-re-agent/docs/getting-started.md)).
- **Frida** (`pip install frida-tools`) for the runtime differential (original
  vs. our build).
- **[`gh`](https://cli.github.com/)** (GitHub CLI) and Git.
- *Optional:* the [code-review-graph](https://github.com/tirth8205/code-review-graph)
  MCP server for fast caller/callee navigation.

Only the **build, game, and Frida require Windows**; the RE tools (Ghidra, the
agents, the graph) run on any OS. Any layout works — the author edits on Linux
and builds/runs in a Windows VM over SSH, but doing everything on a single
Windows machine with no VM is just as valid. Tell the AI agent your setup and it
adapts the workflow. The author's specific host/VM split and tool routing are in
**[AGENTS.md](AGENTS.md)** / **[CLAUDE.md](CLAUDE.md)**.

## The workflow (and how to reproduce it)

This project is built on an **adapted and improved fork of
[re-agent](https://github.com/dryxio/auto-re-agent)** — the autonomous
reverse-engineering agent demoed
[reverse-engineering GTA San Andreas](https://www.youtube.com/watch?v=zBQJYMKmwAs).
The fork is vendored at [`vendor/auto-re-agent/`](vendor/auto-re-agent/).

**The key difference is philosophy.** re-agent was built to run autonomously
across a whole game. This project does the opposite on purpose. Instead of letting
AIs churn 24/7 to *approximately* reverse the entire engine (unaffordable, and it
buries subtle bugs that surface months later in a different system), the recovery
advances **one feature at a time**, each made **byte-for-byte faithful** to the
binary before moving to the next. It is slower to a fully playable game — but far
cheaper in AI-subscription cost, and far less likely to ship the kind of subtle
divergence that later costs a Frida-level hunt to track down.

**re-agent is used here as a tool, not as the author.** The C++ is written by a
steered AI coding agent; `re-agent reverse` is *not* used to auto-generate code
(it regresses clean recoveries). Instead re-agent provides:

- **Context assembly** —
  [`scripts/reagent_assemble_context.py`](scripts/reagent_assemble_context.py)
  bundles, for one function, the resolved decompile, the binary's required call
  set, the BG2 PDB layout, and IDS constants. The agent reads this bundle, not you.
- **A faithfulness oracle** — `re-agent parity` reports GREEN / YELLOW / RED,
  extended from re-agent's 11 signals to **14 signals plus an objective verifier**
  and a set of custom linters.

## The recover loop, step by step

What *you* do vs. what the tools/AI do:

1. **Pick the next function or feature.** Small and incremental
   (`grep -r "TODO: Incomplete" src/`, an unnamed `sub_`/`FUN_`, or a visible
   gameplay gap).
2. **Assemble context** —
   `python scripts/reagent_assemble_context.py --address 0xADDR`. *(Tool.)*
3. **Let the AI write faithful C++** from the bundle, following the ground-truth
   hierarchy: **PE bytes / disassembly → Frida runtime trace → Ghidra decompile**
   (the decompile is a fallible lift, used to navigate, never trusted blindly).
   *(AI.)*
4. **Verify faithfulness** — `re-agent parity --address 0xADDR` (aim GREEN/YELLOW),
   plus the custom linters that catch what parity is blind to:
   `scripts/parity_offsets.py` (right callee, wrong member),
   `scripts/struct_layout_audit.py` (pack(2) drift),
   `scripts/ctor_vtable_check.py` (class conflation),
   `scripts/vtable_audit.py` (missing overrides),
   `scripts/lint_twin_symmetry.py` (antonym-pair operand swaps). *(Tools — you read
   a PASS/FAIL, not assembly.)*
5. **Build Win32** (VS2019 / MFC). Commit only if it compiles.
6. **Prove it at runtime.** Static parity alone is not proof — it never runs our
   exe. Run `scripts/vm.sh smoke` on our build with the crash oracle armed; for a
   *behavioral* claim, capture a Frida differential (original vs. ours, same hooks)
   and diff the fields/args (see
   [`docs/frida-differential-tracing.md`](docs/frida-differential-tracing.md)).
   For *visual/audio* work like spell effects,
   [`scripts/spell_capture/`](scripts/spell_capture/) auto-records a clip of every
   cast on both builds and plays them side-by-side
   (`spellcap.sh compare Fireball`) — you compare the two videos frame-by-frame,
   no assembly required.
7. **Only then, move to the next function.**

A full narrated walk-through of one real recovery is in
[`docs/recover-tutorial.md`](docs/recover-tutorial.md); the validation details are
in [`docs/recovery-validation-process.md`](docs/recovery-validation-process.md).

## Ways to contribute

You judge **behavior** (does our build act like `IWD2.exe`?), not raw assembly —
the AI handles the machine code.

### Reverse engineering (most needed)

Recover an unimplemented function, or name an anonymous one:

```cpp
// Before
int CGameAIBase::sub_45B6D0() {
    return nfield_58C;
}

// After
// 0x45B6D0
int CGameAIBase::GetAITarget() {
    return m_nAITargetId;   // at offset 0x58C
}
```

### Implement stubs

Find `// TODO: Incomplete` markers and recover the body:

```cpp
// Before
void CAIGroup::GroupAction(CAIAction action, BOOL override, CAIAction* leaderAction) {
    // TODO: Incomplete.
}
```

Assemble the context bundle, let the AI translate the binary's behavior, then
verify with parity + a runtime check. Match the surrounding style.

### Document fields

Name `field_` members with meaningful Hungarian notation:

```cpp
// Before
/* 058C */ int nfield_58C;

// After
/* 058C */ int m_nAITargetId;   // m_ = member, n = int
```

### Testing

Build, run against your own game assets, and report what works and what breaks in
GitHub Issues. This needs **no RE knowledge at all** and is genuinely useful.

## Finding something to do

```bash
grep -r "TODO: Incomplete" src/     # unimplemented functions
grep -r "sub_" src/ | head -50      # unnamed functions
grep -r "field_" src/ | head -50    # unnamed fields
```

Or regenerate the honest progress table:
`.venv-reagent/bin/python scripts/project_status.py`.

## Getting oriented in the code

Start with these, in rough order of complexity:

1. **CGameObject** — base for all world objects
2. **CGameAIBase** — AI, actions, scripts
3. **CGameSprite** — characters (huge but central)
4. **CInfGame** — game-state singleton
5. **CInfinity** — world renderer

The `sub_` name encodes the address: `sub_45B6D0` → `0x0045B6D0`. Use
`python scripts/src_find.py NAME` to jump to any recovered symbol, and the
knowledge-graph MCP tools (see [`README.md`](README.md)) to trace callers/callees.

## Coding standards

- **Indentation:** 4 spaces, no tabs.
- **Brackets:** Allman style.
  ```cpp
  if (condition) {
      // code
  }
  ```
- **Naming:** PascalCase for types/functions, camelCase for locals, `m_` prefix
  for members.
- **Address comment:** every recovered function carries its binary address.
  ```cpp
  // 0x44C8B0
  CGameAIBase::CGameAIBase() {
      // ...
  }
  ```

### Hungarian notation (MFC-style)

`m_` member · `n` int/short · `b` boolean/BYTE · `p` pointer · `dw` DWORD ·
`w` WORD · `sz` null-terminated string · `str` CString · `l` LONG · `f` float ·
`h` handle.

```cpp
class CGameSprite {
    int      m_nHitPoints;      // HP
    BOOL     m_bInCombat;       // TRUE if fighting
    CResRef* m_pResRef;         // Animation resource
};
```

## Use a top-tier model (faithful-or-nothing)

You don't need reverse-engineering expertise — **but the AI does the expert work,
so the AI has to be an expert.** That means a top-tier reasoning model. This is
the one hard requirement, and it is not negotiable.

Reverse engineering is unforgiving: the produced C++ must match the original
binary's behavior **exactly**. A single wrong sign, flipped condition, or
off-by-one constant produces:

1. **Silent divergence** — the game runs but behaves slightly wrong (wrong
   formation angle, broken pathfinding, incorrect damage).
2. **Delayed discovery** — the bug surfaces hours or days later, in a different
   system.
3. **Brutal debugging** — tracking down "why does this formation rotate 60°
   wrong?" took Frida-level ground-truth tracing; the culprit was a single sign
   error (`180.0` vs `-360.0`).

Weaker models (GPT-4o, Haiku, local models) hallucinate C++ that **looks
convincing** but hides subtle logic errors — orders of magnitude harder to find
than doing the RE correctly the first time. If you only have access to a weaker
model, limit yourself to **testing and bug reports**, or to renames/stubs you can
verify yourself; do not attempt large-scale recovery.

## Submitting pull requests

1. Fork the repo and create a focused feature branch.
2. Keep changes small — one class or subsystem at a time.
3. Ensure it compiles (Win32) before committing.
4. Write a clear commit message:
   ```
   rename: CGameSprite movement functions

   Named sub_453160, sub_453170, sub_4531B0:
   - SetBHiding / GetBHiding — hiding state control
   - SetField562C — equipment field setter
   ```
5. Open a PR describing what you recovered and how you verified it.

## Getting help

- **GitHub Discussions** — questions about the code
- **GitHub Issues** — bug reports and feature requests
- **Infinity Engine Discords / Gibberlings3 Forums** — general IE modding and
  technical discussion

## License

By contributing, you agree that your code will be under the
[Sustainable Use License](LICENSE.md).

## Acknowledgments

Contributors will be credited in the project history and future release notes.

---

*Happy reverse engineering! The cold winds of Icewind Dale await.*
