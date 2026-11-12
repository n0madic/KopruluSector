"""Dependency analyzer: AI and IScript closures, trigger decoding, recipes, mission profiles."""
from __future__ import annotations

import json
import os
import struct
import unittest
from collections import Counter
from pathlib import Path

from scdata.analyze import ai, iscript, profile, triggers
from scdata.tests.synthetic import chk_trigger

ROOT = Path(__file__).resolve().parents[4]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_original = unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
EVIDENCE = ROOT / "docs/research/evidence"


def _ai_bin(code: bytes) -> bytes:
    return struct.pack("<I", 4 + len(code)) + code + b"T1  " + struct.pack("<I", 4) + bytes(8) + bytes(4)


class AnalyzeTest(unittest.TestCase):
    def test_ai_closure_rules(self):
        t = ai.Table.load()
        op = {o.name: o for o in t.opcodes.values()}

        def ins(name: str, *values: int) -> bytes:
            o = op[name]
            return bytes([o.code]) + b"".join(v.to_bytes(w, "little") for v, (_, w) in zip(values, o.operands))

        # Layout: jump -> wait -> stop | dead wait | sub: call(ret) -> stop | ret: wait -> return
        base = 4
        jump, wait, stop = ins("random_jump", 255, 0), ins("wait", 1), ins("stop")
        dead = ins("wait", 0)
        sub = base + len(jump) + len(wait) + len(stop) + len(dead)
        call = ins("call", 0)
        ret = sub + len(call) + len(stop)
        code = ins("random_jump", 255, sub) + wait + stop + dead + ins("call", ret) + stop + ins("wait", 0) + ins("return")
        data = _ai_bin(code)
        c = ai.closure(t, (data, data), [(0, base)])
        pcs = sorted(pc for _, pc in c.instructions)
        dead_pc = base + len(jump) + len(wait) + len(stop)
        assert dead_pc not in pcs
        assert pcs == [base, base + len(jump), base + len(jump) + len(wait), sub, sub + len(call), ret, ret + 3]
        with self.assertRaises(ai.AiError):
            ai.decode(t, bytes([0x71]), 0)  # beyond the native dispatcher

    def test_trigger_decoding(self):
        trig = chk_trigger([(triggers.CREATE_UNIT, 0), (triggers.RUN_AI_SCRIPT, 0)])
        rec = bytearray(trig)
        struct.pack_into("<H", rec, 320 + 0x18, 7)  # create unit 7
        struct.pack_into("<I", rec, 320 + 32 + 0x14, int.from_bytes(b"Ter5", "little"))
        rec[0x0F] = 22  # condition Always
        t = triggers.decode(bytes(rec))
        assert triggers.action_histogram(t) == Counter({44: 1, 15: 1})
        assert triggers.condition_histogram(t) == Counter({22: 1})
        assert ai.script_id(triggers.ai_launches(t)[0].number) == b"Ter5"
        assert t[0].actions[0].unit == 7

    def test_production_closure(self):
        ctx = type("Ctx", (), {})()
        ctx.recipes = {0: {111}, 111: {7}, 7: {106}, 106: {7}, 32: {111}}
        out = profile.production_closure(ctx, {106}, available={0, 7, 111, 106})
        assert set(out) == {7, 111, 0}  # 32 (Firebat) is not available
        assert set(profile.production_closure(ctx, {131}, available=set())) == {35, 36}  # larva and egg

    @needs_original
    def test_recipes_match_executable(self):
        from scdata.pe import PeImage

        raw = json.loads((ROOT / "defs/requirements/bw-1.17.0.1.json").read_text())
        pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
        start, end = int(raw["table_va"], 16), int(raw["end_va"], 16)
        words = [0]
        for r in raw["recipes"]:
            words += [r["unit"], *(int(w, 16) for w in r["words"]), 0xFFFF]
        words.append(0xFFFF)
        assert pe.read_va(start, end - start) == struct.pack(f"<{len(words)}H", *words)
        assert len(raw["recipes"]) == 91

    @needs_original
    def test_ai_closure_matches_inventory(self):
        from scdata.mpq.profile import Resolver

        inv = json.loads((EVIDENCE / "ai-inventory.json").read_text())["campaign_scopes"]["catalog"]
        t = ai.Table.load()
        with Resolver(ORIGINAL) as r:
            bins = (r.read("scripts\\aiscript.bin")[1], r.read("scripts\\bwscript.bin")[1])
        total = ai.Closure()
        for sid, row in inv["roots"].items():
            c = ai.closure(t, bins, [ai.resolve(*bins, sid.encode("latin1"))])
            assert len(c.instructions) == row["reachable_instruction_count_upper_bound"], sid
            assert sorted(c.opcode_names()) == sorted(row["opcode_types"]), sid
            total.instructions.update(c.instructions)
        assert len(total.instructions) == inv["reachable_instructions_upper_bound"] == 11497
        counts = Counter(i.opcode.name for i in total.instructions.values())
        assert dict(counts) == inv["reachable_opcode_counts_upper_bound"]

    @needs_original
    def test_triggers_match_manifest(self):
        from scdata.formats import chk
        from scdata.mpq.profile import Resolver

        manifest = json.loads((EVIDENCE / "retail-manifest.json").read_text())
        with Resolver(ORIGINAL) as r:
            for m in manifest["campaign_found"]:
                data = r.read(m["path"])[1]
                trig = next(data[c.payload_offset : c.payload_offset + c.size] for c in chk.enumerate_chunks(data)
                            if c.tag == "TRIG")
                t = triggers.decode(trig)
                assert len(t) == m["TRIG_count"]
                assert {str(k): v for k, v in triggers.condition_histogram(t).items()} == m["condition_types"]
                assert {str(k): v for k, v in triggers.action_histogram(t).items()} == m["action_types"]
                calls = [{"id": ai.script_id(a.number).decode("latin1"), "location": a.location, "action": a.kind,
                          "action_flags": a.flags, "trigger_index": a.trigger, "action_index": a.index,
                          "execution_flags": t[a.trigger].execution_flags, "owners": list(t[a.trigger].owners)}
                         for a in triggers.ai_launches(t)]
                assert calls == m["ai_calls"], m["path"]

    @needs_original
    def test_iscript_closure_matches_inventory(self):
        from scdata.formats import scripts
        from scdata.mpq.profile import Resolver

        ev = json.loads((EVIDENCE / "iscript-sounds.json").read_text())
        t = iscript.Table.load()
        with Resolver(ORIGINAL) as r:
            data = r.read("scripts\\iscript.bin")[1]
        starts = [p for _, h in scripts.iscript_headers(data) for p in iscript.entry_points(t, data, h)]
        c = iscript.walk(t, data, starts)
        assert (len(starts), len(c.pcs)) == (ev["headers_entry_points"], ev["instructions_visited"])
        # The inventory counts operand ids; playsndbtwn plays every id of its range as well.
        assert {int(k) for k in ev["sounds"]} <= c.refs["sound"]

    @needs_original
    def test_m1_choice(self):
        from scdata.catalog import MAPDATA_DAT, MAPDATA_TBL, decode_catalog
        from scdata.formats import chk
        from scdata.mpq.profile import Resolver

        with Resolver(ORIGINAL) as r:
            read = lambda p: r.read(p)[1]  # noqa: E731
            ctx = profile.Context.load(read, r.has)
            tables = chk.LoaderTables.load()
            profiles = [profile.build(e.name, chk.load(read(e.chk_path), tables, vcod_sha256=None), ctx)
                        for e in decode_catalog(read(MAPDATA_DAT), read(MAPDATA_TBL))]
        assert min(profiles, key=lambda p: (p.size(), p.mission)).mission == "terran/terran01"  # the smallest dependency set
        assert len(set().union(*(p.ai_opcodes for p in profiles))) == 64
        assert len(set().union(*(p.ai_roots for p in profiles))) == 173
        t1 = next(p for p in profiles if p.mission == "terran/terran01")
        assert sorted(t1.packs) == ["doodads", "effects", "neutral", "terran", "zerg"]
