"""`converter speech` (converter/emit/speech.py, ADR 0016, localization's stage L5): the merge of a language's triggers
into the base's (only TIME taken, every other difference reported), locales.json's exceptions, where a line's file is
looked up and which lines keep the base's sound. The real ruRU conversion is checked by its report and by
tests/c/test_speech.c's plays."""
from __future__ import annotations

import struct
import unittest
from pathlib import Path

from converter.emit import locale, speech

RECORD = speech.TRIGGER_RECORD
ROOT = Path(__file__).resolve().parents[2]


def action_at(record: int, action: int) -> int:
    return record * RECORD + speech.CONDITIONS + action * speech.ACTION


def time_at(record: int, action: int) -> int:
    return action_at(record, action) + speech.ACTION_TIME


def records(count: int) -> bytearray:
    """`count` trigger records, each byte its offset's low byte (so a difference shows where it is)."""
    return bytearray(k & 0xff for k in range(count * RECORD))


class FakeInstall:
    """Install.has and Install.read over a dict of the language's files."""

    def __init__(self, files: dict[str, bytes]):
        self.files = files

    def has(self, path: str) -> bool:
        return path in self.files

    def read(self, path: str) -> bytes:
        return self.files[path]


class TimeMergedTest(unittest.TestCase):
    def test_the_languages_time_is_taken(self):
        base, theirs = records(2), records(2)
        struct.pack_into("<I", theirs, time_at(1, 3), 4321)
        problems: list[str] = []
        merged, changed, differ = speech.time_merged("m TRIG", bytes(base), bytes(theirs), problems)
        self.assertEqual((changed, differ, problems), (1, [], []))
        self.assertEqual(struct.unpack_from("<I", merged, time_at(1, 3))[0], 4321)
        self.assertEqual(merged[:time_at(1, 3)], base[:time_at(1, 3)])

    def test_an_action_that_differs_in_more_keeps_the_base(self):
        base, theirs = records(1), records(1)
        struct.pack_into("<I", theirs, time_at(0, 5), 99)
        theirs[action_at(0, 5)] ^= 1  # its location too
        merged, changed, differ = speech.time_merged("m TRIG", bytes(base), bytes(theirs), [])
        self.assertEqual((merged, changed, differ), (bytes(base), 0, ["record 0 action 5"]))

    def test_conditions_and_flags_that_differ_are_told(self):
        base, theirs = records(2), records(2)
        theirs[RECORD + 7] ^= 1  # record 1's conditions
        theirs[RECORD - 1] ^= 1  # record 0's execution flags
        merged, _, differ = speech.time_merged("m TRIG", bytes(base), bytes(theirs), [])
        self.assertEqual(differ, ["record 0 flags", "record 1 conditions"])
        self.assertEqual(merged, bytes(base))

    def test_another_size_is_a_problem(self):
        problems: list[str] = []
        base = bytes(records(2))
        merged, changed, differ = speech.time_merged("m MBRF", base, base[:RECORD], problems)
        self.assertEqual((merged, changed, differ), (base, 0, []))
        self.assertEqual(problems, [f"m MBRF: {RECORD} bytes for the base's {2 * RECORD}"])


class LinesTest(unittest.TestCase):
    PATHS = ["campaign\\terran\\terran01\\staredit\\wav\\a.wav", "campaign\\terran\\terran01\\staredit\\wav\\B.WAV",
             "campaign\\terran\\terran01\\staredit\\wav\\c.wav"]

    def test_language_path(self):
        self.assertEqual(speech.language_path("ruRU", self.PATHS[1], ".ogg"),
                         locale.ASSETS.format(locale="ruRU") + "campaign/terran/terran01/staredit/wav/B.ogg")
        self.assertEqual(speech.language_path("ruRU", "sound\\Misc\\x.wav", ".wav"),
                         locale.ASSETS.format(locale="ruRU") + "sound/Misc/x.wav")

    def test_a_missing_or_empty_line_keeps_the_bases_sound(self):
        install = FakeInstall({speech.language_path("ruRU", self.PATHS[0], ".ogg"): b"ogg a",
                               speech.language_path("ruRU", self.PATHS[1], ".ogg"): b""})  # c.wav: none
        fallback = {1: b"base b", 2: b"base c"}
        jobs, files, kept = speech.line_sources(install, "ruRU", self.PATHS, Path("out"), fallback, "terran/terran01")
        self.assertEqual(list(jobs.values()), [(b"ogg a", speech.language_path("ruRU", self.PATHS[0], ".ogg"))])
        self.assertEqual(files, [(0, Path("out/a.ogg")), (1, b"base b"), (2, b"base c")])
        self.assertEqual(kept, self.PATHS[1:])

    def test_a_line_neither_has_is_left_out(self):
        jobs, files, kept = speech.line_sources(FakeInstall({}), "ruRU", self.PATHS[:1], Path("out"), {}, "terran/terran01")
        self.assertEqual((jobs, files, kept), ({}, [], []))


class ExceptionsTest(unittest.TestCase):
    def test_a_languages_exceptions(self):
        defs = {"speech": [{"locale": "ruRU", "mission": "a/b", "why": "w"}, {"locale": "deDE", "mission": "c/d", "why": "x"}]}
        self.assertEqual(speech.speech_exceptions(defs, "ruRU"), {"a/b": "w"})
        self.assertEqual(speech.speech_exceptions({}, "ruRU"), {})

    def test_the_repositorys_exceptions_name_missions_and_reasons(self):
        defs = locale.load_locales()
        index = ROOT / "generated" / "missions" / "index.txt"
        missions = {line.split()[0] for line in index.read_text().splitlines() if line.strip()} if index.exists() else None
        for e in defs.get("speech", ()):
            self.assertTrue(e["why"].strip(), e)
            self.assertIn(e["locale"], {loc["id"] for loc in defs["locales"]}, e)
            if missions is not None:
                self.assertIn(e["mission"], missions, e)


if __name__ == "__main__":
    unittest.main()
