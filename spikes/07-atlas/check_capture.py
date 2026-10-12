#!/usr/bin/env python3
"""Compare a spike-07 capture with the decoded GRP frames.

For every "SPIKE place" line the expected frame 0 (index + opacity, from the original GRP)
is searched near its predicted screen position; the best offset must reproduce every opaque
pixel as gray == palette index and leave every transparent pixel at the clear color.

Usage: check_capture.py --data DIR --log macos-vulkan.log --capture macos-vulkan.png [--scale 2]
"""
from __future__ import annotations

import argparse
import logging
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from converter.formats import grp  # noqa: E402
from converter.mpq.profile import Resolver  # noqa: E402

log = logging.getLogger("check_capture")
PLACE = re.compile(r"place variant=(\S+) id=(\S+) x=(\d+) y=(\d+)")
WORLD_HEIGHT = 480
SEARCH = 4


def expected(g: grp.Grp, canvas: bool) -> tuple[np.ndarray, np.ndarray]:
    f = g.frames[0]
    idx = np.frombuffer(f.indices, np.uint8).reshape(f.height, f.width)
    mask = np.frombuffer(f.mask, np.uint8).reshape(f.height, f.width) > 0
    if not canvas:
        return idx, mask
    ci = np.zeros((g.height, g.width), np.uint8)
    cm = np.zeros((g.height, g.width), bool)
    ci[f.y:f.y + f.height, f.x:f.x + f.width] = idx
    cm[f.y:f.y + f.height, f.x:f.x + f.width] = mask
    return ci, cm


def content_top(rgb: np.ndarray) -> int:
    """First row of the clear color (blue) in column 4: the top of the engine viewport."""
    col = rgb[:, 4]
    for y in range(len(col)):
        r, g, b = col[y]
        if r < 16 and g < 16 and b > 200:
            return y
    raise ValueError("clear color not found")


def check(rgb: np.ndarray, top: int, scale: int, x: int, y: int, idx: np.ndarray, mask: np.ndarray) -> tuple[int, int, int, int]:
    h, w = idx.shape
    big_idx = np.kron(idx, np.ones((scale, scale), np.uint8))
    big_mask = np.kron(mask, np.ones((scale, scale), bool))
    cx, cy = x * scale, top + (WORLD_HEIGHT - y) * scale
    x0, y0 = int(round(cx - w * scale / 2)), int(round(cy - h * scale / 2))
    best = (-1, 0, 0, 0)
    for dy in range(-SEARCH, SEARCH + 1):
        for dx in range(-SEARCH, SEARCH + 1):
            ys, xs = y0 + dy, x0 + dx
            win = rgb[ys:ys + h * scale, xs:xs + w * scale].astype(int)
            if win.shape[:2] != big_idx.shape:
                continue
            gray = (win[:, :, 0] == win[:, :, 1]) & (win[:, :, 1] == win[:, :, 2])
            ok_opaque = int(np.sum(big_mask & gray & (win[:, :, 0] == big_idx)))
            blue = (win[:, :, 2] > 200) & (win[:, :, 0] < 16)
            ok_clear = int(np.sum(~big_mask & blue))
            score = ok_opaque + ok_clear
            if score > best[0] + best[2]:
                best = (ok_opaque, int(big_mask.sum()), ok_clear, int((~big_mask).sum()))
    return best


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", required=True)
    ap.add_argument("--log", required=True, type=Path)
    ap.add_argument("--capture", required=True, type=Path)
    ap.add_argument("--scale", type=int, default=2)
    a = ap.parse_args()
    rgb = np.array(Image.open(a.capture).convert("RGB"))
    top = content_top(rgb)
    failures = 0
    with Resolver(a.data, full_hash=False) as r:
        cache: dict[str, grp.Grp] = {}
        for m in PLACE.finditer(a.log.read_text()):
            variant, anim, x, y = m[1], m[2], int(m[3]), int(m[4])
            name = "unit\\terran\\" + anim.removeprefix("terran_") + ".grp"
            g = cache.setdefault(name, grp.decode(r.read(name)[1]))
            idx, mask = expected(g, "canvas" in variant)
            ok_o, n_o, ok_c, n_c = check(rgb, top, a.scale, x, y, idx, mask)
            exact = ok_o == n_o and ok_c == n_c
            failures += not exact
            print(f"CHECK variant={variant} id={anim} opaque={ok_o}/{n_o} clear={ok_c}/{n_c} exact={exact}")
    print(f"CHECK summary failures={failures}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
