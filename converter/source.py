"""Where the converter's original files come from.

A source provides the files of one data set (the profile `bw-1.17.0.1`) under their MPQ paths:
the MPQ `Resolver` (mpq/profile.py) from a verified 1.17 install, `ScrSource` (casc/source.py) from a
StarCraft: Remastered install. The conversion only calls `read`, `has` and `data_set_hash`, and takes
the rules tables from `profile.id`'s `defs/`; no step reads the executable.

`RecordingSource` wraps a source and records what the conversion reads; `scr-manifest` turns that record
into `defs/scr/inputs-<profile>.json`, the hashes a Remastered source is checked against.
"""
from __future__ import annotations

import hashlib
from typing import Protocol

from .errors import ConverterError
from .mpq.profile import Profile
from .mpq.stormlib import MpqFileNotFound, StormLibError


class SourceError(ConverterError, RuntimeError):
    """A source cannot provide a file."""


class SourceUnreadable(SourceError):
    """A file the data set lists but its original copy cannot be read (the manifest's `unreadable`)."""


class Source(Protocol):
    profile: Profile

    def read(self, name: str) -> tuple[str, bytes]:
        """(provider, bytes) of the data set's file `name` (an MPQ path)."""

    def has(self, name: str) -> bool:
        ...

    def data_set_hash(self) -> bytes:
        """SHA-256 identifying the source's data; shared by every container converted from it."""

    def close(self) -> None:
        ...


class RecordingSource:
    """Forwards to `inner` and records each file read (its SHA-256, size and what `describe` keeps of it), each
    name `has` denied and each read that failed."""

    def __init__(self, inner: Source, describe=None):
        self.inner = inner
        self.profile = inner.profile
        self.describe = describe
        self.files: dict[str, tuple[str, int, dict | None]] = {}
        self.absent: set[str] = set()
        self.unreadable: set[str] = set()

    def read(self, name: str) -> tuple[str, bytes]:
        try:
            provider, data = self.inner.read(name)
        except MpqFileNotFound:
            raise
        except (StormLibError, SourceUnreadable):
            self.unreadable.add(name)
            raise
        if name not in self.files:
            extras = self.describe(name, data) if self.describe else None
            self.files[name] = (hashlib.sha256(data).hexdigest(), len(data), extras)
        return provider, data

    def has(self, name: str) -> bool:
        found = self.inner.has(name)
        if not found:
            self.absent.add(name)
        return found

    def data_set_hash(self) -> bytes:
        return self.inner.data_set_hash()

    def close(self) -> None:
        self.inner.close()

    def __enter__(self) -> "RecordingSource":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()
