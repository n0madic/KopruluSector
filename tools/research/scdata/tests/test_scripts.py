"""IScript and AI script tables: native lookup rules, iscript.kpd/aiscript.kpd, original data."""
from __future__ import annotations

import json
import os
import struct
import unittest
from pathlib import Path

from scdata.formats import scripts
from scdata.kpf import container as kpf
from scdata.kpf import kpd
from scdata.tests.synthetic import AI_RECORDS, BW_RECORDS, ai_bin, iscript_bin

ROOT = Path(__file__).resolve().parents[4]
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

    def test_ai_operand_section(self):
        """AIOP (kp_aiscript_check's table): every ordinal from 0 with its terminator flag and operand widths."""
        from scdata.analyze import ai

        table = ai.Table.load()
        data = ai.section(table)
        assert data[0] == len(table.opcodes) == 0x71
        at = 1
        for code in range(data[0]):
            op = table.opcodes[code]
            flags, count = data[at], data[at + 1]
            assert bool(flags & ai.TERMINATOR) == (op.name in table.terminators), op.name
            widths = data[at + 2 : at + 2 + count]
            assert [w & 0x7F for w in widths] == [w for _, w in op.operands], op.name
            assert [i for i, w in enumerate(widths) if w & ai.ADDRESS] == list(op.targets), op.name
            at += 2 + count
        assert at == len(data)

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_original_scripts(self):
        from scdata.mpq.profile import Resolver

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

    def test_ai_patch(self):
        from scdata.analyze import ai_asm
        from scdata.analyze.ai import AiError

        code = bytearray(ai_bin([(b"Mele", 4), (b"Othr", 8)], scripts.AI_RECORD, code_size=8))
        code[4] = 0x03  # Mele's entry starts with start_town
        base, bw, n = bytes(code), ai_bin([], scripts.BW_RECORD), len(code)
        source = 'stub Mele probe\nprobe:\n    debug shown "hi"  # the text inline\nshown:\n    region_size 2 shown\n    stop\n'
        patch = ai_asm.assemble(source, base, "t")
        out = ai_asm.apply(base, patch)
        assert patch["redirect"] == [["Mele", n]] and len(out) == n + len(patch["code"]) // 2
        probe, shown = n + 7, n + 7 + 6
        # the stub: start_town, multirun probe, goto the entry's next instruction
        assert out[n:probe] == bytes([0x03, 0x48]) + probe.to_bytes(2, "little") + bytes([0x00]) + (5).to_bytes(2, "little")
        assert out[probe:shown] == bytes([0x27]) + shown.to_bytes(2, "little") + b"hi\0"
        assert out[shown:] == bytes([0x3A, 2]) + shown.to_bytes(2, "little") + bytes([0x24])
        assert scripts.resolve_ai(out, bw, b"Mele") == (n, 0)
        assert scripts.resolve_ai(out, bw, b"Othr") == (8, 0)
        with self.assertRaisesRegex(AiError, "not the aiscript.bin"):
            ai_asm.apply(base[:-1] + b"\1", patch)
        for bad, why in (("stub Othr x\nx:\n stop", "does not start with start_town"),
                         ("stub Nope x\nx:\n stop", "no script"), ("goto nowhere", "no label"),
                         ("jump 1", "no opcode"), ("wait", "takes 1 operand"), ("x:\nx:\n stop", "defined twice"),
                         ("region_size 256 0", "does not fit"),
                         ("wait 12O", "line 1: wait .*'12O' is not an integer"),
                         ('debug x "ca\u0161"\nx:\n stop', "line 1: debug: .* outside Latin-1"),
                         ('stub M\u0161le x\nx:\n stop', "line 1: stub: .* outside Latin-1"),
                         ('debug x "open', "line 1: No closing quotation")):
            with self.assertRaisesRegex(AiError, why):
                ai_asm.assemble(bad, base, "bad")
        # a directory record whose entry lies past the file
        far = bytearray(base)
        othr = int.from_bytes(far[0:4], "little") + scripts.AI_RECORD
        far[othr + 4:othr + 8] = (len(far) + 100).to_bytes(4, "little")
        with self.assertRaisesRegex(AiError, "aiscript.bin's directory: .*outside the file"):
            ai_asm.assemble("stub Othr x\nx:\n stop", bytes(far), "far")

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_melee_opcodes_probe(self):
        """tests/ai/melee-opcodes.aiasm over the install's aiscript.bin: every melee script starts the probe, whose
        closure holds the eleven opcodes no stock script reaches and no fatal_error."""
        from scdata.analyze import ai, ai_asm
        from scdata.mpq.profile import Resolver

        with Resolver(ORIGINAL) as r:
            base, bw = r.read(kpd.AISCRIPT_FILE)[1], r.read(kpd.BWSCRIPT_FILE)[1]
        patch = ai_asm.assemble((ROOT / "tests/ai/melee-opcodes.aiasm").read_text(), base, "melee-opcodes")
        out = ai_asm.apply(base, patch)
        assert len(out) <= ai_asm.ADDRESS_LIMIT
        ids = [sid for sid, _ in patch["redirect"]]
        assert sorted(ids) == sorted(f"{r}MC{v}" for r in "TZP" for v in "xu")
        table = ai.Table.load()
        names = ai.closure(table, (out, bw), [ai.resolve(out, bw, sid.encode()) for sid in ids]).opcode_names()
        probed = {"wait_secure", "build_bunkers", "wait_bunkers", "debug", "build_turrets", "wait_turrets",
                  "harass_factor", "region_size", "eval_harass", "wait_upgrades", "implode"}
        assert probed <= names and "fatal_error" not in names, probed - names
