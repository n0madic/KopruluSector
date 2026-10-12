"""Data profile: verified original files and the effective archive resolution order."""
from __future__ import annotations

import hashlib
import json
import logging
from dataclasses import dataclass
from pathlib import Path

from .stormlib import Archive, MpqFileNotFound

log = logging.getLogger(__name__)

DEFS_DIR = Path(__file__).resolve().parents[2] / "defs" / "profile"
DEFAULT_PROFILE = "bw-1.17.0.1"
HASH_CHUNK = 1 << 20


class ProfileError(RuntimeError):
    """Raised when the original data does not match the declared profile."""


@dataclass(frozen=True)
class FileSpec:
    name: str
    size: int
    sha256: str
    role: str = ""


@dataclass(frozen=True)
class Profile:
    id: str
    executable: FileSpec
    archives: tuple[FileSpec, ...]

    @classmethod
    def load(cls, profile_id: str = DEFAULT_PROFILE) -> "Profile":
        raw = json.loads((DEFS_DIR / f"{profile_id}.json").read_text())
        exe = raw["executable"]
        return cls(
            id=raw["id"],
            executable=FileSpec(exe["path"], exe["size"], exe["sha256"]),
            archives=tuple(FileSpec(a["name"], a["size"], a["sha256"], a["role"]) for a in raw["archives"]),
        )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(HASH_CHUNK):
            digest.update(chunk)
    return digest.hexdigest()


def verify_file(root: Path, spec: FileSpec, *, full_hash: bool = True) -> None:
    path = root / spec.name
    if not path.is_file():
        raise ProfileError(f"missing {spec.name} in {root}")
    size = path.stat().st_size
    if size != spec.size:
        raise ProfileError(f"{spec.name}: size {size}, profile expects {spec.size}")
    if full_hash:
        actual = sha256_file(path)
        if actual != spec.sha256:
            raise ProfileError(f"{spec.name}: sha256 {actual}, profile expects {spec.sha256}")


class Resolver:
    """Opens the profile archives and returns the first copy of each path.

    The order is the profile order (Patch_rt, BrooDat, StarDat, then the campaign
    archives BroodWar and StarCraft), as recorded in docs/research/campaigns.md.
    """

    def __init__(self, root: str | Path, profile: Profile | None = None, *, verify: bool = True, full_hash: bool = True):
        self.root = Path(root)
        self.profile = profile or Profile.load()
        if verify:
            verify_file(self.root, self.profile.executable, full_hash=full_hash)
            for spec in self.profile.archives:
                verify_file(self.root, spec, full_hash=full_hash)
        self._archives: list[tuple[FileSpec, Archive]] = []
        try:
            for spec in self.profile.archives:
                self._archives.append((spec, Archive(self.root / spec.name)))
        except Exception:
            self.close()
            raise

    def close(self) -> None:
        for _, archive in self._archives:
            archive.close()
        self._archives.clear()

    def __enter__(self) -> "Resolver":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def locate(self, name: str) -> str:
        """Return the archive name that provides `name`."""
        for spec, archive in self._archives:
            if archive.has(name):
                return spec.name
        raise MpqFileNotFound(f"{name} not found in profile {self.profile.id}")

    def read(self, name: str) -> tuple[str, bytes]:
        """Return (archive name, bytes) for the effective copy of `name`."""
        for spec, archive in self._archives:
            if archive.has(name):
                return spec.name, archive.read(name)
        raise MpqFileNotFound(f"{name} not found in profile {self.profile.id}")
