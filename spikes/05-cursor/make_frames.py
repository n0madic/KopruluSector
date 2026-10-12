#!/usr/bin/env python3
"""Flipbooks whose frame k is a solid 4x4 block of colour index (k % 255) + 1, for the
cursor -> frame spike. Sizes cover common GRP frame counts (17, 68, 229, 391)."""
from __future__ import annotations

import logging
import sys
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent / "generated"
COUNTS = [17, 68, 229, 391]
log = logging.getLogger("make_frames")


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    (OUT / "frames").mkdir(parents=True, exist_ok=True)
    images, animations = [], []
    for n in COUNTS:
        names = []
        for k in range(n):
            name = f"f{n}_{k:03d}.png"
            Image.new("RGBA", (4, 4), ((k % 255) + 1, 0, 0, 255)).save(OUT / "frames" / name)
            names.append(name)
            images.append(f'images {{\n  image: "/spikes/05-cursor/generated/frames/{name}"\n}}')
        frames = "\n".join(f'  images {{\n    image: "/spikes/05-cursor/generated/frames/{x}"\n  }}' for x in names)
        animations.append(f'animations {{\n  id: "n{n}"\n{frames}\n  playback: PLAYBACK_NONE\n  fps: 0\n}}')
    (OUT / "cursor.atlas").write_text("\n".join(images + animations) + "\nmargin: 0\nextrude_borders: 0\ninner_padding: 0\n")
    log.info("wrote %s", OUT / "cursor.atlas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
