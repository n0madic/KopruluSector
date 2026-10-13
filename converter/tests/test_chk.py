"""CHK reader: synthetic boundary cases, and the campaign corpus when KOPRULU_ORIGINAL is set."""
from __future__ import annotations

import hashlib
import json
import os
import struct
import unittest
from pathlib import Path

from converter.catalog import EXTRA_SCENARIOS, MAPDATA_DAT, MAPDATA_TBL, decode_catalog
from converter.formats import chk
from converter.kpf.fixtures import V59, build_chk as build

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_original = unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
TABLES = chk.LoaderTables.load()


def load(data: bytes, **kw) -> chk.Scenario:
    return chk.load(data, TABLES, vcod=b"vcod", **kw)


PAYLOAD_RULES = [
    ("DIM ", bytes(3), "exactly 4"),
    ("FORC", bytes(21), "at most 20"),
    ("MRGN", bytes(0x13EC), "exactly 1280"),  # version 59 uses the 64-location loader
    ("TRIG", bytes(2401), "multiple of 2400"),
    ("UNIT", bytes(35), "multiple of 36"),
    ("MTXM", bytes(0x20001), "at most 131072"),
    ("VCOD", b"other", "VCOD resource"),
]


class ChkTest(unittest.TestCase):
    def test_catalog_skips_null_and_string_zero(self):
        tbl = struct.pack("<3H", 2, 6, 11) + b"null\0" + b"campaign\\terran\\terran01\0"
        entries = decode_catalog(struct.pack("<4I", 1, 0, 2, 0), tbl)
        assert [(e.index, e.directory, e.name) for e in entries] == [(2, "campaign\\terran\\terran01", "terran/terran01")]

    def test_minimal_v59(self):
        s = load(build(V59))
        assert s.version == 59
        assert list(s.sections) == list(V59)
        assert s.u16("DIM ", 1) == 64

    def test_ignored_tags_are_kept_in_order(self):
        s = load(build([("TYPE", b"RAWS"), *V59.items(), ("ISOM", b"x"), ("WAV ", b"")]))
        assert s.ignored == ("TYPE", "ISOM", "WAV ")

    def test_payload_rules(self):
        for tag, payload, message in PAYLOAD_RULES:
            with self.subTest(tag=tag, message=message), self.assertRaisesRegex(chk.ChkError, message):
                load(build({**V59, tag: payload}))

    def test_missing_required(self):
        sections = dict(V59)
        del sections["UPRP"]
        with self.assertRaisesRegex(chk.ChkError, "missing required section 'UPRP'"):
            load(build(sections))

    def test_optional_expansion_overrides_in_v59(self):
        s = load(build({**V59, "UNIx": bytes(0x1048)}))
        assert "UNIx" in s.sections

    def test_unknown_and_unread_tags(self):
        with self.assertRaisesRegex(chk.ChkError, "'ABCD' is neither read"):
            load(build({**V59, "ABCD": b""}))
        # COLR is read only by the version 205 tables.
        with self.assertRaisesRegex(chk.ChkError, "'COLR' is neither read"):
            load(build({**V59, "COLR": bytes(8)}))

    def test_duplicate_read_section_rejected(self):
        with self.assertRaisesRegex(chk.ChkError, "duplicate section 'DIM '"):
            load(build([*V59.items(), ("DIM ", struct.pack("<HH", 128, 128))]))

    def test_versions(self):
        with self.assertRaisesRegex(chk.ChkError, "version 57 has no native loader descriptor"):
            load(build({**V59, "VER ": struct.pack("<H", 57)}))
        v205 = {k: v for k, v in V59.items() if k not in ("UNIS", "UPGS", "TECS", "UPGR", "PTEC")}
        v205.update({"VER ": struct.pack("<H", 205), "UNIx": bytes(0x1048), "UPGx": bytes(0x31A), "TECx": bytes(0x18C),
                     "PUPx": bytes(0x90E), "PTEx": bytes(0x688), "MRGN": bytes(0x13EC), "COLR": bytes(8)})
        assert load(build(v205)).version == 205
        with self.assertRaisesRegex(chk.ChkError, "requires Brood War"):
            load(build(v205), expansion=False)

    def test_era_gate(self):
        data = build({**V59, "ERA ": struct.pack("<H", 5)})
        assert load(data).u16("ERA ") == 5  # raw value; the runtime masks it with 7
        with self.assertRaisesRegex(chk.ChkError, "tileset 5 requires Brood War"):
            load(data, expansion=False)

    def test_enumeration_matches_native(self):
        # A trailing fragment shorter than a chunk header is not enumerated.
        data = build(V59) + b"abc"
        assert [c.tag for c in chk.enumerate_chunks(data)] == list(V59)
        # A truncated payload is enumerated; reading it fails only when the tag is read.
        trailing = b"ISOM" + struct.pack("<I", 100) + b"short"
        assert load(build(V59) + trailing).ignored == ("ISOM",)
        with self.assertRaisesRegex(chk.ChkError, "runs past the end"):
            load(build({k: v for k, v in V59.items() if k != "TRIG"}) + b"TRIG" + struct.pack("<I", 2400))
        with self.assertRaisesRegex(chk.ChkError, "negative size"):
            chk.enumerate_chunks(b"ISOM" + struct.pack("<i", -8))

    @needs_original
    def test_tables_match_executable(self):
        from converter.pe import PeImage

        pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
        raw = TABLES.raw
        names = ("header", "briefing", "melee", "ums")
        base = int(raw["descriptor_table_va"], 16)
        for i, d in enumerate(raw["descriptors"]):
            va = int(d["va"], 16)
            assert va == base + 40 * i
            fields = struct.unpack("<10I", pe.read_va(va, 40))
            assert fields[0] == d["version"] and fields[9] == int(d["expansion_required"])
            for g, name in enumerate(names):
                table_va, count = fields[1 + 2 * g], fields[2 + 2 * g]
                group = d["groups"][name]
                assert table_va == int(group["table_va"], 16) and count == len(group["rows"])
                for r, (tag, handler, required) in enumerate(group["rows"]):
                    t, h, req = struct.unpack("<4sII", pe.read_va(table_va + 12 * r, 12))
                    assert (t.decode("latin1"), h, req) == (tag, int(handler, 16), int(required))
        probe = raw["version_probe"]
        for r, (tag, handler, required) in enumerate(probe["rows"]):
            t, h, req = struct.unpack("<4sII", pe.read_va(int(probe["table_va"], 16) + 12 * r, 12))
            assert (t.decode("latin1"), h, req) == (tag, int(handler, 16), int(required))



