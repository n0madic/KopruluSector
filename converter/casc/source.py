"""The profile's original files read from a StarCraft: Remastered install (`--scr`).

Remastered keeps most of 1.17's files byte for byte, some under `SD/` or the enUS locale, some moved, and
grows the tilesets by appending (docs/research/remastered.md). `ScrSource` looks an MPQ path up in that order,
rebuilds what is derivable, and checks every result against the manifest `defs/scr/inputs-<profile>.json`
(SHA-256 and size of each file the conversion reads from the 1.17 install, written by `scr-manifest`):

- `exact`: a CASC copy has the manifest's hash;
- `derived`: rebuilt from a CASC file to the manifest's hash (a tileset's original prefix, `.vx4` from `.vx4ex`);
- `differs`: no candidate reaches the hash; the first readable copy is used (an approximation);
- `unlisted`: a name the manifest does not hold, read from CASC unchecked.

A name the manifest lists as absent stays absent, and one it lists as unreadable stays unreadable, so the
conversion sees the data set the 1.17 install has. `reports/scr-import.json` records each file's status.
"""
from __future__ import annotations

import hashlib
import json
import logging
import struct
from dataclasses import dataclass
from pathlib import Path

from ..emit.files import write_if_changed
from ..mpq.profile import DEFAULT_PROFILE, Profile
from ..provenance import DEFS_ROOT
from ..source import SourceError, SourceUnreadable
from .storage import CascError, CascStorage, normalize

log = logging.getLogger(__name__)

MANIFEST_DIR = DEFS_ROOT / "scr"
REPORT = Path("reports") / "scr-import.json"
# Where Remastered keeps the profile's files: its SD copies (sounds, campaign maps, briefing texts) first,
# since the copies at the root are its own where both exist (16-bit sounds); the enUS locale last.
PREFIXES = ("SD/", "", "locales/enUS/Assets/SD/", "locales/enUS/Assets/")
# Original files Remastered keeps under another name (found by content).
MOVED = {
    "dlgs/protoss.grp": ("game/consoles/protoss/dlg.grp",),
    "dlgs/terran.grp": ("game/consoles/terran/dlg.grp",),
    "dlgs/zerg.grp": ("game/consoles/zerg/dlg.grp",),
    "files/font/font8.fnt": ("SD/font/font8.fnt",),
    "files/font/font10.fnt": ("SD/font/font10.fnt",),
    "files/font/font14.fnt": ("SD/font/font14.fnt",),
    "files/font/font16.fnt": ("SD/font/font16.fnt",),
    "files/font/font16x.fnt": ("SD/font/font16x.fnt",),
    "glue/palnl/popopup.pcx": ("glue/Battlenet/ToonRegistry/popopup.pcx",),
    "unit/cmdbtns/cmdicons.grp": ("unit/cmdicons/cmdicons.grp",),
    "unit/cmdbtns/pcmdbtns.grp": ("game/consoles/protoss/cmdbtns.grp",),
    "unit/cmdbtns/tcmdbtns.grp": ("game/consoles/terran/cmdbtns.grp",),
    "unit/cmdbtns/zcmdbtns.grp": ("game/consoles/zerg/cmdbtns.grp",),
    "unit/cmdbtns/ticon.pcx": ("game/ticon.pcx",),
}
# Tileset files Remastered grows by appending records: the original is the prefix of the manifest's size.
GROWN = (".cv5", ".vf4", ".vr4")
VX4EX = ".vx4ex"  # `.vx4`'s 16-bit minitile references widened to 32 bits
CV5_GROUP = 52
# CV5 groups that are empty (all zero) in 1.17 and that Remastered fills with its own terrain: [first, end).
CV5_FILLED = {"desert": (770, 1024), "platform": (933, 1024), "twilight": (797, 1024)}


@dataclass(frozen=True)
class Manifest:
    profile: str
    files: dict[str, tuple[str, int, str]]  # normalized name -> (sha256, size, name as the converter spells it)
    absent: frozenset[str]
    unreadable: frozenset[str]

    @classmethod
    def load(cls, profile_id: str = DEFAULT_PROFILE) -> "Manifest":
        path = manifest_path(profile_id)
        if not path.is_file():
            raise SourceError(f"no Remastered input manifest {path} (python3 -m converter scr-manifest)")
        raw = json.loads(path.read_text())
        return cls(raw["profile"], {normalize(n): (h, size, n) for n, size, h in raw["files"]},
                   frozenset(normalize(n) for n in raw["absent"]), frozenset(normalize(n) for n in raw["unreadable"]))


def manifest_path(profile_id: str) -> Path:
    return MANIFEST_DIR / f"inputs-{profile_id}.json"


