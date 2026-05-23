"""Convert MOS to PNG. Handles V1 uncompressed only.

Usage: python scripts/mos_to_png.py <mos_path>
Output: bam_out/<basename>.png
"""
import struct
import sys
import os
import zlib
from PIL import Image


def main():
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()
    sig = data[:8]
    print(f"sig: {sig}")
    if sig.startswith(b"MOSC"):
        # MOSC: header is 12 bytes (sig 8 + decompressed length 4) then zlib data
        unpacked = struct.unpack_from("<I", data, 8)[0]
        decomp = zlib.decompress(data[12:])
        if len(decomp) != unpacked:
            print(f"warn: decomp size {len(decomp)} != expected {unpacked}")
        data = decomp
        sig = data[:8]
        print(f"decompressed sig: {sig}")
    if sig != b"MOS V1  ":
        print(f"unsupported sig {sig!r}")
        sys.exit(1)

    w, h, blockSize = struct.unpack_from("<HHH", data, 8)
    _, palOff = struct.unpack_from("<II", data, 0x10)
    print(f"size: {w}x{h} block={blockSize}")
    nCols = (w + blockSize - 1) // blockSize
    nRows = (h + blockSize - 1) // blockSize
    nBlocks = nCols * nRows
    print(f"blocks: {nCols}x{nRows} ({nBlocks})")

    # MOS V1 layout (from IESDP):
    #   header  (24 bytes)
    #   palette table  : nBlocks * 1024 bytes (256 BGRA per block)
    #   tile lookup    : nBlocks * 4 bytes (offset into data area)
    #   tile data area : raw 8-bit indexed blocks (blockSize*blockSize each)
    pal_table_off = palOff
    lookup_off = pal_table_off + nBlocks * 1024
    data_off = lookup_off + nBlocks * 4

    img = Image.new("RGBA", (w, h), (0, 0, 0, 255))
    pixels = img.load()

    for by in range(nRows):
        for bx in range(nCols):
            blkIdx = by * nCols + bx
            tileOff = struct.unpack_from("<I", data, lookup_off + blkIdx * 4)[0]
            palOffBlk = pal_table_off + blkIdx * 1024
            # block dimensions (last column/row may be smaller)
            bw = min(blockSize, w - bx * blockSize)
            bh = min(blockSize, h - by * blockSize)
            for ty in range(bh):
                for tx in range(bw):
                    idx = data[data_off + tileOff + ty * bw + tx]
                    palE = pal_table_off + blkIdx * 1024 + idx * 4
                    b = data[palE]; g = data[palE + 1]; r = data[palE + 2]
                    pixels[bx * blockSize + tx, by * blockSize + ty] = (r, g, b, 255)

    base = os.path.splitext(os.path.basename(path))[0]
    out_dir = "bam_out"
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"{base}.png")
    img.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
