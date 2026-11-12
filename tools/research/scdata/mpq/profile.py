"""Data profile: verified original files and the effective archive resolution order."""
from __future__ import annotations

import hashlib
import json
import logging
from dataclasses import dataclass
from pathlib import Path

from .stormlib import Archive, MpqFileNotFound, StormLibError
from ..casc.storage import normalize
from ..errors import ConverterError
from ..inputs import Manifest

log = logging.getLogger(__name__)

DEFS_DIR = Path(__file__).resolve().parents[4] / "defs" / "profile"
DEFAULT_PROFILE = "bw-1.17.0.1"
HASH_CHUNK = 1 << 20


class ProfileError(ConverterError, RuntimeError):
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

    def data_set_hash(self) -> bytes:
        """SHA-256 identifying the original data set; shared by every file converted from it."""
        files = (self.executable, *self.archives)
        return hashlib.sha256("\n".join(f"{f.name} {f.size} {f.sha256}" for f in files).encode()).digest()


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

    The archives are checked by content, not as whole files: copies of the same release are packed differently
    (an installer's or a disc's StarDat.mpq and StarCraft.mpq differ in size and hash), while the files the
    conversion reads are the same. Each effective copy must have the input manifest's SHA-256 (inputs.py) and a name the
    manifest lists as absent stays absent, so the conversion sees the reference install's data set. A damaged copy
    (one StormLib cannot read whole: a disc copied with a bad read) raises StormLibError. A name the manifest does not hold (an `extract` of another file) is read unchecked.

    `reference=True` is the reference install itself, for `scr-manifest`, which writes the manifest: its archives
    must have the profile's sizes and nothing is checked against the manifest.
    """

    def __init__(self, root: str | Path, profile: Profile | None = None, *, manifest: Manifest | None = None,
                 reference: bool = False):
        self.root = Path(root)
        self.profile = profile or Profile.load()
        verify_file(self.root, self.profile.executable, full_hash=False)
        for spec in self.profile.archives:
            if reference:
                verify_file(self.root, spec, full_hash=False)
            elif not (self.root / spec.name).is_file():
                raise ProfileError(f"missing {spec.name} in {self.root}")
        self.manifest = None if reference else manifest or Manifest.load(self.profile.id)
        if self.manifest is not None and self.manifest.profile != self.profile.id:
            raise ProfileError(f"manifest of {self.manifest.profile}, profile {self.profile.id}")
        self._checked: set[str] = set()  # normalized names whose effective copy has the manifest's hash
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

    def has(self, name: str) -> bool:
        if self.manifest is not None:
            key = normalize(name)
            if key in self.manifest.files:
                return True  # a listed file the install lacks is refused by `read`, not skipped
            if key in self.manifest.absent:
                return False
        return any(archive.has(name) for _, archive in self._archives)

    def data_set_hash(self) -> bytes:
        return self.profile.data_set_hash()

    def locate(self, name: str) -> str:
        """Return the archive name that provides `name`."""
        for spec, archive in self._archives:
            if archive.has(name):
                return spec.name
        raise MpqFileNotFound(f"{name} not found in profile {self.profile.id}")

    def read(self, name: str) -> tuple[str, bytes]:
        """Return (archive name, bytes) for the effective copy of `name`."""
        key = normalize(name)
        if self.manifest is not None and key in self.manifest.absent:
            raise MpqFileNotFound(f"{name} not found in profile {self.profile.id}")
        found = self._effective(name)
        if found is not None:
            self._check(key, name, *found)
            return found
        if self.manifest is not None and key in self.manifest.files:
            raise ProfileError(f"{name}: in none of the archives in {self.root}, profile {self.profile.id} needs it")
        raise MpqFileNotFound(f"{name} not found in profile {self.profile.id}")

    def read_expected(self, name: str, size: int, sha256: str) -> tuple[str, bytes]:
        """(archive name, bytes) of the effective copy of a file the input manifest does not list but its caller
        knows (the cinematics' movies, defs/campaign/cinematics.json): ProfileError unless it has that size and
        SHA-256, MpqFileNotFound when no archive has it."""
        found = self._effective(name)
        if found is None:
            raise MpqFileNotFound(f"{name} not found in profile {self.profile.id}")
        archive, data = found
        actual = hashlib.sha256(data).hexdigest()
        if len(data) != size or actual != sha256:
            raise ProfileError(f"{archive}: {name} has size {len(data)}, sha256 {actual}; profile {self.profile.id} "
                               f"expects size {size}, sha256 {sha256}")
        return found

    def _effective(self, name: str) -> tuple[str, bytes] | None:
        """(archive name, bytes) of the first archive's copy of `name` in the profile's order, or None."""
        for spec, archive in self._archives:
            if archive.has(name):
                return spec.name, archive.read(name)
        return None

    def _check(self, key: str, name: str, archive: str, data: bytes) -> None:
        if self.manifest is None or key in self._checked:
            return
        entry = self.manifest.files.get(key)
        if entry is None:
            log.warning("%s: not in the input manifest of %s, read unchecked from %s", name, self.profile.id, archive)
        else:
            actual = hashlib.sha256(data).hexdigest()
            if actual != entry.sha256:
                raise ProfileError(f"{archive}: {name} has size {len(data)}, sha256 {actual}; profile {self.profile.id} "
                                   f"expects size {entry.size}, sha256 {entry.sha256}")
        self._checked.add(key)

    def verify_contents(self) -> tuple[list[str], list[str]]:
        """Read every file of the input manifest: a line per file that is missing or differs, and a line per damaged
        copy (one StormLib cannot read whole: a disc copied with a bad read), reported apart: the install's, not the
        data set's."""
        if self.manifest is None:
            raise ProfileError("a reference resolver has no input manifest to verify against")
        problems, damaged = [], []
        for entry in sorted(self.manifest.files.values(), key=lambda e: e.name.lower()):
            try:
                self.read(entry.name)
            except ProfileError as exc:
                problems.append(str(exc))
            except StormLibError as exc:  # a listed name is never MpqFileNotFound: `read` refuses it as ProfileError
                damaged.append(str(exc))
        return problems, damaged