def write_manifest(path: Path, profile_id: str, files: dict[str, tuple[str, int]], absent: set[str],
                   unreadable: set[str]) -> None:
    """One file per line: [name, size, sha256], sorted by name."""
    lines = [json.dumps([name, size, sha]) for name, (sha, size) in sorted(files.items(), key=lambda kv: kv[0].lower())]
    absent_lines = [json.dumps(n) for n in sorted(absent - set(files), key=str.lower)]
    text = (
        "{\n"
        f' "comment": "The files profile {profile_id}\'s conversion reads from the 1.17 install: name, size, SHA-256 '
        '(hashes only). converter/casc/source.py checks a StarCraft: Remastered install against them; written by '
        'python3 -m converter scr-manifest --data DIR.",\n'
        f' "profile": {json.dumps(profile_id)},\n'
        ' "files": [\n  ' + ",\n  ".join(lines) + "\n ],\n"
        ' "absent": [\n  ' + ",\n  ".join(absent_lines) + "\n ],\n"
        f' "unreadable": {json.dumps(sorted(unreadable, key=str.lower))}\n'
        "}\n"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    write_if_changed(path, text)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def vx4_from_vx4ex(data: bytes, size: int) -> bytes | None:
    """The first `size` bytes of `.vx4` from `.vx4ex`; None when the prefix is not 16-bit references."""
    count = size // 2
    if size % 2 or len(data) < count * 4:
        return None
    refs = struct.unpack_from(f"<{count}I", data)
    if any(r > 0xFFFF for r in refs):
        return None
    return struct.pack(f"<{count}H", *refs)


class ScrSource:
    """A StarCraft: Remastered install as a source of profile `profile`'s files."""

    def __init__(self, root: str | Path, profile: Profile | None = None, manifest: Manifest | None = None):
        self.profile = profile or Profile.load()
        self.manifest = manifest or Manifest.load(self.profile.id)
        if self.manifest.profile != self.profile.id:
            raise SourceError(f"manifest of {self.manifest.profile}, profile {self.profile.id}")
        self.storage = CascStorage(root)
        self.version = self.storage.version
        self.status: dict[str, tuple[str, str]] = {}  # name -> (status, CASC name)

    def close(self) -> None:
        self.storage.close()

    def __enter__(self) -> "ScrSource":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def data_set_hash(self) -> bytes:
        build = self.storage.build_info.get("Build Key", "")
        return hashlib.sha256(f"scr {self.version} {build} {self.profile.id}".encode()).digest()

    def _candidates(self, name: str) -> list[str]:
        key = normalize(name)
        names = list(MOVED.get(key, ()))
        names += [p + key for p in PREFIXES]
        if key.endswith(".vx4"):
            names += [p + key + "ex" for p in PREFIXES]
        return [n for n in names if self.storage.has(n)]

    def has(self, name: str) -> bool:
        key = normalize(name)
        if key in self.manifest.files or key in self.manifest.unreadable:
            return True
        if key in self.manifest.absent:
            return False
        return bool(self._candidates(name))

    def _rebuild(self, key: str, casc_name: str, data: bytes, size: int) -> bytes | None:
        if casc_name.lower().endswith(VX4EX):
            return vx4_from_vx4ex(data, size)
        if not key.endswith(GROWN) or len(data) < size:
            return None
        data = data[:size]
        filled = CV5_FILLED.get(Path(key).stem) if key.endswith(".cv5") else None
        if filled:
            first, end = (g * CV5_GROUP for g in filled)
            data = data[:first] + bytes(end - first) + data[end:]
        return data

    def read(self, name: str) -> tuple[str, bytes]:
        key = normalize(name)
        if key in self.manifest.unreadable:
            self.status[name] = ("unreadable", "")
            raise SourceUnreadable(f"{name}: unreadable in profile {self.profile.id}'s install")
        if key in self.manifest.absent:
            raise SourceError(f"{name}: not in profile {self.profile.id}")
        expected = self.manifest.files.get(key)
        fallback = None
        for casc_name in self._candidates(name):
            try:
                data = self.storage.read(casc_name)
            except CascError as exc:  # listed in the root, not installed locally
                log.debug("%s: %s", casc_name, exc)
                continue
            if expected is None:
                return self._found(name, "unlisted", casc_name, data)
            sha, size, _ = expected
            if _sha(data) == sha:
                return self._found(name, "exact", casc_name, data)
            rebuilt = self._rebuild(key, casc_name, data, size)
            if rebuilt is not None and _sha(rebuilt) == sha:
                return self._found(name, "derived", casc_name, rebuilt)
            if fallback is None and not casc_name.lower().endswith(VX4EX):
                fallback = (casc_name, data)
        if fallback is not None:
            return self._found(name, "differs", *fallback)
        self.status[name] = ("missing", "")
        raise SourceError(f"{name} not found in StarCraft: Remastered {self.version}")

    def _found(self, name: str, status: str, casc_name: str, data: bytes) -> tuple[str, bytes]:
        if status in ("differs", "unlisted") and name not in self.status:
            log.debug("%s: %s (CASC %s)", name, status, casc_name)
        self.status[name] = (status, casc_name)
        return f"CASC {casc_name}", data

    def write_report(self, out: Path) -> dict[str, int]:
        """Merge this run's statuses into out/reports/scr-import.json; return the counts by status."""
        path = out / REPORT
        files = json.loads(path.read_text())["files"] if path.is_file() else {}
        files.update({name: list(v) for name, v in self.status.items()})
        counts: dict[str, int] = {}
        for status, _ in files.values():
            counts[status] = counts.get(status, 0) + 1
        report = {"build": self.version, "profile": self.profile.id, "counts": counts,
                  "files": dict(sorted(files.items(), key=lambda kv: kv[0].lower()))}
        path.parent.mkdir(parents=True, exist_ok=True)
        write_if_changed(path, json.dumps(report, indent=1) + "\n")
        return counts
