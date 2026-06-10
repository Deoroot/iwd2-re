#!/usr/bin/env python3
"""On-disk file-format injector (brick e) -- the third awareness layer.

A loader/parser in IWD2.exe identifies a resource by its 4-character signature,
which the compiler folds into a little-endian DWORD immediate. The decompile then
reads e.g. ``if (*param_2 == 0x204d5449)`` -- that constant is ``'ITM '`` packed LE
(0x49,0x54,0x4d,0x20). deepseek (no tool channel) sees the magic number but not what
the bytes that follow it MEAN, so it cannot name the header fields it parses. This
brick detects the signature DWORD and injects the on-disk byte layout (offset / size /
field) from the IESDP spec, so the model names ``ReadDword(off 0x18)`` as ``Flags``,
``ReadResRef(off 0x10)`` as the replacement-item resref, and so on.

Two earlier bricks cover the other layers: (d) IDS = engine constants in switch/case;
(c) BG2 PDB = in-MEMORY class field names. This one is the on-DISK file layout.

VERSION GROUNDING (the key subtlety). IESDP ships several versions per format
(itm_v1 / itm_v1.1 / itm_v2.0) and a 4CC alone does not say which IWD2 uses. So we
read a REAL shipped IWD2 file from ``data/near_infinity_export/<FMT>/`` and take its
version field (bytes 4:8) as ground truth -- IWD2 ships ITM V2.0, CRE V2.2, SPL V2.0,
WED V1.3, STO V9.0, BAMC/CHUI V1 -- then pick the htm whose version matches. Without a
sample file we fall back to the highest-numbered htm (which for ARE/EFF/GAM is also the
IWD2 one). This is why the injected layout matches the actual bytes the loader walks,
not a generic earlier-game header.

Usage::

    python scripts/reagent_file_formats.py --address 0x4016f0   # from export decompile
    python scripts/reagent_file_formats.py --sig "ITM "         # one format directly
    python scripts/reagent_file_formats.py --list               # the signature registry
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
IESDP = REPO / "refs" / "iesdp" / "file_formats" / "ie_formats"
NI = REPO / "data" / "near_infinity_export"
EXPORTS = REPO / ".ghidra-exports"

# Signature 4CC -> (IESDP filename stem, NearInfinity export subdir for version
# grounding | None when not exported). The htm VERSION is pinned at runtime from a real
# shipped file; this table only fixes which format + where its samples live.
SIG_TO_FORMAT: dict[str, tuple[str, str | None]] = {
    "ITM ": ("itm", "ITM"), "SPL ": ("spl", "SPL"), "CRE ": ("cre", "CRE"),
    "CHUI": ("chu", "CHU"), "BAM ": ("bam", "BAM"), "BAMC": ("bam", "BAM"),
    "WED ": ("wed", "WED"), "STOR": ("sto", "STO"), "AREA": ("are", "ARE"),
    "EFF ": ("eff", "EFF"), "GAME": ("gam", "GAM"), "WMAP": ("wmap", "WMP"),
    "MOS ": ("mos", "MOS"), "MOSC": ("mos", "MOS"), "DLG ": ("dlg", "DLG"),
    "PRO ": ("pro", None), "VVC ": ("vvc", None), "WFX ": ("wfx", None),
    "2DA ": ("2da", None),
}

TABLE_RE = re.compile(r"<table\b.*?>(.*?)</table>", re.S | re.I)
ROW_RE = re.compile(r"<tr\b.*?>(.*?)</tr>", re.S | re.I)
CELL_RE = re.compile(r"<td\b.*?>(.*?)</td>", re.S | re.I)
TAG_RE = re.compile(r"<[^>]+>")
OFFSET_RE = re.compile(r"^0x[0-9a-fA-F]+$")


def sig_dword(sig: str) -> int:
    """``'ITM '`` -> 0x204d5449 (the little-endian DWORD the compiler emits)."""
    return int.from_bytes(sig.encode("latin-1"), "little")


def _ver_to_float(s: str) -> float:
    """Loosely parse a version string ('V2.0', 'v9', '1.3') to a comparable float."""
    digits = "".join(ch for ch in s if ch.isdigit() or ch == ".")
    try:
        return float(digits) if digits else -1.0
    except ValueError:
        return float(digits.split(".")[0]) if digits[:1].isdigit() else -1.0


def real_version(ni_dir: str | None) -> str | None:
    """Version field (bytes 4:8) of the first real IWD2 file in the NI export dir."""
    if not ni_dir:
        return None
    d = NI / ni_dir
    if not d.is_dir():
        return None
    files = sorted(d.glob(f"*.{ni_dir}")) or [p for p in sorted(d.iterdir()) if p.is_file()]
    if not files:
        return None
    head = files[0].read_bytes()[:8]
    if len(head) < 8:
        return None
    ver = head[4:8].decode("latin-1", "replace")
    return ver if any(c.isdigit() for c in ver) else None   # 'V2.0' yes, junk no


def pick_htm(stem: str, version: str | None) -> Path | None:
    """Choose the IESDP htm for *stem* matching the real *version* (else highest)."""
    cands = sorted(IESDP.glob(f"{stem}_v*.htm"))
    if not cands:
        base = IESDP / f"{stem}.htm"
        return base if base.is_file() else None
    if version:
        want = _ver_to_float(version)
        exact = [c for c in cands if abs(_ver_to_float(c.stem) - want) < 1e-9]
        if exact:
            return exact[0]
        below = [c for c in cands if _ver_to_float(c.stem) <= want + 1e-9]
        if below:
            return max(below, key=lambda c: _ver_to_float(c.stem))
    return max(cands, key=lambda c: _ver_to_float(c.stem))


def _clean(html: str) -> str:
    """HTML cell -> one-line plain text (keep <a> inner text, drop markup)."""
    t = re.sub(r"<br\s*/?>", " ", html, flags=re.I)
    t = re.sub(r"<li\b[^>]*>", "; ", t, flags=re.I)
    t = TAG_RE.sub("", t)
    t = (t.replace("&nbsp;", " ").replace("&amp;", "&")
         .replace("&lt;", "<").replace("&gt;", ">").replace("&#39;", "'"))
    return re.sub(r"\s+", " ", t).strip()


def parse_header(htm: Path) -> list[tuple[str, str, str]]:
    """Parse the header table (offset, size+type, description) from an IESDP htm.

    The header is the FIRST table holding real ``0x....`` offset rows -- scanning for
    that skips the decorative caption table (``Header: Size = N Bytes``) the newer-style
    pages (itm_v2.0, cre_v2.2) put just ahead of the field table.
    """
    text = htm.read_text(encoding="utf-8", errors="replace")
    for tbl in TABLE_RE.findall(text):
        rows: list[tuple[str, str, str]] = []
        for raw in ROW_RE.findall(tbl):
            cells = [_clean(c) for c in CELL_RE.findall(raw)]
            if len(cells) == 3 and OFFSET_RE.match(cells[0]):
                rows.append((cells[0], cells[1], cells[2]))
        if rows:
            return rows
    return []


def format_block(sig: str, max_rows: int = 64) -> str | None:
    """Resolve *sig* -> the injectable header-layout block (None if unavailable)."""
    entry = SIG_TO_FORMAT.get(sig)
    if not entry:
        return None
    stem, ni_dir = entry
    version = real_version(ni_dir)
    htm = pick_htm(stem, version)
    if htm is None:
        return None
    rows = parse_header(htm)
    if not rows:
        return None
    grounded = f"IWD2 ships {version.strip()}" if version else f"{htm.stem} (no sample, highest spec)"
    head = (f"{sig.strip() or sig!r} 0x{sig_dword(sig):08x} -- {htm.stem} header "
            f"({grounded}; on-disk byte layout, name the parse fields after these):")
    width = max((len(s) for _, s, _ in rows[:max_rows]), default=8)
    lines = [head]
    for off, size, desc in rows[:max_rows]:
        if len(desc) > 90:                       # keep the field name, drop the prose tail
            desc = desc[:88].rsplit(" ", 1)[0] + " ..."
        lines.append(f"  {off}  {size:<{width}}  {desc}")
    if len(rows) > max_rows:
        lines.append(f"  ... ({len(rows) - max_rows} more header fields)")
    return "\n".join(lines)


def detect(text: str) -> list[str]:
    """Signatures whose packed DWORD appears as an immediate in *text* (decompile)."""
    found: list[str] = []
    for sig in SIG_TO_FORMAT:
        dw = sig_dword(sig)
        if re.search(r"(?<![0-9a-fA-Fx])0x0*%x(?![0-9a-fA-F])" % dw, text, re.I):
            found.append(sig)
    return found


def annotate(text: str) -> tuple[str, int]:
    """Detect signatures in *text* and return the joined layout blocks + count."""
    blocks = [b for b in (format_block(s) for s in detect(text)) if b]
    return ("\n\n".join(blocks), len(blocks))


def _load_decompile(address: int) -> str:
    path = EXPORTS / f"{address:08x}.json"
    if not path.is_file():
        raise SystemExit(f"no export for {address:#x} at {path}")
    return json.loads(path.read_text(encoding="utf-8")).get("decompiled") or ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--address", help="resolve format sigs from this export's decompile")
    g.add_argument("--sig", help="emit one format block for this 4CC (e.g. 'ITM ')")
    g.add_argument("--list", action="store_true", help="print the signature registry")
    args = ap.parse_args()

    if args.list:
        for sig, (stem, ni) in SIG_TO_FORMAT.items():
            ver = real_version(ni)
            htm = pick_htm(stem, ver)
            print(f"  {sig!r:8} 0x{sig_dword(sig):08x}  {stem:5} "
                  f"ver={ver.strip() if ver else '?':6} -> {htm.name if htm else '(no htm)'}")
        return 0

    if args.sig:
        block = format_block(args.sig)
        print(block if block else f"no layout for {args.sig!r}")
        return 0 if block else 1

    try:
        addr = int(args.address, 16)
    except ValueError:
        print(f"bad address: {args.address}")
        return 2
    blk, n = annotate(_load_decompile(addr))
    if n:
        print(blk)
    else:
        print(f"no file-format signatures detected in {addr:#x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
