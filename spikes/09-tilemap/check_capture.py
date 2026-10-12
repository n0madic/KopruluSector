#!/usr/bin/env python3
"""Compare a spike-09 capture (map top-left, zoom 1, palette not yet cycled) with the
terrain rendered offline from MTXM -> CV5 -> VX4/VR4 -> WPE.

Usage: check_capture.py --data DIR --capture web-webgl2.png [--scale 1]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from converter.formats import palette, tileset  # noqa: E402
from converter.kpf.container import parse  # noqa: E402
from converter.mpq.profile import Resolver  # noqa: E402

VIEW_W, VIEW_H = 640, 480


def expected_rgb(r: Resolver) -> np.ndarray:
    ts = tileset.decode(*(r.read(f"tileset\\desert.{e}")[1] for e in ("cv5", "vx4", "vr4", "vf4")))
    pal = np.array(palette.decode_wpe(r.read("tileset\\desert.wpe")[1]).rgb, np.uint8)
    kpm = parse((ROOT / "generated/spikes/09-tilemap/desert_256.kpm").read_bytes())
    width = int.from_bytes(kpm.section("META").payload[:2], "little")
    mtxm = np.frombuffer(kpm.section("MTXM").payload, "<u2")
    out = np.zeros((VIEW_H, VIEW_W), np.uint8)
    for ty in range(VIEW_H // 32):
        for tx in range(VIEW_W // 32):
            mega = ts.megatile_for_tile(int(mtxm[ty * width + tx]))
            tile = np.frombuffer(ts.render_megatile(mega), np.uint8).reshape(32, 32)
            out[ty * 32:(ty + 1) * 32, tx * 32:(tx + 1) * 32] = tile
    return pal[out]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", required=True)
    ap.add_argument("--capture", required=True, type=Path)
    ap.add_argument("--scale", type=int, default=1)
    a = ap.parse_args()
    got = np.array(Image.open(a.capture).convert("RGB"))
    got = got[-VIEW_H * a.scale:, :VIEW_W * a.scale][::a.scale, ::a.scale]
    with Resolver(a.data, full_hash=False) as r:
        want = expected_rgb(r)
    equal = np.all(got == want, axis=2)
    print(f"CHECK terrain pixels_equal={int(equal.sum())}/{equal.size} exact={bool(equal.all())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
