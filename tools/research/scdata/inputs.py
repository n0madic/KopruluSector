"""The input manifest `defs/scr/inputs-<profile>.json`: name, size and SHA-256 of each file a profile's game reads
and the names it looked for and did not find (`absent`). `kp_convert manifest DIR` writes it from the reference install
(kp_res recording the runtime's own reads, ADR 0019).

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
    extras: dict  # what kp_manifest.c keeps (images, SMKs), a GRP's palette


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
            raise ManifestError(f"no input manifest {path} (kp_convert manifest DIR)")
        raw = json.loads(path.read_text())
        if raw["profile"] != profile_id:
            raise ManifestError(f"{path}: manifest of {raw['profile']}, profile {profile_id}")
        texts_file = texts_path(profile_id)
        texts = json.loads(texts_file.read_text()) if texts_file.is_file() else {"tbl": {}, "bin": {}}
        files = {normalize(row[0]): Entry(row[2], row[1], row[0], row[3] if len(row) > 3 else {}) for row in raw["files"]}
        return cls(raw["profile"], files, frozenset(normalize(n) for n in raw["absent"]),
                   {kind: {normalize(n): v for n, v in texts.get(kind, {}).items()} for kind in ("tbl", "bin")},
                   raw.get("palettes", {}))


def manifest_path(profile_id: str) -> Path:
    return MANIFEST_DIR / f"inputs-{profile_id}.json"


def texts_path(profile_id: str) -> Path:
    return MANIFEST_DIR / f"texts-{profile_id}.json"
