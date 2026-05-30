# Vtable Audit (`scripts/vtable_audit.py`)

Catches missing/wrong virtual overrides that source-vs-Ghidra reading can't see.

**The bug class:** a virtual method the binary OVERRIDES but our source never
declared (a hole in the `/* 00NN */` vtable slots), so calls silently dispatch
to the base. Invisible at every call site; only visible at the vtable.
This is what hid `CGameSprite::SetCurrAction` (slot 0x90, 0x7338E0) for days.

**How:** pefile only (IWD2.exe has NO RTTI — vtable[-1] = 0xffffffff, so RTTI
auto-discovery is out). Parses each class's `/* 00NN */` slots + `// 0xADDR`
method addrs from src/, locates the real vtable in .rdata by anchoring on those
addrs, diffs each class vtable vs its base slot-by-slot. Flags MISSING /
WRONGADDR / SPURIOUS. Overload-aware (addr sets per method) and capped at the
highest declared slot.

```
python scripts/vtable_audit.py CGameSprite   # one class + its base
python scripts/vtable_audit.py --quiet       # all classes, only those with findings
```

**Triage:** surfaces CANDIDATES; verify each against Ghidra. Known noise: shared
stub addrs filling several slots (e.g. 0x71e750 SetChatEditBoxStatus across
CScreen* classes) and weak base-vtable matches can yield false positives.
High-confidence findings = MISSING whose addr symbolizes to the same class
(recovered-but-not-slotted).

**Run after recovering/editing any polymorphic class**, or when a behavioral
divergence resists source-vs-Ghidra reading.
