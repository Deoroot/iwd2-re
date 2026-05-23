"""Extract BAM frames to PNG. Requires Pillow (pip install pillow).

Usage:
  python scripts/bam_to_png.py <bam_file> [frame_index] [frame_index] ...
  python scripts/bam_to_png.py <bam_file> all

Outputs to ./bam_out/<basename>_frame_<N>.png
"""

import struct
import sys
import os
from PIL import Image


def parse_bam_v1(data):
    sig = data[:8]
    if sig != b"BAM V1  ":
        raise ValueError(f"not BAM V1: {sig!r}")
    n_frames, n_cycles, rle_color, frame_off = struct.unpack_from("<HBBI", data, 8)
    pal_off = struct.unpack_from("<I", data, 0x10)[0]
    lookup_off = struct.unpack_from("<I", data, 0x14)[0]

    # palette: 256 BGRA entries
    palette = []
    for i in range(256):
        b, g, r, a = struct.unpack_from("<BBBB", data, pal_off + i * 4)
        # IWD2 alpha: 0 = opaque, but index 0 is typically green-screen transparent
        palette.append((r, g, b))

    frames = []
    for i in range(n_frames):
        w, h, cx, cy, d = struct.unpack_from("<HHhhI", data, frame_off + i * 12)
        rle = (d & 0x80000000) == 0  # bit 31 CLEAR = RLE encoded
        offset = d & 0x7FFFFFFF
        frames.append({
            "w": w, "h": h, "cx": cx, "cy": cy,
            "offset": offset, "rle": rle,
        })

    return {
        "n_frames": n_frames,
        "n_cycles": n_cycles,
        "rle_color": rle_color,
        "palette": palette,
        "frames": frames,
        "raw": data,
    }


def decode_frame(bam, idx):
    f = bam["frames"][idx]
    w, h = f["w"], f["h"]
    pixels = bytearray(w * h)
    src = bam["raw"]
    pos = f["offset"]
    if f["rle"]:
        rle_color = bam["rle_color"]
        out = 0
        total = w * h
        while out < total:
            b = src[pos]; pos += 1
            if b == rle_color:
                run = src[pos] + 1; pos += 1
                for _ in range(run):
                    if out >= total: break
                    pixels[out] = b
                    out += 1
            else:
                pixels[out] = b
                out += 1
    else:
        pixels = src[pos:pos + w * h]

    img = Image.new("RGBA", (w, h))
    palette = bam["palette"]
    img_data = []
    for px in pixels:
        if px == 0:
            img_data.append((0, 0, 0, 0))  # treat index 0 as transparent
        else:
            r, g, b = palette[px]
            img_data.append((r, g, b, 255))
    img.putdata(img_data)
    return img


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    bam_path = sys.argv[1]
    args = sys.argv[2:]

    with open(bam_path, "rb") as f:
        data = f.read()

    if data[:4] == b"BAMC":
        print("compressed BAM — use BAM_DECOMP version")
        sys.exit(1)

    bam = parse_bam_v1(data)
    print(f"frames={bam['n_frames']} cycles={bam['n_cycles']} rle_color={bam['rle_color']}")

    if not args:
        for i, f in enumerate(bam["frames"]):
            print(f"  frame {i}: {f['w']}x{f['h']} center=({f['cx']},{f['cy']}) rle={f['rle']}")
        return

    indices = []
    if args == ["all"]:
        indices = list(range(bam["n_frames"]))
    else:
        indices = [int(a, 0) for a in args]

    base = os.path.splitext(os.path.basename(bam_path))[0]
    out_dir = "bam_out"
    os.makedirs(out_dir, exist_ok=True)

    for i in indices:
        if i >= bam["n_frames"]:
            print(f"frame {i} out of range")
            continue
        img = decode_frame(bam, i)
        path = os.path.join(out_dir, f"{base}_frame_{i:03d}.png")
        img.save(path)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
