"""IScript and AI script tables: native lookup rules, iscript.kpd/aiscript.kpd, original data."""
from __future__ import annotations

import json
import os
import struct
import unittest
from pathlib import Path

from converter.formats import scripts
from converter.kpf import container as kpf
from converter.kpf import kpd
from converter.kpf.fixtures import AI_RECORDS, BW_RECORDS, ai_bin, iscript_bin

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")


AI_RESOLUTION = [
    (b"Ter5", (16, 0)),  # AIScript wins
    (b"Redi", (24, 1)),  # zero AIScript offset redirects to BWScript
    (b"Both", (20, 0)),  # nonzero AIScript offset is preferred
    (b"BwOn", (28, 1)),  # BWScript only
    (b"Zero", None),  # BWScript offset 0
    (b"None", None),  # missing everywhere
]


class ScriptsTest(unittest.TestCase):
    def test_iscript_first_match_wins(self):
        data = iscript_bin([(5, 0), (3, 1), (5, 1)])
        assert scripts.iscript_headers(data) == [(5, 4), (3, 16)]
        c = kpf.parse(kpd.build_iscript(data, {12: 14, 0: 2}, profile_id="p", data_set_hash=bytes(32)).to_bytes())
        assert c.section("CODE").payload == data
        assert c.section("HDRS").payload == struct.pack("<5H", 2, 3, 16, 5, 4)
        assert c.section("SLOT").payload == bytes([2, 0, 2, 12, 14])

    def test_iscript_errors(self):
        data = iscript_bin([(5, 0)])
        with self.assertRaisesRegex(scripts.ScriptError, "terminator"):
            scripts.iscript_headers(data[:-4])
        with self.assertRaisesRegex(scripts.ScriptError, "SCPE"):
            scripts.iscript_headers(data.replace(b"SCPE", b"XXXX"))

    def test_ai_resolution(self):
        for sid, expected in AI_RESOLUTION:
            with self.subTest(sid=sid):
                assert scripts.resolve_ai(ai_bin(AI_RECORDS, 16), ai_bin(BW_RECORDS, 8), sid) == expected

    def test_ai_directory_errors(self):
        data = ai_bin(AI_RECORDS, 16)
        with self.assertRaisesRegex(scripts.ScriptError, "zero id"):
            scripts.ai_directory(data[:-4], 16)
        with self.assertRaisesRegex(scripts.ScriptError, "outside the file"):
            scripts.ai_directory(ai_bin([(b"Bad!", 10_000)], 16), 16)

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_original_scripts(self):
        from converter.mpq.profile import Resolver

        with Resolver(ORIGINAL) as r:
            isc = r.read(kpd.ISCRIPT_FILE)[1]
            ai, bw = r.read(kpd.AISCRIPT_FILE)[1], r.read(kpd.BWSCRIPT_FILE)[1]
        assert len(scripts.iscript_headers(isc)) == 398
        # Directory sizes from docs/research/campaigns.md: 293 AIScript ids, 156 BWScript ids.
        assert len(scripts.ai_directory(ai, scripts.AI_RECORD)) == 293
        assert len(scripts.ai_directory(bw, scripts.BW_RECORD)) == 156
        manifest = json.loads((ROOT / "docs/research/evidence/retail-manifest.json").read_text())
        launched = {call["id"] for m in manifest["campaign_found"] for call in m["ai_calls"]}
        assert len(launched) == 177
        for sid in launched:
            assert scripts.resolve_ai(ai, bw, sid.encode("latin1")), sid
