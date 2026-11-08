"""The region graph each campaign mission builds in the game (CreateRegions 0x490d80, called by
LoadTileMapSection 0x469170 after it blocks the console strips), read from the "regions" blob of
the emulator's step-1 state dumps (sc_emulator/state.py), as the snapshots of
probe_native_campaign_regions_117.py: ordered region records, raster and split records.

Writes docs/research/evidence/native-ingame-regions.json: per catalog map, the SHA-256 of the
snapshot (JSON, compact separators) and its counts; converter/tests/test_derive.py compares the
converter's RGNS section with it.

Usage (repository root, private venv): python tools/research/ingame_regions.py [--root STATES]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "research"))
from sc_emulator.state import State  # noqa: E402

OUT = ROOT / "docs/research/evidence/native-ingame-regions.json"
REGIONS_AT, SPLITS_AT, RASTER_AT, ROW_WORDS, RECORD = 0x449FC, 0x2000C, 0xC, 256, 64


def snapshot(blob: bytes, base: int, width: int, height: int) -> dict:
    def u(fmt, at):
        return struct.unpack_from(fmt, blob, at)[0]
    n = u("<H", 0)
    rs = []
    for i in range(n):
        a = REGIONS_AT + i * RECORD
        count = blob[a + 7]
        ptr = u("<I", a + 0xC) - base
        neighbors = list(struct.unpack_from("<" + "H" * count, blob, ptr)) if count else []
        rs.append({"id": i, "flags": u("<H", a), "group": u("<H", a + 2), "tiles": u("<H", a + 4),
                   "ground_neighbors": blob[a + 6], "neighbors": neighbors,
                   "center_fixed8": list(struct.unpack_from("<2i", blob, a + 0x10)),
                   "bounds_pixels": list(struct.unpack_from("<4h", blob, a + 0x18)), "cut_rank": blob[a + 0x20]})
    raster = [list(struct.unpack_from("<" + "H" * width, blob, RASTER_AT + y * ROW_WORDS * 2)) for y in range(height)]
    split_count = (u("<I", 8) - base - SPLITS_AT) // 6
    splits = [list(struct.unpack_from("<3H", blob, SPLITS_AT + i * 6)) for i in range(split_count)]
    return {"width": width, "height": height, "regions": rs, "raster": raster, "splits": splits}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=(Path.home() / "koprulu-private/emu/states"))
    args = ap.parse_args()
    names = [line.split(" ") for line in (ROOT / "generated/missions/index.txt").read_text().split("\n") if line]
    maps = {}
    for d in sorted(p for p in args.root.iterdir() if (p / "states" / "s00001.state").exists()):
        st = State(d / "states" / "s00001.state")
        mission = names[st.header["mission"] - 1]
        name, width, height = mission[0], int(mission[2]), int(mission[3])
        snap = snapshot(st.blob("regions"), st.blob_base("regions"), width, height)
        encoded = json.dumps(snap, separators=(",", ":")).encode()
        maps[name] = {"snapshot_sha256": hashlib.sha256(encoded).hexdigest(), "region_count": len(snap["regions"]),
                      "split_count": len(snap["splits"])}
    OUT.write_text(json.dumps({
        "description": "Region graphs built in the game at mission start (CreateRegions 0x490d80 after "
                       "LoadTileMapSection 0x469170 blocks the console strips), from full-game emulator state "
                       "dumps; snapshot layout of probe_native_campaign_regions_117.py.",
        "tool": "tools/research/ingame_regions.py", "maps": dict(sorted(maps.items()))}, indent=1) + "\n")
    print(f"{len(maps)} maps -> {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
