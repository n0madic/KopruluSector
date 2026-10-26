"""Deterministic WAV -> OGG conversion (`oggenc` with a fixed stream serial) for the pack, music and mission sound
containers (converter/emit/packs.py).
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import zlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .files import project_path, write_if_changed

OGG_QUALITY = "3"  # oggenc -q; mono 22 kHz speech/effects


def serial(out: Path) -> int:
    """Ogg stream serial derived from the output path: oggenc picks a random one otherwise."""
    return zlib.crc32(project_path(out).encode()) & 0x7FFFFFFF


def ogg_bytes(data: bytes, stream_serial: int) -> bytes:
    """WAV bytes -> Ogg Vorbis bytes; identical input gives identical output."""
    return subprocess.run(["oggenc", "-Q", "-q", OGG_QUALITY, "-s", str(stream_serial), "-o", "-", "-"],
                          input=data, check=True, capture_output=True).stdout


class OggCache:
    """Converts only what changed: `<root>/ogg.json` maps each OGG (relative to `root`) to
    the SHA-256 of its WAV and the encoder settings."""

    def __init__(self, root: Path):
        self.root = root
        self.path = root / "ogg.json"
        self.entries: dict[str, str] = json.loads(self.path.read_text()) if self.path.exists() else {}
        self.converted = 0

    def convert(self, jobs: list[tuple[bytes, Path]]) -> None:
        """Encode (wav bytes, ogg path) jobs in parallel; skip outputs that are current."""
        todo = []
        for data, out in jobs:
            key = out.relative_to(self.root).as_posix()
            digest = hashlib.sha256(data + f"q{OGG_QUALITY}s{serial(out)}".encode()).hexdigest()
            if out.exists() and self.entries.get(key) == digest:
                continue
            todo.append((data, out, key, digest))

        def run(job):
            data, out, key, digest = job
            write_if_changed(out, ogg_bytes(data, serial(out)))
            return key, digest

        with ThreadPoolExecutor() as pool:
            for key, digest in pool.map(run, todo):
                self.entries[key] = digest
        self.converted += len(todo)
        write_if_changed(self.path, json.dumps(self.entries, indent=0, sort_keys=True) + "\n")
