"""Map string and TBL lookup (`0x4c13b0`) against all native lookup cases."""
from __future__ import annotations

import hashlib
import json
import os
import unittest
from pathlib import Path

from scdata.kpf import kpm

ROOT = Path(__file__).resolve().parents[4]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
PROBE_TEXT_BASE = 0x820000  # where tools/research/probe_native_text_117.py loads the table
EMPTY_STRING = 0x4EC14E  # the executable's empty string, returned past the count


def native_pointer(table: bytes, index: int) -> int:
    """The pointer `0x4c13b0` returns, expressed through the converter's lookup."""
    result = kpm.map_string(table, index)
    if result is None:
        return 0
    low = index & 0xFFFF
    count = int.from_bytes(table[:2], "little")
    if low > count:
        assert result == b""
        return EMPTY_STRING
    offset = int.from_bytes(table[2 * low : 2 * low + 2], "little")
    assert table[offset : offset + len(result)] == result
    return PROBE_TEXT_BASE + offset


class StringsTest(unittest.TestCase):
    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_native_lookups(self):
        from scdata.formats import chk
        from scdata.mpq.profile import Resolver

        cases = [c for c in json.loads((ROOT / "docs/research/evidence/native-text-probes.json").read_text())["cases"]
                 if c["case"] == "complete_native_tbl_or_campaign_str_lookup"]
        tables: dict[str, bytes] = {}
        with Resolver(ORIGINAL) as r:
            for c in cases:
                resource = c["inputs"]["resource"]
                if resource in tables:
                    continue
                if resource.endswith("#STR0"):
                    data = r.read(resource.removesuffix("#STR0").replace("/", "\\"))[1]
                    tables[resource] = next(data[k.payload_offset : k.payload_offset + k.size]
                                            for k in chk.enumerate_chunks(data) if k.tag == "STR ")
                else:
                    tables[resource] = r.read(resource)[1]
        assert len(cases) == 10395
        for c in cases:
            table = tables[c["inputs"]["resource"]]
            assert hashlib.sha256(table).hexdigest() == c["inputs"]["table_sha256"]
            assert native_pointer(table, c["inputs"]["index"]) == c["actual"]["pointer"], c["inputs"]
