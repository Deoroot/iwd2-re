#!/usr/bin/env python3
"""spandrel-based AI upscaler: load any ESRGAN/DAT/SwinIR/HAT .pth model and run it
on CUDA (half precision, optional tiling). chaiNNer uses spandrel under the hood;
this is the same, scriptable. Run with the .venv-upscale venv (torch+spandrel)."""
import torch, numpy as np
from PIL import Image
from spandrel import ModelLoader, ImageModelDescriptor

def load(model_path, device="cuda"):
    m = ModelLoader().load_from_file(model_path)
    assert isinstance(m, ImageModelDescriptor), f"not an image model: {type(m)}"
    m.to(device).eval()
    return m

def _to_t(img, device, half):
    a = np.asarray(img.convert("RGB"), dtype=np.float32) / 255.0
    t = torch.from_numpy(a).permute(2, 0, 1).unsqueeze(0).to(device)
    return t.half() if half else t

def _from_t(t):
    a = t.squeeze(0).permute(1, 2, 0).clamp(0, 1).mul(255).round().byte().cpu().numpy()
    return Image.fromarray(a)

def upscale(model, img, device="cuda", tile=0):
    """PIL RGB -> PIL RGB at model.scale x. tile=0: whole image (small MOS fit in
    8GB easily); >0: tiled with 16px blended overlap to bound VRAM on big inputs."""
    half = model.supports_half and device == "cuda"
    if half:
        model.model.half()
    t = _to_t(img, device, half)
    H, W = t.shape[2], t.shape[3]
    s = model.scale
    with torch.no_grad():
        if tile <= 0 or (H <= tile and W <= tile):
            out = model(t)
        else:
            ov = 16
            out = torch.zeros(1, 3, H * s, W * s, dtype=t.dtype, device=device)
            wsum = torch.zeros(1, 1, H * s, W * s, dtype=t.dtype, device=device)
            for y in range(0, H, tile - ov):
                for x in range(0, W, tile - ov):
                    y2, x2 = min(y + tile, H), min(x + tile, W)
                    op = model(t[:, :, y:y2, x:x2])
                    out[:, :, y*s:y2*s, x*s:x2*s] += op
                    wsum[:, :, y*s:y2*s, x*s:x2*s] += 1
                    if x2 == W: break
                if y2 == H: break
            out = out / wsum.clamp(min=1)
    return _from_t(out.float())

if __name__ == "__main__":
    import sys
    m = load(sys.argv[2])
    print(f"model: scale={m.scale}x  half={m.supports_half}")
    upscale(m, Image.open(sys.argv[1])).save(sys.argv[3])
    print("wrote", sys.argv[3])
