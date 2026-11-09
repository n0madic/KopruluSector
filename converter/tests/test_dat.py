"""DAT layouts, dat.kpd and strings.kpd; original-data checks when KOPRULU_ORIGINAL is set."""
from __future__ import annotations

import hashlib
import json
import os
import struct
import unittest
from pathlib import Path

from converter.formats import dat
from converter.kpf import container as kpf
from converter.kpf import kpd
from converter.tests.private import needs

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_original = unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
LAYOUTS = dat.load_layouts()
UNITS = next(f for f in LAYOUTS if f.table == "units")


def _filled(layout: dat.DatFile) -> bytes:
    return bytes(i % 251 for i in range(layout.size))


class DatTest(unittest.TestCase):
    @needs(ROOT / "docs/research/evidence/dat-schemas.json")
    def test_layout_sizes_match_research(self):
        evidence = json.loads((ROOT / "docs/research/evidence/dat-schemas.json").read_text())
        sizes = {f["file"]: f["expected_bytes"] for f in evidence["files"]}
        assert {f.path: f.size for f in LAYOUTS} == sizes
        assert len({f.fourcc for f in LAYOUTS}) == len(LAYOUTS) == 11

    def test_subranges(self):
        data = _filled(UNITS)
        c = UNITS.column("infestation")
        assert dat.value(UNITS, data, "infestation", 106) == struct.unpack_from("<H", data, c.offset)[0]
        with self.assertRaises(IndexError):
            dat.value(UNITS, data, "infestation", 105)
        ext = UNITS.column("unit_extents")
        assert dat.value(UNITS, data, "unit_extents", 1, 3) == struct.unpack_from("<H", data, ext.offset + 2 * 7)[0]
        with self.assertRaises(IndexError):
            dat.value(UNITS, data, "ready_sound", 106)  # sound columns cover units 0-105
        with self.assertRaises(dat.DatError):
            dat.values(UNITS, data[:-1], "graphics")

    def test_dat_kpd_round_trip(self):
        files = {f.path: _filled(f) for f in LAYOUTS}
        c = kpf.parse(kpd.build_dat(files, LAYOUTS, profile_id="p", data_set_hash=bytes(32)).to_bytes())
        assert c.kind == kpd.DAT_KIND
        for f in LAYOUTS:
            assert c.section(f.fourcc).payload == files[f.path]
        cols = c.section(kpd.COLUMNS).payload
        assert struct.unpack_from("<H", cols, 0)[0] == len(LAYOUTS)
        with self.assertRaises(dat.DatError):
            kpd.build_dat({**files, UNITS.path: b"x"}, LAYOUTS, profile_id="p", data_set_hash=bytes(32))

    @needs_original
    def test_layouts_match_executable(self):
        from converter.pe import PeImage

        pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
        for f in LAYOUTS:
            for i, c in enumerate(f.columns):
                assert struct.unpack("<3I", pe.read_va(f.descriptor_va + 12 * i, 12)) == (
                    c.destination_va, c.element_size, c.count), f"{f.path} column {i}"
            assert struct.unpack("<I", pe.read_va(f.descriptor_va + 12 * len(f.columns), 4))[0] == 0

    @needs_original
    def test_effective_files(self):
        from converter.mpq.profile import Resolver

        evidence = json.loads((ROOT / "docs/research/evidence/dat-schemas.json").read_text())
        hashes = {f["file"]: f["actual_sha256"] for f in evidence["files"]}
        with Resolver(ORIGINAL) as r:
            for f in LAYOUTS:
                data = r.read(f.path)[1]
                dat.check(f, data)
                assert hashlib.sha256(data).hexdigest() == hashes[f.path]
            # Spot values from the research: Marine (0) 40 HP stored as 40 * 256; Command Center (106) 1500.
            units = r.read(UNITS.path)[1]
            assert dat.value(UNITS, units, "hit_points", 0) == 40 * 256
            assert dat.value(UNITS, units, "hit_points", 106) == 1500 * 256
            strings = {p: r.read(p)[1] for p in kpd.TBL_FILES.values()}
        c = kpd.build_strings(strings, profile_id="p", data_set_hash=bytes(32))
        stat = c.section("STAT").payload
        assert struct.unpack_from("<H", stat, 0)[0] == 1547  # entry count (native text research)
        assert stat == kpd.table_utf8(strings["rez\\stat_txt.tbl"])

    def test_text_becomes_utf8(self):
        assert kpd.text_utf8(b"plain") == b"plain"
        assert kpd.text_utf8(b"\xa9 1998") == "\u00a9 1998".encode()
        assert kpd.text_utf8(b"it\x92s") == "it\u2019s".encode()
        assert kpd.text_utf8("\u0443\u0436\u0435".encode()) == "\u0443\u0436\u0435".encode(), "UTF-8 stays"
        assert kpd.text_utf8(b"\x81") == "\x81".encode(), "undefined in Windows: its C1 control, as kp_text_from_byte"

    def test_table_becomes_utf8(self):
        body = b"a\0it\x92s\0end\0"
        header = 2 + 2 * 3
        table = struct.pack("<4H", 3, header, header + 2, header + 7) + body
        utf8 = kpd.table_utf8(table)
        assert utf8[header:] == "a\0it\u2019s\0end\0".encode()
        assert struct.unpack_from("<4H", utf8, 0) == (3, header, header + 2, header + 9), "the offsets past it moved"
        ascii_only = struct.pack("<2H", 1, 4) + b"x\0"
        assert kpd.table_utf8(ascii_only) == ascii_only
        already = struct.pack("<2H", 1, 4) + "it\u2019s\0".encode()
        assert kpd.table_utf8(already) == already, "a table in UTF-8 stays"
        past_end = struct.pack("<3H", 2, 6, 6 + 3 + 1) + b"\x92\0a"  # the second row's offset is 1 past the end
        grown = kpd.table_utf8(past_end)
        assert grown[6:] == "\u2019\0a".encode()
        assert struct.unpack_from("<3H", grown, 0) == (2, 6, len(grown) + 1), "still past the end"

    def test_requirement_sections_round_trip(self):
        files = {f.path: _filled(f) for f in LAYOUTS}
        words = {"RQTU": struct.pack("<5H", 0, 3, 0xFF02, 0xFFFF, 0xFFFF)}
        c = kpf.parse(kpd.build_dat(files, LAYOUTS, profile_id="p", data_set_hash=bytes(32), requirements=words).to_bytes())
        assert c.section("RQTU").payload == words["RQTU"]

    def test_requirement_tables_in_defs(self):
        from converter.formats import requirements

        # Record counts of the research report (docs/research/native-dat-requirements.md) and of
        # the unit production table (docs/research/native-requirements.md).
        expected = {"RQOR": 112, "RQRS": 26, "RQTU": 33, "RQUP": 83, "RQUN": 91}
        for table in requirements.load_tables():
            assert table.words[0] == 0 and table.words[-1] == requirements.END, table.fourcc
            assert len(requirements.record_ids(list(table.words))) == expected[table.fourcc], table.fourcc

    @needs_original
    def test_requirement_tables_from_executable(self):
        from converter.formats import requirements
        from converter.pe import PeImage

        pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
        # Record counts of the research report (docs/research/native-dat-requirements.md) and of
        # the unit production table (docs/research/native-requirements.md).
        expected = {"RQOR": 112, "RQRS": 26, "RQTU": 33, "RQUP": 83, "RQUN": 91}
        for table in requirements.load_tables():
            words = requirements.words(pe, table)
            assert tuple(words) == table.words, f"{table.fourcc}: defs/ words differ from the executable's"
            assert words[0] == 0 and words[-1] == requirements.END
            assert len(requirements.record_ids(words)) == expected[table.fourcc], table.fourcc
        upgrades = next(t for t in requirements.load_tables() if t.fourcc == "RQUP")
        ids = requirements.record_ids(requirements.words(pe, upgrades))
        assert len([i for i in ids if i < upgrades.capacity]) == 51
