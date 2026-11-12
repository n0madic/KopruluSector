"""StormLib binding and profile resolver.

The content checks run on stand-in archives; the rest needs the private original data: set KOPRULU_ORIGINAL to the
directory holding StarCraft.exe and the five MPQs of profile bw-1.17.0.1, otherwise those tests are skipped.
"""
from __future__ import annotations

import hashlib
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scdata.mpq.profile import Profile, ProfileError, Resolver, verify_file
from scdata.mpq.stormlib import Archive, MpqFileNotFound, StormLibError
from scdata.tests.test_scr_source import manifest

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
needs_original = unittest.skipIf(not ORIGINAL, "KOPRULU_ORIGINAL not set")

# Effective files and their providers, from docs/research/campaigns.md.
EFFECTIVE = [
    ("scripts\\aiscript.bin", "Patch_rt.mpq", 62050, "635433e546998e05529a9d6acc806c06b528c7ce5075a206f6f2c8de44ffed6e"),
    ("scripts\\bwscript.bin", "BrooDat.mpq", 26458, "15a1a5b3fb4f2cab2e7b7b507ae04d4e2ef097c77081cfeb950d9a2e4ebee382"),
    ("scripts\\iscript.bin", "Patch_rt.mpq", 40482, "cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0"),
    ("arr\\mapdata.dat", "BrooDat.mpq", 260, "06ce3127b9c77739f32ad606637943f022e5d0344afcf1e978494449ace37967"),
    ("arr\\mapdata.tbl", "BrooDat.mpq", 1775, "ab4715c36e3af0062fffa5308953ef8c40850790144523b90ccd19529862f15b"),
]


@needs_original
class ResolverTest(unittest.TestCase):
    resolver: Resolver

    @classmethod
    def setUpClass(cls) -> None:
        # Sizes are checked always; full hashing of 1.2 GB is done once in test_profile_hashes.
        cls.resolver = Resolver(ORIGINAL)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.resolver.close()

    def test_effective_files(self):
        for name, archive, size, sha256 in EFFECTIVE:
            with self.subTest(name=name):
                provider, data = self.resolver.read(name)
                assert provider == archive
                assert len(data) == size
                assert hashlib.sha256(data).hexdigest() == sha256

    def test_missing_file(self):
        with self.assertRaises(MpqFileNotFound):
            self.resolver.read("no\\such\\file.bin")


class StandInArchive:
    """An archive of the given files by name, in place of an MPQ."""

    contents: dict[str, dict[str, bytes]] = {}  # archive file name -> {MPQ path: bytes}

    def __init__(self, path: str | Path):
        self.files = {k.lower(): v for k, v in self.contents[Path(path).name].items()}

    def has(self, name: str) -> bool:
        return name.lower() in self.files

    def read(self, name: str) -> bytes:
        data = self.files[name.lower()]
        if isinstance(data, Exception):  # a damaged copy
            raise data
        return data

    def close(self) -> None:
        pass


