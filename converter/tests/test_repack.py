"""The repack (converter/repack.py, ADR 0018 stage D8): its groups, its listing, a WAV as Ogg Vorbis when that is
smaller, read back with StormLib; with KOPRULU_ORIGINAL the 1.17 install's whole repack is left to tools/ci.sh, which
plays it (the product's scripts over it write the install's states)."""
from __future__ import annotations

import hashlib
import math
import shutil
import struct
import tempfile
import unittest
from pathlib import Path

from converter import repack
from converter.mpq.stormlib import Archive, MpqFileNotFound
from converter.tests.test_scr_source import PROFILE, manifest

needs_oggenc = unittest.skipIf(shutil.which("oggenc") is None, "oggenc not installed")


def wav(samples: int, rate: int = 22050) -> bytes:
    """A 16-bit mono PCM WAV of a 440 Hz tone."""
    data = b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * 440 * k / rate))) for k in range(samples))
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, 2 * rate,
            2, 16) + b"data" + struct.pack("<I", len(data)) + data)


class StandIn:
    """A source of the given files by name (a 1.17 install's, else a Remastered one's with `status`)."""

    def __init__(self, files: dict[str, bytes], status: dict[str, tuple[str, str]] | None = None, missing=()):
        self.profile = PROFILE
        self.manifest = manifest(files)
        self.files = {n: d for n, d in files.items() if n not in missing}
        if status is not None:
            self.status = status

    def read(self, name: str) -> tuple[str, bytes]:
        if name not in self.files:
            raise MpqFileNotFound(name)
        return "stand-in", self.files[name]

    def read_expected(self, name: str, size: int, sha256: str) -> tuple[str, bytes]:
        raise MpqFileNotFound(name)  # no cinematics


def listing(path: Path) -> list[str]:
    with Archive(path) as a:
        return a.read(repack.LISTING).decode().splitlines()


class GroupTest(unittest.TestCase):
    def test_groups_by_name(self):
        assert repack.group_of("campaign/expterran/terran01/staredit/scenario.chk") == \
            ("koprulu-expterran-terran01.mpq", "campaign/expterran/terran01/")
        assert repack.group_of("campaign/terran/terran01/staredit/wav/t1.wav")[0] == "koprulu-terran-terran01.mpq"
        assert repack.group_of("music/terran1.wav") == (repack.MUSIC, "music/")
        assert repack.group_of("smk/blizzard.smk") == (repack.CINEMATICS, "smk/")
        assert repack.group_of("arr/units.dat") == (repack.CORE, "")
        assert repack.group_of("campaign/odd.chk") == (repack.CORE, "")


class RepackTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.out = Path(self.tmp.name) / "repack"
        self.oggs = repack.OggStore(Path(self.tmp.name) / "ogg")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_groups_and_their_listings(self):
        files = {"arr\\units.dat": b"units", "campaign\\terran\\terran01\\staredit\\scenario.chk": b"chk",
                 "music\\terran1.wav": b"not a WAV the game reads", "rez\\gone.bin": b"lost"}
        report = repack.write(StandIn(files, missing=("rez\\gone.bin",)), self.out, kind="1.17")
        assert sorted(p.name for p in self.out.glob("*.mpq")) == \
            ["koprulu-core.mpq", "koprulu-music.mpq", "koprulu-terran-terran01.mpq"]
        assert report.missing[0] == "rez\\gone.bin"
        core = listing(self.out / "koprulu-core.mpq")
        assert core[:3] == [repack.MAGIC, f"profile {PROFILE.id}", "source 1.17"]
        assert "group koprulu-music.mpq music/" in core
        assert "group koprulu-terran-terran01.mpq campaign/terran/terran01/" in core
        assert f"{hashlib.sha256(b'units').hexdigest()} 5 exact arr\\units.dat" in core
        mission = listing(self.out / "koprulu-terran-terran01.mpq")
        assert mission[-1].endswith(" exact campaign\\terran\\terran01\\staredit\\scenario.chk")
        assert not any(line.startswith("group ") for line in mission)
        with Archive(self.out / "koprulu-music.mpq") as a:
            assert a.read("music\\terran1.wav") == files["music\\terran1.wav"]  # refused by the parser: kept

    def test_single_file_and_statuses(self):
        files = {"arr\\units.dat": b"units", "music\\terran1.wav": b"x", "rez\\x.bin": b"rebuilt"}
        status = {"arr\\units.dat": ("exact", "arr/units.dat"), "music\\terran1.wav": ("differs", "music/terran1.wav"),
                  "rez\\x.bin": ("approximation", "rez/x.xml")}
        repack.write(StandIn(files, status), self.out, kind="remastered", single=True)
        assert [p.name for p in self.out.glob("*.mpq")] == ["koprulu.mpq"]
        lines = listing(self.out / "koprulu.mpq")
        assert lines[2] == "source remastered" and not any(line.startswith("group ") for line in lines)
        assert [line.split(" ")[2] for line in lines[3:]] == ["exact", "differs", "approximation"]

    def test_single_file_keeps_the_cinematics_apart(self):
        source = StandIn({"arr\\units.dat": b"units"})
        source.files["smk\\extra.smk"] = b"a movie"  # not one of the catalog's: read as an extra name
        repack.write(source, self.out, kind="1.17", single=True, extra=["smk\\extra.smk"])
        assert sorted(p.name for p in self.out.glob("*.mpq")) == ["koprulu-cinematics.mpq", "koprulu.mpq"]
        assert "group koprulu-cinematics.mpq smk/" in listing(self.out / "koprulu.mpq")
        with Archive(self.out / "koprulu-cinematics.mpq") as a:
            assert a.read("smk\\extra.smk") == b"a movie"

    def test_extra_names_go_in_unlisted(self):
        source = StandIn({"arr\\units.dat": b"units"})
        source.files["rez\\ours.bin"] = b"read by the game"
        repack.write(source, self.out, kind="1.17", extra=["rez\\ours.bin", "arr\\UNITS.dat"])
        lines = listing(self.out / "koprulu-core.mpq")
        assert lines[-1].endswith(" unlisted rez\\ours.bin") and len(lines) == 5

    @needs_oggenc
    def test_a_wav_as_ogg_when_smaller(self):
        long, short = wav(22050), wav(20)
        files = {"sound\\long.wav": long, "sound\\short.wav": short}
        report = repack.write(StandIn(files), self.out, kind="1.17", oggs=self.oggs)
        assert report.ogg == 1 and report.wav_bytes == len(long)
        lines = {line.split(" ")[-1]: line.split(" ") for line in listing(self.out / "koprulu-core.mpq")[3:]}
        ogg_line = lines["sound\\long.wav"]
        assert ogg_line[2:4] == ["exact", "ogg"]
        assert ogg_line[4:6] == [hashlib.sha256(long).hexdigest(), str(len(long))]
        assert bytes.fromhex(ogg_line[6]) == long[:44]
        assert lines["sound\\short.wav"][2:] == ["exact", "sound\\short.wav"]  # its Ogg is larger
        with Archive(self.out / "koprulu-core.mpq") as a:
            stored = a.read("sound\\long.wav")
        assert stored[:4] == b"OggS" and int(ogg_line[1]) == len(stored)
        assert hashlib.sha256(stored).hexdigest() == ogg_line[0]
        assert self.oggs.encoded == 2
        repack.write(StandIn(files), self.out, kind="1.17", oggs=self.oggs)
        assert self.oggs.encoded == 2, "encoded once"


if __name__ == "__main__":
    unittest.main()
