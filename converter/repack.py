"""The repack (ADR 0018, stage D8): the files the game reads from an install, in their original formats, written into
`koprulu*.mpq` archives with StormLib, each with a `(koprulu)` listing the runtime checks every file against
(native/kp_app/kp_res.c).

The files are the input manifest's (defs/scr/inputs-<profile>.json) but its absent and unreadable names, in the
compiled manifest's order (a Remastered rebuild's palette is kept for the files after it: the order kp_res reads them
in), then the 1.17 install's cinematics (defs/campaign/cinematics.json, checked as the converter checks them). From a
Remastered install (ScrSource) the copies and rebuilds keep their statuses and no cinematics go in: Remastered's are
WebM, which the runtime reads from the install, and the builds without one take the cinematics packs. A file the source
lacks (a 1.17 install without its CDs' archives: music, cinematics, speech) is left out and reported.

A WAV goes in as Ogg Vorbis when that is smaller (oggenc at sound.OGG_QUALITY, the CRC-32 of its normalized name as the
stream serial; encoded once into the cache): the listing keeps the WAV's digest, size and header up to its data chunk,
which the runtime answers for the WAV's header (the durations the game computes from it, kp_files_wav). A WAV the
original's parser refuses, or whose header is longer than kp_files.c's WAV_HEAD, stays a WAV.

Groups, for a build that fetches them when it needs them (the web): `koprulu-core.mpq`, what the program reads when it
starts; a group per mission directory, its CHK and speech (`koprulu-<campaign>-<mission>.mpq`); `koprulu-music.mpq`;
`koprulu-cinematics.mpq`. The core's listing names each group with the prefix of its names. `single` writes one
`koprulu.mpq` instead (the desktops).

The listing (`(koprulu)`, a line each):
    koprulu repack 2
    profile <id>
    source <1.17|remastered>
    group <file> <prefix>                          the core's: a group and the normalized prefix of its names
    <sha256> <size> <status> <name>
    <sha256> <size> <status> ogg <wav sha256> <wav size> <wav header, hex> <name>
The digest and size are the stored bytes'; <status> the source's (exact, derived, differs, equivalent, colour-equal,
approximation, unlisted); <name> as the source spells it.
"""
from __future__ import annotations

import hashlib
import logging
import threading
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from .casc.storage import normalize
from .emit import cinematics, cinematics_smk, sound
from .formats import wav
from .mpq.stormlib import MpqFileNotFound, NewArchive
from .source import SourceError

log = logging.getLogger(__name__)

LISTING = "(koprulu)"
MAGIC = "koprulu repack 2"
SINGLE = "koprulu.mpq"
CORE = "koprulu-core.mpq"
MUSIC = "koprulu-music.mpq"
CINEMATICS = "koprulu-cinematics.mpq"
# kp_files.c WAV_HEAD: the bytes of a WAV's header the runtime reads first
WAV_HEAD = 4096
# the files read and encoded at a time (the music's WAVs are 17 MB each)
WINDOW = 64
# stored as they are: compressed already
UNCOMPRESSED = (".smk", ".ogg")


def group_of(key: str) -> tuple[str, str]:
    """(group file, normalized prefix of its names) of a normalized name: a mission directory's
    `campaign/<campaign>/<mission>/`, the music's, the cinematics'; the core's is (CORE, "")."""
    if key.startswith("campaign/"):
        at = key.find("/staredit/")
        if at > len("campaign/"):
            prefix = key[:at + 1]
            return "koprulu-" + "-".join(prefix.split("/")[1:-1]) + ".mpq", prefix
    if key.startswith("music/"):
        return MUSIC, "music/"
    if key.startswith("smk/"):
        return CINEMATICS, "smk/"
    return CORE, ""


@dataclass
class Stored:
    """A file as the repack stores it."""
    name: str
    data: bytes
    status: str
    wav: tuple[str, int, bytes] | None = None  # the (SHA-256, size, header) of the WAV an Ogg replaces

    def line(self) -> str:
        head = f"{hashlib.sha256(self.data).hexdigest()} {len(self.data)} {self.status}"
        if self.wav:
            digest, size, header = self.wav
            head += f" ogg {digest} {size} {header.hex()}"
        return f"{head} {self.name}\n"


class OggStore:
    """The Ogg Vorbis of each WAV, encoded once: `<dir>/<WAV SHA-256>-q<quality>-s<serial>.ogg`."""

    def __init__(self, root: Path):
        self.root = root
        self.encoded = 0
        self._lock = threading.Lock()

    def ogg(self, data: bytes, serial: int) -> bytes:
        path = self.root / f"{hashlib.sha256(data).hexdigest()}-q{sound.OGG_QUALITY}-s{serial}.ogg"
        if path.is_file():
            return path.read_bytes()
        ogg = sound.ogg_bytes(data, serial)
        self.root.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(f".{threading.get_ident()}.tmp")
        tmp.write_bytes(ogg)
        tmp.replace(path)
        with self._lock:
            self.encoded += 1
        return ogg


def wav_header(data: bytes) -> bytes | None:
    """A WAV's header as the runtime reads it (up to its data chunk), or None when the original's parser refuses the
    WAV or the header is longer than WAV_HEAD."""
    try:
        info = wav.read_info(data)
    except wav.WavError:
        return None
    return data[:info.data_offset] if info.data_offset <= WAV_HEAD else None


