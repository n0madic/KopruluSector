"""Provenance hashes of generated containers: the profile tables under `defs/` a file was
converted with, so a container built from other tables is told apart (container header,
docs/formats/container.md)."""
from __future__ import annotations

import hashlib
from pathlib import Path

DEFS_ROOT = Path(__file__).resolve().parents[1] / "defs"


def provenance_hash(*relative: str) -> bytes:
    """SHA-256 over every `defs/<relative>/*.json`, each keyed by its directory and name."""
    digest = hashlib.sha256()
    for rel in relative:
        for path in sorted((DEFS_ROOT / rel).glob("*.json")):
            digest.update(f"{rel}/{path.name}".encode() + b"\0" + path.read_bytes())
    return digest.digest()
