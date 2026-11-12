"""Where the converter's original files come from.

A source provides the files of one data set (the profile `bw-1.17.0.1`) under their MPQ paths:
the MPQ `Resolver` (mpq/profile.py) from a 1.17 install, `ScrSource` (casc/source.py) from a
StarCraft: Remastered install. The conversion only calls `read`, `has` and `data_set_hash`, and takes
the rules tables from `profile.id`'s `defs/`; no step reads the executable.

`RecordingSource` wraps a source and records the names read (tools/research/scr_manifest.py builds the text tables
of them); the input manifest `defs/scr/inputs-<profile>.json` (inputs.py) is `kp_convert manifest`'s, kp_res recording
the runtime's own reads.
"""
from __future__ import annotations

from typing import Protocol

from .errors import ConverterError
from .mpq.profile import Profile


class SourceError(ConverterError, RuntimeError):
    """A source cannot provide a file."""


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
    """Forwards to `inner` and records the name of each file read, in the order first read."""

    def __init__(self, inner: Source):
        self.inner = inner
        self.profile = inner.profile
        self.names: dict[str, None] = {}

    def read(self, name: str) -> tuple[str, bytes]:
        provider, data = self.inner.read(name)
        self.names.setdefault(name)
        return provider, data

    def has(self, name: str) -> bool:
        return self.inner.has(name)

    def data_set_hash(self) -> bytes:
        return self.inner.data_set_hash()

    def close(self) -> None:
        self.inner.close()

    def __enter__(self) -> "RecordingSource":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()
