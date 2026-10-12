#!/usr/bin/env python3
"""Writes a synthetic mission-sized container for the data-access spike.

Section sizes approximate the largest stock mission (256x256, many triggers and
contours); contents mimic their structure (repeating tile words, sparse walk flags)
so archive compression can be measured. Output: spikes/10-data/generated/mission.kpm
"""
from __future__ import annotations

import hashlib
import logging
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from converter.kpf.container import Container  # noqa: E402

OUT = Path(__file__).resolve().parent / "generated" / "mission.kpm"
SIZES = {  # bytes
    "MTXM": 256 * 256 * 2,
    "MASK": 256 * 256,
    "WALK": 1024 * 1024,
    "CONT": 4 * 7000 * 8,
    "RGNS": 5000 * 64,
    "TRIG": 1024 * 1024,
    "STRS": 128 * 1024,
    "UNIT": 1500 * 36,
}

log = logging.getLogger("make_mission")


def structured(name: str, size: int, rng: random.Random) -> bytes:
    if name == "MTXM":
        words = []
        for _ in range(size // 2):
            words.append(rng.choice((0x0010, 0x0011, 0x0012, 0x0230, 0x0231)) if rng.random() < 0.9 else rng.randrange(0x4000))
        return struct.pack(f"<{len(words)}H", *words)
    if name in ("WALK", "MASK"):
        return bytes(0x80 if rng.random() < 0.15 else 0 for _ in range(size))
    if name == "TRIG":
        return bytes(0 if rng.random() < 0.8 else rng.randrange(256) for _ in range(size))
    return bytes(rng.randrange(256) for _ in range(size))


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    rng = random.Random(1998)
    c = Container(b"MISN", schema_version=1, converter_version=0, profile_id="bw-1.17.0.1",
                  source_hash=hashlib.sha256(b"synthetic").digest())
    for name, size in SIZES.items():
        c.add(name, structured(name, size, rng))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    data = c.to_bytes()
    OUT.write_bytes(data)
    log.info("wrote %s (%d bytes)", OUT, len(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
