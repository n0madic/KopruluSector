"""palettes.kpd: native table shapes and loader buffer sizes; original data when available."""
from __future__ import annotations

import json
import os
import struct
import unittest
from pathlib import Path

from converter.formats import pcx
from converter.kpf import container as kpf
from converter.kpf import kpd
from converter.kpf.fixtures import palettes_fixture

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")


class PalettesTest(unittest.TestCase):
    def test_pcx_round_trip(self):
        pixels = bytes((i * 37) % 256 for i in range(300 * 3))
        p = pcx.decode(pcx.encode(300, 3, pixels))
        assert (p.width, p.height, p.pixels) == (300, 3, pixels)

    def test_fixture_layout(self):
        c = kpf.parse(palettes_fixture(bytes(32)))
        pal0 = c.section("PAL0").payload
        rows = struct.unpack_from("<H", pal0, 0)[0]
        assert rows == 64 and pal0[2 : 2 + 256] == bytes(256)
        assert [s.fourcc for s in c.sections] == [f"PAL{i}".encode() for i in range(8)] + [b"TUNI", b"TMIN", b"TSEL", b"THPB"]
        assert len(c.section("TUNI").payload) == 0x60

    def test_short_pcx_rejected(self):
        with self.assertRaises(pcx.PcxError):
            kpd._pcx_prefix(pcx.encode(4, 1, bytes(4)), 8, "tiny.pcx")

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_original_shapes(self):
        from converter.mpq.profile import Resolver

        shapes = json.loads((ROOT / "docs/research/evidence/native-pixel-blitters-probes.json").read_text())["palette_decoded_shapes"]
        with Resolver(ORIGINAL) as r:
            for path, (w, h) in shapes.items():
                p = pcx.decode(r.read(path)[1])
                assert (p.width, p.height) == (w, h), path
            c = kpd.build_palettes(lambda p: r.read(p)[1], profile_id="p", data_set_hash=bytes(32))
            tunit = pcx.decode(r.read("game\\tunit.pcx")[1]).pixels
        assert c.section("TUNI").payload == tunit[:0x60]
        for i in range(8):
            pal = c.section(f"PAL{i}").payload
            expected = sum(2 + 256 * (h + 1) for h in (63, 32, 40, 63, 255, 1, 1)) + kpd.DARK_SIZE + kpd.SHIFT_SIZE
            assert len(pal) == expected
