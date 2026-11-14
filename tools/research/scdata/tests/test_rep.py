"""The 1.17 replay reader (scdata.formats.rep): synthetic files, and the emulator's replays when they are present."""
from __future__ import annotations

import struct
import unittest
from pathlib import Path

from scdata.formats import rep

from .private import needs

ROOT = Path(__file__).resolve().parents[4]
EMULATOR_REPLAY = ROOT / "koprulu-private/emu/rp/rp-terran01/replays/01-m02.rep"


def _header(frames=100, campaign=2, seed=0x1234) -> bytes:
    raw = bytearray(rep.HEADER_SIZE)
    struct.pack_into("<IH", raw, 1, frames, campaign)
    struct.pack_into("<I", raw, 8, seed)
    raw[0x3A] = 4
    raw[0x46] = 1
    o = 0xA1 + rep.PLAYER_RECORD
    struct.pack_into("<Ii3B", raw, o, 1, 0, 2, 1, 1)
    raw[o + 11:o + 15] = b"Jim\0"
    return bytes(raw)


def _record() -> bytes:
    # frame 3: a select of two units, then a stop; frame 9: game speed
    return (struct.pack("<IB", 3, 1 + 6 + 1 + 2) + bytes([0, 9, 2, 1, 0, 2, 0]) + bytes([0, 0x1A, 0])
            + struct.pack("<IB", 9, 3) + bytes([0, 0xF, 6]))


class RepTest(unittest.TestCase):
    def test_round_trip(self):
        r = rep.Replay(rep.decode_header(_header()), _record(), bytes(range(256)) * 40, b"KPRX tail")
        back = rep.read(rep.write(r))
        self.assertEqual((back.header.frames, back.header.campaign, back.header.seed, back.header.speed), (100, 2, 0x1234, 4))
        self.assertEqual(back.header.players[1].name, b"Jim")
        self.assertEqual((back.record, back.chk, back.tail), (r.record, r.chk, r.tail))

    def test_commands(self):
        frames = rep.decode_commands(_record())
        self.assertEqual([f.frame for f in frames], [3, 9])
        self.assertEqual([c.hex() for _, c in frames[0].commands], ["090201000200", "1a00"])
        self.assertEqual(frames[1].commands, [(0, b"\x0f\x06")])

    def test_empty_record(self):
        r = rep.read(rep.write(rep.Replay(rep.decode_header(_header()), b"", b"VER ", b"")))
        self.assertEqual(r.record, b"")

    def test_refusals(self):
        good = rep.write(rep.Replay(rep.decode_header(_header()), _record(), b"x" * 9000, b""))
        for cut in (0, 3, 8, 20, len(good) - 1):
            with self.assertRaises(rep.RepError):
                rep.read(good[:cut])
        bad = bytearray(good)
        bad[30] ^= 1  # a header byte: the block's checksum
        with self.assertRaises(rep.RepError):
            rep.read(bytes(bad))
        with self.assertRaises(rep.RepError):
            rep.decode_commands(struct.pack("<IB", 1, 2) + bytes([0, 0x60]))  # an unknown ID
        with self.assertRaises(rep.RepError):
            rep.decode_commands(struct.pack("<IB", 1, 9) + bytes([0, 9]))  # the block past the end

    @needs(EMULATOR_REPLAY)
    def test_emulator_replay(self):
        """terran01 played by the chain (one cheat), its LastReplay as the original wrote it: imploded chunks."""
        r = rep.read(EMULATOR_REPLAY.read_bytes())
        self.assertEqual((r.header.campaign, r.header.brood_war, r.header.speed), (2, 0, 4))
        self.assertEqual(r.header.players[1].name, b"Anonymous")
        self.assertEqual(r.chk[:4], b"VER ")
        self.assertEqual(r.tail, b"")
        self.assertEqual([(f.frame, [c.hex() for _, c in f.commands]) for f in rep.decode_commands(r.record)],
                         [(311, ["1280020000"])])
        self.assertEqual(rep.read(rep.write(r)).chk, r.chk)


if __name__ == "__main__":
    unittest.main()
