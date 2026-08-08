#!/usr/bin/env python3
"""Hook-table -> Frida JS renderer, shared by frida_probe.py and frida_orig.py.

One schema, two runtimes: frida_probe.py generates a standalone driver for a
quick look; frida_orig.py runs the same table unattended with a verdict.  Keep
new keys optional so a table written for one still renders under the other.

Hook spec (JSON):
{
  "process": "IWD2.exe",              // frida_probe only; frida_orig uses --exe
  "log": "tmp_frida_probe.log",       // VM-side, relative to C:\\iwd2-re
  "hooks": [
    { "addr": "0x4063e0", "name": "GST", "conv": "thiscall",
      "args": ["s32", "s32", "s16"],
      "this_dump": [ {"off": "0x06", "type": "s16", "label": "posx"} ],
      "globals": [ {"chain": ["0x8CF6D8", "0x104C"], "type": "u32",
                    "label": "idLocalPlayer"} ],
      "bt": true,                     // 8-frame EBP walk on entry
      "ret": "s32",                   // log retval onLeave
      "max": 200 }                    // stop after N hits (default 500)
  ]
}

Types: u8 u16 u32 s16 s32 ptr f32 str wstr.  "str"/"wstr" deref, guarded.
conv: thiscall (this=ecx, stack args -> args[0..]) | cdecl | stdcall.
Addresses absolute (ImageBase 0x400000, no ASLR).  Hook function ENTRIES only.

"globals" is a deref chain: the first element is an absolute address, every
element after it is an offset applied after a readPointer().  A single-element
chain reads the global itself.  That is how you log engine state a hook's own
`this` does not reach -- e.g. g_pChitin(0x8CF6D8)->cNetwork.m_idLocalPlayer(+0x104C).
"""

READERS = {   # NativePointer -> value
    "u8":   "{p}.readU8()",
    "u16":  "{p}.readU16()",
    "u32":  "{p}.readU32()",
    "s16":  "(({p}.readU32() << 16) >> 16)",
    "s32":  "{p}.readS32()",
    "f32":  "{p}.readFloat()",
    "ptr":  "{p}.readPointer().toString()",
    "str":  "{p}.readUtf8String(64)",
    "wstr": "{p}.readUtf16String(64)",
}
ARG_VALUE = {   # NativePointer arg -> logged value
    "u32": "{a}.toUInt32()",
    "s32": "{a}.toInt32()",
    "s16": "(({a}.toInt32() << 16) >> 16)",
    "u16": "({a}.toUInt32() & 0xffff)",
    "u8":  "({a}.toUInt32() & 0xff)",
    "ptr": "{a}.toString()",
    "f32": "{a}.toInt32()",          # raw bits; floats passed on stack: read via dump if needed
    "str": "rdstr({a})",
    "wstr": "rdwstr({a})",
}

# JS helpers every rendered hook may reference.  ebpwalk mirrors the one in
# frida_crash_guard.py: x86 frame chain, bounded by a plausible-stack window so
# a garbage EBP stops the walk instead of faulting.
PRELUDE = r"""function guard(f) { try { return f(); } catch (e) { return 'ERR:' + e; } }
function rdstr(p)  { try { return p.readUtf8String(64); } catch (e) { return 'ERR'; } }
function rdwstr(p) { try { return p.readUtf16String(64); } catch (e) { return 'ERR'; } }
function ebpwalk(fp, depth) {
  const out = [];
  const lo = fp, hi = fp.add(0x80000);
  for (let i = 0; i < depth && !fp.isNull(); i++) {
    if (fp.compare(lo) < 0 || fp.compare(hi) > 0) break;
    try {
      out.push(fp.add(4).readPointer().toString());
      fp = fp.readPointer();
    } catch (e) { break; }
  }
  return out;
}"""


def _global_js(g):
    """{"chain": [...], "type": t} -> a guarded read expression."""
    chain = [c if isinstance(c, str) else hex(c) for c in g["chain"]]
    expr = f"ptr({chain[0]})"
    for off in chain[1:]:
        expr = f"{expr}.readPointer().add({off})"
    return READERS.get(g.get("type", "u32"), READERS["u32"]).format(p=expr)


def js_for_hook(h, idx):
    name = h["name"]
    addr = h["addr"]
    conv = h.get("conv", "thiscall")
    args = h.get("args", [])
    maxn = int(h.get("max", 500))
    parts = []
    parts.append(f"let cnt{idx} = 0;")
    parts.append(f"Interceptor.attach(ptr({addr}), {{")
    parts.append("  onEnter(args) {")
    parts.append(f"    if (++cnt{idx} > {maxn}) return;")
    fields = [f"tag: '{name}'"]
    if conv == "thiscall":
        parts.append("    const thiz = this.context.ecx;")
        fields.append("this: thiz.toString()")
    for i, t in enumerate(args):
        expr = ARG_VALUE.get(t, ARG_VALUE["u32"]).format(a=f"args[{i}]")
        fields.append(f"a{i}_{t}: {expr}")
    for d in h.get("this_dump", []):
        off = d["off"] if isinstance(d["off"], str) else hex(d["off"])
        rd = READERS.get(d.get("type", "u32"), READERS["u32"]).format(p=f"thiz.add({off})")
        fields.append(f"{d.get('label', 'f_' + off)}: guard(() => {rd})")
    for g in h.get("globals", []):
        label = g.get("label", "g_" + str(g["chain"][0]))
        fields.append(f"{label}: guard(() => {_global_js(g)})")
    if h.get("bt"):
        fields.append("bt: guard(() => ebpwalk(this.context.ebp, 8))")
    parts.append("    send({ " + ", ".join(fields) + " });")
    if h.get("ret"):
        parts.append("  },")
        parts.append("  onLeave(retval) {")
        parts.append(f"    if (cnt{idx} > {maxn}) return;")
        rexpr = ARG_VALUE.get(h["ret"], ARG_VALUE["u32"]).format(a="retval")
        parts.append(f"    send({{ tag: '{name}_ret', ret: {rexpr} }});")
    parts.append("  }")
    parts.append("});")
    return "\n".join(parts)


def hooks_js(hooks):
    return "\n".join(js_for_hook(h, i) for i, h in enumerate(hooks))


def hit_counts_js(hooks, period_ms=2000):
    """Re-emit per-hook totals on a timer.

    Two reasons this is a timer and not a teardown handler: the wrapper kills
    the game rather than letting the driver unwind (same as frida_crash_guard),
    and the counters keep incrementing past a hook's "max", so the totals stay
    true even once logging has stopped."""
    counts = ", ".join("'%s': cnt%d" % (h["name"], i) for i, h in enumerate(hooks))
    return "setInterval(() => send({ tag: 'HITS', counts: { %s } }), %d);" % (counts, period_ms)


def install_dump_js(hooks):
    """Gotcha 5: dump 8 bytes at every hook site so a silent no-fire can be
    diffed against `sym.py bytes ADDR 8` instead of blamed on the address."""
    entries = ", ".join(
        "{ name: '%s', addr: ptr(%s) }" % (h["name"], h["addr"]) for h in hooks
    )
    return (
        "send({ tag: 'sites', sites: [%s].map(h => ({\n"
        "  name: h.name, addr: h.addr.toString(),\n"
        "  bytes: guard(() => Array.from(new Uint8Array(h.addr.readByteArray(8)))\n"
        "    .map(b => ('0' + b.toString(16)).slice(-2)).join('')) })) });" % entries
    )