@needs_original
class ChkCorpusTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        from converter.mpq.profile import Resolver
        from converter.pe import PeImage

        pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
        vcod = pe.resource(*TABLES.vcod_resource)
        with Resolver(ORIGINAL, full_hash=False) as r:
            catalog = decode_catalog(r.read(MAPDATA_DAT)[1], r.read(MAPDATA_TBL)[1])
            maps = {e.directory: r.read(e.chk_path)[1] for e in catalog}
            extras = {d: r.read(d + "\\staredit\\scenario.chk")[1] for d in EXTRA_SCENARIOS}
        cls.corpus = catalog, maps, extras, vcod

    def test_campaign_corpus(self):
        catalog, maps, extras, vcod = self.corpus
        manifest = json.loads((ROOT / "docs/research/evidence/retail-manifest.json").read_text())
        hashes = {m["path"]: m["sha256"] for m in manifest["campaign_found"]}
        assert len(catalog) == 64
        versions = {}
        for entry in catalog:
            data = maps[entry.directory]
            assert hashlib.sha256(data).hexdigest() == hashes[entry.chk_path]
            s = chk.load(data, TABLES, vcod=vcod)
            versions[s.version] = versions.get(s.version, 0) + 1
        assert versions == {59: 31, 205: 33}
        for directory, data in extras.items():
            with self.assertRaisesRegex(chk.ChkError, "version 57"):
                chk.load(data, TABLES, vcod=vcod)
