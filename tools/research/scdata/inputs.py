"""The input manifest `defs/scr/inputs-<profile>.json`: name, size and SHA-256 of each file a profile's conversion
reads and the names it looked for and did not find (`absent`). `python3 tools/research/scr_manifest.py` writes it from the
reference install.

Both sources are checked against it by content: the MPQ `Resolver` (mpq/profile.py), whose archives may be packed
differently from the reference copies, and `ScrSource` (casc/source.py), which also rebuilds what Remastered changed.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .casc.storage import normalize
from .errors import ConverterError
from .provenance import DEFS_ROOT

MANIFEST_DIR = DEFS_ROOT / "scr"


class ManifestError(ConverterError, RuntimeError):
    """The input manifest is missing or belongs to another profile."""


@dataclass(frozen=True)
class Entry:
    sha256: str
    size: int
    name: str  # as the converter spells it
    extras: dict  # what rebuild.describe keeps (images, SMKs), a GRP's palette


@dataclass(frozen=True)
class Manifest:
    profile: str
    files: dict[str, Entry]  # by normalized name
    absent: frozenset[str]
    texts: dict  # defs/scr/texts-<profile>.json: string tables ("tbl") and dialogs ("bin")
    palettes: dict[str, list[str]]  # palette hash -> the PCX files with it

    @classmethod
    def load(cls, profile_id: str) -> "Manifest":
        path = manifest_path(profile_id)
        if not path.is_file():
            raise ManifestError(f"no input manifest {path} (python3 tools/research/scr_manifest.py)")
        raw = json.loads(path.read_text())
        if raw["profile"] != profile_id:
            raise ManifestError(f"{path}: manifest of {raw['profile']}, profile {profile_id}")
        texts_file = texts_path(profile_id)
        texts = json.loads(texts_file.read_text()) if texts_file.is_file() else {"tbl": {}, "bin": {}}
        files = {normalize(row[0]): Entry(row[2], row[1], row[0], row[3] if len(row) > 3 else {}) for row in raw["files"]}
        return cls(raw["profile"], files, frozenset(normalize(n) for n in raw["absent"]),
                   {kind: {normalize(n): v for n, v in texts.get(kind, {}).items()} for kind in ("tbl", "bin")},
                   raw.get("palettes", {}))


def damaged_entries(profile_id: str, damaged: set[str]) -> dict[str, tuple[str, int, dict]]:
    """The committed manifest's entries (sha256, size, extras by name) of the files the reference install holds but
    cannot read whole (`RecordingSource.damaged`: a disc copied with a bad read). The profile's data is the release's,
    not one copy's (issue #2: the reference StarCraft.mpq's PAB05ura.wav, docs/research/campaigns.md); a damaged file
    the manifest does not list needs its entry from a sound copy first."""
    if not damaged:
        return {}
    manifest = Manifest.load(profile_id)
    out = {}
    for name in sorted(damaged, key=str.lower):
        entry = manifest.files.get(normalize(name))
        if entry is None:
            raise ManifestError(f"{name}: the reference install's copy cannot be read whole and "
                                f"{manifest_path(profile_id)} has no entry for it (take its size and SHA-256 from a sound copy)")
        out[entry.name] = (entry.sha256, entry.size, entry.extras)
    return out


def manifest_path(profile_id: str) -> Path:
    return MANIFEST_DIR / f"inputs-{profile_id}.json"


def texts_path(profile_id: str) -> Path:
    return MANIFEST_DIR / f"texts-{profile_id}.json"