class ContentCheckTest(unittest.TestCase):
    """Archives are judged by the files the conversion reads, not by their size or hash."""

    REFERENCE = {"arr\\units.dat": b"units", "rez\\stat_txt.tbl": b"strings", "scripts\\iscript.bin": b"patched"}

    def setUp(self):
        self.profile = Profile.load()
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        with (self.root / self.profile.executable.name).open("wb") as f:
            f.truncate(self.profile.executable.size)
        for spec in self.profile.archives:
            (self.root / spec.name).write_bytes(b"packed otherwise")  # not the reference copies' sizes
        self.manifest = manifest(self.REFERENCE, absent=("glue\\none.pcx",))
        self.archives = {spec.name: {} for spec in self.profile.archives}
        self.archives["Patch_rt.mpq"] = {"scripts\\iscript.bin": b"patched"}
        self.archives["StarDat.mpq"] = {"arr\\units.dat": b"units", "rez\\stat_txt.tbl": b"strings",
                                        "scripts\\iscript.bin": b"older", "glue\\none.pcx": b"extra",
                                        "maps\\other.chk": b"unlisted"}
        patcher = mock.patch("scdata.mpq.profile.Archive", StandInArchive)
        patcher.start()
        self.addCleanup(patcher.stop)
        StandInArchive.contents = self.archives

    def resolver(self) -> Resolver:
        return Resolver(self.root, self.profile, manifest=self.manifest)

    def test_archives_of_other_sizes_with_the_same_files_pass(self):
        with self.resolver() as r:
            assert r.read("arr\\units.dat") == ("StarDat.mpq", b"units")
            assert r.read("scripts\\iscript.bin") == ("Patch_rt.mpq", b"patched")  # the effective copy is checked
            assert r.verify_contents() == ([], [])

    def test_a_changed_file_is_refused_by_name(self):
        self.archives["StarDat.mpq"]["rez\\stat_txt.tbl"] = b"edited"
        with self.resolver() as r:
            with self.assertRaisesRegex(ProfileError, r"StarDat\.mpq: rez\\stat_txt\.tbl has size 6"):
                r.read("rez\\stat_txt.tbl")
            problems, damaged = r.verify_contents()
            assert len(problems) == 1 and not damaged

    def test_a_listed_file_the_archives_lack_is_refused(self):
        del self.archives["StarDat.mpq"]["arr\\units.dat"]
        with self.resolver() as r:
            assert r.has("arr\\units.dat")
            with self.assertRaisesRegex(ProfileError, r"arr\\units\.dat: in none of the archives"):
                r.read("arr\\units.dat")
            problems, damaged = r.verify_contents()
            assert len(problems) == 1 and not damaged

    def test_a_damaged_copy_is_told_apart(self):
        # a disc copied with a bad read (issue #2): the archive holds the listed name, StormLib cannot read it whole
        self.archives["StarDat.mpq"]["sound\\bad.wav"] = StormLibError("sound\\bad.wav: short read 4096/8192")
        self.manifest = manifest({**self.REFERENCE, "sound\\bad.wav": b"the release's"}, absent=("glue\\none.pcx",))
        with self.resolver() as r:
            assert r.has("sound\\bad.wav")
            with self.assertRaises(StormLibError):
                r.read("sound\\bad.wav")
            assert r.verify_contents() == ([], ["sound\\bad.wav: short read 4096/8192"])

    def test_the_reference_data_set_is_kept(self):
        with self.resolver() as r:
            assert not r.has("glue\\none.pcx")
            with self.assertRaises(MpqFileNotFound):
                r.read("glue\\none.pcx")
            with self.assertLogs("scdata.mpq.profile", "WARNING"):
                assert r.read("maps\\other.chk") == ("StarDat.mpq", b"unlisted")

    def test_a_file_the_caller_knows_is_checked_by_its_hash(self):
        # the cinematics' movies: not in the input manifest, their sizes and hashes in defs/campaign/cinematics.json
        movie = b"movie"
        self.archives["StarDat.mpq"]["smk\\blizzard.smk"] = movie
        self.archives["BroodWar.mpq"]["smk\\blizzard.smk"] = b"shadowed"
        sha256 = hashlib.sha256(movie).hexdigest()
        with self.resolver() as r, self.assertNoLogs("scdata.mpq.profile", "WARNING"):
            assert r.read_expected("smk\\blizzard.smk", len(movie), sha256) == ("StarDat.mpq", movie)
            with self.assertRaisesRegex(ProfileError, r"StarDat\.mpq: smk\\blizzard\.smk has size 5, sha256 "):
                r.read_expected("smk\\blizzard.smk", len(movie), "0" * 64)
            with self.assertRaisesRegex(ProfileError, "expects size 6"):
                r.read_expected("smk\\blizzard.smk", 6, sha256)
            with self.assertRaises(MpqFileNotFound):
                r.read_expected("smk\\none.smk", 1, sha256)

    def test_the_reference_install_keeps_its_sizes(self):
        with self.assertRaisesRegex(ProfileError, r"Patch_rt\.mpq: size 16"):
            Resolver(self.root, self.profile, reference=True)

    def test_a_missing_archive_is_refused(self):
        (self.root / "StarCraft.mpq").unlink()
        with self.assertRaisesRegex(ProfileError, "missing StarCraft.mpq"):
            self.resolver()


@needs_original
class RepackedArchiveTest(unittest.TestCase):
    def test_an_archive_with_other_bytes_after_its_files_passes(self):
        # The reported case: a StarDat.mpq 23 bytes longer than the reference copy, with the same files.
        profile = Profile.load()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for spec in (profile.executable, *profile.archives):
                if spec.name != "StarDat.mpq":
                    (root / spec.name).symlink_to(Path(ORIGINAL).resolve() / spec.name)
            shutil.copyfile(Path(ORIGINAL) / "StarDat.mpq", root / "StarDat.mpq")
            with (root / "StarDat.mpq").open("ab") as f:
                f.write(bytes(23))
            with self.assertRaises(ProfileError):
                verify_file(root, next(s for s in profile.archives if s.name == "StarDat.mpq"))
            with Resolver(root, profile) as r:
                assert r.read("cursor\\arrow.grp")[0] == "StarDat.mpq"  # checked against the manifest
                for name, archive, _, _ in EFFECTIVE:
                    assert r.read(name)[0] == archive


@needs_original
class MpqTest(unittest.TestCase):
    def test_read_from_stardat(self):
        with Archive(Path(ORIGINAL) / "StarDat.mpq") as archive:
            assert archive.has("arr\\units.dat")
            data = archive.read("arr\\units.dat")
        assert len(data) > 0

    def test_profile_hashes(self):
        profile = Profile.load()
        for spec in (profile.executable, *profile.archives):
            verify_file(Path(ORIGINAL), spec, full_hash=True)

    def test_profile_rejects_wrong_size(self):
        profile = Profile.load()
        spec = profile.archives[0]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            (tmp_path / spec.name).write_bytes(b"not an archive")
            with self.assertRaises(ProfileError):
                verify_file(tmp_path, spec)
