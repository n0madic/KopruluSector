#!/usr/bin/env python3
"""Synthetic indexed frames for rendering spikes (no original assets).

Each PNG stores the palette index in R and the opacity mask in A (G = B = 0), the
layout produced by the converter for indexed atlases. Frames contain index ramps,
one-pixel lines (to judge scaling) and a player-colour block
(indices 8..15). Index 0 marks transparency, as in the converter's atlases (ADR 0003). Output: spikes/common/generated/frames/frame_NN.png and a .atlas.
"""
from __future__ import annotations

import logging
import sys
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent / "generated"
FRAMES = 17  # like a GRP direction set
SIZE = 64

log = logging.getLogger("make_test_frames")


def frame(n: int) -> Image.Image:
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            dx, dy = x - SIZE // 2, y - SIZE // 2
            if dx * dx + dy * dy > (SIZE // 2 - 2) ** 2:
                px[x, y] = (0, 0, 0, 0)
                continue  # transparent outside the disc
            if 24 <= y < 40 and 20 <= x < 44:
                index = 8 + (x - 20) // 3  # player colour indices 8..15
            elif x == SIZE // 2 or y == (n * 3) % SIZE:
                index = 1  # one-pixel lines
            else:
                index = (x * 4 + y + n * 15) % 255 + 1  # never 0: index 0 is transparent (ADR 0003)
            px[x, y] = (index, 0, 0, 255)
    return img


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    frames_dir = OUT / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    lines = []
    for n in range(FRAMES):
        path = frames_dir / f"frame_{n:02d}.png"
        frame(n).save(path)
        lines.append(f'  images {{\n    image: "/spikes/common/generated/frames/{path.name}"\n  }}')
    atlas = OUT / "test_frames.atlas"
    animation = "\n".join(f'    images {{\n      image: "/spikes/common/generated/frames/frame_{n:02d}.png"\n    }}'
                          for n in range(FRAMES))
    atlas.write_text("\n".join(lines) + f"\nanimations {{\n  id: \"all\"\n{animation}\n  playback: PLAYBACK_NONE\n  fps: 0\n}}\nmargin: 0\nextrude_borders: 0\ninner_padding: 0\n")
    log.info("wrote %d frames and %s", FRAMES, atlas)
    return 0


if __name__ == "__main__":
    sys.exit(main())
