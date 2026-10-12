"""Helpers for writing Defold text resources (protobuf text format)."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def project_path(path: Path) -> str:
    """Absolute Defold resource path ("/generated/...") of a file inside the project."""
    return "/" + path.resolve().relative_to(ROOT).as_posix()


def quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def write_if_changed(path: Path, text: str | bytes) -> bool:
    """Write only when content differs, so bob's incremental build sees stable inputs."""
    data = text.encode() if isinstance(text, str) else text
    if path.exists() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True
