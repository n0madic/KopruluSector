"""Mission container writer: synthetic round trip, and the campaign corpus with original data."""
from __future__ import annotations

import os
import struct
import tempfile
import unittest
from pathlib import Path

from converter import provenance
from converter.formats import chk
from converter.kpf import container as kpf
from converter.kpf import dump, kpm
from converter.kpf.fixtures import V59, build_chk as build, chk_strings, chk_trigger, fixture_tileset, fixture_wav

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
DATA_SET = bytes(range(32))
TILESET = fixture_tileset()[0], fixture_tileset()[3]
TABLES = chk.LoaderTables.load()


def _scenario(**overrides: bytes) -> chk.Scenario:
    sections = {**V59, "DIM ": struct.pack("<HH", 8, 4), "MTXM": bytes(64), "MASK": bytes(32),
                "STR ": chk_strings(b"name", b"a.wav", b"b.wav"), **overrides}
    return chk.load(build(sections), TABLES, vcod=b"vcod")


class KpmTest(unittest.TestCase):
    def test_map_string(self):
        strs = chk_strings(b"one", b"two")
        assert kpm.map_string(strs, 0) is None
        assert kpm.map_string(strs, 2) == b"two"
        assert kpm.map_string(strs, 3) == b""  # past the count: the native empty string

    def test_wav_ids_and_sounds(self):
        trig = chk_trigger([(8, 2), (7, 0x10003), (9, 1)])  # Play WAV, Transmission (low word), Display Text
        mbrf = chk_trigger([(2, 2), (8, 0)])
        s = _scenario(TRIG=trig, MBRF=mbrf)
        assert kpm.wav_string_ids(s.sections) == [2, 3]
        sounds = kpm.mission_sounds(s, "campaign\\x", lambda p: fixture_wav(4410) if p.endswith("a.wav") else None)
        assert [(x.string_id, x.path, x.exists, x.data_length, x.avg_bytes_per_sec) for x in sounds] == [
            (2, "campaign\\x\\a.wav", True, 4410, 44100), (3, "campaign\\x\\b.wav", False, 0, 0)]
        # A file the native WAV parser rejects (no fmt chunk) plays nothing, like a missing one.
        rejected: list[str] = []
        sounds = kpm.mission_sounds(s, "campaign\\x", lambda p: b"RIFF\0\0\0\0WAVE", rejected)
        assert [x.exists for x in sounds] == [False, False] and rejected == ["campaign\\x\\a.wav", "campaign\\x\\b.wav"]

    def test_round_trip(self):
        s = _scenario(FORC=bytes(range(20)), TRIG=chk_trigger([(8, 2)]))
        sounds = kpm.mission_sounds(s, "campaign\\x", lambda p: fixture_wav(8))
        data = kpm.build(s, sounds, profile_id="bw-1.17.0.1", data_set_hash=DATA_SET, tileset=TILESET).to_bytes()
        c = kpf.parse(data)
        assert c.kind == kpm.KIND and c.source_hash == DATA_SET
        assert [x.fourcc.decode() for x in c.sections] == [
            "META", "MTXM", "MASK", "SPRT", "UNIT", "LOCS", "STRS", "TRIG", "MBRF", "UPRP", "RULE", "WAVS",
            "WALK", "RGNS", "CONT"]
        meta = kpm.META.unpack(c.section("META").payload)
        assert meta[:4] == (59, 0, 8, 4) and meta[6] == bytes(range(20)) and meta[10] == 0
        assert c.section("WAVS").payload == struct.pack("<H", 1) + kpm.WAVS_ENTRY.pack(2, 0, 8, 44100)
        assert "string 2 -> sound 0" in dump.dump(data)
        # Provenance: every defs/ table directory the conversion and the PACK analysis read.
        assert c.provenance_hash == provenance.provenance_hash("chk", "ai", "iscript", "requirements", "dat")
        assert all(list((provenance.DEFS_ROOT / d).glob("*.json")) for d in kpm.PROVENANCE)

    def test_rule_in_dispatch_order(self):
        # UNIx after UNIS in file order and in the v59 table; put it first in the file instead.
        items = [("UNIx", bytes(0x1048))] + list({**V59, "DIM ": struct.pack("<HH", 8, 4), "MTXM": bytes(64),
                                                   "MASK": bytes(32)}.items())
        s = chk.load(build(items), TABLES, vcod=b"vcod")
        rule = kpm.build(s, [], profile_id="p", data_set_hash=DATA_SET, tileset=TILESET).section("RULE").payload
        tags, pos = [], 0
        while pos < len(rule):
            tag, size = struct.unpack_from("<4sI", rule, pos)
            tags.append(tag.decode())
            pos += 8 + size
        assert tags == ["UNIS", "UPGS", "TECS", "PUNI", "UPGR", "PTEC", "UNIx"]

    def test_size_mismatch_rejected(self):
        with self.assertRaises(kpm.KpmError):
            kpm.build(_scenario(MASK=bytes(31)), [], profile_id="p", data_set_hash=DATA_SET, tileset=TILESET)

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_corpus_conversion_is_deterministic(self):
        from converter.missions import convert
        from converter.mpq.profile import Resolver

        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            with Resolver(ORIGINAL, full_hash=False) as r:
                first = convert(r, tmp_path / "a")
                convert(r, tmp_path / "b")
            assert len(first["missions"]) == 64 and len(first["rejected"]) == 4
            files = sorted(p.relative_to(tmp_path / "a") for p in (tmp_path / "a").rglob("*") if p.is_file())
            assert len(files) == 323  # 64 missions + 64 minimaps, index.txt, missions.json, analysis.json, 64 profiles,
            # 128 viewer overlays
            for f in files:
                assert (tmp_path / "a" / f).read_bytes() == (tmp_path / "b" / f).read_bytes()
            sounds = [s for m in first["missions"] for s in m["sounds"]]
            unreadable = "campaign\\protoss\\protoss10\\staredit\\wav\\PAB05ura.wav"
            assert first["unreadable_sounds"] == [unreadable]
            assert sounds and all(s["exists"] for s in sounds if s["path"] != unreadable)
