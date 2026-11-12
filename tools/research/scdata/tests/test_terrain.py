"""Terrain import against native outputs (docs/research/evidence/native-terrain-import-probes.json)."""
from __future__ import annotations

import hashlib
import json
import os
import struct
import unittest
from pathlib import Path

from scdata.derive import terrain
from scdata.formats import chk
from scdata.formats.palette import TILESETS
from scdata.kpf import container as kpf
from scdata.kpf import kpd

ROOT = Path(__file__).resolve().parents[4]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")


def _vf4(*words: int) -> bytes:
    return struct.pack("<16H", *words)


MEGATILE_FLAG_RULES = [
    ([1] * 13 + [0] * 3, terrain.FLAG_WALKABLE | terrain.FLAG_PARTIAL_WALK),  # walk count 13 > 12
    ([1] * 12 + [0] * 4, terrain.FLAG_UNWALKABLE | terrain.FLAG_PARTIAL_WALK),  # 12 is not > 12
    ([1] * 16, terrain.FLAG_WALKABLE),  # full walk: no partial flag
    ([0] * 16, terrain.FLAG_UNWALKABLE),
    ([4] * 12 + [0] * 4, terrain.FLAG_UNWALKABLE | terrain.FLAG_HIGH),
    ([6] * 6 + [0] * 10, terrain.FLAG_UNWALKABLE | terrain.FLAG_MIDDLE),  # middle + high counts sum to 12
    ([2] * 11 + [0] * 5, terrain.FLAG_UNWALKABLE),
    ([8] + [0] * 15, terrain.FLAG_UNWALKABLE | terrain.FLAG_VERY_HIGH),
    ([0xFFF0] * 16, terrain.FLAG_UNWALKABLE),  # bits above 0x8 are ignored
]


class TerrainTest(unittest.TestCase):
    def test_megatile_flag_rules(self):
        for words, flags in MEGATILE_FLAG_RULES:
            with self.subTest(words=words, flags=flags):
                assert terrain.megatile_flags(_vf4(*words)) == [flags]

    def test_import_high_bit_and_missing_group(self):
        cv5 = bytearray(52 * 2)
        struct.pack_into("<H", cv5, 52 + 2, 0xFFFF)
        struct.pack_into("<H", cv5, 52 + 20 + 3 * 2, 7)
        mflags = [0] * 8
        mflags[7] = terrain.FLAG_WALKABLE
        ids, flags = terrain.import_terrain(struct.pack("<H", 0x8000 | 0x13), 1, 1, bytes(cv5), mflags)
        assert ids == [7 | 0x8000]
        assert flags == [0xFFFF | (0xD8FA << 16) | terrain.FLAG_WALKABLE | terrain.FLAG_TILE_HIGH_BIT]
        with self.assertRaises(terrain.TerrainError):
            terrain.import_terrain(struct.pack("<H", 0x20), 1, 1, bytes(cv5), mflags)

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_native_outputs(self):
        from scdata.mpq.profile import Resolver

        evidence = json.loads((ROOT / "docs/research/evidence/native-terrain-import-probes.json").read_text())
        cases = {c["name"]: c for c in evidence["cases"]}
        tilesets = {}
        with Resolver(ORIGINAL) as r:
            for name in TILESETS:
                files = {ext: r.read(f"tileset\\{name}.{ext}")[1] for ext in kpd.TILESET_EXTENSIONS}
                c = kpf.parse(kpd.build_tileset(name, files, profile_id="p", data_set_hash=bytes(32)).to_bytes())
                mflags = c.section("MFLG").payload
                assert hashlib.sha256(mflags).hexdigest() == \
                    cases[f"retail_megatiles_{name}"]["observed"]["native_output_sha256"]
                tilesets[name] = (files["cv5"], list(struct.unpack(f"<{len(mflags) // 4}I", mflags)))
            maps = 0
            for name, case in cases.items():
                if not name.startswith("campaign_import_"):
                    continue
                data = r.read(name[len("campaign_import_"):])[1]
                sections = {}
                for c in chk.enumerate_chunks(data):  # the four VER=57 maps have no loader descriptor
                    sections.setdefault(c.tag, data[c.payload_offset : c.payload_offset + c.size])
                inp = case["inputs"]
                cv5, mflags = tilesets[inp["tileset"]]
                ids, flags = terrain.import_terrain(sections["MTXM"], inp["width"], inp["height"], cv5, mflags)
                assert hashlib.sha256(struct.pack(f"<{len(ids)}H", *ids)).hexdigest() == \
                    case["observed"]["native_megatile_ids_sha256"], name
                assert hashlib.sha256(struct.pack(f"<{len(flags)}I", *flags)).hexdigest() == \
                    case["observed"]["native_flags_sha256"], name
                maps += 1
        assert maps == 68
