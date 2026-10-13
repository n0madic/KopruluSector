"""StormLib binding and profile resolver against the private original data.

Set KOPRULU_ORIGINAL to the directory holding StarCraft.exe and the five MPQs of
profile bw-1.17.0.1; otherwise these tests are skipped.
"""
from __future__ import annotations

import hashlib
import os
import tempfile
import unittest
from pathlib import Path

from converter.mpq.profile import Profile, ProfileError, Resolver, verify_file
from converter.mpq.stormlib import Archive, MpqFileNotFound

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
        cls.resolver = Resolver(ORIGINAL, full_hash=False)

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
