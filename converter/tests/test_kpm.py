"""The mission helpers of the overlays and the survey: map strings, WAV strings and sounds; the campaign survey with the
original data."""
from __future__ import annotations

import hashlib
import os
import struct
import unittest

from converter.formats import chk
from converter.kpf import kpm
from converter.kpf.fixtures import V59, build_chk as build, chk_strings, chk_trigger, fixture_wav

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
TABLES = chk.LoaderTables.load()


def _scenario(**overrides: bytes) -> chk.Scenario:
    sections = {**V59, "DIM ": struct.pack("<HH", 8, 4), "MTXM": bytes(64), "MASK": bytes(32),
                "STR ": chk_strings(b"name", b"a.wav", b"b.wav"), **overrides}
    return chk.load(build(sections), TABLES, vcod_sha256=hashlib.sha256(b"vcod").hexdigest())


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

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_corpus_survey(self):
        from converter.missions import load_catalog, survey
        from converter.mpq.profile import Resolver

        unreadable = "campaign\\protoss\\protoss10\\staredit\\wav\\PAB05ura.wav"
        with Resolver(ORIGINAL) as r:
            first = survey(r)
            assert first == survey(r) and len(first.packs) == 64 and len(first.rejected) == 4
            assert first.unreadable_sounds == [unreadable]
            sounds = [s for m in load_catalog(r) for s in m.sounds]
        assert sounds and all(s.exists for s in sounds if s.path != unreadable)
