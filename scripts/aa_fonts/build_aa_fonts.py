#!/usr/bin/env python3
"""Rebuild the IWD2EE "non-pixelated (AA) fonts" 2x BAMs from bundled sources.

Outputs (relative to the mod's iwd2ee/ dir passed as argv[1]):
  bam/bam_2x_ui/aa_fonts/NUMFONT.BAM      Roboto Condensed Bold  (HP digits, white + black drop shadow)
  bam/bam_2x_ui/aa_fonts/INFOFONT.BAM     Rajdhani Bold          (floating identify text, Latin/cp1252)
  bam/bam_2x_ui_ru/aa_fonts/INFOFONT.BAM  Play Bold              (Russian Cyrillic, cp1251)

All glyphs are encoded for the engine's tint model (CVidFont::RealizePalette: index1=bg,
index255=fg; CGameText SetColor(white,black)) -> ink = high index, bevel/shadow = index 1.
Frame index = charcode-1. All three fit-to-cell: NUMFONT from the stock 2x NUMFONT,
Western INFOFONT from the stock Western (cp1252) INFOFONT, Russian INFOFONT from the stock
Russian (cp1251) INFOFONT (font_src/INFOFONT_1x_ru.BAM, from a Russian install's
Override) so the Cyrillic cell widths match exactly.

Fonts bundled in font_src/ (Roboto Condensed = Apache-2.0; Rajdhani, Play = OFL-1.1).
Rajdhani has NO Cyrillic -> Russian uses Play (square-ish grotesque with Cyrillic).

Usage:  python build_aa_fonts.py /path/to/IWD2EE/iwd2ee
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from build_font_bam import build

SRC = os.path.join(HERE, "font_src")
ROBOTO = os.path.join(SRC, "RobotoCondensed.ttf")
RAJDHANI = os.path.join(SRC, "Rajdhani-Bold.ttf")
PLAY = os.path.join(SRC, "Play-Bold.ttf")
NUM_STOCK = os.path.join(SRC, "NUMFONT_2x_stock.BAM")
INFO_VANILLA = os.path.join(SRC, "INFOFONT_1x_vanilla.BAM")  # Western (cp1252)
INFO_RU = os.path.join(SRC, "INFOFONT_1x_ru.BAM")            # Russian stock (Cyrillic cells)

NUM_CHARS = set("0123456789/.-:")
WEST = {chr(c) for c in list(range(0x21, 0x7f)) + list(range(0xC0, 0x100))}
RU = set(chr(c) for c in range(0x21, 0x7f))
for _c in range(0x80, 0x100):
    try:
        RU.add(bytes([_c]).decode("cp1251"))
    except Exception:
        pass


def main(mod):
    aa = os.path.join(mod, "bam", "bam_2x_ui", "aa_fonts")
    aaru = os.path.join(mod, "bam", "bam_2x_ui_ru", "aa_fonts")
    os.makedirs(aa, exist_ok=True)
    os.makedirs(aaru, exist_ok=True)
    build(NUM_STOCK, ROBOTO, NUM_CHARS, os.path.join(aa, "NUMFONT.BAM"),
          stretch=1.0, edge_off=2, var=[700])
    build(INFO_VANILLA, RAJDHANI, WEST, os.path.join(aa, "INFOFONT.BAM"),
          stretch=1.0, edge_off=2, src_scale=2)
    # Russian: source the STOCK Russian INFOFONT (real Cyrillic cell geometry) so
    # the footprint matches; autocell off (cells are already correct for cp1251).
    build(INFO_RU, PLAY, RU, os.path.join(aaru, "INFOFONT.BAM"),
          stretch=1.0, edge_off=2, src_scale=2, codepage="cp1251", autocell=False)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__); sys.exit(1)
    main(sys.argv[1])
