"""StormLib binding and profile resolver against the private original data.

Set KOPRULU_ORIGINAL to the directory holding StarCraft.exe and the five MPQs of
profile bw-1.17.0.1; otherwise these tests are skipped.
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path

import pytest

from converter.mpq.profile import Profile, ProfileError, Resolver, verify_file
from converter.mpq.stormlib import Archive, MpqFileNotFound

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
pytestmark = pytest.mark.skipif(not ORIGINAL, reason="KOPRULU_ORIGINAL not set")

# Effective files and their providers, from docs/research/campaigns.md.
EFFECTIVE = [
    ("scripts\\aiscript.bin", "Patch_rt.mpq", 62050, "635433e546998e05529a9d6acc806c06b528c7ce5075a206f6f2c8de44ffed6e"),
    ("scripts\\bwscript.bin", "BrooDat.mpq", 26458, "15a1a5b3fb4f2cab2e7b7b507ae04d4e2ef097c77081cfeb950d9a2e4ebee382"),
    ("scripts\\iscript.bin", "Patch_rt.mpq", 40482, "cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0"),
    ("arr\\mapdata.dat", "BrooDat.mpq", 260, "06ce3127b9c77739f32ad606637943f022e5d0344afcf1e978494449ace37967"),
    ("arr\\mapdata.tbl", "BrooDat.mpq", 1775, "ab4715c36e3af0062fffa5308953ef8c40850790144523b90ccd19529862f15b"),
]


@pytest.fixture(scope="module")
def resolver():
    # Sizes are checked always; full hashing of 1.2 GB is done once in test_profile_hashes.
    with Resolver(ORIGINAL, full_hash=False) as r:
        yield r


@pytest.mark.parametrize("name,archive,size,sha256", EFFECTIVE)
def test_effective_files(resolver, name, archive, size, sha256):
    provider, data = resolver.read(name)
    assert provider == archive
    assert len(data) == size
    assert hashlib.sha256(data).hexdigest() == sha256


def test_read_from_stardat():
    with Archive(Path(ORIGINAL) / "StarDat.mpq") as archive:
        assert archive.has("arr\\units.dat")
        data = archive.read("arr\\units.dat")
    assert len(data) > 0


def test_missing_file(resolver):
    with pytest.raises(MpqFileNotFound):
        resolver.read("no\\such\\file.bin")


def test_profile_hashes():
    profile = Profile.load()
    for spec in (profile.executable, *profile.archives):
        verify_file(Path(ORIGINAL), spec, full_hash=True)


def test_profile_rejects_wrong_size(tmp_path):
    profile = Profile.load()
    spec = profile.archives[0]
    (tmp_path / spec.name).write_bytes(b"not an archive")
    with pytest.raises(ProfileError):
        verify_file(tmp_path, spec)
