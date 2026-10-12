#!/usr/bin/env python3
"""Byte check of spike 6 on the native macOS (Vulkan/Metal) backend.

Builds the spike with show.settings (first batch, bottom-left 4x4 tiles copied 1:1 into
the window), captures the window with `screencapture -l` and compares the red channel
of each tile, read in the original top-down order with guards, with the original
SHA-256. Requires macOS screen-recording permission for the terminal.
"""
from __future__ import annotations

import hashlib
import logging
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from converter.kpf.container import parse  # noqa: E402

TILE, SHOWN = 320, 4  # 4x4 tiles
OUT = ROOT / "build" / "spikes" / "06-palette"
log = logging.getLogger("native_check")


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    build = OUT / "native-build"
    subprocess.run(["java", "-jar", str(ROOT / "tools/toolchain/bob.jar"), "--root", str(ROOT),
                    "--settings", "spikes/06-palette/spike.settings", "--settings", "spikes/06-palette/show.settings",
                    "--output", str(build.relative_to(ROOT)), "--variant", "debug", "build"], check=True, cwd=ROOT,
                   stdout=subprocess.DEVNULL)
    engine = subprocess.Popen([str(ROOT / "tools/toolchain/dmengine"), str(build / "game.projectc")],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(6)
        window = subprocess.run([str(ROOT / "tools/toolchain/window_id")], capture_output=True, text=True, check=True).stdout.strip()
        shot = OUT / "native_tiles.png"
        subprocess.run(["screencapture", "-x", "-o", f"-l{window}", str(shot)], check=True)
    finally:
        engine.terminate()
        engine.wait()
    img = Image.open(shot).convert("RGB")
    width, height = img.size
    size = TILE * SHOWN
    # Content is the bottom-left of the capture (the title bar is above it).
    px = img.load()
    hashes = parse((ROOT / "spikes/06-palette/generated/cases.kpd").read_bytes()).section("HASH").payload
    ok = 0
    for t in range(SHOWN * SHOWN):
        # Tile t sits at GL tile column t % 8, row t // 8 of the 8x8 grid; only the
        # bottom-left 4x4 block is visible.
        col, row = t % 8, t // 8
        if col >= SHOWN or row >= SHOWN:
            continue
        buf = bytearray([0xE7] * 16)
        for r in range(TILE):
            gl_y = row * TILE + TILE - 1 - r            # GL row inside the window content
            y = height - 1 - gl_y                         # image rows count downwards
            for c in range(TILE):
                buf.append(px[col * TILE + c, y][0])
        buf += bytes([0xE7] * 16)
        match = hashlib.sha256(buf).digest() == hashes[t * 32:(t + 1) * 32]
        ok += match
    shown = sum(1 for t in range(SHOWN * SHOWN) if t % 8 < SHOWN and t // 8 < SHOWN)
    log.info("SPIKE native byte check (capture %dx%d): %d of %d visible tiles match", width, height, ok, shown)
    return 0 if ok == shown else 1


if __name__ == "__main__":
    sys.exit(main())
