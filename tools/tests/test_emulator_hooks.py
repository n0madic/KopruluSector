"""The emulator's hook addresses against the original executable.

The logger's CRT draw filter (tools/research/sc_emulator/scenario.py) leaves out the CRT draws Koprulu makes itself:
the creep variant callback's and the start's music pick. Each address it names must be a `call` of CRT rand in
StarCraft.exe, or the filter silently logs a draw Koprulu also makes (the frame-0 music pick once reached the oracle
twice and shifted every later creep variant). The Play Custom driver's AI patch (custom.py) swaps aiscript.bin's
buffer at LoadAiScriptBins' `ret` and back at FreeAiScriptBins' entry. The constants are read from the source text, so
the test needs neither the emulator nor Unicorn.
"""
from __future__ import annotations

import os
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENARIO = ROOT / "tools" / "research" / "sc_emulator" / "scenario.py"
CUSTOM = ROOT / "tools" / "research" / "sc_emulator" / "custom.py"
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
CALL_SIZE = 5  # e8 rel32
CALL_OPCODE = 0xE8


def constants(source: Path = SCENARIO) -> dict[str, int]:
    """The hex constants of a module's top-level assignments, single or paired (`A, B = 0x1, 0x2`)."""
    out: dict[str, int] = {}
    for line in source.read_text().splitlines():
        m = re.match(r"^([A-Z_][A-Z0-9_, ]*?)\s*=\s*(0x[0-9A-Fa-f]+(?:\s*,\s*0x[0-9A-Fa-f]+)*)\s*(?:#.*)?$", line)
        if m:
            for name, value in zip(m[1].split(","), m[2].split(",")):
                out[name.strip()] = int(value, 16)
    return out


class EmulatorHooksTest(unittest.TestCase):
    def test_constants_are_read(self):
        c = constants()
        for name in ("CRT_RAND", "CRT_SRAND", "PICK_MUSIC_RAND", "CREEP_VARIANT", "CREEP_VARIANT_SIZE"):
            assert name in c, name

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_skipped_callers_call_crt_rand(self):
        sys.path.insert(0, str(ROOT / "tools" / "research"))
        from scdata.pe import PeImage

        c = constants()
        pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")

        def calls_rand(return_address: int) -> bool:
            code = pe.read_va(return_address - CALL_SIZE, CALL_SIZE)
            return code[0] == CALL_OPCODE and return_address + struct.unpack("<i", code[1:])[0] == c["CRT_RAND"]

        # the music pick of LoadConsoleSkinAndPickMusic 0x443680: one draw past the game's seed
        assert calls_rand(c["PICK_MUSIC_RAND"])
        # NewCreepTileVariant's three draws (rand() % 100, then % 7 or % 6) all lie in the skipped range
        start, size = c["CREEP_VARIANT"], c["CREEP_VARIANT_SIZE"]
        body = pe.read_va(start, size)
        returns = [start + i + CALL_SIZE for i in range(size - CALL_SIZE + 1)
                   if body[i] == CALL_OPCODE and calls_rand(start + i + CALL_SIZE)]
        assert len(returns) == 3, [hex(r) for r in returns]

    @unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")
    def test_ai_patch_hooks(self):
        sys.path.insert(0, str(ROOT / "tools" / "research"))
        from scdata.pe import PeImage

        c = constants(CUSTOM)
        pe = PeImage.load(Path(ORIGINAL) / "StarCraft.exe")
        load = 0x493800  # LoadAiScriptBins: aiscript.bin, then bwscript.bin, then the script pool's reset
        body = pe.read_va(load, c["LOAD_AI_BINS_RET"] + 1 - load)
        assert body[-1:] == b"\xc3" and body.count(b"\xc3") == 1, "the hook is the function's only ret"
        assert b"\xa3" + struct.pack("<I", c["AISCRIPT_BIN"]) in body  # mov [0x6a2e74], eax
        # FreeAiScriptBins starts with mov ecx, [0x6a2e74]: the buffer it frees is read after the hook put it back
        assert pe.read_va(c["FREE_AI_BINS"], 6) == b"\x8b\x0d" + struct.pack("<I", c["AISCRIPT_BIN"])


if __name__ == "__main__":
    unittest.main()
