"""Writing the converter's outputs: stable bytes, written only when they change."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def project_path(path: Path) -> str:
    """The path of a file inside the repository ("/generated/..."); the OGG streams' serials derive from it."""
    return "/" + path.resolve().relative_to(ROOT).as_posix()


def write_if_changed(path: Path, text: str | bytes) -> bool:
    """Write only when the content differs, so incremental steps see stable inputs."""
    data = text.encode() if isinstance(text, str) else text
    if path.exists() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True