def store(name: str, data: bytes, status: str, oggs: OggStore | None) -> Stored:
    """The file as stored: a WAV as Ogg Vorbis when that is smaller."""
    if oggs is None or not name.lower().endswith(".wav"):
        return Stored(name, data, status)
    header = wav_header(data)
    if header is None:
        return Stored(name, data, status)
    try:
        ogg = oggs.ogg(data, sound.serial_of(normalize(name)))
    except sound.SoundError as e:
        log.warning("%s: kept as a WAV (%s)", name, e)
        return Stored(name, data, status)
    if len(ogg) >= len(data):
        return Stored(name, data, status)
    return Stored(name, ogg, status, (hashlib.sha256(data).hexdigest(), len(data), header))


@dataclass
class Report:
    files: dict[str, int] = field(default_factory=dict)   # group -> files
    sizes: dict[str, int] = field(default_factory=dict)   # group -> stored bytes
    statuses: dict[str, int] = field(default_factory=dict)
    missing: list[str] = field(default_factory=list)
    ogg: int = 0
    wav_bytes: int = 0  # the WAVs' bytes the Oggs replace
    ogg_bytes: int = 0


def _reads(source, extra: list[str], movies: bool) -> list[tuple[str, str, int | None, str | None]]:
    """(name, key, expected size, expected SHA-256) of each file to read, in order: the manifest's, the 1.17
    cinematics' (`movies`: with their reference copies' size and digest), then `extra`'s names the manifest does not
    hold."""
    manifest = source.manifest
    out = [(manifest.files[key].name, key, None, None) for key in sorted(manifest.files)]
    if movies:
        known = cinematics_smk.expected()
        for movie in cinematics.table()[0]:
            name = cinematics_smk.SMK_PATH.format(name=movie)
            size, digest = known[movie]
            out.append((name, normalize(name), size, digest))
    keys = {key for _, key, _, _ in out} | manifest.absent | manifest.unreadable
    for name in extra:
        if normalize(name) not in keys:
            out.append((name, normalize(name), None, None))
            keys.add(normalize(name))
    return out


def _read(source, kind: str, name: str, size: int | None, digest: str | None) -> tuple[str, bytes] | None:
    """(status, bytes) of a file, or None when the source lacks it (a Remastered file nothing rebuilds, a movie)."""
    try:
        if size is not None:
            _, data = source.read_expected(name, size, digest)
        else:
            _, data = source.read(name)
    except (MpqFileNotFound, SourceError) as e:
        log.debug("%s: %s", name, e)
        return None
    if kind == "remastered":
        return source.status.get(name, ("unlisted", ""))[0], data
    return ("exact" if size is not None or normalize(name) in source.manifest.files else "unlisted"), data


def write(source, out: Path, *, kind: str, single: bool = False, extra: list[str] | None = None,
          oggs: OggStore | None = None) -> Report:
    """Write the repack of `source` (`kind` "1.17": a Resolver; "remastered": a ScrSource, its statuses) into `out`:
    its groups, or one koprulu.mpq (`single`); the koprulu*.mpq there before are removed."""
    reads = _reads(source, extra or [], movies=kind == "1.17")
    root = SINGLE if single else CORE

    def group(key: str) -> str:
        return root if single else group_of(key)[0]

    counts = {root: 0}
    for _, key, _, _ in reads:
        counts[group(key)] = counts.get(group(key), 0) + 1
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("koprulu*.mpq"):
        old.unlink()
    head = [f"{MAGIC}\n", f"profile {source.profile.id}\n", f"source {kind}\n"]
    archives = {g: NewArchive(out / g, n + 8) for g, n in counts.items()}
    listings = {g: list(head) for g in archives}
    prefixes: dict[str, str] = {}
    report = Report()
    try:
        with ThreadPoolExecutor() as pool:
            for at in range(0, len(reads), WINDOW):
                jobs = []
                for name, key, size, digest in reads[at:at + WINDOW]:
                    got = _read(source, kind, name, size, digest)
                    if got is None:
                        report.missing.append(name)
                    else:
                        jobs.append((name, key, *got))
                for (name, key, status, _), stored in zip(jobs, pool.map(
                        lambda job: store(job[0], job[3], job[2], oggs), jobs)):
                    g = group(key)
                    prefixes[g] = group_of(key)[1]
                    archives[g].add(stored.name, stored.data,
                                    compress=stored.wav is None and not key.endswith(UNCOMPRESSED))
                    listings[g].append(stored.line())
                    report.files[g] = report.files.get(g, 0) + 1
                    report.sizes[g] = report.sizes.get(g, 0) + len(stored.data)
                    report.statuses[status] = report.statuses.get(status, 0) + 1
                    if stored.wav:
                        report.ogg += 1
                        report.wav_bytes += stored.wav[1]
                        report.ogg_bytes += len(stored.data)
        if not single:
            listings[root][len(head):len(head)] = [f"group {g} {prefixes[g]}\n" for g in sorted(report.files) if g != root]
        for g, archive in archives.items():
            archive.add(LISTING, "".join(listings[g]).encode())
    finally:
        for archive in archives.values():
            archive.close()
    for g in archives:
        if g != root and g not in report.files:
            (out / g).unlink()
    return report
